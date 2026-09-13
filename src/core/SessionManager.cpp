#include "atlas/core/SessionManager.hpp"

namespace atlas::core {

SessionManager::SessionManager(storage::StorageManager& storage) : storage_(storage) {}

std::string SessionManager::storageKey(const std::string& session_id) const {
    return "sessions/" + session_id;
}

nlohmann::json SessionManager::getHistory(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    if (auto it = cache_.find(session_id); it != cache_.end()) {
        return it->second;
    }

    if (auto loaded = storage_.load(storageKey(session_id)); loaded.has_value()) {
        cache_[session_id] = *loaded;
        return *loaded;
    }

    nlohmann::json empty_history = nlohmann::json::array();
    cache_[session_id] = empty_history;
    return empty_history;
}

void SessionManager::appendMessage(const std::string& session_id, const nlohmann::json& message) {
    std::lock_guard lock(mutex_);
    auto& history = cache_[session_id];
    if (!history.is_array()) {
        history = nlohmann::json::array();
    }
    history.push_back(message);
    storage_.save(storageKey(session_id), history);
}

void SessionManager::resetSession(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    cache_[session_id] = nlohmann::json::array();
    storage_.remove(storageKey(session_id));
}

} // namespace atlas::core
