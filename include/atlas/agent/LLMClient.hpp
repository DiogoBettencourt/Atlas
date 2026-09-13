#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace atlas::agent {

// Thin HTTP client wrapping Ollama's local REST API
// (POST /api/chat, non-streaming). Talks to a locally running
// `ollama serve` instance; no network egress ever leaves the machine.
class LLMClient {
public:
    explicit LLMClient(std::string host = "127.0.0.1", int port = 11434);

    LLMClient(const LLMClient&) = delete;
    LLMClient& operator=(const LLMClient&) = delete;
    LLMClient(LLMClient&&) = delete;
    LLMClient& operator=(LLMClient&&) = delete;

    // Sends a single non-streaming chat completion request.
    // `messages` follows Ollama's chat message array format, and `tools`
    // (optional) follows the {"type":"function",...} schema produced by
    // ToolManager::schemasJson(). Returns the raw "message" object from the
    // Ollama response, e.g. {"role":"assistant","content":"...",
    // "tool_calls":[...]}. Throws std::runtime_error on transport failure
    // or a non-200 response.
    [[nodiscard]] nlohmann::json chat(const std::string& model,
                                       const nlohmann::json& messages,
                                       const nlohmann::json& tools = nlohmann::json::array());

private:
    std::string host_;
    int port_;
};

} // namespace atlas::agent
