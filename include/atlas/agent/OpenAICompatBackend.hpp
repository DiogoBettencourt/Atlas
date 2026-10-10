#pragma once

#include "atlas/agent/LLMBackend.hpp"

#include <string>

namespace atlas::agent {

// LLMBackend over the OpenAI-style chat API (POST {base}/v1/chat/completions,
// non-streaming). Despite the name this involves no OpenAI service: it is the
// request format that llama.cpp's `llama-server` (HIP/ROCm and CUDA builds),
// LM Studio, vLLM and most other local servers speak, so one backend covers
// all of them. Everything stays on whatever machine `api_base` points at.
//
// Atlas's canonical message shape (see LLMBackend.hpp) is Ollama-flavored, so
// this class translates at the boundary. The differences it absorbs:
//   - tool_calls[].function.arguments is a JSON *string* on the wire but an
//     object in Atlas's shape;
//   - the wire format requires tool-call ids, and tool-result messages must
//     carry the matching tool_call_id, which Atlas's history doesn't store;
//   - reasoning text arrives as `reasoning_content` (llama.cpp / DeepSeek
//     style) or `reasoning`, or inline as <think>...</think> in `content`;
//     all of it is mapped onto the canonical `thinking` field.
class OpenAICompatBackend : public LLMBackend {
public:
    // `api_base` is "http://host:port" or "https://host:port", optionally
    // followed by a path; a trailing "/v1" and/or "/" are accepted. HTTPS
    // needs a build with OpenSSL. `api_key`, if non-empty, is sent as
    // "Authorization: Bearer <key>". Throws std::invalid_argument for a base
    // URL that isn't http(s)://.
    explicit OpenAICompatBackend(std::string api_base = "http://127.0.0.1:8081",
                                 std::string api_key = {});

    OpenAICompatBackend(const OpenAICompatBackend&) = delete;
    OpenAICompatBackend& operator=(const OpenAICompatBackend&) = delete;
    OpenAICompatBackend(OpenAICompatBackend&&) = delete;
    OpenAICompatBackend& operator=(OpenAICompatBackend&&) = delete;

    [[nodiscard]] nlohmann::json chat(
        const std::string& model,
        const nlohmann::json& messages,
        const nlohmann::json& tools = nlohmann::json::array()) override;

    [[nodiscard]] nlohmann::json chatStream(
        const std::string& model,
        const nlohmann::json& messages,
        const nlohmann::json& tools,
        const DeltaCallback& on_delta) override;

    [[nodiscard]] std::string name() const override { return "openai"; }

    // Atlas history -> wire messages. Stringifies tool-call arguments,
    // supplies tool-call ids (reusing an existing "id" if the call has one,
    // otherwise synthesizing "call_N"), pairs each tool-result message with
    // the oldest not-yet-answered call id, and drops fields the wire format
    // doesn't have (e.g. `thinking`). Public so it can be unit-tested.
    [[nodiscard]] static nlohmann::json toWireMessages(const nlohmann::json& messages);

    // Wire assistant message -> Atlas's canonical shape. Public for tests.
    [[nodiscard]] static nlohmann::json fromWireMessage(const nlohmann::json& wire_message);

private:
    std::string origin_;    // "http://host:port"
    std::string endpoint_;  // "/v1/chat/completions" (or under a custom path)
    std::string api_base_;  // as given, for error messages
    std::string api_key_;
};

} // namespace atlas::agent
