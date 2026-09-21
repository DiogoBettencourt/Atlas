#include "atlas/tools/ToolManager.hpp"

#include "atlas/tools/EditFileTool.hpp"
#include "atlas/tools/GitHubPRTool.hpp"
#include "atlas/tools/GitTool.hpp"
#include "atlas/tools/ListDirectoryTool.hpp"
#include "atlas/tools/ReadFileTool.hpp"
#include "atlas/tools/SearchSymbolTool.hpp"
#include "atlas/tools/WriteFileTool.hpp"

namespace atlas::tools {

void ToolManager::registerTool(std::unique_ptr<Tool> tool) {
    const std::string tool_name = tool->name();
    if (tools_.find(tool_name) == tools_.end()) {
        order_.push_back(tool_name);
    }
    tools_[tool_name] = std::move(tool);
}

void ToolManager::registerDefaultTools(core::SymbolIndexer& indexer,
                                        const std::string& self_repo_root,
                                        const std::string& github_repo) {
    registerTool(std::make_unique<ReadFileTool>());
    registerTool(std::make_unique<ListDirectoryTool>());
    registerTool(std::make_unique<WriteFileTool>());
    registerTool(std::make_unique<EditFileTool>());
    registerTool(std::make_unique<SearchSymbolTool>(indexer));
    // Registered unconditionally: with an empty self_repo_root these two
    // simply refuse every call with an explanatory error, so there's no
    // difference in the tool roster shown to the model whether or not
    // self-improvement is enabled - only in whether the calls succeed.
    registerTool(std::make_unique<GitTool>(self_repo_root));
    registerTool(std::make_unique<GitHubPRTool>(self_repo_root, github_repo));
}

bool ToolManager::hasTool(const std::string& name) const {
    return tools_.find(name) != tools_.end();
}

nlohmann::json ToolManager::schemasJson() const {
    nlohmann::json schemas = nlohmann::json::array();
    for (const auto& name : order_) {
        schemas.push_back(tools_.at(name)->toFunctionSchema());
    }
    return schemas;
}

nlohmann::json ToolManager::execute(const std::string& tool_name,
                                     const nlohmann::json& arguments,
                                     const std::string& workspace_root) const {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) {
        return nlohmann::json{{"error", "unknown tool: " + tool_name}};
    }
    return it->second->execute(arguments, workspace_root);
}

} // namespace atlas::tools
