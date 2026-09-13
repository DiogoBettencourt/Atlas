#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace atlas::tools {

// Base interface for every agent-callable tool. Concrete tools must be
// stateless with respect to conversation (all context comes in via
// `arguments` and `workspace_root`) so they can be safely shared across
// sessions through the ToolManager registry.
class Tool {
public:
    virtual ~Tool() = default;

    // Machine name used by the LLM to invoke this tool, e.g. "read_file".
    [[nodiscard]] virtual std::string name() const = 0;

    // Human-readable description shown to the LLM in the tool schema.
    [[nodiscard]] virtual std::string description() const = 0;

    // JSON Schema (as an OpenAI/Ollama-style "parameters" object) describing
    // the arguments this tool accepts.
    [[nodiscard]] virtual nlohmann::json parametersSchema() const = 0;

    // Executes the tool. `workspace_root` is the sandbox root the tool must
    // confine all filesystem operations to. Returns a JSON-serializable
    // result (or an {"error": "..."} object on failure) that gets fed back
    // into the LLM's context as a tool result message.
    [[nodiscard]] virtual nlohmann::json execute(
        const nlohmann::json& arguments,
        const std::string& workspace_root) const = 0;

    // Convenience: renders this tool as an LLM-compatible function schema,
    // matching the {"type": "function", "function": {...}} shape used by
    // Ollama / OpenAI-style chat completion APIs.
    [[nodiscard]] nlohmann::json toFunctionSchema() const {
        return nlohmann::json{
            {"type", "function"},
            {"function", {
                {"name", name()},
                {"description", description()},
                {"parameters", parametersSchema()}
            }}
        };
    }
};

} // namespace atlas::tools
