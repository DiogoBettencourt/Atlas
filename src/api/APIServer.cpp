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
    // 1. Apply CORS headers ONCE to all incoming requests globally
    server_.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    // 2. Handle OPTIONS preflight requests globally (returns 200 OK for any route)
    server_.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    server_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(nlohmann::json{{"status", "ok"}}.dump(), "application/json");
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