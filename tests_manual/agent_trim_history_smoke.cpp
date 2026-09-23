// Manual smoke test for Agent::trimHistory - the bound on how much of a
// session's history actually gets sent to the LLM each turn, added to fix
// unbounded prompt growth in long-running sessions (SessionManager still
// keeps the full history regardless; this only bounds the prompt).
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

// Builds a history of `turns` user/assistant pairs (2 messages each), so
// callers can construct histories of a known, predictable shape.
json makeTurns(int turns) {
    json history = json::array();
    for (int i = 0; i < turns; ++i) {
        history.push_back({{"role", "user"}, {"content", "message " + std::to_string(i)}});
        history.push_back({{"role", "assistant"}, {"content", "reply " + std::to_string(i)}});
    }
    return history;
}
} // namespace

int main() {
    // -----------------------------------------------------------------
    // Under the cap: returned unchanged, not just equal-looking.
    // -----------------------------------------------------------------
    {
        json history = makeTurns(3); // 6 messages
        auto trimmed = Agent::trimHistory(history, 40);
        expect(trimmed == history, "under cap: history returned unchanged");
    }

    // -----------------------------------------------------------------
    // Exactly at the cap: unchanged.
    // -----------------------------------------------------------------
    {
        json history = makeTurns(20); // 40 messages
        auto trimmed = Agent::trimHistory(history, 40);
        expect(trimmed == history, "exactly at cap: history returned unchanged");
    }

    // -----------------------------------------------------------------
    // Over the cap, cap lands exactly on a user-message boundary: trims
    // to precisely max_messages and keeps whole turns intact.
    // -----------------------------------------------------------------
    {
        json history = makeTurns(30); // 60 messages, turns are 2 msgs each
        auto trimmed = Agent::trimHistory(history, 40);
        expect(trimmed.size() == 40, "clean boundary: trims to exactly max_messages");
        expect(trimmed.front()["role"] == "user", "clean boundary: trimmed window starts on a user message");
        expect(trimmed.front()["content"] == "message 10",
               "clean boundary: trimmed window starts at the correct turn");
        expect(trimmed.back() == history.back(), "clean boundary: most recent message preserved exactly");
    }

    // -----------------------------------------------------------------
    // Over the cap, hard cut would land mid-turn (on an "assistant" or
    // "tool" message): walks forward to the next user message instead of
    // starting with an orphaned non-user message.
    // -----------------------------------------------------------------
    {
        json history = makeTurns(30); // 60 messages
        // 41 lands the naive cut one message into a turn (on an
        // "assistant" message, since turns are user-then-assistant) -
        // trimHistory must walk forward past it to the next "user".
        auto trimmed = Agent::trimHistory(history, 41);
        expect(trimmed.size() < 41, "mid-turn cut: walks past the boundary, ending up under the cap");
        expect(trimmed.front()["role"] == "user", "mid-turn cut: trimmed window still starts on a user message");
    }

    // -----------------------------------------------------------------
    // No "user" message anywhere in the window at all: falls back to a
    // hard cut at exactly max_messages rather than sending nothing.
    // -----------------------------------------------------------------
    {
        json history = json::array();
        for (int i = 0; i < 50; ++i) {
            history.push_back({{"role", "tool"}, {"content", "result " + std::to_string(i)}});
        }
        auto trimmed = Agent::trimHistory(history, 40);
        expect(trimmed.size() == 40, "no user boundary: falls back to a hard cut at max_messages");
    }

    // -----------------------------------------------------------------
    // Empty history: returned unchanged (empty), no crash.
    // -----------------------------------------------------------------
    {
        json history = json::array();
        auto trimmed = Agent::trimHistory(history, 40);
        expect(trimmed.empty(), "empty history: stays empty, no crash");
    }

    std::cout << (failures == 0 ? "\nAll checks passed.\n" : "\nSome checks FAILED.\n");
    return failures == 0 ? 0 : 1;
}
