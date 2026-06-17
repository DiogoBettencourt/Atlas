#pragma once

#include "atlas/tools/Tool.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

namespace atlas::tools {

/**
 * @brief Centralized registry for managing and executing AI tools.
 */
class ToolManager {
public:
    ToolManager() = default;
    ~ToolManager() = default;

    // Takes exclusive ownership of a tool and registers it by its name
    void registerTool(std::unique_ptr<Tool> tool);

    // Executes a tool by name, returning the result or an error string
    std::string executeTool(const std::string& name, const nlohmann::json& arguments);

    // Compiles all registered tool schemas into a single JSON array for the LLM
    nlohmann::json getAllToolSchemas() const;

    // Returns a simple list of registered tool names
    std::vector<std::string> getRegisteredToolNames() const;

private:
    // Maps the tool's name (e.g., "read_file") to its instance
    std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
};

} // namespace atlas::tools
