#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace atlas::agent {

// Abstract chat-completion backend: the single seam between Agent and
// whatever actually runs the model (Ollama today; an OpenAI-compatible
// server such as llama-server, or an embedded llama.cpp, later - see #47).
// Agent only ever talks to this interface, so adding a backend never
// touches the ReAct loop.
class LLMBackend {
public:
    virtual ~LLMBackend() = default;

    // Sends a single non-streaming chat completion request.
    //
    // `messages` is an array of {"role","content",...} objects and `tools`
    // (optional) follows the {"type":"function",...} schema produced by
    // ToolManager::schemasJson().
    //
    // Returns the assistant message in Atlas's canonical shape, whatever the
    // backend's wire format is:
    //   {"role":"assistant",
    //    "content":"...",                      // may be empty on a tool turn
    //    "tool_calls":[{"function":{"name":"...","arguments":{...}}}],  // optional
    //    "thinking":"..."}                     // optional reasoning text
    // Backends are responsible for translating into this shape - in
    // particular, reasoning text always lands in "thinking" so Agent never
    // needs to know which server produced it.
    //
    // Throws std::runtime_error on transport failure, a non-success status,
    // or a malformed response.
    [[nodiscard]] virtual nlohmann::json chat(
        const std::string& model,
        const nlohmann::json& messages,
        const nlohmann::json& tools = nlohmann::json::array()) = 0;

    // Receives each piece of text as the model generates it. `kind` is
    // "thinking" (reasoning) or "content" (the visible reply); `text` is
    // just the new piece, not the text so far.
    using DeltaCallback = std::function<void(const std::string& kind, const std::string& text)>;

    // Same request and the same return value as chat(), but the backend asks
    // its server to stream and calls `on_delta` as text arrives, so a caller
    // can show the model's thinking and answer live instead of after the
    // whole generation. The returned message is the complete one, exactly
    // what chat() would have returned.
    //
    // The default implementation just calls chat() and never calls
    // `on_delta`, so a backend that can't stream still works.
    [[nodiscard]] virtual nlohmann::json chatStream(
        const std::string& model,
        const nlohmann::json& messages,
        const nlohmann::json& tools,
        const DeltaCallback& on_delta) {
        (void)on_delta;
        return chat(model, messages, tools);
    }

    // Short identifier for logs / the startup banner, e.g. "ollama".
    [[nodiscard]] virtual std::string name() const = 0;
};

} // namespace atlas::agent
