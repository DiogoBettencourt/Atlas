#include "atlas/agent/OllamaBackend.hpp"

#include "StreamLines.hpp"

#include <exception>
#include <httplib.h>
#include <stdexcept>

namespace atlas::agent {

OllamaBackend::OllamaBackend(std::string host, int port) : host_(std::move(host)), port_(port) {}

nlohmann::json OllamaBackend::chat(const std::string& model,
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
            "OllamaBackend: failed to reach Ollama at " + host_ + ":" + std::to_string(port_) +
            " - is `ollama serve` running?");
    }
    if (response->status != 200) {
        throw std::runtime_error("OllamaBackend: Ollama returned HTTP " +
                                  std::to_string(response->status) + ": " + response->body);
    }

    nlohmann::json parsed = nlohmann::json::parse(response->body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("message")) {
        throw std::runtime_error("OllamaBackend: malformed response from Ollama: " + response->body);
    }

    return parsed["message"];
}

nlohmann::json OllamaBackend::chatStream(const std::string& model,
                                         const nlohmann::json& messages,
                                         const nlohmann::json& tools,
                                         const DeltaCallback& on_delta) {
    httplib::Client client(host_, port_);
    client.set_connection_timeout(5, 0);
    // Applies between chunks, not to the whole reply, so a slow model that
    // keeps producing tokens never trips it.
    client.set_read_timeout(300, 0);

    nlohmann::json payload{{"model", model}, {"messages", messages}, {"stream", true}};
    if (!tools.empty()) {
        payload["tools"] = tools;
    }

    httplib::Request request;
    request.method = "POST";
    request.path = "/api/chat";
    request.set_header("Content-Type", "application/json");
    request.body = payload.dump();

    int status = 0;
    std::string error_body;    // the body of a non-200 response
    std::string stream_error;  // an {"error": "..."} line inside a 200 stream
    std::string content;
    std::string thinking;
    nlohmann::json tool_calls = nlohmann::json::array();
    std::exception_ptr callback_error;
    StreamLines lines;

    auto handle_line = [&](const std::string& line) {
        nlohmann::json chunk = nlohmann::json::parse(line, nullptr, false);
        if (chunk.is_discarded() || !chunk.is_object()) {
            return; // not JSON (a stray blank or proxy line): nothing to learn from it
        }
        if (chunk.contains("error")) {
            stream_error = chunk["error"].is_string() ? chunk["error"].get<std::string>()
                                                      : chunk["error"].dump();
            return;
        }
        if (!chunk.contains("message") || !chunk["message"].is_object()) {
            return;
        }
        const auto& message = chunk["message"];

        auto text_of = [&message](const char* key) {
            return message.contains(key) && message[key].is_string() ? message[key].get<std::string>()
                                                                      : std::string{};
        };
        const std::string thinking_piece = text_of("thinking");
        const std::string content_piece = text_of("content");

        if (!thinking_piece.empty()) {
            thinking += thinking_piece;
            if (on_delta) on_delta("thinking", thinking_piece);
        }
        if (!content_piece.empty()) {
            content += content_piece;
            if (on_delta) on_delta("content", content_piece);
        }
        if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
            for (const auto& call : message["tool_calls"]) tool_calls.push_back(call);
        }
    };

    request.response_handler = [&status](const httplib::Response& response) {
        status = response.status;
        return true;
    };
    request.content_receiver = [&](const char* data, std::size_t length, std::uint64_t, std::uint64_t) {
        if (status != 200) {
            error_body.append(data, length);
            return true;
        }
        try {
            lines.feed(data, length, handle_line);
        } catch (...) {
            // The caller's callback threw: stop reading and rethrow below.
            callback_error = std::current_exception();
            return false;
        }
        return true;
    };

    auto response = client.send(request);

    if (callback_error) {
        std::rethrow_exception(callback_error);
    }
    if (!response && status == 0) {
        throw std::runtime_error(
            "OllamaBackend: failed to reach Ollama at " + host_ + ":" + std::to_string(port_) +
            " - is `ollama serve` running?");
    }
    if (status != 200) {
        throw std::runtime_error("OllamaBackend: Ollama returned HTTP " + std::to_string(status) +
                                 ": " + error_body);
    }
    if (!response) {
        throw std::runtime_error("OllamaBackend: connection to Ollama was lost mid-reply");
    }
    lines.flush(handle_line);
    if (!stream_error.empty()) {
        throw std::runtime_error("OllamaBackend: Ollama reported an error: " + stream_error);
    }

    nlohmann::json message{{"role", "assistant"}, {"content", content}};
    if (!thinking.empty()) message["thinking"] = thinking;
    if (!tool_calls.empty()) message["tool_calls"] = tool_calls;
    return message;
}

} // namespace atlas::agent
