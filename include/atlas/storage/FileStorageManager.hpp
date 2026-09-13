#pragma once

#include "atlas/storage/StorageManager.hpp"
#include <filesystem>
#include <mutex>

namespace atlas::storage {

// Filesystem-backed implementation of StorageManager. Each key maps to a
// single ".json" file rooted at `data_root`, with '/' in keys mapped to
// nested directories (e.g. key "sessions/abc" -> <root>/sessions/abc.json).
// A mutex serializes access since Atlas may be driven by multiple httplib
// worker threads concurrently.
class FileStorageManager final : public StorageManager {
public:
    explicit FileStorageManager(std::filesystem::path data_root);

    FileStorageManager(const FileStorageManager&) = delete;
    FileStorageManager& operator=(const FileStorageManager&) = delete;
    FileStorageManager(FileStorageManager&&) = delete;
    FileStorageManager& operator=(FileStorageManager&&) = delete;

    void save(const std::string& key, const nlohmann::json& document) override;
    [[nodiscard]] std::optional<nlohmann::json> load(const std::string& key) const override;
    [[nodiscard]] bool exists(const std::string& key) const override;
    bool remove(const std::string& key) override;
    [[nodiscard]] std::vector<std::string> listKeys(const std::string& prefix) const override;

private:
    [[nodiscard]] std::filesystem::path pathFor(const std::string& key) const;

    std::filesystem::path data_root_;
    mutable std::mutex mutex_;
};

} // namespace atlas::storage
