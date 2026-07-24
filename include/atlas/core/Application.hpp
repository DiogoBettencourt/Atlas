#pragma once

#include "atlas/storage/StorageManager.hpp"
#include "atlas/core/WorkspaceManager.hpp"
#include "atlas/tools/ToolManager.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/core/SymbolIndexer.hpp" // <--- THIS (Include the header)
#include <nlohmann/json.hpp>
#include <asio.hpp>
#include <memory>
#include <filesystem>

namespace atlas::core {

class Application {
public:
    Application(int argc, char* argv[]);
    ~Application();

    void run();
    void stop();

    storage::StorageManager& getStorageManager();
    tools::ToolManager& getToolManager();
    SessionManager& getSessionManager();
    
    SymbolIndexer& getSymbolIndexer(); // <--- THIS (Public getter)

    static std::filesystem::path getExecutableDir();

private:
    void loadConfiguration();
    void initializeStorage();
    void setupSignalHandling();

    asio::io_context io_context_;
    asio::signal_set signals_;
    nlohmann::json config_;
    
    std::unique_ptr<storage::StorageManager> storage_;
    WorkspaceManager workspace_manager_;
    tools::ToolManager tool_manager_;
    SessionManager session_manager_;
    
    SymbolIndexer symbol_indexer_; // <--- THIS (The missing variable!)
};

} // namespace atlas::core