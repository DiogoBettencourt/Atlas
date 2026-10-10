// Manual smoke test for streaming: OllamaBackend::chatStream (NDJSON),
// OpenAICompatBackend::chatStream (SSE), and Agent reporting the live
// pieces as thinking_delta / content_delta events. Runs against local stub
// servers that send their streams in small, awkwardly cut chunks, since
// real servers split lines (and UTF-8 characters) wherever they like.
#include "atlas/agent/Agent.hpp"
#include "atlas/agent/OllamaBackend.hpp"
#include "atlas/agent/OpenAICompatBackend.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/core/SymbolIndexer.hpp"
#include "atlas/storage/FileStorageManager.hpp"
#include "atlas/tools/ToolManager.hpp"

#include <httplib.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using json = nlohmann::json;
using atlas::agent::Agent;
using atlas::agent::OllamaBackend;
using atlas::agent::OpenAICompatBackend;
namespace fs = std::filesystem;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}

bool throwsWith(const std::function<void()>& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::exception& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    }
    return false;
}

// Sends `body` as a chunked response in pieces of `chunk` bytes.
void streamBody(httplib::Response& res, std::string body, std::size_t chunk, const char* type) {
    res.set_chunked_content_provider(type, [body, chunk](std::size_t offset, httplib::DataSink& sink) {
        if (offset >= body.size()) {
            sink.done();
            return true;
        }
        const std::size_t n = std::min(chunk, body.size() - offset);
        sink.write(body.data() + offset, n);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return true;
    });
}

class StubServer {
public:
    using Handler = std::function<void(const httplib::Request&, httplib::Response&)>;

    StubServer(const std::string& path, Handler handler) {
        server_.Post(path, [handler](const httplib::Request& req, httplib::Response& res) { handler(req, res); });
        port_ = server_.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        while (!server_.is_running()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ~StubServer() {
        server_.stop();
        thread_.join();
    }
    StubServer(const StubServer&) = delete;
    StubServer& operator=(const StubServer&) = delete;

    int port() const { return port_; }
    std::string base() const { return "http://127.0.0.1:" + std::to_string(port_); }

private:
    httplib::Server server_;
    int port_ = 0;
    std::thread thread_;
};

using Deltas = std::vector<std::pair<std::string, std::string>>;

std::string joined(const Deltas& deltas, const std::string& kind) {
    std::string out;
    for (const auto& [k, text] : deltas) {
        if (k == kind) out += text;
    }
    return out;
}

std::string ollamaLine(const json& message, bool done = false) {
    return json{{"model", "m"}, {"message", message}, {"done", done}}.dump() + "\n";
}

std::string sseLine(const json& delta) {
    return "data: " + json{{"choices", json::array({{{"delta", delta}}})}}.dump() + "\n\n";
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // Ollama: NDJSON stream
    // ------------------------------------------------------------------
    {
        std::string seen_body;
        std::string stream = ollamaLine({{"role", "assistant"}, {"thinking", "The user "}}) +
                             ollamaLine({{"role", "assistant"}, {"thinking", "wants caf\xC3\xA9."}}) +
                             ollamaLine({{"role", "assistant"}, {"content", "Hel"}}) +
                             ollamaLine({{"role", "assistant"}, {"content", "lo!"}}) +
                             ollamaLine({{"role", "assistant"}, {"content", ""}}, true);
        StubServer server("/api/chat", [&](const httplib::Request& req, httplib::Response& res) {
            seen_body = req.body;
            streamBody(res, stream, 7, "application/x-ndjson");
        });
        OllamaBackend backend("127.0.0.1", server.port());

        Deltas deltas;
        json message = backend.chatStream("m", json::array({{{"role", "user"}, {"content", "hi"}}}), json::array(),
                                          [&](const std::string& k, const std::string& t) { deltas.emplace_back(k, t); });

        expect(json::parse(seen_body).value("stream", false) == true, "ollama: asks for stream:true");
        expect(deltas.size() == 4, "ollama: one delta per piece, in order (7-byte chunks reassembled into lines)");
        expect(joined(deltas, "thinking") == "The user wants caf\xC3\xA9.", "ollama: thinking deltas, UTF-8 intact");
        expect(joined(deltas, "content") == "Hello!", "ollama: content deltas");
        expect(deltas.front().first == "thinking" && deltas.back().first == "content", "ollama: thinking arrives before content");
        expect(message.value("content", "") == "Hello!" && message.value("thinking", "") == "The user wants caf\xC3\xA9.",
               "ollama: returned message is the complete one");
        expect(message.value("role", "") == "assistant" && !message.contains("tool_calls"), "ollama: no tool_calls key when none came");

        // Without a callback it still works.
        json plain = backend.chatStream("m", json::array(), json::array(), nullptr);
        expect(plain.value("content", "") == "Hello!", "ollama: a null callback is fine");
    }

    {
        std::string stream = ollamaLine({{"role", "assistant"}, {"thinking", "need a file"}}) +
                             ollamaLine({{"role", "assistant"}, {"content", ""},
                                         {"tool_calls", json::array({{{"function", {{"name", "read_file"},
                                                                                    {"arguments", {{"path", "a.txt"}}}}}}})}}) +
                             ollamaLine({{"role", "assistant"}, {"content", ""}}, true);
        StubServer server("/api/chat", [&](const httplib::Request&, httplib::Response& res) {
            streamBody(res, stream, 1000, "application/x-ndjson");
        });
        OllamaBackend backend("127.0.0.1", server.port());
        json message = backend.chatStream("m", json::array(), json::array(), [](const std::string&, const std::string&) {});
        expect(message.contains("tool_calls") && message["tool_calls"].size() == 1 &&
                   message["tool_calls"][0]["function"]["arguments"]["path"] == "a.txt",
               "ollama: tool calls collected with their arguments as an object");
        expect(message.value("content", "x") == "", "ollama: tool turn has empty content");
    }

    {
        StubServer bad("/api/chat", [](const httplib::Request&, httplib::Response& res) {
            res.status = 404;
            res.set_content("{\"error\":\"model 'nope' not found\"}", "application/json");
        });
        OllamaBackend backend("127.0.0.1", bad.port());
        expect(throwsWith([&] { (void)backend.chatStream("nope", json::array(), json::array(), nullptr); }, "HTTP 404"),
               "ollama: a non-200 status throws with the status");
        expect(throwsWith([&] { (void)backend.chatStream("nope", json::array(), json::array(), nullptr); }, "not found"),
               "ollama: ...and the server's message");
    }

    {
        std::string stream = ollamaLine({{"role", "assistant"}, {"content", "partial"}}) +
                             json{{"error", "out of memory"}}.dump() + "\n";
        StubServer server("/api/chat", [&](const httplib::Request&, httplib::Response& res) {
            streamBody(res, stream, 10, "application/x-ndjson");
        });
        OllamaBackend backend("127.0.0.1", server.port());
        expect(throwsWith([&] { (void)backend.chatStream("m", json::array(), json::array(), nullptr); }, "out of memory"),
               "ollama: an error line inside the stream throws");
    }

    {
        OllamaBackend backend("127.0.0.1", 1); // nothing listens on port 1
        expect(throwsWith([&] { (void)backend.chatStream("m", json::array(), json::array(), nullptr); }, "ollama serve"),
               "ollama: unreachable server gives the usual hint");
    }

    {
        std::string stream = ollamaLine({{"role", "assistant"}, {"content", "a"}}) +
                             ollamaLine({{"role", "assistant"}, {"content", "b"}}, true);
        StubServer server("/api/chat", [&](const httplib::Request&, httplib::Response& res) {
            streamBody(res, stream, 50, "application/x-ndjson");
        });
        OllamaBackend backend("127.0.0.1", server.port());
        expect(throwsWith([&] {
                   (void)backend.chatStream("m", json::array(), json::array(),
                                            [](const std::string&, const std::string&) { throw std::runtime_error("callback boom"); });
               }, "callback boom"),
               "ollama: an exception from the delta callback propagates to the caller");
    }

    // ------------------------------------------------------------------
    // OpenAI-compatible: SSE stream
    // ------------------------------------------------------------------
    {
        std::string seen_auth;
        std::string seen_body;
        std::string stream = ": keep-alive comment\r\n\r\n" +
                             sseLine({{"role", "assistant"}, {"reasoning_content", "Let me "}}) +
                             sseLine({{"reasoning_content", "think."}}) + sseLine({{"content", "The answer"}}) +
                             sseLine({{"content", " is 4."}}) + "data: [DONE]\n\n";
        StubServer server("/v1/chat/completions", [&](const httplib::Request& req, httplib::Response& res) {
            seen_auth = req.get_header_value("Authorization");
            seen_body = req.body;
            streamBody(res, stream, 9, "text/event-stream");
        });
        OpenAICompatBackend backend(server.base(), "sekret");

        Deltas deltas;
        json message = backend.chatStream("m", json::array({{{"role", "user"}, {"content", "2+2?"}}}), json::array(),
                                          [&](const std::string& k, const std::string& t) { deltas.emplace_back(k, t); });
        expect(json::parse(seen_body).value("stream", false) == true, "openai: asks for stream:true");
        expect(seen_auth == "Bearer sekret", "openai: sends the API key");
        expect(joined(deltas, "thinking") == "Let me think.", "openai: reasoning_content becomes thinking deltas");
        expect(joined(deltas, "content") == "The answer is 4.", "openai: content deltas");
        expect(message.value("content", "") == "The answer is 4." && message.value("thinking", "") == "Let me think.",
               "openai: returned message is the complete canonical one");
    }

    {
        // Tool call split across many chunks, two calls, arguments arriving as string fragments.
        std::string stream =
            sseLine({{"tool_calls", json::array({{{"index", 0}, {"id", "call_a"}, {"type", "function"},
                                                  {"function", {{"name", "read_file"}, {"arguments", ""}}}}})}}) +
            sseLine({{"tool_calls", json::array({{{"index", 0}, {"function", {{"arguments", "{\"pa"}}}}})}}) +
            sseLine({{"tool_calls", json::array({{{"index", 0}, {"function", {{"arguments", "th\":\"x.txt\"}"}}}}})}}) +
            sseLine({{"tool_calls", json::array({{{"index", 1}, {"id", "call_b"}, {"type", "function"},
                                                  {"function", {{"name", "list_directory"}, {"arguments", "{}"}}}}})}}) +
            "data: [DONE]\n\n";
        StubServer server("/v1/chat/completions", [&](const httplib::Request&, httplib::Response& res) {
            streamBody(res, stream, 5, "text/event-stream");
        });
        OpenAICompatBackend backend(server.base());
        json message = backend.chatStream("m", json::array(), json::array(), [](const std::string&, const std::string&) {});
        expect(message.contains("tool_calls") && message["tool_calls"].size() == 2, "openai: two streamed tool calls assembled");
        if (message.contains("tool_calls") && message["tool_calls"].size() == 2) {
            expect(message["tool_calls"][0]["function"]["name"] == "read_file" &&
                       message["tool_calls"][0]["function"]["arguments"]["path"] == "x.txt",
                   "openai: fragmented arguments are joined and parsed into an object");
            expect(message["tool_calls"][0].value("id", "") == "call_a" && message["tool_calls"][1].value("id", "") == "call_b",
                   "openai: server tool-call ids kept, in index order");
        }
    }

    {
        // Inline <think> split across chunk boundaries (a server with reasoning parsing off).
        std::string stream = sseLine({{"content", "<thi"}}) + sseLine({{"content", "nk>I should "}}) +
                             sseLine({{"content", "check.</th"}}) + sseLine({{"content", "ink>\n\nDone"}}) +
                             sseLine({{"content", "!"}}) + "data: [DONE]\n\n";
        StubServer server("/v1/chat/completions", [&](const httplib::Request&, httplib::Response& res) {
            streamBody(res, stream, 64, "text/event-stream");
        });
        OpenAICompatBackend backend(server.base());
        Deltas deltas;
        json message = backend.chatStream("m", json::array(), json::array(),
                                          [&](const std::string& k, const std::string& t) { deltas.emplace_back(k, t); });
        expect(joined(deltas, "thinking") == "I should check.", "openai: inline <think> block streamed as thinking");
        expect(joined(deltas, "content") == "Done!", "openai: text after </think> streamed as content, leading blank lines dropped");
        expect(message.value("thinking", "") == "I should check." && message.value("content", "") == "Done!",
               "openai: final message matches what the non-streaming path would give");
    }

    {
        // Content that merely starts with '<' is not mistaken for a think block.
        std::string stream = sseLine({{"content", "<"}}) + sseLine({{"content", "b>bold</b> text"}}) + "data: [DONE]\n\n";
        StubServer server("/v1/chat/completions", [&](const httplib::Request&, httplib::Response& res) {
            streamBody(res, stream, 64, "text/event-stream");
        });
        OpenAICompatBackend backend(server.base());
        Deltas deltas;
        json message = backend.chatStream("m", json::array(), json::array(),
                                          [&](const std::string& k, const std::string& t) { deltas.emplace_back(k, t); });
        expect(joined(deltas, "content") == "<b>bold</b> text" && joined(deltas, "thinking").empty(),
               "openai: '<b>' is content, not thinking");
        expect(message.value("content", "") == "<b>bold</b> text", "openai: ...and the final content is intact");
    }

    {
        StubServer denied("/v1/chat/completions", [](const httplib::Request&, httplib::Response& res) {
            res.status = 401;
            res.set_content("{}", "application/json");
        });
        OpenAICompatBackend backend(denied.base());
        expect(throwsWith([&] { (void)backend.chatStream("m", json::array(), json::array(), nullptr); }, "check the API key"),
               "openai: 401 points at the API key");

        std::string stream = sseLine({{"content", "x"}}) + "data: {\"error\":{\"message\":\"context too long\"}}\n\n";
        StubServer failing("/v1/chat/completions", [&](const httplib::Request&, httplib::Response& res) {
            streamBody(res, stream, 16, "text/event-stream");
        });
        OpenAICompatBackend backend2(failing.base());
        expect(throwsWith([&] { (void)backend2.chatStream("m", json::array(), json::array(), nullptr); }, "context too long"),
               "openai: an error object inside the stream throws with its message");

        OpenAICompatBackend dead("http://127.0.0.1:1");
        expect(throwsWith([&] { (void)dead.chatStream("m", json::array(), json::array(), nullptr); }, "failed to reach"),
               "openai: unreachable server");
    }

    // ------------------------------------------------------------------
    // Agent: live events ahead of the complete ones
    // ------------------------------------------------------------------
    {
        fs::path root = fs::temp_directory_path() / "atlas_streaming_backends_smoke";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "ws", ec);

        std::string stream = ollamaLine({{"role", "assistant"}, {"thinking", "Greeting. "}}) +
                             ollamaLine({{"role", "assistant"}, {"thinking", "Reply politely."}}) +
                             ollamaLine({{"role", "assistant"}, {"content", "Hi "}}) +
                             ollamaLine({{"role", "assistant"}, {"content", "there"}}, true);
        int requests = 0;
        std::vector<bool> stream_flags;
        StubServer server("/api/chat", [&](const httplib::Request& req, httplib::Response& res) {
            ++requests;
            stream_flags.push_back(json::parse(req.body).value("stream", false));
            streamBody(res, stream, 11, "application/x-ndjson");
        });

        OllamaBackend backend("127.0.0.1", server.port());
        atlas::storage::FileStorageManager storage(root / "store");
        atlas::core::SessionManager sessions(storage);
        atlas::core::SymbolIndexer indexer;
        atlas::tools::ToolManager tools;
        tools.registerDefaultTools(indexer);
        Agent agent(backend, tools, sessions, "m");

        std::vector<json> events;
        std::string reply = agent.chat("hello", "s1", (root / "ws").string(), [&](const json& e) { events.push_back(e); }, true);

        std::vector<std::string> types;
        for (const auto& e : events) types.push_back(e.value("type", std::string{}));
        const std::vector<std::string> expected = {"iteration_start", "thinking_delta", "thinking_delta", "content_delta",
                                                   "content_delta",   "thinking",       "final"};
        expect(types == expected, "agent: iteration_start, live deltas, then the complete thinking and final events");
        expect(reply == "Hi there", "agent: returns the full reply");
        if (events.size() == expected.size()) {
            expect(events[1]["content"] == "Greeting. " && events[3]["content"] == "Hi ", "agent: delta events carry just the new text");
            expect(events[5]["content"] == "Greeting. Reply politely." && events[6]["reply"] == "Hi there",
                   "agent: thinking and final events still carry the complete text");
        }
        auto history = sessions.getHistory("s1");
        expect(history.size() == 2 && history[1].value("content", "") == "Hi there",
               "agent: the saved assistant message is the whole reply, not a piece");

        // Streaming off: one plain request, no delta events.
        agent.setStreaming(false);
        // (the stub only speaks NDJSON, so reuse it: a non-stream request still parses
        // the first line of this body as one JSON document)
        std::vector<json> plain_events;
        StubServer plain("/api/chat", [&](const httplib::Request& req, httplib::Response& res) {
            stream_flags.push_back(json::parse(req.body).value("stream", true));
            res.set_content(json{{"message", {{"role", "assistant"}, {"content", "whole reply"}}}}.dump(), "application/json");
        });
        OllamaBackend plain_backend("127.0.0.1", plain.port());
        Agent agent2(plain_backend, tools, sessions, "m");
        agent2.setStreaming(false);
        std::string reply2 = agent2.chat("again", "s2", (root / "ws").string(), [&](const json& e) { plain_events.push_back(e); }, true);
        bool any_delta = false;
        for (const auto& e : plain_events) {
            const std::string t = e.value("type", std::string{});
            if (t == "thinking_delta" || t == "content_delta") any_delta = true;
        }
        expect(reply2 == "whole reply" && !any_delta, "agent: setStreaming(false) uses plain requests and emits no deltas");
        expect(stream_flags.size() == 2 && stream_flags[0] == true && stream_flags[1] == false,
               "agent: stream flag on the wire follows setStreaming");

        // Streaming on (the default) but the caller didn't ask for deltas:
        // a plain request and the same events as before.
        Agent agent3(plain_backend, tools, sessions, "m");
        std::vector<json> quiet_events;
        std::string reply3 = agent3.chat("third", "s3", (root / "ws").string(), [&](const json& e) { quiet_events.push_back(e); });
        std::vector<std::string> quiet_types;
        for (const auto& e : quiet_events) quiet_types.push_back(e.value("type", std::string{}));
        expect(reply3 == "whole reply" && quiet_types == std::vector<std::string>({"iteration_start", "final"}),
               "agent: no delta events unless the caller opts in");
        expect(stream_flags.size() == 3 && stream_flags[2] == false, "agent: ...and the backend is not asked to stream");

        fs::remove_all(root, ec);
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return failures == 0 ? 0 : 1;
}
