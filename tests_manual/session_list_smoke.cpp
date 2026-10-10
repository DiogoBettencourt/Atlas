// Manual smoke test for SessionManager::listSessions (backs GET
// /sessions) and the FileStorageManager::lastModified it relies on.
#include "atlas/core/SessionManager.hpp"
#include "atlas/storage/FileStorageManager.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

using atlas::core::SessionManager;
using atlas::core::SessionSummary;
using atlas::storage::FileStorageManager;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}
} // namespace

int main() {
    fs::path root = fs::temp_directory_path() / "atlas_session_list_smoke";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    FileStorageManager storage(root);
    SessionManager sessions(storage);

    // No sessions directory at all yet.
    expect(sessions.listSessions().empty(), "empty store lists no sessions");

    // lastModified: missing key -> nullopt; written key -> roughly now.
    expect(!storage.lastModified("sessions/nope").has_value(), "lastModified of a missing key is nullopt");

    sessions.appendMessage("old", {{"role", "user"}, {"content", "first question"}});
    sessions.appendMessage("old", {{"role", "assistant"}, {"content", "first answer"}});

    auto modified = storage.lastModified("sessions/old");
    expect(modified.has_value(), "lastModified of a written key is set");
    if (modified.has_value()) {
        auto age = std::chrono::system_clock::now() - *modified;
        expect(age < std::chrono::seconds(30) && age > -std::chrono::seconds(30),
               "lastModified is close to the current time");
    }

    // Make sure the next write has a strictly later mtime on coarse filesystems.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    sessions.appendMessage("new", {{"role", "user"}, {"content", "  how   do\nI   build\tthis?  "}});
    sessions.appendMessage("new", {{"role", "assistant"}, {"content", ""}, {"tool_calls", json::array({{{"function", {{"name", "read_file"}}}}})}});
    sessions.appendMessage("new", {{"role", "tool"}, {"name", "read_file"}, {"content", "file text"}});
    sessions.appendMessage("new", {{"role", "assistant"}, {"content", "Run cmake."}});
    // A compaction checkpoint lives in a sibling directory and must not
    // show up as a session of its own.
    sessions.setSummary("new", SessionSummary{2, "summary text"});

    auto list = sessions.listSessions();
    expect(list.size() == 2, "two sessions listed (summary checkpoint is not one)");
    if (list.size() == 2) {
        expect(list[0].id == "new" && list[1].id == "old", "most recently updated session first");
        expect(list[0].title == "how do I build this?", "title collapses whitespace");
        expect(list[0].message_count == 2, "message_count skips tool results and empty tool-call turns");
        expect(list[1].title == "first question", "title is the first user message");
        expect(list[1].message_count == 2, "plain user/assistant exchange counts both turns");
        expect(list[0].updated_at.size() == 20 && list[0].updated_at.back() == 'Z' &&
                   list[0].updated_at[10] == 'T',
               "updated_at is ISO 8601 UTC");
    }

    // Long titles are shortened, and never in the middle of a UTF-8 character.
    std::string long_text;
    for (int i = 0; i < 40; ++i) long_text += "\xC3\xA9"; // 40 x 'é' = 80 bytes
    sessions.appendMessage("long", {{"role", "user"}, {"content", long_text}});
    std::string long_title;
    for (const auto& s : sessions.listSessions()) {
        if (s.id == "long") long_title = s.title;
    }
    expect(!long_title.empty() && long_title.size() < long_text.size(), "long title is shortened");
    bool valid_utf8 = true;
    for (std::size_t i = 0; i + 1 < long_title.size(); ) {
        unsigned char c = static_cast<unsigned char>(long_title[i]);
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > long_title.size()) { valid_utf8 = false; break; }
        for (std::size_t j = 1; j < len; ++j) {
            if ((static_cast<unsigned char>(long_title[i + j]) & 0xC0) != 0x80) valid_utf8 = false;
        }
        i += len;
    }
    expect(valid_utf8, "shortened title is still valid UTF-8");

    // A session with no user text gets an empty title but is still listed.
    sessions.appendMessage("quiet", {{"role", "assistant"}, {"content", "hello"}});
    bool found_quiet = false;
    for (const auto& s : sessions.listSessions()) {
        if (s.id == "quiet") { found_quiet = s.title.empty() && s.message_count == 1; }
    }
    expect(found_quiet, "session without a user message has an empty title");

    // Deleting removes it from the list.
    sessions.resetSession("old");
    bool old_gone = true;
    for (const auto& s : sessions.listSessions()) {
        if (s.id == "old") old_gone = false;
    }
    expect(old_gone, "resetSession removes the session from the list");

    fs::remove_all(root, ec);
    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return failures == 0 ? 0 : 1;
}
