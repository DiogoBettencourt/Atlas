#include "atlas/tools/ToolManager.hpp"
#include <iostream>

namespace atlas::tools {

void ToolManager::registerTool(std::unique_ptr<Tool> tool) {
    if (tool) {
        std::string name = tool->name();
        tools_[name] = std::move(tool);
        std::cout << "Registered Tool: " << name << "\n";
    }
}

std::string ToolManager::executeTool(const std::string& name, const nlohmann::json& arguments) {
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        try {
            return it->second->execute(arguments);
        } catch (const std::exception& e) {
            return "Error executing tool '" + name + "': " + std::string(e.what());
        }
    }
    return "Error: Tool '" + name + "' is not registered.";
}

nlohmann::json ToolManager::getToolSchemas() const {
    nlohmann::json schemas = nlohmann::json::array();
    
    for (const auto& [name, tool] : tools_) {
        // Format exactly as OpenAI / Ollama expects for tool calling
        schemas.push_back({
            {"type", "function"},
            {"function", {
                {"name", tool->name()},
                {"description", tool->description()},
                {"parameters", tool->parametersSchema()}
            }}
        });
    }
    
    return schemas;
}

} // namespace atlas::tools