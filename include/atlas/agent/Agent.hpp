#pragma once

#include "atlas/agent/LLMClient.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/tools/ToolManager.hpp"
#include <functional>
#include <string>

namespace atlas::agent {

// ReAct (Reason + Act) loop engine. Given a user message, repeatedly calls
// the LLM, executes any requested tool calls against the active workspace,
// feeds results back, and returns once the model produces a plain-text
// final answer or a loop-guard iteration limit is hit.
class Agent {
public:
    // Invoked synchronously, on the calling thread, once per notable event
    // during the loop - e.g. {"type":"tool_call","name":"read_file",
    // "arguments":{...}} or {"type":"tool_result","name":"read_file",
    // "result":{...}}. Lets a caller show live progress (a streaming HTTP
    // response, a CLI spinner, etc.) instead of waiting silently for the
    // whole loop to finish. Never called from a background thread, so it's
    // safe for the callback to write directly to a socket/console.
    using EventCallback = std::function<void(const nlohmann::json&)>;

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
    // natural-language reply. If `on_event` is set, it's called for every
    // tool call issued and every tool result received, in order, before
    // this function returns the final reply.
    [[nodiscard]] std::string chat(const std::string& message,
                                    const std::string& session_id,
                                    const std::string& workspace_root = ".",
                                    const EventCallback& on_event = {});

    void setMaxIterations(unsigned int max_iterations) { max_iterations_ = max_iterations; }

    // Attempts to pull a tool_calls array off an assistant message, whether
    // it arrived as native Ollama tool_calls JSON, a bare JSON object in
    // `content` (e.g. {"name": "read_file", "arguments": {...}}), or a
    // fenced ```json code block - all observed in practice from local
    // models that don't consistently honor structured tool calling. Public
    // and static (pure function of its input, no Agent state) so it can be
    // unit-tested directly against captured model responses without a live
    // Ollama server - see tests_manual/agent_extract_tool_calls_smoke.cpp.
    [[nodiscard]] static nlohmann::json extractToolCalls(const nlohmann::json& assistant_message);

private:
    LLMClient& llm_client_;
    tools::ToolManager& tool_manager_;
    core::SessionManager& session_manager_;
    std::string model_name_;
    unsigned int max_iterations_ = 20;
};

} // namespace atlas::agent
