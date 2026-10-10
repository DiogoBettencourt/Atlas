#include "atlas/core/Application.hpp"

#include "atlas/agent/OllamaBackend.hpp"
#include "atlas/agent/OpenAICompatBackend.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
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

// Reads an environment variable, treating unset and empty the same.
std::string envOr(const char* name, const std::string& fallback) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
        std::string result(value);
        std::free(value);
        return result.empty() ? fallback : result;
    }
    return fallback;
#else
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
#endif
}

} // namespace

nlohmann::json Application::buildConfig(int argc, char* argv[]) {
    nlohmann::json config;
    config["model"] = argOr(argc, argv, "model", "qwen3:14b");
    config["port"] = std::stoi(argOr(argc, argv, "port", "8080"));
    config["bind_address"] = argOr(argc, argv, "bind", "127.0.0.1");
    config["data_dir"] = argOr(argc, argv, "data-dir", "./atlas_data/storage");
    config["workspaces_dir"] = argOr(argc, argv, "workspaces-dir", "./atlas_data/workspaces");
    config["ui_dir"] = argOr(argc, argv, "ui-dir", "./packages/ui/dist");
    config["backend"] = argOr(argc, argv, "backend", "ollama");
    config["ollama_host"] = argOr(argc, argv, "ollama-host", "127.0.0.1");
    config["ollama_port"] = std::stoi(argOr(argc, argv, "ollama-port", "11434"));
    // Only used by --backend=openai. llama-server defaults to port 8080,
    // which is Atlas's own default, so we expect it on 8081 (--port=8081).
    config["api_base"] = argOr(argc, argv, "api-base", "http://127.0.0.1:8081");
    // Prefer the environment variable: a --api-key=... argument is visible
    // in the process list.
    config["api_key"] = argOr(argc, argv, "api-key", envOr("ATLAS_API_KEY", ""));
    // Self-improvement: empty by default (feature disabled). Set both to
    // let the agent open PRs against its own repository - see README.
    config["self_repo"] = argOr(argc, argv, "self-repo", "");
    config["github_repo"] = argOr(argc, argv, "github-repo", "");
    return config;
}

std::unique_ptr<agent::LLMBackend> Application::makeBackend(const nlohmann::json& config) {
    const std::string backend = config["backend"].get<std::string>();
    if (backend == "ollama") {
        return std::make_unique<agent::OllamaBackend>(config["ollama_host"].get<std::string>(),
                                                      config["ollama_port"].get<int>());
    }
    if (backend == "openai") {
        return std::make_unique<agent::OpenAICompatBackend>(config["api_base"].get<std::string>(),
                                                            config["api_key"].get<std::string>());
    }
    throw std::invalid_argument("unknown --backend '" + backend + "' (supported: ollama, openai)");
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
      llm_backend_(makeBackend(config_)),
      agent_(*llm_backend_, tool_manager_, session_manager_, config_["model"].get<std::string>()),
      api_server_(agent_, workspace_manager_, session_manager_,
                  config_["bind_address"].get<std::string>(), config_["port"].get<int>()) {
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

    // Serve the built AtlasUI at /ui if it's there. Missing is normal (the
    // UI is an optional, separately built package), so only say so quietly.
    const std::string ui_dir = config_["ui_dir"].get<std::string>();
    const bool ui_served = !ui_dir.empty() && api_server_.serveUi(ui_dir);

    setupSignalHandling();

    std::cout << "Atlas v" << ATLAS_VERSION << " initialized" << std::endl
              << "  model:          " << config_["model"].get<std::string>() << std::endl
              << "  backend:        " << llm_backend_->name();
    if (llm_backend_->name() == "ollama") {
        std::cout << " (" << config_["ollama_host"].get<std::string>() << ":"
                  << config_["ollama_port"].get<int>() << ")";
    } else if (llm_backend_->name() == "openai") {
        std::cout << " (" << config_["api_base"].get<std::string>()
                  << (config_["api_key"].get<std::string>().empty() ? "" : ", api key set") << ")";
    }
    std::cout << std::endl
              << "  data dir:       " << config_["data_dir"].get<std::string>() << std::endl
              << "  workspaces dir: " << config_["workspaces_dir"].get<std::string>() << std::endl
              << "  ui:             "
              << (ui_served ? "http://" + config_["bind_address"].get<std::string>() + ":" +
                                  std::to_string(config_["port"].get<int>()) + "/ui/ (" + ui_dir + ")"
                            : "not served (build packages/ui, or pass --ui-dir=<dir>)")
              << std::endl
              << "  tools:          " << tool_manager_.toolCount() << " registered" << std::endl;

    if (self_repo.empty()) {
        std::cout << "  self-improve:   disabled (pass --self-repo=<path> and "
                     "--github-repo=<owner/name> to enable)" << std::endl;
    } else {
        std::cout << "  self-improve:   enabled, repo=" << self_repo << ", github="
                   << (github_repo.empty() ? "(not set - github_pr disabled)" : github_repo)
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
