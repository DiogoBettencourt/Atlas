// Manual smoke test for Agent::extractToolCalls against captured shapes of
// real Ollama /api/chat responses - including the bare-JSON-content case
// this file's PR fixes, reproduced live from qwen2.5-coder:14b during a
// self-improvement test run (see PR description).
#include "atlas/agent/Agent.hpp"

#include <iostream>
#include <string>

using json = nlohmann::json;

namespace {
int failures = 0;

json parseMessage(const std::string& raw) {
    auto parsed = json::parse(raw, nullptr, false);
    if (parsed.is_discarded()) {
        std::cout << "FAIL: test fixture itself is invalid JSON: " << raw << "\n";
        ++failures;
    }
    return parsed;
}

void expectToolCall(const std::string& label, const json& assistant_message,
                     const std::string& expected_name) {
    auto calls = atlas::agent::Agent::extractToolCalls(assistant_message);
    bool ok = calls.is_array() && calls.size() == 1 && calls[0].contains("function") &&
              calls[0]["function"].value("name", std::string{}) == expected_name;
    std::cout << (ok ? "ok: " : "FAIL: ") << label << " -> " << calls.dump() << "\n";
    if (!ok) ++failures;
}

void expectNoToolCall(const std::string& label, const json& assistant_message) {
    auto calls = atlas::agent::Agent::extractToolCalls(assistant_message);
    bool ok = calls.is_array() && calls.empty();
    std::cout << (ok ? "ok: " : "FAIL: ") << label << " -> " << calls.dump() << "\n";
    if (!ok) ++failures;
}
} // namespace

int main() {
    std::cout << "== native tool_calls ==\n";
    expectToolCall("native format", parseMessage(R"({
        "role": "assistant",
        "content": "",
        "tool_calls": [
            {"function": {"name": "read_file", "arguments": {"path": "x.cpp"}}}
        ]
    })"), "read_file");

    std::cout << "\n== fenced ```json block ==\n";
    expectToolCall("fenced block", parseMessage(
        "{\"role\": \"assistant\", \"content\": \"Sure, let me check that.\\n"
        "```json\\n{\\\"name\\\": \\\"search_symbol\\\", \\\"arguments\\\": "
        "{\\\"query\\\": \\\"Agent\\\"}}\\n```\\n\"}"
    ), "search_symbol");

    std::cout << "\n== bare JSON object, no fence (the bug this PR fixes) ==\n";
    // Exactly what qwen2.5-coder:14b returned live: pretty-printed JSON,
    // no fence, no native tool_calls.
    expectToolCall("bare JSON, pretty-printed", parseMessage(
        "{\"role\": \"assistant\", \"content\": \"{\\n  \\\"name\\\": "
        "\\\"list_directory\\\",\\n  \\\"arguments\\\": {\\n    \\\"path\\\": "
        "\\\".\\\"\\n  }\\n}\"}"
    ), "list_directory");

    std::cout << "\n== bare JSON object, compact, with surrounding whitespace ==\n";
    expectToolCall("bare JSON, compact", parseMessage(
        "{\"role\": \"assistant\", \"content\": \"  {\\\"name\\\":\\\"read_file\\\","
        "\\\"arguments\\\":{\\\"path\\\":\\\"README.md\\\"}}  \\n\"}"
    ), "read_file");

    std::cout << "\n== plain-text final answer (must NOT be misread as a tool call) ==\n";
    expectNoToolCall("plain greeting", parseMessage(R"({
        "role": "assistant",
        "content": "Hello! How can I assist you today?"
    })"));

    std::cout << "\n== prose that merely contains braces (must NOT trigger) ==\n";
    expectNoToolCall("prose with braces", parseMessage(R"({
        "role": "assistant",
        "content": "In C++, a function body is wrapped in { and } braces."
    })"));

    std::cout << "\n" << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures))
              << "\n";
    return failures == 0 ? 0 : 1;
}
