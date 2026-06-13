#pragma once

#include "atlas/storage/StorageManager.hpp"
#include <filesystem>
#include <string>

namespace atlas::storage {

class FileStorageManager : public StorageManager {
public:
    // Constructor requires a base directory where all workspaces will be stored
    explicit FileStorageManager(const std::string& base_directory);
    ~FileStorageManager() override = default;

    // Delete copy and move constructors to prevent accidental duplication of the storage manager
    FileStorageManager(const FileStorageManager&) = delete;
    FileStorageManager& operator=(const FileStorageManager&) = delete;
    FileStorageManager(FileStorageManager&&) = delete;
    FileStorageManager& operator=(FileStorageManager&&) = delete;

    // Interface Implementation
    bool initialize() override;
    bool saveWorkspace(const std::string& workspace_id, const nlohmann::json& data) override;
    std::optional<nlohmann::json> loadWorkspace(const std::string& workspace_id) override;
    std::vector<std::string> listWorkspaces() override;
    bool deleteWorkspace(const std::string& workspace_id) override;

private:
    // Helper function to construct the full file path for a given workspace ID
    std::filesystem::path getFilePath(const std::string& workspace_id) const;

    std::filesystem::path base_directory_;
};

} // namespace atlas::storage
