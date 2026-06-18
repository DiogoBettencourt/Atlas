#pragma once

#include "atlas/agent/Agent.hpp"
#include <httplib.h>
#include <string>
#include <thread>
#include <memory>

namespace atlas::api {

class APIServer {
public:
    APIServer(agent::Agent& agent, const std::string& host = "localhost", int port = 8080);
    ~APIServer();

    void start();
    void stop();

private:
    agent::Agent& agent_;
    std::string host_;
    int port_;
    httplib::Server server_;
    std::unique_ptr<std::thread> server_thread_;

    void setupRoutes();
};

} // namespace atlas::api
