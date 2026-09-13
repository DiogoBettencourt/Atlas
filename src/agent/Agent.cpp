#include "atlas/agent/Agent.hpp"
#include <iostream>
#include <algorithm>
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
    const int MAX_ITERATIONS = 10; // Expanded loop guard to support multi-file writing

    while (iterations < MAX_ITERATIONS) {
        // 2. Fetch up-to-date history for this session
        auto history_opt = session_manager_.getSessionHistory(session_id);
        if (!history_opt) {
            return "Error: Invalid session ID.";
        }

        // 3. Send history to the LLM
        std::string response = llm_client_.generateResponse(*history_opt, model_name_, tool_manager_.getToolSchemas());

        if (response.empty()) {
            return "Error: Received empty response from LLM.";
        }

        // 4. Parse Tool Calls & Fallback Extraction for Markdown Leaks
        bool used_tool = false;
        std::string tool_name;
        nlohmann::json tool_args;
        std::string candidate_json = response;

        // Fallback Step A: Extract JSON from markdown fences if present
        size_t json_start = candidate_json.find("```json");
        if (json_start != std::string::npos) {
            json_start += 7; 
            size_t json_end = candidate_json.find("```", json_start);
            if (json_end != std::string::npos) {
                candidate_json = candidate_json.substr(json_start, json_end - json_start);
            }
        } else {
            // Fallback for generic markdown code fences
            size_t generic_start = candidate_json.find("```");
            if (generic_start != std::string::npos) {
                generic_start += 3;
                size_t generic_end = candidate_json.find("```", generic_start);
                if (generic_end != std::string::npos) {
                    candidate_json = candidate_json.substr(generic_start, generic_end - generic_start);
                }
            }
        }

        // Fallback Step B: Trim leading and trailing whitespace/newlines
        size_t first_valid = candidate_json.find_first_not_of(" \t\n\r");
        size_t last_valid = candidate_json.find_last_not_of(" \t\n\r");
        if (first_valid != std::string::npos && last_valid != std::string::npos) {
            candidate_json = candidate_json.substr(first_valid, (last_valid - first_valid + 1));
        }

        // Fallback Step C: Validate standard JSON object structure
        try {
            if (!candidate_json.empty() && candidate_json.front() == '{' && candidate_json.back() == '}') {
                auto json_res = nlohmann::json::parse(candidate_json);
                if (json_res.contains("name") && json_res.contains("arguments")) {
                    used_tool = true;
                    tool_name = json_res["name"].get<std::string>();
                    tool_args = json_res["arguments"];
                }
            }
        } catch (...) {
            // Not a valid tool-call payload; proceed as standard text response
            used_tool = false;
        }

        // 5. If NO tools are used, append the final answer and return
        if (!used_tool) {
            session_manager_.appendMessage(session_id, "assistant", response);
            return response;
        }

        // 6. Execute Tool
        std::cout << "\n   [Atlas is using tool: " << tool_name << "]\n";

        // Record the LLM's invocation step into the history
        session_manager_.appendMessage(session_id, "assistant", response);

        std::string tool_result;
        try {
            tool_result = tool_manager_.executeTool(tool_name, tool_args); 
        } catch (const std::exception& e) {
            tool_result = "Tool Execution Error: " + std::string(e.what());
        }

        // Output partial tool result to the server terminal for debugging
        std::cout << "   [Tool Output]: " << tool_result.substr(0, std::min<size_t>(tool_result.length(), 100)) << "...\n";

        // Feed tool results back into history for the next iteration step
        session_manager_.appendMessage(session_id, "user", "Tool Result:\n" + tool_result);

        iterations++;
    }

    return "Error: Agent reached maximum iterations without providing a final answer.";
}

} // namespace atlas::agent