// Manual smoke test for Agent::extractToolCalls - the ReAct loop's logic
// for pulling a tool call out of whatever the model actually returned.
// Every fixture below is either a minimal native-shape message or text
// captured verbatim from a real qwen2.5-coder:14b response during a live
// self-improvement run, not a synthetic best case.
#include "atlas/agent/Agent.hpp"

#include <iostream>
#include <string>

using json = nlohmann::json;
using atlas::agent::Agent;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}
} // namespace

int main() {
    // -----------------------------------------------------------------
    // Native tool_calls: passed straight through unchanged.
    // -----------------------------------------------------------------
    {
        json msg = json::parse(R"({
            "content": "",
            "tool_calls": [
                {"function": {"name": "read_file", "arguments": {"path": "a.txt"}}}
            ]
        })");
        auto calls = Agent::extractToolCalls(msg);
        expect(calls.size() == 1, "native tool_calls: one call returned");
        expect(calls[0]["function"]["name"] == "read_file", "native tool_calls: name preserved");
    }

    // -----------------------------------------------------------------
    // Fenced ```json block inside content (pre-existing fallback).
    // -----------------------------------------------------------------
    {
        json msg = json::parse(R"({
            "content": "Sure, here goes:\n```json\n{\"name\": \"list_directory\", \"arguments\": {\"path\": \".\"}}\n```"
        })");
        auto calls = Agent::extractToolCalls(msg);
        expect(calls.size() == 1, "fenced block: one call returned");
        expect(calls[0]["function"]["name"] == "list_directory", "fenced block: name preserved");
    }

    // -----------------------------------------------------------------
    // Bare single JSON object, no fencing at all.
    // -----------------------------------------------------------------
    {
        json msg = json::object();
        msg["content"] =
            "{\n \"name\": \"list_directory\",\n \"arguments\": {\n  \"path\": \".\"\n }\n}";
        auto calls = Agent::extractToolCalls(msg);
        expect(calls.size() == 1, "bare object: one call returned");
        expect(calls[0]["function"]["name"] == "list_directory", "bare object: name preserved");
        expect(calls[0]["function"]["arguments"]["path"] == ".", "bare object: arguments preserved");
    }

    // -----------------------------------------------------------------
    // The actual live bug: several bare JSON objects concatenated
    // back-to-back with no fencing or separators, representing a whole
    // multi-step plan the model front-loaded into one response instead of
    // waiting for each tool result. Only the first must be executed.
    // -----------------------------------------------------------------
    {
        json msg = json::object();
        msg["content"] =
            "{\n  \"name\": \"edit_file\",\n  \"arguments\": {\n    \"path\": \"README.md\",\n"
            "    \"old_text\": \"# Atlas\",\n    \"new_text\": \"# Atlas\\n\\n**note**\"\n  }\n}\n\n"
            "{\n  \"name\": \"git\",\n  \"arguments\": {\n    \"action\": \"add\",\n"
            "    \"paths\": [\"README.md\"]\n  }\n}\n\n"
            "{\n  \"name\": \"git\",\n  \"arguments\": {\n    \"action\": \"commit\",\n"
            "    \"message\": \"Add note\"\n  }\n}\n\n"
            "{\n  \"name\": \"github_pr\",\n  \"arguments\": {\n    \"title\": \"x\",\n"
            "    \"head\": \"h\",\n    \"base\": \"dev\"\n  }\n}";
        auto calls = Agent::extractToolCalls(msg);
        expect(calls.size() == 1, "concatenated objects: exactly one call returned, not four");
        expect(calls[0]["function"]["name"] == "edit_file",
               "concatenated objects: the FIRST call (edit_file) is the one taken");
        expect(calls[0]["function"]["arguments"]["path"] == "README.md",
               "concatenated objects: first call's arguments preserved");
    }

    // -----------------------------------------------------------------
    // Plain-text final answer: must not be misread as a tool call.
    // -----------------------------------------------------------------
    {
        json msg = json::object();
        msg["content"] = "The CMakeLists.txt configures three FetchContent dependencies.";
        auto calls = Agent::extractToolCalls(msg);
        expect(calls.empty(), "plain text: no tool call extracted");
    }

    // -----------------------------------------------------------------
    // Prose that merely mentions braces (e.g. describing a C++ function
    // body) must not be mistaken for a tool call object.
    // -----------------------------------------------------------------
    {
        json msg = json::object();
        msg["content"] = "A minimal function looks like `void f() { return; }` - no tool needed here.";
        auto calls = Agent::extractToolCalls(msg);
        expect(calls.empty(), "prose with braces but no \"name\" key: no tool call extracted");
    }

    // -----------------------------------------------------------------
    // Unbalanced / truncated JSON must not crash or hang.
    // -----------------------------------------------------------------
    {
        json msg = json::object();
        msg["content"] = "{\"name\": \"read_file\", \"arguments\": {\"path\": \"a.txt\"";
        auto calls = Agent::extractToolCalls(msg);
        expect(calls.empty(), "truncated object: no tool call extracted, no crash");
    }

    std::cout << (failures == 0 ? "\nAll checks passed.\n" : "\nSome checks FAILED.\n");
    return failures == 0 ? 0 : 1;
}
