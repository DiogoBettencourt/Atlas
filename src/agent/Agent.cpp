#include "atlas/agent/Agent.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace atlas::agent {

Agent::Agent(LLMClient& llm_client,
             tools::ToolManager& tool_manager,
             core::SessionManager& session_manager,
             std::string model_name)
    : llm_client_(llm_client),
      tool_manager_(tool_manager),
      session_manager_(session_manager),
      model_name_(std::move(model_name)) {}

namespace {

nlohmann::json synthesizeToolCall(const nlohmann::json& parsed) {
    return nlohmann::json::array({nlohmann::json{
        {"function", {
            {"name", parsed["name"]},
            {"arguments", parsed.value("arguments", nlohmann::json::object())}
        }}
    }});
}

std::string trimWhitespace(const std::string& s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    auto begin = std::find_if(s.begin(), s.end(), not_space);
    auto end = std::find_if(s.rbegin(), s.rend(), not_space).base();
    return (begin < end) ? std::string(begin, end) : std::string{};
}

} // namespace

nlohmann::json Agent::extractToolCalls(const nlohmann::json& assistant_message) {
    if (assistant_message.contains("tool_calls") && assistant_message["tool_calls"].is_array() &&
        !assistant_message["tool_calls"].empty()) {
        return assistant_message["tool_calls"];
    }

    if (!assistant_message.contains("content") || !assistant_message["content"].is_string()) {
        return nlohmann::json::array();
    }
    std::string content = assistant_message["content"].get<std::string>();

    // Fallback 1: the whole message is (aside from surrounding whitespace) a
    // bare JSON object describing the call, e.g.
    // {"name": "read_file", "arguments": {"path": "x.cpp"}}
    // Observed in practice from qwen2.5-coder:14b: some responses skip both
    // native tool_calls and the fenced-block convention below entirely.
    {
        auto parsed = nlohmann::json::parse(trimWhitespace(content), nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object() && parsed.contains("name") &&
            parsed["name"].is_string()) {
            return synthesizeToolCall(parsed);
        }
    }

    // Fallback 2: some smaller / non-native-tool-calling models leak their
    // intended call as a fenced JSON block in `content`, e.g.
    // ```json\n{"name": "read_file", "arguments": {"path": "x.cpp"}}\n```
    static const std::regex fence_re(R"(```(?:json)?\s*([\s\S]*?)```)");
    std::smatch match;
    if (std::regex_search(content, match, fence_re)) {
        auto parsed = nlohmann::json::parse(match[1].str(), nullptr, false);
        if (!parsed.is_discarded() && parsed.contains("name")) {
            return synthesizeToolCall(parsed);
        }
    }

    return nlohmann::json::array();
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
emit({{"type", "iteration_start"}, {"iteration", iteration + 1}, {"max_iterations", max_iterations_}});

nlohmann::json history = session_manager_.getHistory(session_id);

// ---------------------------------------------------------
// ARCHITECT FIX: Inject Strict System Prompt
// ---------------------------------------------------------
nlohmann::json system_msg = {
{"role", "system"},
{"content", "You are Atlas, an expert C++20 software architect. "
   "CRITICAL RULE: If the user simply greets you, asks a general question, or does not require file operations, respond naturally in plain text WITHOUT invoking any tools. "
   "Only use tools when explicitly necessary to read, search, or write code."}
};
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

std::string give_up = "Agent stopped: exceeded maximum tool-call iterations ("
+ std::to_string(max_iterations_) + ") without a final answer.";
emit({{"type", "error"}, {"message", give_up}});
return give_up;
}

} // namespace atlas::agent
