#include "atlas/core/SessionManager.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace atlas::core {

namespace {

constexpr std::size_t kMaxTitleBytes = 60;

// Collapses runs of whitespace to single spaces and shortens to roughly
// kMaxTitleBytes, never cutting through the middle of a UTF-8 sequence.
std::string makeTitle(const std::string& text) {
    std::string collapsed;
    bool pending_space = false;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pending_space = !collapsed.empty();
            continue;
        }
        if (pending_space) {
            collapsed.push_back(' ');
            pending_space = false;
        }
        collapsed.push_back(c);
    }
    if (collapsed.size() <= kMaxTitleBytes) {
        return collapsed;
    }
    std::size_t cut = kMaxTitleBytes;
    // Back up while `cut` points at a UTF-8 continuation byte (10xxxxxx).
    while (cut > 0 && (static_cast<unsigned char>(collapsed[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return collapsed.substr(0, cut) + "\xE2\x80\xA6"; // "…"
}

std::string toIso8601(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

} // namespace

SessionManager::SessionManager(storage::StorageManager& storage) : storage_(storage) {}

std::string SessionManager::storageKey(const std::string& session_id) const {
    return "sessions/" + session_id;
}

std::string SessionManager::summaryStorageKey(const std::string& session_id) const {
    return "sessions/" + session_id + "/summary";
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

std::optional<SessionSummary> SessionManager::getSummary(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    auto loaded = storage_.load(summaryStorageKey(session_id));
    if (!loaded.has_value() || !loaded->is_object()) {
        return std::nullopt;
    }
    SessionSummary summary;
    summary.covers_through_index = loaded->value("covers_through_index", std::size_t{0});
    summary.summary = loaded->value("summary", std::string{});
    return summary;
}

void SessionManager::setSummary(const std::string& session_id, const SessionSummary& summary) {
    std::lock_guard lock(mutex_);
    nlohmann::json doc = {
        {"covers_through_index", summary.covers_through_index},
        {"summary", summary.summary}
    };
    storage_.save(summaryStorageKey(session_id), doc);
}

std::vector<SessionInfo> SessionManager::listSessions() {
    std::lock_guard lock(mutex_);

    struct Entry {
        SessionInfo info;
        std::chrono::system_clock::time_point modified{};
    };
    std::vector<Entry> entries;

    const std::string prefix = "sessions/";
    for (const std::string& key : storage_.listKeys("sessions")) {
        if (key.rfind(prefix, 0) != 0) {
            continue;
        }
        auto doc = storage_.load(key);
        if (!doc.has_value() || !doc->is_array()) {
            continue;
        }

        Entry entry;
        entry.info.id = key.substr(prefix.size());
        for (const auto& message : *doc) {
            if (!message.is_object()) {
                continue;
            }
            const std::string role = message.value("role", std::string{});
            if (role != "user" && role != "assistant") {
                continue;
            }
            const auto content = message.find("content");
            if (content == message.end() || !content->is_string() ||
                content->get<std::string>().empty()) {
                continue;
            }
            ++entry.info.message_count;
            if (role == "user" && entry.info.title.empty()) {
                entry.info.title = makeTitle(content->get<std::string>());
            }
        }
        if (auto modified = storage_.lastModified(key); modified.has_value()) {
            entry.modified = *modified;
            entry.info.updated_at = toIso8601(*modified);
        }
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.modified != b.modified) {
            return a.modified > b.modified;
        }
        return a.info.id < b.info.id;
    });

    std::vector<SessionInfo> result;
    result.reserve(entries.size());
    for (auto& entry : entries) {
        result.push_back(std::move(entry.info));
    }
    return result;
}

void SessionManager::resetSession(const std::string& session_id) {
    std::lock_guard lock(mutex_);
    cache_[session_id] = nlohmann::json::array();
    storage_.remove(storageKey(session_id));
    storage_.remove(summaryStorageKey(session_id));
}

} // namespace atlas::core
