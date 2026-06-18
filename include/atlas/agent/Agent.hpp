#pragma once

#include "atlas/agent/LLMClient.hpp"
#include "atlas/tools/ToolManager.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace atlas::agent {

class Agent {
public:
    // The Agent requires a communication client and a registry of tools to interact with the world
    Agent(LLMClient& llm_client, tools::ToolManager& tool_manager, const std::string& model_name = "llama3");

    // Main interaction endpoint. This blocks and runs the autonomous loop until a final answer is ready.
    std::string chat(const std::string& user_input);

    // Clears the active conversation context
    void clearHistory();

private:
    LLMClient& llm_client_;
    tools::ToolManager& tool_manager_;
    std::string model_name_;

    // Stores the active context window (using raw JSON for easy compatibility with the Ollama API)
    nlohmann::json messages_history_;
};

} // namespace atlas::agent
