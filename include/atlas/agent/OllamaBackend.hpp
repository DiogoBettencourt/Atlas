#pragma once

#include "atlas/agent/LLMBackend.hpp"

#include <string>

namespace atlas::agent {

// LLMBackend over Ollama's local REST API (POST /api/chat; chat() asks for one
// reply, chatStream() reads Ollama's NDJSON stream).
// Talks to a locally running `ollama serve` instance; no network egress
// ever leaves the machine. Ollama's message shape (including a separate
// `thinking` field for reasoning models) already matches the canonical
// LLMBackend shape, so the response's "message" object is returned as-is.
class OllamaBackend : public LLMBackend {
public:
    explicit OllamaBackend(std::string host = "127.0.0.1", int port = 11434);

    OllamaBackend(const OllamaBackend&) = delete;
    OllamaBackend& operator=(const OllamaBackend&) = delete;
    OllamaBackend(OllamaBackend&&) = delete;
    OllamaBackend& operator=(OllamaBackend&&) = delete;

    [[nodiscard]] nlohmann::json chat(
        const std::string& model,
        const nlohmann::json& messages,
        const nlohmann::json& tools = nlohmann::json::array()) override;

    [[nodiscard]] nlohmann::json chatStream(
        const std::string& model,
        const nlohmann::json& messages,
        const nlohmann::json& tools,
        const DeltaCallback& on_delta) override;

    [[nodiscard]] std::string name() const override { return "ollama"; }

private:
    std::string host_;
    int port_;
};

} // namespace atlas::agent
