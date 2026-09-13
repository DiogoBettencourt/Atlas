#include "atlas/agent/LLMClient.hpp"

#include <httplib.h>
#include <stdexcept>

namespace atlas::agent {

LLMClient::LLMClient(std::string host, int port) : host_(std::move(host)), port_(port) {}

nlohmann::json LLMClient::chat(const std::string& model,
                                const nlohmann::json& messages,
                                const nlohmann::json& tools) {
    httplib::Client client(host_, port_);
    client.set_connection_timeout(5, 0);
    client.set_read_timeout(300, 0); // local inference can be slow on CPU

    nlohmann::json payload{
        {"model", model},
        {"messages", messages},
        {"stream", false}
    };
    if (!tools.empty()) {
        payload["tools"] = tools;
    }

    auto response = client.Post("/api/chat", payload.dump(), "application/json");

    if (!response) {
        throw std::runtime_error(
            "LLMClient: failed to reach Ollama at " + host_ + ":" + std::to_string(port_) +
            " - is `ollama serve` running?");
    }
    if (response->status != 200) {
        throw std::runtime_error("LLMClient: Ollama returned HTTP " +
                                  std::to_string(response->status) + ": " + response->body);
    }

    nlohmann::json parsed = nlohmann::json::parse(response->body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("message")) {
        throw std::runtime_error("LLMClient: malformed response from Ollama: " + response->body);
    }

    return parsed["message"];
}

} // namespace atlas::agent
