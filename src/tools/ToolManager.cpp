#include "atlas/tools/ToolManager.hpp"

namespace atlas::tools {

void ToolManager::registerTool(std::unique_ptr<Tool> tool) {
    if (tool) {
        std::string name = tool->name();
        tools_[name] = std::move(tool);
    }
}

std::string ToolManager::executeTool(const std::string& name, const nlohmann::json& arguments) {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        return "Error: Tool '" + name + "' is not registered or does not exist.";
    }

    // Execute the tool and safely catch any unexpected C++ exceptions
    // so they don't crash the server, but instead return as an observation to the LLM.
    try {
        return it->second->execute(arguments);
    } catch (const std::exception& e) {
        return std::string("Error during tool execution: ") + e.what();
    } catch (...) {
        return "Unknown fatal error occurred during tool execution.";
    }
}

nlohmann::json ToolManager::getAllToolSchemas() const {
    nlohmann::json schemas = nlohmann::json::array();

    for (const auto& [name, tool] : tools_) {
        nlohmann::json tool_def = {
            {"type", "function"},
            {"function", {
                {"name", tool->name()},
                {"description", tool->description()},
                {"parameters", tool->parametersSchema()}
            }}
        };
        schemas.push_back(tool_def);
    }

    return schemas;
}

std::vector<std::string> ToolManager::getRegisteredToolNames() const {
    std::vector<std::string> names;
    names.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        names.push_back(name);
    }
    return names;
}

} // namespace atlas::tools
