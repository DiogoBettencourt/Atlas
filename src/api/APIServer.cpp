#include "atlas/api/APIServer.hpp"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

namespace atlas::api {

APIServer::APIServer(agent::Agent& agent, core::WorkspaceManager& workspace_manager,
                     core::SessionManager& session_manager,
                     std::string bind_address, int port)
    : agent_(agent),
      workspace_manager_(workspace_manager),
      session_manager_(session_manager),
      bind_address_(std::move(bind_address)),
      port_(port) {
    registerRoutes();
}

void APIServer::registerRoutes() {
    // 1. Apply CORS headers ONCE to all incoming requests globally
    server_.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    // 2. Handle OPTIONS preflight requests globally (returns 200 OK for any route)
    server_.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    server_.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
        nlohmann::json body = info_;
        body["status"] = "ok";
        res.set_content(body.dump(), "application/json");
    });

    server_.Get("/sessions", [this](const httplib::Request&, httplib::Response& res) {
        nlohmann::json sessions = nlohmann::json::array();
        for (const auto& info : session_manager_.listSessions()) {
            sessions.push_back({{"id", info.id},
                                {"title", info.title},
                                {"message_count", info.message_count},
                                {"updated_at", info.updated_at}});
        }
        res.set_content(nlohmann::json{{"sessions", sessions}}.dump(), "application/json");
    });

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

    // Path-param routes (regex-matched by cpp-httplib; req.matches[1] is
    // the captured session id) rather than the body-based style /chat and
    // /chat/stream use - these two are plain reads/deletes keyed entirely
    // by the id in the URL, with nothing else to pass, so there's no body
    // to justify POST-with-JSON for. Both just forward to SessionManager
    // methods (getHistory/resetSession) that already existed for Agent's
    // own internal use - this is the first thing to expose them over HTTP.
    server_.Get(R"(/sessions/([^/]+)/history)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string session_id = req.matches[1];
        res.set_content(
            nlohmann::json{{"session_id", session_id},
                           {"messages", session_manager_.getHistory(session_id)}}
                .dump(),
            "application/json");
    });

    server_.Delete(R"(/sessions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string session_id = req.matches[1];
        session_manager_.resetSession(session_id);
        res.set_content(
            nlohmann::json{{"session_id", session_id}, {"deleted", true}}.dump(),
            "application/json");
    });

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
        // Opt-in: a client that wants the model's text as it is generated
        // (AtlasUI) sets "stream_deltas": true. Others see the same events as before.
        const bool stream_deltas = body.value("stream_deltas", false);

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
            [this, message, session_id, workspace_root, stream_deltas](std::size_t /*offset*/,
                                                        httplib::DataSink& sink) {
                auto emit = [&sink](const nlohmann::json& event) {
                    std::string line = event.dump();
                    line.push_back('\n');
                    sink.write(line.data(), line.size());
                };

                try {
                    std::string reply =
                        agent_.chat(message, session_id, workspace_root.string(), emit, stream_deltas);
                    (void)reply;
                } catch (const std::exception& e) {
                    emit(nlohmann::json{{"type", "error"}, {"message", e.what()}});
                }

                sink.done();
                return true;
            });
    });
}

void APIServer::setInfo(nlohmann::json info) {
    info_ = std::move(info);
}

bool APIServer::serveUi(const std::string& directory) {
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return false;
    }
    if (!server_.set_mount_point("/ui", directory)) {
        return false;
    }
    server_.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/ui/");
    });
    server_.Get("/ui", [](const httplib::Request&, httplib::Response& res) {
        res.set_redirect("/ui/");
    });
    return true;
}

void APIServer::run() {
    std::cout << "Atlas API server listening on http://" << bind_address_ << ":" << port_
              << " (POST /chat, POST /chat/stream, GET /health, "
                 "GET /sessions, GET /sessions/:id/history, DELETE /sessions/:id)" << std::endl;
    if (!server_.listen(bind_address_, port_)) {
        throw std::runtime_error("APIServer: failed to bind " + bind_address_ + ":" +
                                 std::to_string(port_));
    }
}

void APIServer::stop() {
    server_.stop();
}

} // namespace atlas::api