// Manual smoke test for SessionManager's compaction-checkpoint storage
// (getSummary/setSummary) added alongside Agent's history compaction -
// verifies checkpoints round-trip through a real FileStorageManager (not
// just in memory), sit under their own storage key separate from the raw
// history, and get cleared by resetSession like the history itself does.
#include "atlas/core/SessionManager.hpp"
#include "atlas/storage/FileStorageManager.hpp"

#include <filesystem>
#include <iostream>
#include <string>

using atlas::core::SessionManager;
using atlas::core::SessionSummary;
using atlas::storage::FileStorageManager;
namespace fs = std::filesystem;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}
} // namespace

int main() {
    fs::path root = fs::temp_directory_path() / "atlas_session_summary_smoke";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // -----------------------------------------------------------------
    // No checkpoint yet: getSummary reports std::nullopt, not a default-
    // constructed (and misleadingly present) SessionSummary.
    // -----------------------------------------------------------------
    {
        FileStorageManager storage(root);
        SessionManager sessions(storage);
        auto summary = sessions.getSummary("session-a");
        expect(!summary.has_value(), "no checkpoint: getSummary returns nullopt");
    }

    // -----------------------------------------------------------------
    // Round-trips through disk: written by one SessionManager instance,
    // read back correctly by a fresh one pointed at the same storage.
    // -----------------------------------------------------------------
    {
        {
            FileStorageManager storage(root);
            SessionManager sessions(storage);
            SessionSummary checkpoint;
            checkpoint.covers_through_index = 40;
            checkpoint.summary = "User asked to bound session history; Agent::trimHistory added.";
            sessions.setSummary("session-b", checkpoint);
        }
        {
            FileStorageManager storage(root);
            SessionManager sessions(storage);
            auto summary = sessions.getSummary("session-b");
            expect(summary.has_value(), "round-trip: checkpoint persisted across instances");
            if (summary) {
                expect(summary->covers_through_index == 40, "round-trip: covers_through_index preserved");
                expect(summary->summary == "User asked to bound session history; Agent::trimHistory added.",
                       "round-trip: summary text preserved");
            }
        }
    }

    // -----------------------------------------------------------------
    // Separate from the raw history: setting a checkpoint doesn't alter
    // getHistory(), and appending messages doesn't alter the checkpoint.
    // -----------------------------------------------------------------
    {
        FileStorageManager storage(root);
        SessionManager sessions(storage);

        sessions.appendMessage("session-c", {{"role", "user"}, {"content", "hello"}});
        SessionSummary checkpoint;
        checkpoint.covers_through_index = 1;
        checkpoint.summary = "greeting exchanged";
        sessions.setSummary("session-c", checkpoint);
        sessions.appendMessage("session-c", {{"role", "assistant"}, {"content", "hi there"}});

        auto history = sessions.getHistory("session-c");
        auto summary = sessions.getSummary("session-c");
        expect(history.size() == 2, "independence: history keeps growing untouched by the checkpoint");
        expect(summary.has_value() && summary->covers_through_index == 1,
               "independence: checkpoint untouched by a later appendMessage");
    }

    // -----------------------------------------------------------------
    // resetSession clears both the history and the checkpoint.
    // -----------------------------------------------------------------
    {
        FileStorageManager storage(root);
        SessionManager sessions(storage);

        sessions.appendMessage("session-d", {{"role", "user"}, {"content", "hello"}});
        SessionSummary checkpoint;
        checkpoint.covers_through_index = 1;
        checkpoint.summary = "greeting exchanged";
        sessions.setSummary("session-d", checkpoint);

        sessions.resetSession("session-d");

        auto history = sessions.getHistory("session-d");
        auto summary = sessions.getSummary("session-d");
        expect(history.empty(), "resetSession: history cleared");
        expect(!summary.has_value(), "resetSession: checkpoint cleared too");
    }

    fs::remove_all(root, ec);

    std::cout << (failures == 0 ? "\nAll checks passed.\n" : "\nSome checks FAILED.\n");
    return failures == 0 ? 0 : 1;
}
