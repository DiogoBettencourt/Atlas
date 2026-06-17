#include <httplib.h>
#include "atlas/agent/LLMClient.hpp"
#include <iostream>
#include <stdexcept>

namespace atlas::agent {

LLMClient::LLMClient(const std::string& host, int port)
    : host_(host), port_(port) {}

nlohmann::json LLMClient::generateChatResponse(
    const std::string& model_name,
    const nlohmann::json& messages,
    const nlohmann::json& tools)
{
    // 1. Set up the HTTP client
    httplib::Client cli(host_, port_);
    cli.set_read_timeout(120, 0); // Give the LLM up to 2 minutes to think

    // 2. Construct the Ollama API payload
    nlohmann::json payload = {
        {"model", model_name},
        {"messages", messages},
        {"stream", false} // For Version 1, we will wait for the complete response
    };

    // Only inject tools if they were provided
    if (tools != nullptr && !tools.empty()) {
        payload["tools"] = tools;
    }

    std::string payload_str = payload.dump();

    // 3. Send the POST request to Ollama's chat endpoint
    auto res = cli.Post("/api/chat", payload_str, "application/json");

    // 4. Handle HTTP errors
    if (!res) {
        auto err = res.error();
        throw std::runtime_error("HTTP Client Error [" + httplib::to_string(err) + "]: Could not connect to LLM at " + host_ + ":" + std::to_string(port_));
    }

    if (res->status != 200) {
        throw std::runtime_error("LLM API Error (Status " + std::to_string(res->status) + "): " + res->body);
    }

    // 5. Parse and return the successful JSON response
    try {
        return nlohmann::json::parse(res->body);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse LLM JSON response: " + std::string(e.what()));
    }
}

} // namespace atlas::agent
