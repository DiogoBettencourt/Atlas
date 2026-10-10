#include "atlas/agent/OpenAICompatBackend.hpp"

#include <httplib.h>

#include <deque>
#include <stdexcept>

namespace atlas::agent {

namespace {

using json = nlohmann::json;

constexpr const char* kErrPrefix = "OpenAICompatBackend: ";

std::string trimTrailingSlashes(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

// If `content` starts (after whitespace) with a <think>...</think> block,
// moves that block's text into `thinking` and leaves the remainder as the
// content. Servers started with reasoning parsing off (e.g.
// --reasoning-format none) leave the model's raw reasoning inline.
void extractInlineThinking(std::string& content, std::string& thinking) {
    static const std::string kOpen = "<think>";
    static const std::string kClose = "</think>";

    std::size_t start = content.find_first_not_of(" \t\r\n");
    if (start == std::string::npos || content.compare(start, kOpen.size(), kOpen) != 0) return;

    std::size_t close = content.find(kClose, start + kOpen.size());
    if (close == std::string::npos) return; // unterminated: leave it alone

    thinking = content.substr(start + kOpen.size(), close - (start + kOpen.size()));
    std::size_t rest = content.find_first_not_of(" \t\r\n", close + kClose.size());
    content = rest == std::string::npos ? std::string{} : content.substr(rest);

    // Trim the reasoning text's own surrounding whitespace.
    std::size_t a = thinking.find_first_not_of(" \t\r\n");
    std::size_t b = thinking.find_last_not_of(" \t\r\n");
    thinking = a == std::string::npos ? std::string{} : thinking.substr(a, b - a + 1);
}

std::string stringOr(const json& obj, const char* key) {
    if (obj.is_object() && obj.contains(key) && obj[key].is_string()) {
        return obj[key].get<std::string>();
    }
    return {};
}

} // namespace

OpenAICompatBackend::OpenAICompatBackend(std::string api_base, std::string api_key)
    : api_base_(api_base), api_key_(std::move(api_key)) {
    const std::size_t scheme_end = api_base.find("://");
    const std::string scheme = scheme_end == std::string::npos ? "" : api_base.substr(0, scheme_end);
    if (scheme != "http" && scheme != "https") {
        throw std::invalid_argument(std::string(kErrPrefix) + "api base must start with http:// or https:// (got '" +
                                    api_base + "')");
    }

    const std::size_t path_start = api_base.find('/', scheme_end + 3);
    origin_ = api_base.substr(0, path_start);
    std::string path = path_start == std::string::npos ? std::string{} : trimTrailingSlashes(api_base.substr(path_start));

    if (origin_.size() <= scheme_end + 3) {
        throw std::invalid_argument(std::string(kErrPrefix) + "api base is missing a host (got '" + api_base + "')");
    }

    // "http://h:8081" and "http://h:8081/v1" both mean the same endpoint.
    const bool ends_in_v1 = path.size() >= 3 && path.compare(path.size() - 3, 3, "/v1") == 0;
    endpoint_ = (ends_in_v1 ? path : path + "/v1") + "/chat/completions";
}

json OpenAICompatBackend::toWireMessages(const json& messages) {
    json out = json::array();
    std::deque<std::string> unanswered_ids;
    int next_id = 0;

    for (const auto& msg : messages) {
        const std::string role = msg.value("role", std::string{});

        if (role == "assistant") {
            json wire{{"role", "assistant"}, {"content", stringOr(msg, "content")}};

            if (msg.contains("tool_calls") && msg["tool_calls"].is_array() && !msg["tool_calls"].empty()) {
                json calls = json::array();
                for (const auto& call : msg["tool_calls"]) {
                    const json fn = call.contains("function") ? call["function"] : json::object();

                    std::string id = stringOr(call, "id");
                    if (id.empty()) id = "call_" + std::to_string(++next_id);
                    unanswered_ids.push_back(id);

                    std::string arguments = "{}";
                    if (fn.contains("arguments")) {
                        arguments = fn["arguments"].is_string() ? fn["arguments"].get<std::string>()
                                                                : fn["arguments"].dump();
                    }

                    calls.push_back({{"id", id},
                                     {"type", "function"},
                                     {"function", {{"name", stringOr(fn, "name")}, {"arguments", arguments}}}});
                }
                wire["tool_calls"] = std::move(calls);
            }
            out.push_back(std::move(wire));

        } else if (role == "tool") {
            std::string id = stringOr(msg, "tool_call_id");
            if (id.empty()) {
                if (!unanswered_ids.empty()) {
                    id = unanswered_ids.front();
                    unanswered_ids.pop_front();
                } else {
                    // History was trimmed so the originating call is gone.
                    id = "call_" + std::to_string(++next_id);
                }
            }
            out.push_back({{"role", "tool"}, {"tool_call_id", id}, {"content", stringOr(msg, "content")}});

        } else {
            json wire{{"role", role}};
            wire["content"] = msg.contains("content") ? msg["content"] : json("");
            out.push_back(std::move(wire));
        }
    }
    return out;
}

json OpenAICompatBackend::fromWireMessage(const json& wire) {
    std::string content = stringOr(wire, "content"); // null -> ""
    std::string thinking = stringOr(wire, "reasoning_content");
    if (thinking.empty()) thinking = stringOr(wire, "reasoning");
    if (thinking.empty()) extractInlineThinking(content, thinking);

    json out{{"role", "assistant"}, {"content", content}};
    if (!thinking.empty()) out["thinking"] = thinking;

    if (wire.contains("tool_calls") && wire["tool_calls"].is_array() && !wire["tool_calls"].empty()) {
        json calls = json::array();
        for (const auto& call : wire["tool_calls"]) {
            const json fn = call.contains("function") ? call["function"] : json::object();

            json arguments = json::object();
            if (fn.contains("arguments")) {
                if (fn["arguments"].is_string()) {
                    const std::string raw = fn["arguments"].get<std::string>();
                    if (!raw.empty()) {
                        json parsed = json::parse(raw, nullptr, false);
                        // Keep unparseable text as-is rather than silently
                        // turning it into {}; Agent copes with either.
                        arguments = parsed.is_discarded() ? json(raw) : parsed;
                    }
                } else if (fn["arguments"].is_object()) {
                    arguments = fn["arguments"];
                }
            }

            json entry{{"type", "function"}, {"function", {{"name", stringOr(fn, "name")}, {"arguments", arguments}}}};
            const std::string id = stringOr(call, "id");
            if (!id.empty()) entry["id"] = id;
            calls.push_back(std::move(entry));
        }
        out["tool_calls"] = std::move(calls);
    }
    return out;
}

json OpenAICompatBackend::chat(const std::string& model, const json& messages, const json& tools) {
    httplib::Client client(origin_);
    if (!client.is_valid()) {
        throw std::runtime_error(std::string(kErrPrefix) + "cannot create a client for '" + api_base_ +
                                 "' (https:// needs a build with OpenSSL)");
    }
    client.set_connection_timeout(5, 0);
    client.set_read_timeout(300, 0); // local inference can be slow on CPU

    json payload{{"model", model}, {"messages", toWireMessages(messages)}, {"stream", false}};
    if (!tools.empty()) payload["tools"] = tools;

    httplib::Headers headers;
    if (!api_key_.empty()) headers.emplace("Authorization", "Bearer " + api_key_);

    auto response = client.Post(endpoint_, headers, payload.dump(), "application/json");

    if (!response) {
        throw std::runtime_error(std::string(kErrPrefix) + "failed to reach " + api_base_ +
                                 " - is the server running? (e.g. `llama-server -m model.gguf --port 8081`)");
    }
    if (response->status == 401 || response->status == 403) {
        throw std::runtime_error(std::string(kErrPrefix) + "server returned HTTP " +
                                 std::to_string(response->status) +
                                 " - check the API key (set ATLAS_API_KEY or pass --api-key)");
    }
    if (response->status != 200) {
        throw std::runtime_error(std::string(kErrPrefix) + "server returned HTTP " +
                                 std::to_string(response->status) + ": " + response->body);
    }

    json parsed = json::parse(response->body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("choices") || !parsed["choices"].is_array() ||
        parsed["choices"].empty() || !parsed["choices"][0].contains("message") ||
        !parsed["choices"][0]["message"].is_object()) {
        throw std::runtime_error(std::string(kErrPrefix) + "malformed response: " + response->body);
    }

    return fromWireMessage(parsed["choices"][0]["message"]);
}

} // namespace atlas::agent
