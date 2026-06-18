#include "atlas/agent/Agent.hpp"
#include <iostream>

namespace atlas::agent {

Agent::Agent(LLMClient& llm_client, tools::ToolManager& tool_manager, const std::string& model_name)
    : llm_client_(llm_client), tool_manager_(tool_manager), model_name_(model_name) {

    messages_history_ = nlohmann::json::array();

    messages_history_.push_back({
        {"role", "system"},
        {"content", "You are Atlas. You are an autonomous workspace agent. "
                    "1. When asked to read a file, ALWAYS prioritize using the 'read_file' tool immediately. "
                    "2. DO NOT ask the user for the path if you can infer it or if they have already provided the filename. "
                    "3. If the user refers to a project file, assume it is in the workspace root. "
                    "4. Be concise and tool-oriented."}
    });
}

void Agent::clearHistory() {
    auto system_prompt = messages_history_[0];
    messages_history_ = nlohmann::json::array({system_prompt});
}

std::string Agent::chat(const std::string& user_input) {
    messages_history_.push_back({{"role", "user"}, {"content", user_input}});
    nlohmann::json tools = tool_manager_.getAllToolSchemas();
    const int MAX_ITERATIONS = 5;

    // Track previous call to break loops
    std::string last_tool_call_signature = "";

    for (int i = 0; i < MAX_ITERATIONS; ++i) {
        nlohmann::json response = llm_client_.generateChatResponse(model_name_, messages_history_, tools);

        if (!response.contains("message")) return "Error: Invalid response format.";
        auto message = response["message"];
        messages_history_.push_back(message);

        bool used_tool = false;
        std::string current_call_signature = "";

        // Helper to execute and track
        auto execute_and_track = [&](const std::string& name, const nlohmann::json& args) {
            std::string sig = name + ":" + args.dump();
            if (sig == last_tool_call_signature) return false; // Loop detected

            last_tool_call_signature = sig;
            std::cout << "\n   [Atlas is using tool: " << name << "]\n";
            std::string tool_result = tool_manager_.executeTool(name, args);

            std::string observation = "[SYSTEM OBSERVATION - Tool Result]:\n" + tool_result +
                                      "\n\nINSTRUCTION: You have the data. DO NOT call this tool again. Answer the user.";
            messages_history_.push_back({{"role", "user"}, {"content", observation}});
            return true;
        };

        if (message.contains("tool_calls") && !message["tool_calls"].empty()) {
            for (const auto& tool_call : message["tool_calls"]) {
                if (execute_and_track(tool_call["function"]["name"], tool_call["function"]["arguments"]))
                    used_tool = true;
            }
        }
        else if (message.contains("content") && !message["content"].is_null()) {
            std::string content = message["content"].get<std::string>();
            if (content.find("\"name\"") != std::string::npos && content.find("\"arguments\"") != std::string::npos) {
                try {
                    size_t start = content.find('{'); size_t end = content.rfind('}');
                    if (start != std::string::npos && end != std::string::npos) {
                        auto parsed = nlohmann::json::parse(content.substr(start, end - start + 1));
                        if (execute_and_track(parsed["name"], parsed["arguments"]))
                            used_tool = true;
                    }
                } catch (...) {}
            }
        }

        if (used_tool) { continue; }
        if (message.contains("content") && !message["content"].is_null()) {
            return message["content"].get<std::string>();
        }
    }

    return "\n  [!] Agent reached iteration limit.";
}

} // namespace atlas::agent
