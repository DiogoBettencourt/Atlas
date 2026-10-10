#include "atlas/agent/OpenAICompatBackend.hpp"

#include "CancelWatch.hpp"
#include "StreamLines.hpp"

#include <httplib.h>

#include <atomic>
#include <deque>
#include <exception>
#include <map>
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

// Routes a streamed reply's text to "thinking" or "content" as it arrives.
// Servers that parse reasoning themselves send it in its own field and never
// need this; for the ones that leave the model's raw `<think>...</think>`
// inline at the start of the content, this finds that block live (the same
// thing extractInlineThinking does on a finished reply).
class ThinkSplitter {
public:
    using Emit = std::function<void(const char* kind, const std::string& text)>;

    // The server sent reasoning separately, so there is no inline block to look for.
    void markNoInline(const Emit& emit) {
        if (state_ == State::Done) return;
        state_ = State::Done;
        if (!pending_.empty()) {
            emit("content", pending_);
            pending_.clear();
        }
    }

    void feed(const std::string& piece, const Emit& emit) {
        if (piece.empty()) return;
        if (state_ == State::Done) {
            emit("content", piece);
            return;
        }
        pending_ += piece;

        if (state_ == State::Start) {
            const std::size_t first = pending_.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return; // only whitespace so far
            const std::string head = pending_.substr(first);
            if (head.size() < kOpen.size() && kOpen.compare(0, head.size(), head) == 0) {
                return; // could still turn into "<think>"
            }
            if (head.compare(0, kOpen.size(), kOpen) != 0) {
                state_ = State::Done;
                emit("content", pending_);
                pending_.clear();
                return;
            }
            state_ = State::InThink;
            pending_ = head.substr(kOpen.size());
        }

        // InThink
        const std::size_t close = pending_.find(kClose);
        if (close != std::string::npos) {
            if (close > 0) emit("thinking", pending_.substr(0, close));
            std::string rest = pending_.substr(close + kClose.size());
            const std::size_t keep = rest.find_first_not_of(" \t\r\n");
            rest = keep == std::string::npos ? std::string{} : rest.substr(keep);
            state_ = State::Done;
            pending_.clear();
            if (!rest.empty()) emit("content", rest);
            return;
        }
        // Hold back a possible partial "</think>" at the end.
        const std::size_t hold = kClose.size() - 1;
        if (pending_.size() > hold) {
            emit("thinking", pending_.substr(0, pending_.size() - hold));
            pending_.erase(0, pending_.size() - hold);
        }
    }

    // The stream ended: release whatever is still held back.
    void finish(const Emit& emit) {
        if (pending_.empty()) return;
        emit(state_ == State::InThink ? "thinking" : "content", pending_);
        pending_.clear();
    }

private:
    enum class State { Start, InThink, Done };
    inline static const std::string kOpen = "<think>";
    inline static const std::string kClose = "</think>";
    State state_ = State::Start;
    std::string pending_;
};

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

json OpenAICompatBackend::chatStream(const std::string& model, const json& messages, const json& tools,
                                     const DeltaCallback& on_delta, const CancelCheck& cancelled) {
    httplib::Client client(origin_);
    if (!client.is_valid()) {
        throw std::runtime_error(std::string(kErrPrefix) + "cannot create a client for '" + api_base_ +
                                 "' (https:// needs a build with OpenSSL)");
    }
    client.set_connection_timeout(5, 0);
    client.set_read_timeout(300, 0); // between chunks, not for the whole reply

    json payload{{"model", model}, {"messages", toWireMessages(messages)}, {"stream", true}};
    if (!tools.empty()) payload["tools"] = tools;

    httplib::Request request;
    request.method = "POST";
    request.path = endpoint_;
    request.set_header("Content-Type", "application/json");
    if (!api_key_.empty()) request.set_header("Authorization", "Bearer " + api_key_);
    request.body = payload.dump();

    struct ToolAccumulator {
        std::string id;
        std::string name;
        std::string arguments;
    };

    int status = 0;
    std::string error_body;
    std::string stream_error;
    std::string content;   // raw, exactly as the server sent it
    std::string reasoning;
    std::map<int, ToolAccumulator> tool_parts;
    std::exception_ptr callback_error;
    std::atomic<bool> was_cancelled{false};
    ThinkSplitter splitter;
    StreamLines lines;

    const ThinkSplitter::Emit emit_delta = [&on_delta](const char* kind, const std::string& text) {
        if (on_delta) on_delta(kind, text);
    };

    auto handle_line = [&](const std::string& line) {
        if (line.rfind("data:", 0) != 0) return; // SSE comments, "event:" lines, etc.
        std::string data = line.substr(5);
        const std::size_t first = data.find_first_not_of(' ');
        data = first == std::string::npos ? std::string{} : data.substr(first);
        if (data.empty() || data == "[DONE]") return;

        json chunk = json::parse(data, nullptr, false);
        if (chunk.is_discarded() || !chunk.is_object()) return;
        if (chunk.contains("error")) {
            stream_error = chunk["error"].is_string()
                               ? chunk["error"].get<std::string>()
                               : (chunk["error"].is_object() ? stringOr(chunk["error"], "message") : "");
            if (stream_error.empty()) stream_error = chunk["error"].dump();
            return;
        }
        if (!chunk.contains("choices") || !chunk["choices"].is_array() || chunk["choices"].empty()) return;
        const json& choice = chunk["choices"][0];
        if (!choice.is_object() || !choice.contains("delta") || !choice["delta"].is_object()) return;
        const json& delta = choice["delta"];

        std::string reasoning_piece = stringOr(delta, "reasoning_content");
        if (reasoning_piece.empty()) reasoning_piece = stringOr(delta, "reasoning");
        if (!reasoning_piece.empty()) {
            reasoning += reasoning_piece;
            splitter.markNoInline(emit_delta);
            if (on_delta) on_delta("thinking", reasoning_piece);
        }

        const std::string content_piece = stringOr(delta, "content");
        if (!content_piece.empty()) {
            content += content_piece;
            splitter.feed(content_piece, emit_delta);
        }

        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            int position = 0;
            for (const auto& call : delta["tool_calls"]) {
                const int index = call.contains("index") && call["index"].is_number_integer()
                                      ? call["index"].get<int>()
                                      : position;
                ++position;
                ToolAccumulator& acc = tool_parts[index];
                const std::string id = stringOr(call, "id");
                if (acc.id.empty() && !id.empty()) acc.id = id;
                if (call.contains("function") && call["function"].is_object()) {
                    const json& fn = call["function"];
                    // The name arrives whole (some servers repeat it on every
                    // chunk), the arguments arrive as string fragments.
                    if (acc.name.empty()) acc.name = stringOr(fn, "name");
                    acc.arguments += stringOr(fn, "arguments");
                }
            }
        }
    };

    request.response_handler = [&status](const httplib::Response& response) {
        status = response.status;
        return true;
    };
    request.content_receiver = [&](const char* data, std::size_t length, std::uint64_t, std::uint64_t) {
        if (cancelled && cancelled()) {
            was_cancelled = true;
            return false;
        }
        if (status != 200) {
            error_body.append(data, length);
            return true;
        }
        try {
            lines.feed(data, length, handle_line);
        } catch (...) {
            callback_error = std::current_exception();
            return false;
        }
        return true;
    };

    // Closes the connection the moment Stop is pressed, even while the server
    // is still reading the prompt and has sent nothing yet.
    CancelWatch watch(cancelled, [&] {
        was_cancelled = true;
        client.stop();
    });

    auto response = client.send(request);

    if (was_cancelled) {
        // Keep what the user already saw: the text so far, minus any tool calls.
        json wire{{"role", "assistant"}, {"content", content}};
        if (!reasoning.empty()) wire["reasoning_content"] = reasoning;
        throw Cancelled(fromWireMessage(wire));
    }
    if (callback_error) std::rethrow_exception(callback_error);
    if (!response && status == 0) {
        throw std::runtime_error(std::string(kErrPrefix) + "failed to reach " + api_base_ +
                                 " - is the server running? (e.g. `llama-server -m model.gguf --port 8081`)");
    }
    if (status == 401 || status == 403) {
        throw std::runtime_error(std::string(kErrPrefix) + "server returned HTTP " + std::to_string(status) +
                                 " - check the API key (set ATLAS_API_KEY or pass --api-key)");
    }
    if (status != 200) {
        throw std::runtime_error(std::string(kErrPrefix) + "server returned HTTP " + std::to_string(status) +
                                 ": " + error_body);
    }
    if (!response) {
        throw std::runtime_error(std::string(kErrPrefix) + "connection to the server was lost mid-reply");
    }
    lines.flush(handle_line);
    splitter.finish(emit_delta);
    if (!stream_error.empty()) {
        throw std::runtime_error(std::string(kErrPrefix) + "server reported an error: " + stream_error);
    }

    // Reassemble the streamed pieces into the same wire message a
    // non-streaming reply would have carried, and translate it the same way.
    json wire{{"role", "assistant"}, {"content", content}};
    if (!reasoning.empty()) wire["reasoning_content"] = reasoning;
    if (!tool_parts.empty()) {
        json calls = json::array();
        for (const auto& [index, acc] : tool_parts) {
            (void)index;
            json call{{"type", "function"}, {"function", {{"name", acc.name}, {"arguments", acc.arguments}}}};
            if (!acc.id.empty()) call["id"] = acc.id;
            calls.push_back(std::move(call));
        }
        wire["tool_calls"] = std::move(calls);
    }
    return fromWireMessage(wire);
}

} // namespace atlas::agent
