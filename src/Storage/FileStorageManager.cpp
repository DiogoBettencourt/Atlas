#include "atlas/storage/FileStorageManager.hpp"
#include <fstream>
#include <iostream>

namespace atlas::storage {

FileStorageManager::FileStorageManager(const std::string& base_directory)
    : base_directory_(base_directory) {
}

bool FileStorageManager::initialize() {
    try {
        // If the directory doesn't exist, create it (and any missing parent directories)
        if (!std::filesystem::exists(base_directory_)) {
            std::filesystem::create_directories(base_directory_);
        }
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Storage Error: Failed to initialize directory: " << e.what() << '\n';
        return false;
    }
}

bool FileStorageManager::saveWorkspace(const std::string& workspace_id, const nlohmann::json& data) {
    auto filepath = getFilePath(workspace_id);

    // Open file stream for writing
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Storage Error: Could not open file for writing: " << filepath << '\n';
        return false;
    }

    // Dump JSON data with an indentation of 4 spaces for human-readability
    file << data.dump(4);
    return true;
}

std::optional<nlohmann::json> FileStorageManager::loadWorkspace(const std::string& workspace_id) {
    auto filepath = getFilePath(workspace_id);

    if (!std::filesystem::exists(filepath)) {
        return std::nullopt; // Workspace file doesn't exist
    }

    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Storage Error: Could not open file for reading: " << filepath << '\n';
        return std::nullopt;
    }

    try {
        nlohmann::json data;
        file >> data;
        return data;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Storage Error: JSON parsing failed for " << filepath << ": " << e.what() << '\n';
        return std::nullopt;
    }
}

std::vector<std::string> FileStorageManager::listWorkspaces() {
    std::vector<std::string> workspaces;

    if (!std::filesystem::exists(base_directory_)) {
        return workspaces;
    }

    // Iterate through all files in the base directory
    for (const auto& entry : std::filesystem::directory_iterator(base_directory_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            // Extract the filename without the .json extension
            workspaces.push_back(entry.path().stem().string());
        }
    }

    return workspaces;
}

bool FileStorageManager::deleteWorkspace(const std::string& workspace_id) {
    auto filepath = getFilePath(workspace_id);
    try {
        return std::filesystem::remove(filepath);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Storage Error: Failed to delete " << filepath << ": " << e.what() << '\n';
        return false;
    }
}

std::filesystem::path FileStorageManager::getFilePath(const std::string& workspace_id) const {
    // Safely appends the filename to the directory path: e.g., "base_dir/workspace_123.json"
    return base_directory_ / (workspace_id + ".json");
}

} // namespace atlas::storage
