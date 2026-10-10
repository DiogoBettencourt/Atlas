// Manual smoke test for stopping a turn: the backends drop a request when its
// CancelCheck turns true (even before the model has sent anything), Agent ends
// the turn cleanly and keeps the history valid, and POST /sessions/:id/cancel
// ends a running /chat/stream turn with a "cancelled" event.
#include "atlas/agent/Agent.hpp"
#include "atlas/agent/OllamaBackend.hpp"
#include "atlas/agent/OpenAICompatBackend.hpp"
#include "atlas/api/APIServer.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/core/SymbolIndexer.hpp"
#include "atlas/core/WorkspaceManager.hpp"
#include "atlas/storage/FileStorageManager.hpp"
#include "atlas/tools/ToolManager.hpp"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
using atlas::agent::Agent;
using atlas::agent::Cancelled;
using atlas::agent::LLMBackend;
using atlas::agent::OllamaBackend;
using atlas::agent::OpenAICompatBackend;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}

double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// A port nothing is listening on. The probe server has to be started and
// stopped properly: a bound-but-never-listened httplib::Server keeps its
// socket open (SO_REUSEPORT lets the real server share the port), and the
// kernel then hands it half of the incoming connections.
int freePort() {
    httplib::Server probe;
    const int port = probe.bind_to_any_port("127.0.0.1");
    std::thread runner([&] { probe.listen_after_bind(); });
    while (!probe.is_running()) sleepMs(5);
    probe.stop();
    runner.join();
    return port;
}

class StubServer {
public:
    using Handler = std::function<void(const httplib::Request&, httplib::Response&)>;

    StubServer(const std::string& path, Handler handler) {
        server_.Post(path, [handler](const httplib::Request& req, httplib::Response& res) { handler(req, res); });
        port_ = server_.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        while (!server_.is_running()) sleepMs(5);
    }
    ~StubServer() {
        server_.stop();
        thread_.join();
    }
    StubServer(const StubServer&) = delete;
    StubServer& operator=(const StubServer&) = delete;

    int port() const { return port_; }

private:
    httplib::Server server_;
    int port_ = 0;
    std::thread thread_;
};

std::string ollamaLine(const json& message) {
    return json{{"model", "m"}, {"message", message}, {"done", false}}.dump() + "\n";
}

std::string sseLine(const json& delta) {
    return "data: " + json{{"choices", json::array({{{"delta", delta}}})}}.dump() + "\n\n";
}

// Writes `first`, then keeps writing `more` every 20ms until a write fails
// (the client hung up) or ~4s pass. `client_left` records whether it was the
// client hanging up that ended it.
void slowStream(httplib::Response& res, const char* type, std::string first, std::string more,
                std::atomic<bool>& client_left) {
    res.set_chunked_content_provider(type, [first, more, &client_left, started = Clock::now(),
                                            sent_first = false](std::size_t, httplib::DataSink& sink) mutable {
        if (!sent_first) {
            sent_first = true;
            return sink.write(first.data(), first.size());
        }
        if (secondsSince(started) > 4.0) {
            sink.done();
            return true;
        }
        sleepMs(20);
        if (!sink.write(more.data(), more.size())) {
            client_left = true;
            return false;
        }
        return true;
    });
}

// Sets `flag` after `ms` on a background thread.
struct CancelAfter {
    CancelAfter(std::atomic<bool>& flag, int ms) : thread([&flag, ms] { sleepMs(ms); flag = true; }) {}
    ~CancelAfter() { thread.join(); }
    std::thread thread;
};

// A backend whose chatStream() streams a thinking piece and some text, then
// waits for the cancel - the shape of a slow model.
class SlowBackend : public LLMBackend {
public:
    json chat(const std::string&, const json&, const json&) override { return {{"role", "assistant"}, {"content", "plain"}}; }
    json chatStream(const std::string&, const json&, const json&, const DeltaCallback& on_delta,
                    const atlas::agent::CancelCheck& cancelled) override {
        ++streams;
        if (on_delta) on_delta("thinking", "pondering");
        if (on_delta) on_delta("content", "Partial answer");
        for (int i = 0; i < 400; ++i) {
            if (cancelled && cancelled()) {
                observed_cancel = true;
                throw Cancelled(json{{"role", "assistant"}, {"content", "Partial answer"}, {"thinking", "pondering"}});
            }
            if (chatty && i % 5 == 0 && on_delta) on_delta("thinking", ".");
            sleepMs(10);
        }
        return {{"role", "assistant"}, {"content", "finished normally"}};
    }
    std::string name() const override { return "slow"; }

    std::atomic<int> streams{0};
    std::atomic<bool> observed_cancel{false};
    // Keep sending thinking pieces while waiting, like a model that is still going.
    bool chatty = false;
};

// A backend that asks for two tool calls in one message.
class ToolsBackend : public LLMBackend {
public:
    json chat(const std::string&, const json&, const json&) override {
        return {{"role", "assistant"},
                {"content", ""},
                {"tool_calls", json::array({{{"function", {{"name", "list_directory"}, {"arguments", {{"path", "."}}}}}},
                                            {{"function", {{"name", "list_directory"}, {"arguments", {{"path", "."}}}}}}})}};
    }
    std::string name() const override { return "tools"; }
};

struct Env {
    fs::path root;
    std::unique_ptr<atlas::storage::FileStorageManager> storage;
    std::unique_ptr<atlas::core::SessionManager> sessions;
    atlas::core::SymbolIndexer indexer;
    atlas::tools::ToolManager tools;

    explicit Env(const std::string& name) {
        root = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "ws", ec);
        storage = std::make_unique<atlas::storage::FileStorageManager>(root / "store");
        sessions = std::make_unique<atlas::core::SessionManager>(*storage);
        tools.registerDefaultTools(indexer);
    }
    ~Env() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

} // namespace

int main(int argc, char** argv) {
    const bool only_http = argc > 1;
    if (!only_http) {
    // ------------------------------------------------------------------
    // Ollama: stopped mid-stream
    // ------------------------------------------------------------------
    {
        std::atomic<bool> client_left{false};
        StubServer server("/api/chat", [&](const httplib::Request&, httplib::Response& res) {
            slowStream(res, "application/x-ndjson", ollamaLine({{"role", "assistant"}, {"thinking", "Let me think. "}}),
                       ollamaLine({{"role", "assistant"}, {"thinking", "more "}}), client_left);
        });
        OllamaBackend backend("127.0.0.1", server.port());
        std::atomic<bool> stop{false};
        std::string seen_thinking;
        const auto start = Clock::now();
        bool threw = false;
        json partial;
        {
            CancelAfter later(stop, 300);
            try {
                (void)backend.chatStream("m", json::array(), json::array(),
                                         [&](const std::string& kind, const std::string& text) {
                                             if (kind == "thinking") seen_thinking += text;
                                         },
                                         [&] { return stop.load(); });
            } catch (const Cancelled& c) {
                threw = true;
                partial = c.partial();
            }
        }
        expect(threw, "ollama: chatStream throws Cancelled once the check turns true");
        expect(secondsSince(start) < 2.0, "ollama: ...and returns promptly, not after the whole reply");
        expect(partial.value("thinking", "").rfind("Let me think. ", 0) == 0 && partial.value("role", "") == "assistant",
               "ollama: the Cancelled error carries what had been produced");
        expect(seen_thinking.rfind("Let me think. ", 0) == 0, "ollama: the live pieces were delivered before the stop");
        sleepMs(100);
        expect(client_left.load(), "ollama: the connection was really closed (the server saw the client leave)");
    }

    // ------------------------------------------------------------------
    // Ollama: stopped while the server has sent nothing yet
    // ------------------------------------------------------------------
    {
        std::atomic<bool> release{false};
        StubServer server("/api/chat", [&](const httplib::Request&, httplib::Response& res) {
            for (int i = 0; i < 300 && !release; ++i) sleepMs(10); // reading a long prompt...
            res.set_content(json{{"message", {{"role", "assistant"}, {"content", "late"}}}}.dump(), "application/json");
        });
        OllamaBackend backend("127.0.0.1", server.port());
        std::atomic<bool> stop{false};
        const auto start = Clock::now();
        bool threw = false;
        {
            CancelAfter later(stop, 300);
            try {
                (void)backend.chatStream("m", json::array(), json::array(), nullptr, [&] { return stop.load(); });
            } catch (const Cancelled&) {
                threw = true;
            }
        }
        const double took = secondsSince(start);
        release = true;
#ifdef _WIN32
        // Windows' shutdown() doesn't wake a read that is blocked waiting for the
        // server's first bytes (Linux's does), so there a stop pressed while the
        // model is still reading the prompt takes effect when its answer starts.
        // It must still end as cancelled, just not promptly.
        (void)took;
        expect(threw, "ollama: a stop before the first token ends as cancelled (on Windows, once the server answers)");
#else
        expect(threw && took < 2.0, "ollama: a stop before the first token still returns promptly");
#endif
    }

    // ------------------------------------------------------------------
    // Ollama: no cancel check -> unchanged behavior
    // ------------------------------------------------------------------
    {
        StubServer server("/api/chat", [&](const httplib::Request&, httplib::Response& res) {
            res.set_content(ollamaLine({{"role", "assistant"}, {"content", "whole"}}), "application/x-ndjson");
        });
        OllamaBackend backend("127.0.0.1", server.port());
        json message = backend.chatStream("m", json::array(), json::array(), nullptr);
        expect(message.value("content", "") == "whole", "ollama: without a cancel check the reply comes back as before");
        std::atomic<bool> never{false};
        json message2 = backend.chatStream("m", json::array(), json::array(), nullptr, [&] { return never.load(); });
        expect(message2.value("content", "") == "whole", "ollama: a cancel check that never fires changes nothing");
    }

    // ------------------------------------------------------------------
    // OpenAI-compatible: stopped mid-stream
    // ------------------------------------------------------------------
    {
        std::atomic<bool> client_left{false};
        StubServer server("/v1/chat/completions", [&](const httplib::Request&, httplib::Response& res) {
            slowStream(res, "text/event-stream", sseLine({{"reasoning_content", "Thinking. "}}),
                       sseLine({{"content", "Some "}}), client_left);
        });
        OpenAICompatBackend backend("http://127.0.0.1:" + std::to_string(server.port()));
        std::atomic<bool> stop{false};
        const auto start = Clock::now();
        bool threw = false;
        json partial;
        {
            CancelAfter later(stop, 300);
            try {
                (void)backend.chatStream("m", json::array(), json::array(), nullptr, [&] { return stop.load(); });
            } catch (const Cancelled& c) {
                threw = true;
                partial = c.partial();
            }
        }
        expect(threw && secondsSince(start) < 2.0, "openai: chatStream throws Cancelled promptly");
        expect(partial.value("thinking", "") == "Thinking. " && partial.value("content", "").rfind("Some ", 0) == 0 &&
                   !partial.contains("tool_calls"),
               "openai: the partial reply keeps thinking and text, never tool calls");
        sleepMs(100);
        expect(client_left.load(), "openai: the connection was really closed");
    }

    // ------------------------------------------------------------------
    // Agent: stop during generation keeps what the user saw
    // ------------------------------------------------------------------
    {
        Env env("atlas_cancel_smoke_agent");
        SlowBackend backend;
        Agent agent(backend, env.tools, *env.sessions, "m");
        std::atomic<bool> stop{false};
        std::vector<json> events;
        std::string reply;
        {
            CancelAfter later(stop, 200);
            reply = agent.chat("hello", "s1", (env.root / "ws").string(), [&](const json& e) { events.push_back(e); }, true,
                               [&] { return stop.load(); });
        }
        std::vector<std::string> types;
        for (const auto& e : events) types.push_back(e.value("type", std::string{}));
        expect(types == std::vector<std::string>({"iteration_start", "thinking_delta", "content_delta", "cancelled"}),
               "agent: live pieces, then a cancelled event and nothing after it");
        expect(reply.empty(), "agent: a stopped turn returns an empty reply");
        auto history = env.sessions->getHistory("s1");
        expect(history.size() == 2 && history[1].value("content", "") == "Partial answer" &&
                   history[1].value("thinking", "") == "pondering",
               "agent: the partial answer is saved so reopening the session shows what the user saw");

        // The next message simply continues.
        agent.setStreaming(false);
        std::string next = agent.chat("again", "s1", (env.root / "ws").string());
        expect(next == "plain", "agent: the session still works after a stop");
    }

    // A stop that arrives before the turn starts generating anything.
    {
        Env env("atlas_cancel_smoke_agent2");
        SlowBackend backend;
        Agent agent(backend, env.tools, *env.sessions, "m");
        std::vector<json> events;
        std::string reply = agent.chat("hi", "s1", (env.root / "ws").string(), [&](const json& e) { events.push_back(e); }, true,
                                       [] { return true; });
        expect(reply.empty() && events.size() == 1 && events[0].value("type", "") == "cancelled" && backend.streams == 0,
               "agent: already cancelled -> no model request at all");
        auto history = env.sessions->getHistory("s1");
        expect(history.size() == 1, "agent: only the user's message is in the history");
    }

    // ------------------------------------------------------------------
    // Agent: stop between tool calls leaves a valid history
    // ------------------------------------------------------------------
    {
        Env env("atlas_cancel_smoke_tools");
        ToolsBackend backend;
        Agent agent(backend, env.tools, *env.sessions, "m");
        std::atomic<bool> stop{false};
        std::vector<json> events;
        std::string reply = agent.chat("go", "s1", (env.root / "ws").string(),
                                       [&](const json& e) {
                                           events.push_back(e);
                                           if (e.value("type", "") == "tool_call") stop = true; // Stop pressed during the first tool
                                       },
                                       false, [&] { return stop.load(); });
        int tool_calls = 0, tool_results = 0;
        for (const auto& e : events) {
            if (e.value("type", "") == "tool_call") ++tool_calls;
            if (e.value("type", "") == "tool_result") ++tool_results;
        }
        expect(reply.empty() && events.back().value("type", "") == "cancelled", "agent: stopping mid-step ends with cancelled");
        expect(tool_calls == 1 && tool_results == 1, "agent: the second tool call never ran");
        auto history = env.sessions->getHistory("s1");
        // user, assistant (2 calls), tool result, skipped tool result
        expect(history.size() == 4 && history[2].value("role", "") == "tool" && history[3].value("role", "") == "tool" &&
                   history[3].value("content", "").find("cancelled") != std::string::npos,
               "agent: every requested tool call has a result, so the history stays valid");
    }

    }
    // ------------------------------------------------------------------
    // HTTP: POST /sessions/:id/cancel ends a running /chat/stream turn
    // ------------------------------------------------------------------
    {
        Env env("atlas_cancel_smoke_http");
        atlas::core::WorkspaceManager workspaces(env.root / "workspaces");
        SlowBackend backend;
        Agent agent(backend, env.tools, *env.sessions, "m");

        // APIServer binds in run(); pick a free port by probing.
        const int port = freePort();
        atlas::api::APIServer server(agent, workspaces, *env.sessions, "127.0.0.1", port);
        std::thread runner([&] { server.run(); });
        httplib::Client client("127.0.0.1", port);
        client.set_read_timeout(5, 0);
        for (int i = 0; i < 100; ++i) {
            if (client.Get("/health")) break;
            sleepMs(20);
        }

        auto idle = client.Post("/sessions/nothing/cancel", "{}", "application/json");
        expect(idle && idle->status == 200 && json::parse(idle->body).value("cancelled", true) == false,
               "http: cancel with no running turn answers cancelled:false");

        std::string body;
        std::thread turn([&] {
            httplib::Client c("127.0.0.1", port);
            c.set_read_timeout(10, 0);
            auto res = c.Post("/chat/stream", json{{"session_id", "s1"}, {"message", "hi"}, {"stream_deltas", true}}.dump(),
                              "application/json");
            if (res) body = res->body;
        });
        for (int i = 0; i < 100 && backend.streams == 0; ++i) sleepMs(20);

        auto second = client.Post("/chat/stream", json{{"session_id", "s1"}, {"message", "again"}}.dump(), "application/json");
        expect(second && second->status == 409, "http: a second turn in the same session is refused while one runs");

        const auto start = Clock::now();
        auto stopped = client.Post("/sessions/s1/cancel", "{}", "application/json");
        expect(stopped && stopped->status == 200 && json::parse(stopped->body).value("cancelled", false) == true,
               "http: cancel finds the running turn");
        turn.join();
        expect(secondsSince(start) < 2.0 && backend.observed_cancel, "http: the turn ended promptly because of the cancel");
        expect(body.find("\"type\":\"cancelled\"") != std::string::npos, "http: the stream ends with a cancelled event");

        // The session is free again.
        auto again = client.Post("/sessions/s1/cancel", "{}", "application/json");
        expect(again && json::parse(again->body).value("cancelled", true) == false, "http: the finished turn is no longer registered");

        // The client going away (tab closed) stops the turn too.
        backend.streams = 0;
        backend.observed_cancel = false;
        backend.chatty = true;
        {
            httplib::Client leaver("127.0.0.1", port);
            leaver.set_read_timeout(10, 0);
            httplib::Request request;
            request.method = "POST";
            request.path = "/chat/stream";
            request.set_header("Content-Type", "application/json");
            request.body = json{{"session_id", "s2"}, {"message", "hi"}, {"stream_deltas", true}}.dump();
            request.content_receiver = [](const char*, std::size_t, std::uint64_t, std::uint64_t) {
                return false; // hang up after the first piece
            };
            (void)leaver.send(request);
        }
        for (int i = 0; i < 100 && !backend.observed_cancel; ++i) sleepMs(20);
        expect(backend.observed_cancel, "http: a client that hangs up stops its turn");
        sleepMs(100);
        auto after = client.Post("/sessions/s2/cancel", "{}", "application/json");
        expect(after && json::parse(after->body).value("cancelled", true) == false,
               "http: ...and the session is free again afterwards");

        server.stop();
        runner.join();
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return failures == 0 ? 0 : 1;
}
