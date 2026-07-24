#include "atlas/core/Application.hpp"
#include "atlas/storage/FileStorageManager.hpp"
#include "atlas/tools/ReadFileTool.hpp"
#include "atlas/tools/SearchSymbolTool.hpp"
#include "atlas/core/SymbolIndexer.hpp"
#include <iostream>
#include <windows.h> // For GetModuleFileName
#include <stdexcept>

namespace atlas::core {

std::filesystem::path Application::getExecutableDir() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

Application::Application(int /*argc*/, char* /*argv*/[])
    : io_context_(),
      signals_(io_context_, SIGINT, SIGTERM)
{
    loadConfiguration();
    initializeStorage();

    // 1. Get our absolute root path (This will work now!)
    auto exe_dir = getExecutableDir();
    std::filesystem::path root_path = exe_dir.parent_path().parent_path();

    // 2. Register this folder as the "Atlas" workspace
    workspace_manager_.addWorkspace("AtlasCore", root_path);

    // 3. Index the codebase symbols immediately
    symbol_indexer_.indexWorkspace(root_path);

    // 4. Pass the dynamic manager to the tools
    tool_manager_.registerTool(std::make_unique<atlas::tools::ReadFileTool>(workspace_manager_));
    
    // ADD THIS LINE:
    tool_manager_.registerTool(std::make_unique<atlas::tools::SearchSymbolTool>(symbol_indexer_));

    setupSignalHandling();
}

Application::~Application() {
    std::cout << "Atlas shutting down gracefully." << std::endl;
}

atlas::tools::ToolManager& Application::getToolManager() {
    return tool_manager_;
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

core::SessionManager& Application::getSessionManager() {
    return session_manager_;
}

} // namespace atlas::core