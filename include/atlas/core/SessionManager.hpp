#pragma once

#include "atlas/storage/StorageManager.hpp"
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace atlas::core {

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
    [[nodiscard]] nlohmann::json getHistory(const std::string& session_id);

    // Appends a single message to the session's history and persists it.
    void appendMessage(const std::string& session_id, const nlohmann::json& message);

    // Clears a session's history both in memory and on disk.
    void resetSession(const std::string& session_id);

private:
    [[nodiscard]] std::string storageKey(const std::string& session_id) const;

    storage::StorageManager& storage_;
    std::unordered_map<std::string, nlohmann::json> cache_;
    std::mutex mutex_;
};

} // namespace atlas::core
