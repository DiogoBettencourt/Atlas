#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace atlas::tools {

/**
 * @brief Abstract interface for all AI Tools in the Atlas workspace.
 * * Every tool must implement this interface so the Agent runtime can dynamically
 * load, describe, and execute them without hardcoding dependencies.
 */
class Tool {
public:
    virtual ~Tool() = default;

    /**
     * @brief The unique name of the tool (e.g., "read_file", "search_web").
     * @return A string containing only lowercase letters and underscores.
     */
    virtual std::string name() const = 0;

    /**
     * @brief A detailed description of what the tool does.
     * This is critical, as the LLM uses this text to decide whether to use the tool.
     */
    virtual std::string description() const = 0;

    /**
     * @brief Returns the JSON Schema defining the expected arguments.
     * This schema will be passed directly to the LLM to enforce strict argument types.
     */
    virtual nlohmann::json parametersSchema() const = 0;

    /**
     * @brief Executes the tool with the provided JSON arguments.
     * * @param arguments A JSON object matching the parametersSchema.
     * @return A string representing the result (Observation) or an error message.
     */
    virtual std::string execute(const nlohmann::json& arguments) = 0;
};
} // namespace atlas::tools
