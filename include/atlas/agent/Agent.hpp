#pragma once

#include "atlas/agent/LLMClient.hpp"
#include "atlas/core/SessionManager.hpp"
#include "atlas/tools/ToolManager.hpp"
#include <cstddef>
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

    // Bounds how many of a session's most recent messages are sent to the
    // LLM on each turn. SessionManager still keeps (and persists) the
    // full, untrimmed history regardless - this only bounds what chat()
    // actually puts in the prompt. Without this, a long-running session's
    // prompt size grows without bound: every past user/assistant/tool
    // message gets resent on every single turn, so cost, latency, and
    // eventually context-window overflow all grow linearly with
    // conversation length forever. The default of 40 is a pragmatic
    // window (roughly 15-20 user turns, depending on how many tool calls
    // each one takes), not a tuned figure - raise or lower it based on
    // the model's actual context window.
    void setMaxHistoryMessages(std::size_t max_history_messages) {
        max_history_messages_ = max_history_messages;
    }

    // Attempts to pull a tool_calls array off an assistant message. Tries,
    // in order: native Ollama `tool_calls` JSON; a fenced ```json code
    // block inside `content`; a bare JSON object (or the first of several
    // concatenated bare objects) directly in `content` with no fencing at
    // all. The last two cover smaller / non-native-tool-calling models
    // that leak their intended call into plain text instead of using
    // Ollama's structured tool-calling format. Public (rather than
    // private) so it can be unit-tested directly against captured model
    // output without needing a live Ollama server - see
    // tests_manual/agent_extract_tool_calls_smoke.cpp.
    [[nodiscard]] static nlohmann::json extractToolCalls(const nlohmann::json& assistant_message);

    // Returns the tail of `history` capped at `max_messages` entries, but
    // only ever cuts on a "user"-role message boundary so a trimmed
    // window never starts mid-turn - e.g. with an orphaned "tool" result
    // whose preceding assistant tool_call got cut off, which some models
    // handle poorly. Returns `history` unchanged if it's already within
    // the cap. If no "user" boundary exists within the window at all
    // (max_messages smaller than a single turn takes, or no user message
    // in that span), falls back to a hard cut at exactly max_messages
    // rather than sending nothing. Public and static, like
    // extractToolCalls, so it can be unit-tested directly - see
    // tests_manual/agent_trim_history_smoke.cpp.
    [[nodiscard]] static nlohmann::json trimHistory(const nlohmann::json& history,
                                                     std::size_t max_messages);

private:
    LLMClient& llm_client_;
    tools::ToolManager& tool_manager_;
    core::SessionManager& session_manager_;
    std::string model_name_;
    unsigned int max_iterations_ = 20;
    std::size_t max_history_messages_ = 40;
};

} // namespace atlas::agent
