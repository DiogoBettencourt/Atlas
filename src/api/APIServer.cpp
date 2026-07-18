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
    server_.Post("/chat", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto json_req = nlohmann::json::parse(req.body);
            
            if (!json_req.contains("message") || !json_req["message"].is_string()) {
                res.status = 400;
                res.set_content(R"({"error": "Missing or invalid 'message' field."})", "application/json");
                return;
            }

            std::string user_message = json_req["message"];
            
            std::string session_id;
            if (json_req.contains("session_id") && json_req["session_id"].is_string() && !json_req["session_id"].get<std::string>().empty()) {
                session_id = json_req["session_id"].get<std::string>();
            } else {
                session_id = agent_.getSessionManager().createSession();
            }

            std::cout << "\n[API] Received message for session: " << session_id << "\n";

            std::string agent_response = agent_.chat(user_message, session_id);

            nlohmann::json json_res = {
                {"session_id", session_id},
                {"response", agent_response}
            };
            res.set_content(json_res.dump(), "application/json");
            
        } catch (const nlohmann::json::parse_error&) {
            res.status = 400;
            res.set_content(R"({"error": "Invalid JSON format."})", "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(R"({"error": "Internal server error."})", "application/json");
            std::cerr << "API Error: " << e.what() << "\n";
        }
    });
}

void APIServer::start() {
    std::cout << "Starting API Server on http://" << host_ << ":" << port_ << "\n";
    
    // FIX: Use std::make_unique to assign the thread to the pointer
    server_thread_ = std::make_unique<std::thread>([this]() {
        server_.listen(host_, port_);
    });
}

void APIServer::stop() {
    if (server_.is_running()) {
        server_.stop();
    }
    // FIX: Check if the pointer exists AND if the thread is joinable
    if (server_thread_ && server_thread_->joinable()) {
        server_thread_->join();
    }
}

} // namespace atlas::api