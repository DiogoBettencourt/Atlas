#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace atlas::storage {

// Abstract persistence interface. Atlas stores everything as
// human-readable, git-diffable JSON documents identified by a namespaced
// key (e.g. "sessions/abc123", "workspaces/my-project"). Alternative
// backends (SQLite, embedded KV store) can implement this same interface
// without touching call sites in core/.
class StorageManager {
public:
    virtual ~StorageManager() = default;

    virtual void save(const std::string& key, const nlohmann::json& document) = 0;

    [[nodiscard]] virtual std::optional<nlohmann::json> load(const std::string& key) const = 0;

    [[nodiscard]] virtual bool exists(const std::string& key) const = 0;

    virtual bool remove(const std::string& key) = 0;

    // Lists all keys currently stored under `prefix` (e.g. "sessions/").
    [[nodiscard]] virtual std::vector<std::string> listKeys(const std::string& prefix) const = 0;
};

} // namespace atlas::storage
