#pragma once

#include <asio.hpp>
#include <memory>
#include <nlohmann/json.hpp>

#include "atlas/agent/Agent.hpp"
#include "atlas/agent/LLMClient.hpp"
#include "atlas/api/APIServer.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/core/SymbolIndexer.hpp"
#include "atlas/core/WorkspaceManager.hpp"
#include "atlas/storage/FileStorageManager.hpp"
#include "atlas/tools/ToolManager.hpp"

namespace atlas::core {

// Central nervous system of Atlas. Bootstraps configuration, wires every
// subsystem together in dependency order, owns the asio::io_context event
// loop used for OS signal handling, and drives the blocking API server.
class Application {
public:
    explicit Application(int argc, char* argv[]);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Runs the application until an OS signal (SIGINT/SIGTERM) is
    // received, at which point the API server is stopped and run()
    // returns.
    void run();

    tools::ToolManager& getToolManager() { return tool_manager_; }

private:
    // Builds the runtime configuration from CLI args (with sane defaults),
    // e.g. --model=qwen2.5-coder:14b --port=8080 --data-dir=./atlas_data.
    // A free function rather than an instance method because it must run
    // before any other member is constructed (config_ feeds their
    // constructor arguments in the initializer list).
    static nlohmann::json buildConfig(int argc, char* argv[]);

    void setupSignalHandling();

    asio::io_context io_context_;
    asio::signal_set signals_;
    nlohmann::json config_;

    storage::FileStorageManager storage_manager_;
    WorkspaceManager workspace_manager_;
    tools::ToolManager tool_manager_;
    SessionManager session_manager_;
    SymbolIndexer symbol_indexer_;

    agent::LLMClient llm_client_;
    agent::Agent agent_;
    api::APIServer api_server_;
};

} // namespace atlas::core
