#include "atlas/agent/Agent.hpp"

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

nlohmann::json Agent::extractToolCalls(const nlohmann::json& assistant_message) {
    if (assistant_message.contains("tool_calls") && assistant_message["tool_calls"].is_array() &&
        !assistant_message["tool_calls"].empty()) {
        return assistant_message["tool_calls"];
    }

    // Fallback: some smaller / non-native-tool-calling models leak their
    // intended call as a fenced JSON block in `content`, e.g.
    // ```json\n{"name": "read_file", "arguments": {"path": "x.cpp"}}\n```
    if (assistant_message.contains("content") && assistant_message["content"].is_string()) {
        static const std::regex fence_re(R"(```(?:json)?\s*([\s\S]*?)```)");
        std::string content = assistant_message["content"].get<std::string>();
        std::smatch match;
        if (std::regex_search(content, match, fence_re)) {
            auto parsed = nlohmann::json::parse(match[1].str(), nullptr, false);
            if (!parsed.is_discarded() && parsed.contains("name")) {
                nlohmann::json synthetic_call = {
                    {"function", {
                        {"name", parsed["name"]},
                        {"arguments", parsed.value("arguments", nlohmann::json::object())}
                    }}
                };
                return nlohmann::json::array({synthetic_call});
            }
        }
    }

    return nlohmann::json::array();
}

std::string Agent::chat(const std::string& message,
                         const std::string& session_id,
                         const std::string& workspace_root,
                         const EventCallback& on_event) {
    // Firing an event when there's no listener would just build JSON for
    // nothing on every step of every request, so make it a genuine no-op.
    auto emit = [&on_event](const nlohmann::json& event) {
        if (on_event) on_event(event);
    };

    session_manager_.appendMessage(session_id, {{"role", "user"}, {"content", message}});

    for (unsigned int iteration = 0; iteration < max_iterations_; ++iteration) {
        emit({{"type", "iteration_start"}, {"iteration", iteration + 1}, {"max_iterations", max_iterations_}});

        nlohmann::json history = session_manager_.getHistory(session_id);

        nlohmann::json assistant_message;
        try {
            assistant_message = llm_client_.chat(model_name_, history, tool_manager_.schemasJson());
        } catch (const std::exception& e) {
            std::string error_msg = std::string("Agent error: ") + e.what();
            emit({{"type", "error"}, {"message", error_msg}});
            return error_msg;
        }

        // Persist the assistant turn (even if it also contains tool calls)
        // so the transcript accurately reflects what the model produced.
        session_manager_.appendMessage(session_id, assistant_message);

        nlohmann::json tool_calls = extractToolCalls(assistant_message);
        if (tool_calls.empty()) {
            std::string reply = assistant_message.value("content", std::string{});
            emit({{"type", "final"}, {"reply", reply}});
            return reply;
        }

        // Some models narrate their plan in `content` alongside the tool
        // call itself (rather than leaving it empty) - surface that too so
        // a live viewer sees the model's reasoning, not just raw tool I/O.
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
                // Ollama may hand back arguments as a JSON-encoded string
                // rather than a native object, depending on model output.
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
        // Loop again so the model can react to the tool results.
    }

    std::string give_up = "Agent stopped: exceeded maximum tool-call iterations ("
           + std::to_string(max_iterations_) + ") without a final answer.";
    emit({{"type", "error"}, {"message", give_up}});
    return give_up;
}

} // namespace atlas::agent
