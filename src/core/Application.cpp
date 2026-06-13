#include "atlas/core/Application.hpp"
#include <iostream>

namespace atlas::core {

Application::Application(int /*argc*/, char* /*argv*/[])
    : io_context_(),
      signals_(io_context_, SIGINT, SIGTERM)
{
    loadConfiguration();
    setupSignalHandling();
}

Application::~Application() {
    std::cout << "Atlas shutting down gracefully." << std::endl;
}

void Application::run() {
    std::cout << "Starting Atlas AI Workspace [Version: "
              << config_.value("version", std::string("unknown")) << "]\n";
    std::cout << "Press Ctrl+C to exit.\n";

    io_context_.run();
}

void Application::stop() {
    io_context_.stop();
}

void Application::loadConfiguration() {
    // TODO: read from config file / env and merge with defaults.
    config_ = {
        {"name", "Atlas"},
        {"version", "0.1.0"},
        {"local_first", true}
    };
}

void Application::setupSignalHandling() {
    signals_.async_wait(
        [this](const asio::error_code& error, int signal_number) {
            if (!error) {
                std::cout << "\nReceived signal " << signal_number
                          << ". Initiating shutdown..." << std::endl;
                stop();
            }
        });
}

} // namespace atlas::core
