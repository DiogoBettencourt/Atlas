#include "atlas/api/APIServer.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

namespace atlas::api {

APIServer::APIServer(agent::Agent& agent, core::WorkspaceManager& workspace_manager,
                      std::string bind_address, int port)
    : agent_(agent),
      workspace_manager_(workspace_manager),
      bind_address_(std::move(bind_address)),
      port_(port) {
    registerRoutes();
}

void APIServer::registerRoutes() {
    server_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(nlohmann::json{{"status", "ok"}}.dump(), "application/json");
    });

    // POST /chat - synchronous. Waits for the whole ReAct loop to finish,
    // but the response now includes a full `steps` trace of every
    // tool_call/tool_result/assistant_thought the agent went through, not
    // just the bare final reply - so a client that doesn't want to deal
    // with streaming still gets full visibility after the fact.
    server_.Post("/chat", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("message") || !body.contains("session_id")) {
            res.status = 400;
            res.set_content(
                nlohmann::json{{"error", "expected JSON body: {session_id, message, workspace?}"}}
                    .dump(),
                "application/json");
            return;
        }

        std::string session_id = body["session_id"].get<std::string>();
        std::string message = body["message"].get<std::string>();
        std::string workspace_name = body.value("workspace", std::string("default"));

        try {
            std::filesystem::path workspace_root =
                workspace_manager_.createOrGetWorkspace(workspace_name);

            nlohmann::json steps = nlohmann::json::array();
            std::string reply = agent_.chat(message, session_id, workspace_root.string(),
                [&steps](const nlohmann::json& event) {
                    // The final/error events just restate the return value;
                    // keep the trace focused on what actually happened
                    // in-between (thoughts, tool calls, tool results).
                    std::string type = event.value("type", std::string{});
                    if (type == "final" || type == "error" || type == "iteration_start") {
                        return;
                    }
                    steps.push_back(event);
                });

            res.set_content(
                nlohmann::json{{"session_id", session_id}, {"reply", reply}, {"steps", steps}}
                    .dump(),
                "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // POST /chat/stream - live progress. Streams newline-delimited JSON
    // (NDJSON): one `{"type": ..., ...}` object per line, in real time as
    // the agent works, ending with a `{"type":"final","reply":"..."}` or
    // `{"type":"error",...}` line. Consume with any HTTP client that reads
    // the response body incrementally (curl, or PowerShell's
    // HttpClient/StreamReader rather than Invoke-RestMethod, which buffers
    // the whole response) - see README for a ready-to-paste example.
    server_.Post("/chat/stream", [this](const httplib::Request& req, httplib::Response& res) {
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded() || !body.contains("message") || !body.contains("session_id")) {
            res.status = 400;
            res.set_content(
                nlohmann::json{{"error", "expected JSON body: {session_id, message, workspace?}"}}
                    .dump(),
                "application/json");
            return;
        }

        std::string session_id = body["session_id"].get<std::string>();
        std::string message = body["message"].get<std::string>();
        std::string workspace_name = body.value("workspace", std::string("default"));

        std::filesystem::path workspace_root;
        try {
            workspace_root = workspace_manager_.createOrGetWorkspace(workspace_name);
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(nlohmann::json{{"error", e.what()}}.dump(), "application/json");
            return;
        }

        res.set_chunked_content_provider(
            "application/x-ndjson",
            [this, message, session_id, workspace_root](std::size_t /*offset*/,
                                                          httplib::DataSink& sink) {
                auto emit = [&sink](const nlohmann::json& event) {
                    std::string line = event.dump();
                    line.push_back('\n');
                    sink.write(line.data(), line.size());
                };

                try {
                    std::string reply =
                        agent_.chat(message, session_id, workspace_root.string(), emit);
                    // agent_.chat already emits a terminal "final" or
                    // "error" event internally, so there's nothing more to
                    // send here - just close the stream.
                    (void)reply;
                } catch (const std::exception& e) {
                    emit(nlohmann::json{{"type", "error"}, {"message", e.what()}});
                }

                sink.done();
                return true;
            });
    });
}

void APIServer::run() {
    std::cout << "Atlas API server listening on http://" << bind_address_ << ":" << port_
              << " (POST /chat, POST /chat/stream, GET /health)" << std::endl;
    if (!server_.listen(bind_address_, port_)) {
        throw std::runtime_error("APIServer: failed to bind " + bind_address_ + ":" +
                                  std::to_string(port_));
    }
}

void APIServer::stop() {
    server_.stop();
}

} // namespace atlas::api
