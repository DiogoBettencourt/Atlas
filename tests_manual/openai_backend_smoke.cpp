// Manual smoke test for OpenAICompatBackend. Three layers:
//   1. the pure message translation (Atlas shape <-> OpenAI wire shape),
//   2. the HTTP behavior against a local stub server (path, auth header,
//      error mapping), and
//   3. a full Agent tool-call round trip through the stub, which is the
//      closest we can get to a real llama-server without a GPU or model.
#include "atlas/agent/Agent.hpp"
#include "atlas/agent/OpenAICompatBackend.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/core/SymbolIndexer.hpp"
#include "atlas/storage/FileStorageManager.hpp"
#include "atlas/tools/ToolManager.hpp"

#include <httplib.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
using atlas::agent::Agent;
using atlas::agent::OpenAICompatBackend;
namespace fs = std::filesystem;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}

// Calls fn and reports whether it threw an exception whose message contains `needle`.
bool throwsWith(const std::function<void()>& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::exception& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
    }
    return false;
}

// A throwaway HTTP server on an ephemeral localhost port standing in for llama-server.
class StubServer {
public:
    using Handler = std::function<void(const httplib::Request&, httplib::Response&)>;

    explicit StubServer(Handler handler, const std::string& path = "/v1/chat/completions") {
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

    std::string base() const { return "http://127.0.0.1:" + std::to_string(port_); }

private:
    httplib::Server server_;
    int port_ = 0;
    std::thread thread_;
};

json reply(const json& message) { return {{"choices", json::array({{{"message", message}, {"finish_reason", "stop"}}})}}; }

void respond(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

json readFileCall(const std::string& id, const std::string& path) {
    return {{"id", id},
            {"type", "function"},
            {"function", {{"name", "read_file"}, {"arguments", json{{"path", path}}.dump()}}}};
}

} // namespace

int main() {
    // =================================================================
    // 1. Translation: Atlas history -> wire
    // =================================================================
    {
        json history = json::array({
            {{"role", "system"}, {"content", "be brief"}},
            {{"role", "user"}, {"content", "read a.txt"}},
            {{"role", "assistant"},
             {"content", ""},
             {"thinking", "I should read it"},
             {"tool_calls", json::array({{{"function", {{"name", "read_file"}, {"arguments", {{"path", "a.txt"}}}}}}})}},
            {{"role", "tool"}, {"name", "read_file"}, {"content", "{\"content\":\"hi\"}"}},
            {{"role", "assistant"}, {"content", "it says hi"}},
        });
        json wire = OpenAICompatBackend::toWireMessages(history);
        expect(wire.size() == 5, "toWire: message count preserved");
        expect(wire[0]["role"] == "system" && wire[0]["content"] == "be brief", "toWire: system message passes through");
        const json& call = wire[2]["tool_calls"][0];
        expect(call["id"] == "call_1", "toWire: id synthesized for a call without one");
        expect(call["type"] == "function", "toWire: call type is function");
        expect(call["function"]["arguments"].is_string(), "toWire: arguments are a JSON string on the wire");
        expect(json::parse(call["function"]["arguments"].get<std::string>()) == json({{"path", "a.txt"}}),
               "toWire: stringified arguments round-trip");
        expect(!wire[2].contains("thinking"), "toWire: thinking is dropped from history");
        expect(wire[3]["role"] == "tool" && wire[3]["tool_call_id"] == "call_1",
               "toWire: tool result paired with the call id");
        expect(!wire[3].contains("name"), "toWire: tool message has no non-standard name field");
    }
    {
        json history = json::array({
            {{"role", "assistant"},
             {"content", ""},
             {"tool_calls", json::array({{{"function", {{"name", "a"}, {"arguments", json::object()}}}},
                                         {{"function", {{"name", "b"}, {"arguments", json::object()}}}}})}},
            {{"role", "tool"}, {"content", "first"}},
            {{"role", "tool"}, {"content", "second"}},
        });
        json wire = OpenAICompatBackend::toWireMessages(history);
        expect(wire[0]["tool_calls"][0]["id"] == "call_1" && wire[0]["tool_calls"][1]["id"] == "call_2",
               "toWire: parallel calls get distinct ids");
        expect(wire[1]["tool_call_id"] == "call_1" && wire[2]["tool_call_id"] == "call_2",
               "toWire: tool results pair with calls in order");
    }
    {
        json history = json::array({
            {{"role", "assistant"},
             {"content", ""},
             {"tool_calls", json::array({{{"id", "abc"},
                                          {"function", {{"name", "a"}, {"arguments", "{\"x\":1}"}}}}})}},
            {{"role", "tool"}, {"content", "r"}},
        });
        json wire = OpenAICompatBackend::toWireMessages(history);
        expect(wire[0]["tool_calls"][0]["id"] == "abc" && wire[1]["tool_call_id"] == "abc",
               "toWire: an existing call id is reused and paired");
        expect(wire[0]["tool_calls"][0]["function"]["arguments"] == "{\"x\":1}",
               "toWire: string arguments pass through unchanged");
    }
    {
        json history = json::array({{{"role", "tool"}, {"content", "orphan"}}});
        json wire = OpenAICompatBackend::toWireMessages(history);
        expect(wire[0]["tool_call_id"].is_string() && !wire[0]["tool_call_id"].get<std::string>().empty(),
               "toWire: a tool result whose call was trimmed away still gets an id");
    }

    // =================================================================
    // 1b. Translation: wire -> Atlas shape
    // =================================================================
    {
        json wire = {{"role", "assistant"},
                     {"content", nullptr},
                     {"tool_calls", json::array({readFileCall("call_9", "x.cpp")})}};
        json msg = OpenAICompatBackend::fromWireMessage(wire);
        expect(msg["content"] == "", "fromWire: null content becomes an empty string");
        expect(msg["tool_calls"][0]["function"]["arguments"] == json({{"path", "x.cpp"}}),
               "fromWire: string arguments are parsed into an object");
        expect(msg["tool_calls"][0]["id"] == "call_9", "fromWire: call id is preserved");
        expect(!msg.contains("thinking"), "fromWire: no thinking key when there is none");
    }
    {
        json msg = OpenAICompatBackend::fromWireMessage(
            {{"role", "assistant"}, {"content", "42"}, {"reasoning_content", "worked it out"}});
        expect(msg["thinking"] == "worked it out" && msg["content"] == "42", "fromWire: reasoning_content -> thinking");
    }
    {
        json msg = OpenAICompatBackend::fromWireMessage(
            {{"role", "assistant"}, {"content", "42"}, {"reasoning", "alt field"}});
        expect(msg["thinking"] == "alt field", "fromWire: `reasoning` -> thinking");
    }
    {
        json msg = OpenAICompatBackend::fromWireMessage(
            {{"role", "assistant"}, {"content", "<think>\nplan it\n</think>\n\nthe answer"}});
        expect(msg["thinking"] == "plan it", "fromWire: inline <think> block is extracted");
        expect(msg["content"] == "the answer", "fromWire: content is what follows the </think>");
    }
    {
        json msg = OpenAICompatBackend::fromWireMessage({{"role", "assistant"}, {"content", "<think>never closed"}});
        expect(!msg.contains("thinking") && msg["content"] == "<think>never closed",
               "fromWire: an unterminated <think> is left untouched");
    }
    {
        json wire = {{"role", "assistant"},
                     {"content", ""},
                     {"tool_calls", json::array({{{"function", {{"name", "t"}, {"arguments", "{not json"}}}}})}};
        json msg = OpenAICompatBackend::fromWireMessage(wire);
        expect(msg["tool_calls"][0]["function"]["arguments"] == "{not json",
               "fromWire: unparseable arguments are kept as text, not erased");
    }

    // =================================================================
    // 2. HTTP behavior against a stub server
    // =================================================================
    {
        std::vector<json> bodies;
        std::vector<std::string> auth;
        StubServer server([&](const httplib::Request& req, httplib::Response& res) {
            bodies.push_back(json::parse(req.body));
            auth.push_back(req.get_header_value("Authorization"));
            respond(res, reply({{"role", "assistant"}, {"content", "pong"}}));
        });

        OpenAICompatBackend backend(server.base());
        json tools = json::array({{{"type", "function"}, {"function", {{"name", "read_file"}}}}});
        json out = backend.chat("some-model", json::array({{{"role", "user"}, {"content", "ping"}}}), tools);
        expect(out["content"] == "pong" && out["role"] == "assistant", "http: reply is translated to the canonical shape");
        expect(bodies.size() == 1 && bodies[0]["model"] == "some-model", "http: model is sent");
        expect(bodies[0]["stream"] == false, "http: non-streaming request");
        expect(bodies[0]["messages"][0]["content"] == "ping", "http: messages are sent");
        expect(bodies[0]["tools"] == tools, "http: tool schemas are passed through unchanged");
        expect(auth[0].empty(), "http: no Authorization header without a key");
        expect(backend.name() == "openai", "http: backend name");

        (void)backend.chat("m", json::array({{{"role", "user"}, {"content", "x"}}}));
        expect(!bodies[1].contains("tools"), "http: no tools field when none are offered");
    }
    {
        std::string auth;
        StubServer server([&](const httplib::Request& req, httplib::Response& res) {
            auth = req.get_header_value("Authorization");
            respond(res, reply({{"role", "assistant"}, {"content", "ok"}}));
        });
        OpenAICompatBackend backend(server.base(), "sekret");
        (void)backend.chat("m", json::array({{{"role", "user"}, {"content", "x"}}}));
        expect(auth == "Bearer sekret", "http: API key is sent as a Bearer token");
    }
    {
        StubServer server([&](const httplib::Request&, httplib::Response& res) {
            respond(res, reply({{"role", "assistant"}, {"content", "ok"}}));
        });
        OpenAICompatBackend with_v1(server.base() + "/v1");
        OpenAICompatBackend with_slash(server.base() + "/v1/");
        OpenAICompatBackend with_bare_slash(server.base() + "/");
        json msgs = json::array({{{"role", "user"}, {"content", "x"}}});
        expect(with_v1.chat("m", msgs)["content"] == "ok", "url: base ending in /v1 works");
        expect(with_slash.chat("m", msgs)["content"] == "ok", "url: base ending in /v1/ works");
        expect(with_bare_slash.chat("m", msgs)["content"] == "ok", "url: base ending in / works");
    }
    {
        StubServer prefixed([&](const httplib::Request&, httplib::Response& res) {
            respond(res, reply({{"role", "assistant"}, {"content", "prefixed"}}));
        }, "/api/v1/chat/completions");
        OpenAICompatBackend backend(prefixed.base() + "/api");
        expect(backend.chat("m", json::array({{{"role", "user"}, {"content", "x"}}}))["content"] == "prefixed",
               "url: a custom path prefix is honored");
    }
    {
        json msgs = json::array({{{"role", "user"}, {"content", "x"}}});
        {
            StubServer s([](const httplib::Request&, httplib::Response& res) { respond(res, {{"error", "no"}}, 401); });
            OpenAICompatBackend b(s.base());
            expect(throwsWith([&] { (void)b.chat("m", msgs); }, "API key"), "errors: 401 points at the API key");
        }
        {
            StubServer s([](const httplib::Request&, httplib::Response& res) { respond(res, {{"error", "boom"}}, 500); });
            OpenAICompatBackend b(s.base());
            expect(throwsWith([&] { (void)b.chat("m", msgs); }, "HTTP 500"), "errors: non-200 status is reported");
        }
        {
            StubServer s([](const httplib::Request&, httplib::Response& res) { res.set_content("not json", "text/plain"); });
            OpenAICompatBackend b(s.base());
            expect(throwsWith([&] { (void)b.chat("m", msgs); }, "malformed"), "errors: non-JSON body is malformed");
        }
        {
            StubServer s([](const httplib::Request&, httplib::Response& res) { respond(res, {{"choices", json::array()}}); });
            OpenAICompatBackend b(s.base());
            expect(throwsWith([&] { (void)b.chat("m", msgs); }, "malformed"), "errors: empty choices is malformed");
        }
        {
            OpenAICompatBackend b("http://127.0.0.1:1");
            expect(throwsWith([&] { (void)b.chat("m", msgs); }, "failed to reach"),
                   "errors: unreachable server is reported");
        }
    }
    {
        auto invalid = [](const std::string& base) {
            try {
                OpenAICompatBackend b(base);
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        };
        expect(invalid("localhost:8081"), "ctor: a base without a scheme is rejected");
        expect(invalid("ftp://host"), "ctor: a non-http scheme is rejected");
        expect(invalid("http://"), "ctor: a missing host is rejected");
    }

    // =================================================================
    // 3. Full Agent round trip through the stub
    // =================================================================
    {
        fs::path root = fs::temp_directory_path() / "atlas_openai_backend_smoke";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::path workspace = root / "ws";
        fs::create_directories(workspace, ec);
        {
            std::ofstream f(workspace / "hello.txt");
            f << "hello from the workspace";
        }

        std::vector<json> bodies;
        StubServer server([&](const httplib::Request& req, httplib::Response& res) {
            bodies.push_back(json::parse(req.body));
            if (bodies.size() == 1) {
                respond(res, reply({{"role", "assistant"},
                                    {"content", nullptr},
                                    {"reasoning_content", "I need to read the file"},
                                    {"tool_calls", json::array({readFileCall("call_abc", "hello.txt")})}}));
            } else {
                respond(res, reply({{"role", "assistant"}, {"content", "the file says hello"}}));
            }
        });

        atlas::storage::FileStorageManager storage(root / "storage");
        atlas::core::SessionManager sessions(storage);
        atlas::core::SymbolIndexer indexer;
        atlas::tools::ToolManager tools;
        tools.registerDefaultTools(indexer);

        OpenAICompatBackend backend(server.base());
        Agent agent(backend, tools, sessions, "any-model");

        std::vector<std::string> types;
        std::vector<json> events;
        std::string final_reply = agent.chat("what is in hello.txt?", "s1", workspace.string(), [&](const json& e) {
            types.push_back(e.value("type", std::string{}));
            events.push_back(e);
        });

        expect(final_reply == "the file says hello", "agent: final reply comes back through the backend");
        expect(types == std::vector<std::string>{"iteration_start", "thinking", "tool_call", "tool_result",
                                                 "iteration_start", "final"},
               "agent: event order includes thinking mapped from reasoning_content");
        expect(events[1]["content"] == "I need to read the file", "agent: thinking text is the reasoning_content");
        expect(events[3]["result"].value("content", std::string{}) == "hello from the workspace",
               "agent: the tool ran against the workspace using the parsed arguments");

        expect(bodies.size() == 2, "agent: exactly two backend requests");
        if (bodies.size() == 2) {
            const json& msgs = bodies[1]["messages"];
            const json* assistant = nullptr;
            const json* tool = nullptr;
            for (const auto& m : msgs) {
                if (m["role"] == "assistant" && m.contains("tool_calls")) assistant = &m;
                if (m["role"] == "tool") tool = &m;
            }
            expect(assistant != nullptr && tool != nullptr, "agent: second request replays the call and its result");
            if (assistant != nullptr && tool != nullptr) {
                expect((*assistant)["tool_calls"][0]["function"]["arguments"].is_string(),
                       "agent: replayed call has string arguments");
                expect((*assistant)["tool_calls"][0]["id"] == "call_abc" && (*tool)["tool_call_id"] == "call_abc",
                       "agent: the server's call id is echoed on the tool result");
                expect(!assistant->contains("thinking"), "agent: replayed history has no thinking field");
            }
        }
        fs::remove_all(root, ec);
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
