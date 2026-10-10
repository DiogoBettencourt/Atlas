#pragma once

#include "atlas/agent/Agent.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/core/WorkspaceManager.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>

namespace atlas::api {

// Hosts a local REST endpoint on top of cpp-httplib, mapping frontend
// requests (CLI, Qt, React, ...) onto backend agent sessions. Runs
// synchronously; call run() from a dedicated thread if the embedding
// application needs to do other work concurrently.
class APIServer {
public:
    APIServer(agent::Agent& agent, core::WorkspaceManager& workspace_manager,
              core::SessionManager& session_manager,
              std::string bind_address = "127.0.0.1", int port = 8080);

    APIServer(const APIServer&) = delete;
    APIServer& operator=(const APIServer&) = delete;
    APIServer(APIServer&&) = delete;
    APIServer& operator=(APIServer&&) = delete;

    // Extra fields reported by GET /health next to {"status":"ok"} (Atlas
    // version, backend, model) so a client can show what it is talking to.
    // Call before run().
    void setInfo(nlohmann::json info);

    // Serves the static files in `directory` under /ui/ (and redirects / and
    // /ui to it). Returns false, and serves nothing, if `directory` doesn't
    // exist. Call before run().
    bool serveUi(const std::string& directory);

    // Blocks, serving requests until stop() is called from another thread.
    void run();

    void stop();

private:
    void registerRoutes();

    agent::Agent& agent_;
    core::WorkspaceManager& workspace_manager_;
    core::SessionManager& session_manager_;
    std::string bind_address_;
    int port_;
    nlohmann::json info_ = nlohmann::json::object();
    httplib::Server server_;
};

} // namespace atlas::api
