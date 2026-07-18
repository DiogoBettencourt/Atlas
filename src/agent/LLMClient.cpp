#include "atlas/agent/LLMClient.hpp"
#include <httplib.h> // Assuming you are using cpp-httplib
#include <iostream>

namespace atlas::agent {

LLMClient::LLMClient() {}
LLMClient::~LLMClient() {}

std::string LLMClient::generateResponse(const std::vector<nlohmann::json>& messages, 
                                        const std::string& model_name,
                                        const nlohmann::json& tools) {
                                            
    // Connect to local Ollama instance
    httplib::Client cli("127.0.0.1", 11434);
    cli.set_read_timeout(120, 0); // 2 minute timeout for generation

    nlohmann::json payload;
    payload["model"] = model_name;
    payload["messages"] = messages;
    payload["stream"] = false; // We want the full response at once for now

    // If tools are provided, pass them to Ollama
    if (!tools.empty() && tools.is_array()) {
        payload["tools"] = tools;
    }

    auto res = cli.Post("/api/chat", payload.dump(), "application/json");

    if (res && res->status == 200) {
        try {
            auto response_json = nlohmann::json::parse(res->body);
            
            // Check if the model decided to use a native tool call (Ollama specific)
            if (response_json.contains("message") && response_json["message"].contains("tool_calls")) {
                 auto tool_calls = response_json["message"]["tool_calls"];
                 if (!tool_calls.empty()) {
                     // Format it into the JSON string that our Agent.cpp expects to parse
                     nlohmann::json tool_invocation = {
                         {"name", tool_calls[0]["function"]["name"]},
                         {"arguments", nlohmann::json::parse(tool_calls[0]["function"]["arguments"].get<std::string>())}
                     };
                     return tool_invocation.dump();
                 }
            }
            
            // Otherwise, return standard text response
            return response_json["message"]["content"].get<std::string>();
            
        } catch (const std::exception& e) {
            return std::string("Error parsing LLM response: ") + e.what();
        }
    } else {
        std::string err_info = res ? std::to_string(res->status) : "Connection failed";
        return "LLM API Error: " + err_info;
    }
}

} // namespace atlas::agent