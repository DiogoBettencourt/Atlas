// Manual smoke test for Agent::buildCompactionRequest - the prompt Agent
// sends the LLM to (re)compute a session's compaction summary when its
// history's tail grows past the configured bound. Purely checks prompt
// shape/content, since actually exercising compaction end-to-end needs a
// live LLMClient (see chat()'s implementation in Agent.cpp instead).
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

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}
} // namespace

int main() {
    // -----------------------------------------------------------------
    // Shape: always exactly one plain "user" message, ready to hand
    // straight to LLMClient::chat with no tools.
    // -----------------------------------------------------------------
    {
        json to_summarize = json::array({
            {{"role", "user"}, {"content", "please refactor GitTool"}},
            {{"role", "assistant"}, {"content", "done, see src/tools/GitTool.cpp"}}
        });
        json request = Agent::buildCompactionRequest("", to_summarize);
        expect(request.is_array() && request.size() == 1, "shape: single-message request");
        expect(request[0]["role"] == "user", "shape: request message role is user");
        expect(request[0]["content"].is_string(), "shape: request content is a string");
    }

    // -----------------------------------------------------------------
    // No previous summary: doesn't fabricate an "existing summary"
    // section that isn't there.
    // -----------------------------------------------------------------
    {
        json to_summarize = json::array({
            {{"role", "user"}, {"content", "fix the crash in list_directory_and_pull_smoke"}}
        });
        json request = Agent::buildCompactionRequest("", to_summarize);
        std::string content = request[0]["content"].get<std::string>();
        expect(!contains(content, "Existing summary"),
               "no previous summary: omits the existing-summary section entirely");
        expect(contains(content, "fix the crash in list_directory_and_pull_smoke"),
               "no previous summary: includes the message content to summarize");
    }

    // -----------------------------------------------------------------
    // With a previous summary: threads it in so repeated compactions
    // accumulate instead of each one starting from scratch.
    // -----------------------------------------------------------------
    {
        json to_summarize = json::array({
            {{"role", "assistant"}, {"content", "opened PR #11 for the history bound fix"}}
        });
        json request = Agent::buildCompactionRequest("User is building AtlasUI and AtlasCLI.",
                                                       to_summarize);
        std::string content = request[0]["content"].get<std::string>();
        expect(contains(content, "Existing summary"),
               "with previous summary: includes the existing-summary section");
        expect(contains(content, "User is building AtlasUI and AtlasCLI."),
               "with previous summary: previous summary text is threaded in verbatim");
        expect(contains(content, "opened PR #11 for the history bound fix"),
               "with previous summary: also includes the new messages to summarize");
    }

    // -----------------------------------------------------------------
    // Each summarized message is tagged with its role so the model can
    // tell who said what.
    // -----------------------------------------------------------------
    {
        json to_summarize = json::array({
            {{"role", "user"}, {"content", "what does GitTool do"}},
            {{"role", "assistant"}, {"content", "it wraps git CLI operations"}}
        });
        json request = Agent::buildCompactionRequest("", to_summarize);
        std::string content = request[0]["content"].get<std::string>();
        expect(contains(content, "[user] what does GitTool do"),
               "role tagging: user turn tagged and preserved");
        expect(contains(content, "[assistant] it wraps git CLI operations"),
               "role tagging: assistant turn tagged and preserved");
    }

    // -----------------------------------------------------------------
    // Empty input: doesn't crash, still returns a well-formed request.
    // -----------------------------------------------------------------
    {
        json request = Agent::buildCompactionRequest("", json::array());
        expect(request.is_array() && request.size() == 1, "empty input: still a well-formed request");
    }

    std::cout << (failures == 0 ? "\nAll checks passed.\n" : "\nSome checks FAILED.\n");
    return failures == 0 ? 0 : 1;
}
