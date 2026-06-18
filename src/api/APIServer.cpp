#include "atlas/api/APIServer.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

namespace atlas::api {

APIServer::APIServer(agent::Agent& agent, const std::string& host, int port)
    : agent_(agent), host_(host), port_(port) {
    setupRoutes();
}

APIServer::~APIServer() {
    stop();
}

void APIServer::setupRoutes() {
    // Basic Health Check Endpoint
    server_.Get("/status", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status": "Atlas AI Online", "version": "0.1.0"})", "application/json");
    });

    // Main Chat Endpoint
    server_.Post("/chat", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_req = nlohmann::json::parse(req.body);
            if (!json_req.contains("message") || !json_req["message"].is_string()) {
                res.status = 400;
                res.set_content(R"({"error": "Missing or invalid 'message' field"})", "application/json");
                return;
            }

            std::string user_message = json_req["message"];
            std::cout << "\n[API] Received message from client.\n";

            // Pass the message to our autonomous agent
            std::string agent_response = agent_.chat(user_message);

            // Package the response
            nlohmann::json json_res = {
                {"response", agent_response}
            };
            res.set_content(json_res.dump(), "application/json");

        } catch (const std::exception& e) {
            res.status = 500;
            nlohmann::json err = {{"error", e.what()}};
            res.set_content(err.dump(), "application/json");
        }
    });
}

void APIServer::start() {
    std::cout << "[API] Starting server on http://" << host_ << ":" << port_ << "\n";
    // Run the server in a background thread so it doesn't block the main application loop
    server_thread_ = std::make_unique<std::thread>([this]() {
        server_.listen(host_, port_);
    });
}

void APIServer::stop() {
    if (server_.is_running()) {
        server_.stop();
    }
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
        std::cout << "[API] Server stopped gracefully.\n";
    }
}

} // namespace atlas::api
