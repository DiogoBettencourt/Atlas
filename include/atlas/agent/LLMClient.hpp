#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace atlas::agent {

/**
 * @brief Handles HTTP communication with a local LLM instance (e.g., Ollama).
 */
class LLMClient {
public:
    // Defaults to the standard local Ollama address and port
    explicit LLMClient(const std::string& host = "localhost", int port = 11434);
    ~LLMClient() = default;

    /**
     * @brief Sends a chat payload to the LLM and waits for the response.
     * @param model_name The name of the model to use (e.g., "llama3:latest")
     * @param messages A JSON array of the conversation history
     * @param tools An optional JSON array of available tools (can be null/empty)
     * @return The complete JSON response from the LLM
     */
    nlohmann::json generateChatResponse(
        const std::string& model_name,
        const nlohmann::json& messages,
        const nlohmann::json& tools = nullptr
    );

private:
    std::string host_;
    int port_;
};

} // namespace atlas::agent
