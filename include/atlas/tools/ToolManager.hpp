#pragma once

#include "atlas/tools/Tool.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace atlas::core { class SymbolIndexer; }

namespace atlas::tools {

// Dynamic strategy-pattern registry for agent tools. Owns every registered
// Tool and exposes both the LLM-facing JSON schema list and a safe
// dispatch/execute path.
class ToolManager {
public:
    ToolManager() = default;

    ToolManager(const ToolManager&) = delete;
    ToolManager& operator=(const ToolManager&) = delete;
    ToolManager(ToolManager&&) = delete;
    ToolManager& operator=(ToolManager&&) = delete;

    // Registers a tool instance. Later registrations with the same name
    // overwrite earlier ones.
    void registerTool(std::unique_ptr<Tool> tool);

    // Registers the built-in tool set: read_file, write_file, edit_file,
    // search_symbol, plus git/github_pr (which self-disable with a clear
    // error unless self_repo_root is non-empty - see GitTool/GitHubPRTool).
    // `indexer` and any non-empty repo path must outlive this ToolManager.
    void registerDefaultTools(core::SymbolIndexer& indexer,
                               const std::string& self_repo_root = {},
                               const std::string& github_repo = {});

    // Returns true if a tool with this name is registered.
    [[nodiscard]] bool hasTool(const std::string& name) const;

    // Returns the full list of function schemas for every registered tool,
    // suitable for inclusion in an Ollama/OpenAI chat completion request's
    // "tools" field.
    [[nodiscard]] nlohmann::json schemasJson() const;

    // Executes the named tool against the given arguments, scoped to
    // `workspace_root`. Returns {"error": "unknown tool: <name>"} if the
    // tool is not registered instead of throwing, so the agent loop can
    // feed the error back to the model gracefully.
    [[nodiscard]] nlohmann::json execute(const std::string& tool_name,
                                          const nlohmann::json& arguments,
                                          const std::string& workspace_root) const;

    [[nodiscard]] std::size_t toolCount() const { return tools_.size(); }

private:
    std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
    // Preserves registration order for stable schema listings.
    std::vector<std::string> order_;
};

} // namespace atlas::tools
