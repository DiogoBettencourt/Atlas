#pragma once

#include "atlas/tools/Tool.hpp"
#include <memory>
#include <string>
#include <map>
#include <nlohmann/json.hpp>

namespace atlas::tools {

class ToolManager {
public:
    ToolManager() = default;
    ~ToolManager() = default;

    // Registers a tool into the manager
    void registerTool(std::unique_ptr<Tool> tool);

    // Executes a tool by name and returns the string result
    std::string executeTool(const std::string& name, const nlohmann::json& arguments);

    // NEW: Gathers all registered tool schemas to send to the LLM
    nlohmann::json getToolSchemas() const;

private:
    std::map<std::string, std::unique_ptr<Tool>> tools_;
};

} // namespace atlas::tools