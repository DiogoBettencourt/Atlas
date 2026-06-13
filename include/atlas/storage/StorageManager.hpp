#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace atlas::storage {

/**
 * @brief Abstract interface for Atlas storage mechanisms.
 * * This ensures the core application remains entirely decoupled from
 * the actual persistence layer (e.g., local files, SQLite, or cloud).
 */
class StorageManager {
public:
    // Virtual destructor is mandatory for abstract base classes
    virtual ~StorageManager() = default;

    // Initializes the storage (e.g., creating necessary directories or database tables)
    virtual bool initialize() = 0;

    // Saves workspace data. Returns true on success.
    virtual bool saveWorkspace(const std::string& workspace_id, const nlohmann::json& data) = 0;

    // Loads workspace data. Returns std::nullopt if the workspace doesn't exist.
    virtual std::optional<nlohmann::json> loadWorkspace(const std::string& workspace_id) = 0;

    // Returns a list of all available workspace IDs
    virtual std::vector<std::string> listWorkspaces() = 0;

    // Deletes a workspace and all its data. Returns true on success.
    virtual bool deleteWorkspace(const std::string& workspace_id) = 0;
};

} // namespace atlas::storage
