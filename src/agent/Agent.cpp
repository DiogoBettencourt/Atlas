#include "atlas/agent/Agent.hpp"

#include <algorithm>
#include <optional>
#include <regex>

namespace atlas::agent {

namespace {

// Scans `text` for the first syntactically-balanced top-level JSON object
// (a '{' ... matching '}' span, respecting quoted strings and backslash
// escapes) and returns that substring, or std::nullopt if none is found.
//
// Only the *first* balanced object is ever considered. This matters
// because some models, when given a multi-step instruction, front-load
// several tool calls into a single response back-to-back with no fencing
// or separators at all - e.g. observed live:
//   {"name":"edit_file",...} {"name":"git","arguments":{"action":"add"...
//   {"name":"git","arguments":{"action":"commit"...} {"name":"github_pr"...
// The ReAct loop in chat() below only executes one tool call per
// iteration and feeds its real result back before asking the model what
// to do next, so grabbing just the first object here and letting later
// iterations pick up the remaining steps (once they're no longer guesses)
// is the correct behavior, not a limitation to work around.
std::optional<std::string> findFirstJsonObject(const std::string& text) {
    std::size_t start = text.find('{');
    if (start == std::string::npos) {
        return std::nullopt;
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (std::size_t i = start; i < text.size(); ++i) {
        char c = text[i];

        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return text.substr(start, i - start + 1);
            }
        }
    }

    // Unbalanced (truncated mid-object, etc.) - not usable.
    return std::nullopt;
}

// Builds the same synthetic native-tool-call shape used elsewhere in this
// file from a parsed `{"name": ..., "arguments": {...}}` object, or
// nlohmann::json::array() if `parsed` doesn't look like a tool call.
nlohmann::json toSyntheticToolCall(const nlohmann::json& parsed) {
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("name")) {
        return nlohmann::json::array();
    }
    nlohmann::json synthetic_call = {
        {"function", {
            {"name", parsed["name"]},
            {"arguments", parsed.value("arguments", nlohmann::json::object())}
        }}
    };
    return nlohmann::json::array({synthetic_call});
}

} // namespace

Agent::Agent(LLMClient& llm_client,
             tools::ToolManager& tool_manager,
             core::SessionManager& session_manager,
             std::string model_name)
    : llm_client_(llm_client),
      tool_manager_(tool_manager),
      session_manager_(session_manager),
      model_name_(std::move(model_name)) {}

nlohmann::json Agent::extractToolCalls(const nlohmann::json& assistant_message) {
    if (assistant_message.contains("tool_calls") && assistant_message["tool_calls"].is_array() &&
        !assistant_message["tool_calls"].empty()) {
        return assistant_message["tool_calls"];
    }

    if (!assistant_message.contains("content") || !assistant_message["content"].is_string()) {
        return nlohmann::json::array();
    }
    std::string content = assistant_message["content"].get<std::string>();

    // Fallback 1: some smaller / non-native-tool-calling models leak their
    // intended call as a fenced JSON block in `content`, e.g.
    // ```json\n{"name": "read_file", "arguments": {"path": "x.cpp"}}\n```
    static const std::regex fence_re(R"(```(?:json)?\s*([\s\S]*?)```)");
    std::smatch match;
    if (std::regex_search(content, match, fence_re)) {
        auto parsed = nlohmann::json::parse(match[1].str(), nullptr, false);
        nlohmann::json call = toSyntheticToolCall(parsed);
        if (!call.empty()) {
            return call;
        }
    }

    // Fallback 2: no fencing at all - a bare JSON object (or, as observed
    // live, several bare objects concatenated back-to-back). Scan for the
    // first balanced object anywhere in the text; see findFirstJsonObject
    // for why only the first one is used.
    if (auto candidate = findFirstJsonObject(content)) {
        auto parsed = nlohmann::json::parse(*candidate, nullptr, false);
        nlohmann::json call = toSyntheticToolCall(parsed);
        if (!call.empty()) {
            return call;
        }
    }

    return nlohmann::json::array();
}

nlohmann::json Agent::trimHistory(const nlohmann::json& history, std::size_t max_messages) {
    if (!history.is_array() || history.size() <= max_messages) {
        return history;
    }

    std::size_t start = history.size() - max_messages;

    // Walk forward to the next "user" message so the trimmed window never
    // starts mid-turn - see the doc comment on the declaration for why.
    std::size_t boundary = start;
    while (boundary < history.size() && history[boundary].value("role", std::string{}) != "user") {
        ++boundary;
    }
    if (boundary < history.size()) {
        start = boundary;
    }
    // else: no "user" message anywhere in the window - fall back to the
    // hard cut at the original `start` rather than sending nothing.

    nlohmann::json trimmed = nlohmann::json::array();
    for (std::size_t i = start; i < history.size(); ++i) {
        trimmed.push_back(history[i]);
    }
    return trimmed;
}

nlohmann::json Agent::buildCompactionRequest(const std::string& previous_summary,
                                              const nlohmann::json& messages_to_summarize) {
    std::string transcript;
    if (messages_to_summarize.is_array()) {
        for (const auto& entry : messages_to_summarize) {
            std::string role = entry.value("role", std::string{"unknown"});
            std::string content = entry.value("content", std::string{});
            transcript += "[" + role + "] " + content + "\n";
        }
    }

    std::string instruction =
        "You are compacting an AI coding agent's conversation history to keep its "
        "prompt size bounded. Summarize the conversation turns below concisely but "
        "completely, preserving: what the user asked for, decisions made and why, "
        "specific file paths and code changes discussed, results of tool calls with "
        "lasting relevance, and any still-open or pending tasks. Write it as plain "
        "prose notes for your own later reference, not a transcript or a reply to "
        "anyone. Do not include pleasantries and do not restate this instruction.\n\n";

    if (!previous_summary.empty()) {
        instruction += "Existing summary of even earlier turns - fold this in, don't "
                        "drop anything it captures:\n" + previous_summary + "\n\n";
    }

    instruction += "Conversation turns to summarize:\n" + transcript;

    return nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"content", instruction}}
    });
}

std::string Agent::chat(const std::string& message,
                         const std::string& session_id,
                         const std::string& workspace_root,
                         const EventCallback& on_event) {
    auto emit = [&on_event](const nlohmann::json& event) {
        if (on_event) on_event(event);
    };

    session_manager_.appendMessage(session_id, {{"role", "user"}, {"content", message}});

    for (unsigned int iteration = 0; iteration < max_iterations_; ++iteration) {
        emit({{"type", "iteration_start"},
              {"iteration", iteration + 1},
              {"max_iterations", max_iterations_}});

        // SessionManager keeps (and persists) the session's full history
        // regardless of what we do here. What actually goes to the LLM
        // this turn is built in three steps:
        //   1. Take the "tail" - everything after the last compaction
        //      checkpoint (or the whole history, if there isn't one yet).
        //   2. If that tail is still within max_history_messages_, send it
        //      as-is (plus the persisted summary, if any, folded into the
        //      system prompt below).
        //   3. If it's grown past the bound, fold the aged-out prefix into
        //      an updated summary via one extra LLM call instead of just
        //      dropping it, and persist the new checkpoint so this only
        //      has to happen again once the tail regrows. If that
        //      summarization call itself fails for any reason, fall back
        //      to hard-dropping the prefix for this turn only (the old
        //      trimHistory-only behavior) rather than failing the user's
        //      actual request over it - it'll simply be retried next time
        //      the tail crosses the bound again.
        nlohmann::json full_history = session_manager_.getHistory(session_id);
        auto checkpoint = session_manager_.getSummary(session_id);
        std::size_t covers = checkpoint ? std::min(checkpoint->covers_through_index, full_history.size())
                                         : std::size_t{0};

        nlohmann::json tail = nlohmann::json::array();
        for (std::size_t i = covers; i < full_history.size(); ++i) {
            tail.push_back(full_history[i]);
        }

        nlohmann::json history = trimHistory(tail, max_history_messages_);
        if (history.size() < tail.size()) {
            std::size_t cut = tail.size() - history.size();
            nlohmann::json to_summarize = nlohmann::json::array();
            for (std::size_t i = 0; i < cut; ++i) {
                to_summarize.push_back(tail[i]);
            }

            try {
                std::string previous_summary = checkpoint ? checkpoint->summary : std::string{};
                nlohmann::json summarization_request =
                    buildCompactionRequest(previous_summary, to_summarize);
                nlohmann::json summary_reply = llm_client_.chat(model_name_, summarization_request);

                core::SessionSummary new_checkpoint;
                new_checkpoint.covers_through_index = covers + cut;
                new_checkpoint.summary = summary_reply.value("content", previous_summary);
                session_manager_.setSummary(session_id, new_checkpoint);
                checkpoint = new_checkpoint;
            } catch (const std::exception&) {
                // Best-effort: the aged-out prefix was already excluded
                // from `history` above via trimHistory, so this turn
                // simply proceeds with the plain hard-drop behavior
                // instead of the summarized one.
            }
        }

        nlohmann::json system_msg = {
            {"role", "system"},
            {"content", "You are Atlas, an expert C++20 software architect. "
                        "CRITICAL RULE: If the user simply greets you, asks a general "
                        "question, or does not require file operations, respond "
                        "naturally in plain text WITHOUT invoking any tools. Only use "
                        "tools when explicitly necessary to read, search, or write "
                        "code."}
        };
        if (checkpoint) {
            system_msg["content"] = system_msg["content"].get<std::string>() +
                "\n\n--- Summary of earlier conversation ---\n" + checkpoint->summary;
        }
        history.insert(history.begin(), system_msg);

        nlohmann::json assistant_message;
        try {
            assistant_message = llm_client_.chat(model_name_, history, tool_manager_.schemasJson());
        } catch (const std::exception& e) {
            std::string error_msg = std::string("Agent error: ") + e.what();
            emit({{"type", "error"}, {"message", error_msg}});
            return error_msg;
        }

        session_manager_.appendMessage(session_id, assistant_message);

        nlohmann::json tool_calls = extractToolCalls(assistant_message);
        if (tool_calls.empty()) {
            std::string reply = assistant_message.value("content", std::string{});
            emit({{"type", "final"}, {"reply", reply}});
            return reply;
        }

        std::string interim_content = assistant_message.value("content", std::string{});
        if (!interim_content.empty()) {
            emit({{"type", "assistant_thought"}, {"content", interim_content}});
        }

        for (const auto& call : tool_calls) {
            std::string tool_name;
            nlohmann::json arguments = nlohmann::json::object();

            if (call.contains("function")) {
                tool_name = call["function"].value("name", std::string{});
                auto raw_args = call["function"].value("arguments", nlohmann::json::object());
                if (raw_args.is_string()) {
                    auto parsed = nlohmann::json::parse(raw_args.get<std::string>(), nullptr, false);
                    arguments = parsed.is_discarded() ? nlohmann::json::object() : parsed;
                } else {
                    arguments = raw_args;
                }
            }

            emit({{"type", "tool_call"}, {"name", tool_name}, {"arguments", arguments}});

            nlohmann::json result = tool_name.empty()
                ? nlohmann::json{{"error", "malformed tool call: missing function name"}}
                : tool_manager_.execute(tool_name, arguments, workspace_root);

            emit({{"type", "tool_result"}, {"name", tool_name}, {"result", result}});

            nlohmann::json tool_message{
                {"role", "tool"},
                {"content", result.dump()}
            };
            if (!tool_name.empty()) {
                tool_message["name"] = tool_name;
            }
            session_manager_.appendMessage(session_id, tool_message);
        }
    }

    std::string give_up = "Agent stopped: exceeded maximum tool-call iterations (" +
                           std::to_string(max_iterations_) + ") without a final answer.";
    emit({{"type", "error"}, {"message", give_up}});
    return give_up;
}

} // namespace atlas::agent
