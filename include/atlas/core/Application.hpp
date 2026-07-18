#pragma once

#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include "atlas/tools/ToolManager.hpp"
#include "atlas/core/WorkspaceManager.hpp"
#include "atlas/core/SessionManager.hpp"

// Forward declaration of the StorageManager interface to avoid circular includes
namespace atlas::storage {
    class StorageManager;
}

namespace atlas::core {

class Application {
public:
    Application(int argc, char* argv[]);

    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    void run();
    void stop();

    // Expose the storage manager to other components of the application safely
    storage::StorageManager& getStorageManager();

    // Expose the tool manager to other components of the application safely
    tools::ToolManager& getToolManager();

    core::SessionManager& getSessionManager();

private:
    void loadConfiguration();
    void setupSignalHandling();
    void initializeStorage();

    asio::io_context io_context_;
    nlohmann::json config_;
    asio::signal_set signals_;

    std::filesystem::path getExecutableDir();

    // Exclusive ownership of the active storage manager
    std::unique_ptr<storage::StorageManager> storage_;

    tools::ToolManager tool_manager_;
    core::SessionManager session_manager_;
    core::WorkspaceManager workspace_manager_;
};

} // namespace atlas::core
