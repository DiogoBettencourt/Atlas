#pragma once

#include "atlas/storage/StorageManager.hpp"
#include <cstddef>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace atlas::core {

// A compaction checkpoint: an LLM-generated summary standing in for every
// message in a session's full history up to (but not including) index
// `covers_through_index`. Persisted under its own storage key, separate
// from the append-only history itself, so it can be recomputed and
// replaced as compaction runs again without ever touching the raw log
// that getHistory()/appendMessage() maintain - the complete, uncompacted
// history stays on disk exactly as before. This is purely an index into
// it that a caller (Agent) can use to shrink what actually goes into a
// prompt, while nothing is ever lost from storage.
struct SessionSummary {
    std::size_t covers_through_index = 0;
    std::string summary;
};

// Owns per-session chat history (role/content/tool-call turns) and
// persists it through a StorageManager so conversations survive restarts.
// Sessions are cached in memory and lazily loaded from storage on first
// access.
class SessionManager {
public:
    explicit SessionManager(storage::StorageManager& storage);

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;

    // Returns the full message history for `session_id` (creating an empty
    // one if it doesn't exist yet), in Ollama/OpenAI chat message format:
    // [{"role": "user"|"assistant"|"system"|"tool", "content": "...", ...}]
    //
    // This is always the COMPLETE history - nothing is ever trimmed or
    // summarized away here. Compaction (see SessionSummary above) only
    // affects what a caller chooses to send to the LLM; it never touches
    // what's returned by this method or what's on disk.
    [[nodiscard]] nlohmann::json getHistory(const std::string& session_id);

    // Appends a single message to the session's history and persists it.
    void appendMessage(const std::string& session_id, const nlohmann::json& message);

    // Returns the session's current compaction checkpoint, or std::nullopt
    // if none has been computed yet (e.g. the session is still short
    // enough that no compaction has ever been needed).
    [[nodiscard]] std::optional<SessionSummary> getSummary(const std::string& session_id);

    // Persists a new compaction checkpoint for `session_id`, replacing any
    // previous one. Does not touch the session's raw history.
    void setSummary(const std::string& session_id, const SessionSummary& summary);

    // Clears a session's history and its compaction checkpoint, both in
    // memory and on disk.
    void resetSession(const std::string& session_id);

private:
    [[nodiscard]] std::string storageKey(const std::string& session_id) const;
    [[nodiscard]] std::string summaryStorageKey(const std::string& session_id) const;

    storage::StorageManager& storage_;
    std::unordered_map<std::string, nlohmann::json> cache_;
    std::mutex mutex_;
};

} // namespace atlas::core
