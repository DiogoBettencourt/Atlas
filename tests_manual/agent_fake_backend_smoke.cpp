// Manual smoke test driving Agent::chat end-to-end through a scripted
// FakeBackend instead of a live Ollama. This is the first test that
// exercises the ReAct loop itself: event ordering, tool execution, session
// persistence, and the `thinking` event added in v0.7.1.
#include "atlas/agent/Agent.hpp"
#include "atlas/agent/LLMBackend.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/core/SymbolIndexer.hpp"
#include "atlas/storage/FileStorageManager.hpp"
#include "atlas/tools/ToolManager.hpp"

#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;
using atlas::agent::Agent;
using atlas::agent::LLMBackend;
namespace fs = std::filesystem;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}

// Replays scripted assistant messages in order and records every request.
// A scripted entry of {"__throw": "..."} makes that call throw instead.
class FakeBackend : public LLMBackend {
public:
    void push(json message) { script_.push_back(std::move(message)); }

    json chat(const std::string& model, const json& messages, const json& tools) override {
        requests_.push_back({{"model", model}, {"messages", messages}, {"tools", tools}});
        if (script_.empty()) throw std::runtime_error("FakeBackend: script exhausted");
        json next = script_.front();
        script_.pop_front();
        if (next.contains("__throw")) throw std::runtime_error(next["__throw"].get<std::string>());
        return next;
    }

    std::string name() const override { return "fake"; }

    const std::vector<json>& requests() const { return requests_; }

private:
    std::deque<json> script_;
    std::vector<json> requests_;
};

std::vector<std::string> types(const std::vector<json>& events) {
    std::vector<std::string> out;
    for (const auto& e : events) out.push_back(e.value("type", std::string{"?"}));
    return out;
}

} // namespace

int main() {
    fs::path root = fs::temp_directory_path() / "atlas_fake_backend_smoke";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::path workspace = root / "ws";
    fs::create_directories(workspace, ec);
    {
        std::ofstream f(workspace / "hello.txt");
        f << "hello from the workspace";
    }

    atlas::storage::FileStorageManager storage(root / "storage");
    atlas::core::SessionManager sessions(storage);
    atlas::core::SymbolIndexer indexer;
    atlas::tools::ToolManager tools;
    tools.registerDefaultTools(indexer);

    FakeBackend backend;
    Agent agent(backend, tools, sessions, "fake-model");

    auto run = [&](const std::string& message, const std::string& session, std::vector<json>& events) {
        return agent.chat(message, session, workspace.string(),
                          [&events](const json& e) { events.push_back(e); });
    };

    // -----------------------------------------------------------------
    // Plain reply: no tools, one LLM call.
    // -----------------------------------------------------------------
    {
        backend.push({{"role", "assistant"}, {"content", "hi there"}});
        std::vector<json> events;
        std::string reply = run("hello", "plain", events);
        expect(reply == "hi there", "plain reply: returns the assistant content");
        expect(types(events) == std::vector<std::string>{"iteration_start", "final"},
               "plain reply: events are iteration_start, final");
        expect(backend.requests().size() == 1, "plain reply: exactly one backend call");
        expect(backend.requests()[0]["model"] == "fake-model", "plain reply: configured model is passed through");
        expect(backend.requests()[0]["messages"][0]["role"] == "system",
               "plain reply: system prompt is prepended");
        expect(backend.requests()[0]["tools"].is_array() && !backend.requests()[0]["tools"].empty(),
               "plain reply: tool schemas are offered to the backend");
        expect(sessions.getHistory("plain").size() == 2, "plain reply: user + assistant persisted");
    }

    // -----------------------------------------------------------------
    // Tool call, then final reply: the tool really runs against the
    // workspace and its result is fed back into the second request.
    // -----------------------------------------------------------------
    {
        size_t before = backend.requests().size();
        backend.push({{"role", "assistant"},
                      {"content", ""},
                      {"tool_calls", json::array({{{"function",
                          {{"name", "read_file"}, {"arguments", {{"path", "hello.txt"}}}}}}})}});
        backend.push({{"role", "assistant"}, {"content", "the file says hello"}});
        std::vector<json> events;
        std::string reply = run("what is in hello.txt?", "tools", events);
        expect(reply == "the file says hello", "tool turn: returns the final reply");
        expect(types(events) == std::vector<std::string>{"iteration_start", "tool_call", "tool_result",
                                                         "iteration_start", "final"},
               "tool turn: event order is iteration, tool_call, tool_result, iteration, final");
        expect(events[1]["name"] == "read_file" && events[1]["arguments"]["path"] == "hello.txt",
               "tool turn: tool_call event carries name and arguments");
        expect(events[2]["result"].value("content", std::string{}) == "hello from the workspace",
               "tool turn: read_file actually ran against the workspace");
        expect(backend.requests().size() == before + 2, "tool turn: two backend calls");
        const json& second = backend.requests().back()["messages"];
        expect(second.back()["role"] == "tool", "tool turn: second request ends with the tool result message");
        expect(second.back()["content"].get<std::string>().find("hello from the workspace") != std::string::npos,
               "tool turn: tool result content reaches the backend");
    }

    // -----------------------------------------------------------------
    // Reasoning text: emitted as a `thinking` event right after the LLM
    // call that produced it, ahead of the final event.
    // -----------------------------------------------------------------
    {
        backend.push({{"role", "assistant"}, {"content", "42"}, {"thinking", "let me work this out"}});
        std::vector<json> events;
        std::string reply = run("meaning of life?", "thinking", events);
        expect(reply == "42", "thinking: reply is still the content only");
        expect(types(events) == std::vector<std::string>{"iteration_start", "thinking", "final"},
               "thinking: event order is iteration_start, thinking, final");
        expect(events[1]["content"] == "let me work this out", "thinking: event carries the reasoning text");
    }

    // -----------------------------------------------------------------
    // Interim content on a tool turn is an assistant_thought, distinct
    // from thinking.
    // -----------------------------------------------------------------
    {
        backend.push({{"role", "assistant"},
                      {"content", "I'll read the file first"},
                      {"thinking", "need the contents"},
                      {"tool_calls", json::array({{{"function",
                          {{"name", "read_file"}, {"arguments", {{"path", "hello.txt"}}}}}}})}});
        backend.push({{"role", "assistant"}, {"content", "done"}});
        std::vector<json> events;
        (void)run("read it", "thought", events);
        expect(types(events) == std::vector<std::string>{"iteration_start", "thinking", "assistant_thought",
                                                         "tool_call", "tool_result", "iteration_start",
                                                         "final"},
               "thought: thinking precedes assistant_thought on a tool turn");
    }

    // -----------------------------------------------------------------
    // Backend failure: surfaced as an error event and an "Agent error"
    // reply, not an exception out of chat().
    // -----------------------------------------------------------------
    {
        backend.push({{"__throw", "backend exploded"}});
        std::vector<json> events;
        std::string reply = run("anyone there?", "err", events);
        expect(reply.find("Agent error") != std::string::npos && reply.find("backend exploded") != std::string::npos,
               "error: reply reports the backend failure");
        expect(types(events) == std::vector<std::string>{"iteration_start", "error"},
               "error: events are iteration_start, error");
    }

    // -----------------------------------------------------------------
    // Iteration cap: a model that never stops calling tools is cut off.
    // -----------------------------------------------------------------
    {
        agent.setMaxIterations(2);
        for (int i = 0; i < 2; ++i) {
            backend.push({{"role", "assistant"},
                          {"content", ""},
                          {"tool_calls", json::array({{{"function",
                              {{"name", "read_file"}, {"arguments", {{"path", "hello.txt"}}}}}}})}});
        }
        std::vector<json> events;
        std::string reply = run("loop forever", "loop", events);
        expect(reply.find("exceeded maximum") != std::string::npos, "cap: gives up with the iteration-limit message");
        expect(!events.empty() && events.back()["type"] == "error", "cap: last event is an error");
    }

    fs::remove_all(root, ec);
    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
