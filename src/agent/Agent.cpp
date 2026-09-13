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

// Helper function to trim whitespace from a string
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string Agent::chat(const std::string& message, const std::string& session_id) {
    session_manager_.appendMessage(session_id, "user", message);

    int iterations = 0;
    const int MAX_ITERATIONS = 10;

    while (iterations < MAX_ITERATIONS) {
        auto history_opt = session_manager_.getSessionHistory(session_id);
        if (!history_opt) {
            return "Error: Invalid session ID.";
        }

        std::string response = llm_client_.generateResponse(*history_opt, model_name_, tool_manager_.getToolSchemas());

        if (response.empty()) {
            return "Error: Received empty response from LLM.";
        }

        bool used_tool = false;
        std::string tool_name;
        nlohmann::json tool_args;
        std::string candidate_json = response;
        std::string thought_process = "";

        // ---------------------------------------------------------
        // ARCHITECT FIX: Extract Thoughts + JSON Tool Call
        // ---------------------------------------------------------
        size_t json_start = candidate_json.find("```json");
        if (json_start != std::string::npos) {
            // Everything before the markdown block is the AI's internal reasoning
            thought_process = trim(candidate_json.substr(0, json_start));
            
            json_start += 7; 
            size_t json_end = candidate_json.find("```", json_start);
            if (json_end != std::string::npos) {
                candidate_json = candidate_json.substr(json_start, json_end - json_start);
            }
        } else {
            size_t generic_start = candidate_json.find("```");
            if (generic_start != std::string::npos) {
                thought_process = trim(candidate_json.substr(0, generic_start));
                generic_start += 3;
                size_t generic_end = candidate_json.find("```", generic_start);
                if (generic_end != std::string::npos) {
                    candidate_json = candidate_json.substr(generic_start, generic_end - generic_start);
                }
            }
        }

        // Cleanup and parse candidate JSON
        candidate_json = trim(candidate_json);
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
            used_tool = false;
        }

        if (!used_tool) {
            session_manager_.appendMessage(session_id, "assistant", response);
            return response;
        }

        // ---------------------------------------------------------
        // TERMINAL REPORTING: Show Atlas Thinking & Acting
        // ---------------------------------------------------------
        if (!thought_process.empty()) {
            std::cout << "\n   [Atlas is thinking...] \n   " << thought_process << "\n";
        }
        std::cout << "   [Atlas is executing tool]: " << tool_name << "\n";

        session_manager_.appendMessage(session_id, "assistant", response);

        std::string tool_result;
        try {
            tool_result = tool_manager_.executeTool(tool_name, tool_args); 
        } catch (const std::exception& e) {
            tool_result = "Tool Execution Error: " + std::string(e.what());
        }

        std::cout << "   [Tool Output]: " << tool_result.substr(0, std::min<size_t>(tool_result.length(), 100)) << "...\n";

        session_manager_.appendMessage(session_id, "user", "Tool Result:\n" + tool_result);

        iterations++;
    }

    return "Error: Agent reached maximum iterations without providing a final answer.";
}

} // namespace atlas::agent