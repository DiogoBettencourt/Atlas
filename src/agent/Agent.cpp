#include "atlas/agent/Agent.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

namespace atlas::agent {

Agent::Agent(LLMClient& llm_client, 
             tools::ToolManager& tool_manager, 
             core::SessionManager& session_manager, 
             const std::string& model_name)
    : llm_client_(llm_client), 
      tool_manager_(tool_manager), 
      session_manager_(session_manager), 
      model_name_(model_name) {}

std::string Agent::chat(const std::string& message, const std::string& session_id) {
    // 1. Save user message to the session
    session_manager_.appendMessage(session_id, "user", message);

    int iterations = 0;
    const int MAX_ITERATIONS = 5; // Loop guard to prevent infinite tool execution

    while (iterations < MAX_ITERATIONS) {
        // 2. Fetch the up-to-date history for this specific session
        auto history_opt = session_manager_.getSessionHistory(session_id);
        if (!history_opt) {
            return "Error: Invalid session ID.";
        }

        // 3. Send history to the LLM
        std::string response = llm_client_.generateResponse(*history_opt, model_name_, tool_manager_.getToolSchemas());

        // 4. Parse Tool Calls
        // (This block safely checks if the model returned a JSON tool invocation instead of plain text)
        bool used_tool = false;
        std::string tool_name;
        nlohmann::json tool_args;

        try {
            // Basic heuristic: if it looks like a JSON object, attempt to parse it
            if (!response.empty() && response.front() == '{' && response.back() == '}') {
                auto json_res = nlohmann::json::parse(response);
                if (json_res.contains("name") && json_res.contains("arguments")) {
                    used_tool = true;
                    tool_name = json_res["name"].get<std::string>();
                    tool_args = json_res["arguments"];
                }
            }
        } catch (...) {
            // Not a JSON tool call, treat as a standard text response
            used_tool = false;
        }

        // 5. If NO tools are used, append the final answer and return
        if (!used_tool) {
            session_manager_.appendMessage(session_id, "assistant", response);
            return response;
        }

        // 6. If tools ARE used, execute them
        std::cout << "\n   [Atlas is using tool: " << tool_name << "]\n";

        // Append the LLM's thought process/tool request to history
        session_manager_.appendMessage(session_id, "assistant", response);

        // Execute the tool via ToolManager
        std::string tool_result;
        try {
            // Note: If your ToolManager uses a different execution signature, adjust this line
            tool_result = tool_manager_.executeTool(tool_name, tool_args); 
        } catch (const std::exception& e) {
            tool_result = "Tool Execution Error: " + std::string(e.what());
        }

        // Append the result back as a user/system message for the next reasoning loop
        session_manager_.appendMessage(session_id, "user", "Tool Result:\n" + tool_result);

        iterations++;
    }

    return "Error: Agent reached maximum iterations without providing a final answer.";
}

} // namespace atlas::agent