#pragma once

#include "atlas/agent/LLMClient.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/tools/ToolManager.hpp"
#include <string>

namespace atlas::agent {

// ReAct (Reason + Act) loop engine. Given a user message, repeatedly calls
// the LLM, executes any requested tool calls against the active workspace,
// feeds results back, and returns once the model produces a plain-text
// final answer or a loop-guard iteration limit is hit.
class Agent {
public:
    Agent(LLMClient& llm_client,
          tools::ToolManager& tool_manager,
          core::SessionManager& session_manager,
          std::string model_name);

    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent(Agent&&) = delete;
    Agent& operator=(Agent&&) = delete;

    // Runs the ReAct loop for `session_id`, sandboxing any tool file
    // operations to `workspace_root`. Returns the assistant's final
    // natural-language reply.
    [[nodiscard]] std::string chat(const std::string& message,
                                    const std::string& session_id,
                                    const std::string& workspace_root = ".");

    void setMaxIterations(unsigned int max_iterations) { max_iterations_ = max_iterations; }

private:
    // Attempts to pull a tool_calls array off an assistant message, whether
    // it arrived as native Ollama tool_calls JSON or leaked into the
    // content field as a fenced ```json code block (common with smaller
    // models that don't fully honor structured tool calling).
    [[nodiscard]] static nlohmann::json extractToolCalls(const nlohmann::json& assistant_message);

    LLMClient& llm_client_;
    tools::ToolManager& tool_manager_;
    core::SessionManager& session_manager_;
    std::string model_name_;
    unsigned int max_iterations_ = 20;
};

} // namespace atlas::agent
