#include "atlas/core/Application.hpp"

#include <filesystem>
#include <iostream>
#include <thread>

namespace atlas::core {

namespace {

// Very small hand-rolled "--key=value" / "--flag value" CLI parser: enough
// for Atlas's handful of startup options without pulling in a dependency.
std::string argOr(int argc, char* argv[], const std::string& key, const std::string& fallback) {
    const std::string prefix = "--" + key + "=";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind(prefix, 0) == 0) {
            return arg.substr(prefix.size());
        }
    }
    return fallback;
}

} // namespace

nlohmann::json Application::buildConfig(int argc, char* argv[]) {
    nlohmann::json config;
    config["model"] = argOr(argc, argv, "model", "qwen2.5-coder:14b");
    config["port"] = std::stoi(argOr(argc, argv, "port", "8080"));
    config["bind_address"] = argOr(argc, argv, "bind", "127.0.0.1");
    config["data_dir"] = argOr(argc, argv, "data-dir", "./atlas_data/storage");
    config["workspaces_dir"] = argOr(argc, argv, "workspaces-dir", "./atlas_data/workspaces");
    config["ollama_host"] = argOr(argc, argv, "ollama-host", "127.0.0.1");
    config["ollama_port"] = std::stoi(argOr(argc, argv, "ollama-port", "11434"));
    // Self-improvement: empty by default (feature disabled). Set both to
    // let the agent open PRs against its own repository - see README.
    config["self_repo"] = argOr(argc, argv, "self-repo", "");
    config["github_repo"] = argOr(argc, argv, "github-repo", "");
    return config;
}

Application::Application(int argc, char* argv[])
    : io_context_(),
      signals_(io_context_, SIGINT, SIGTERM),
      config_(buildConfig(argc, argv)),
      storage_manager_(std::filesystem::path(config_["data_dir"].get<std::string>())),
      workspace_manager_(std::filesystem::path(config_["workspaces_dir"].get<std::string>())),
      tool_manager_(),
      session_manager_(storage_manager_),
      symbol_indexer_(),
      llm_client_(config_["ollama_host"].get<std::string>(), config_["ollama_port"].get<int>()),
      agent_(llm_client_, tool_manager_, session_manager_, config_["model"].get<std::string>()),
      api_server_(agent_, workspace_manager_, config_["bind_address"].get<std::string>(),
                  config_["port"].get<int>()) {
    std::string self_repo = config_["self_repo"].get<std::string>();
    std::string github_repo = config_["github_repo"].get<std::string>();
    tool_manager_.registerDefaultTools(symbol_indexer_, self_repo, github_repo);

    // Always have a "default" workspace ready to go, and index it
    // immediately so search_symbol is useful from the first request.
    auto default_root = workspace_manager_.createOrGetWorkspace("default");
    symbol_indexer_.indexDirectory(default_root);

    // If self-improvement is enabled, register a dedicated "self" workspace
    // pointing at Atlas's own repository. GitTool/GitHubPRTool only accept
    // calls whose workspace_root canonically matches this exact path, so
    // ordinary chat sessions using the "default" (or any other) workspace
    // can never trigger a git push or PR - only requests explicitly
    // targeting workspace="self" can.
    if (!self_repo.empty()) {
        auto self_root = workspace_manager_.createOrGetWorkspace("self", self_repo);
        symbol_indexer_.indexDirectory(self_root);
    }

    setupSignalHandling();

    std::cout << "Atlas v0.1.0 initialized" << std::endl
              << "  model:          " << config_["model"].get<std::string>() << std::endl
              << "  ollama:         " << config_["ollama_host"].get<std::string>() << ":"
              << config_["ollama_port"].get<int>() << std::endl
              << "  data dir:       " << config_["data_dir"].get<std::string>() << std::endl
              << "  workspaces dir: " << config_["workspaces_dir"].get<std::string>() << std::endl
              << "  tools:          " << tool_manager_.toolCount() << " registered" << std::endl;

    if (self_repo.empty()) {
        std::cout << "  self-improve:   disabled (pass --self-repo=<path> and "
                     "--github-repo=<owner/name> to enable)" << std::endl;
    } else {
        std::cout << "  self-improve:   enabled, repo=" << self_repo << ", github="
                   << (github_repo.empty() ? "(not set - github_pr disabled)" : github_repo)
                   << std::endl
                   << "                  NOTE: symbol_indexer_ is a single shared index; "
                      "indexing the self-repo means search_symbol now covers *it*, not the "
                      "default workspace, until reindexed. See README known limitations."
                   << std::endl;
    }
}

Application::~Application() = default;

void Application::setupSignalHandling() {
    signals_.async_wait([this](const asio::error_code& ec, int signal_number) {
        if (ec) {
            return;
        }
        std::cout << "\nAtlas received signal " << signal_number << ", shutting down..."
                  << std::endl;
        api_server_.stop();
    });
}

void Application::run() {
    // The API server blocks this thread serving HTTP; the asio io_context
    // that watches for SIGINT/SIGTERM needs its own thread so a Ctrl+C can
    // interrupt a long-running request cleanly.
    std::thread signal_thread([this]() { io_context_.run(); });

    api_server_.run(); // blocks until stop() is called from the signal handler

    io_context_.stop();
    if (signal_thread.joinable()) {
        signal_thread.join();
    }

    std::cout << "Atlas shut down cleanly." << std::endl;
}

} // namespace atlas::core
