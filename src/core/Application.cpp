#include "atlas/core/Application.hpp"
#include "atlas/storage/FileStorageManager.hpp"
#include <iostream>
#include <stdexcept>

namespace atlas::core {

Application::Application(int /*argc*/, char* /*argv*/[])
    : io_context_(),
      signals_(io_context_, SIGINT, SIGTERM)
{
    loadConfiguration();
    initializeStorage();
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

atlas::storage::StorageManager& Application::getStorageManager() {
    if (!storage_) {
        throw std::runtime_error("Attempted to access StorageManager before initialization.");
    }
    return *storage_;
}

void Application::loadConfiguration() {
    config_ = {
        {"name", "Atlas"},
        {"version", "0.1.0"},
        {"local_first", true}
    };
}

void Application::initializeStorage() {
    // Instantiate the specific FileStorageManager implementation
    storage_ = std::make_unique<atlas::storage::FileStorageManager>("local_workspaces");

    if (!storage_->initialize()) {
        throw std::runtime_error("Fatal: Failed to initialize local storage layer.");
    }
    std::cout << "Storage layer initialized successfully.\n";
}

void Application::setupSignalHandling() {
    signals_.async_wait(
        [this](const asio::error_code& error, int signal_number) {
            if (!error) {
                std::cout << "\nReceived signal " << signal_number << ". Initiating shutdown..." << std::endl;
                stop();
            }
        });
}

} // namespace atlas::core
