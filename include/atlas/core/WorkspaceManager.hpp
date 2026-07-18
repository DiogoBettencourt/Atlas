#pragma once

#include <filesystem>
#include <string>
#include <map>
#include <optional>

namespace atlas::core {

class WorkspaceManager {
public:
    WorkspaceManager() = default;

    // Register a new workspace by a friendly name (e.g., "Atlas", "MyApp")
    bool addWorkspace(const std::string& name, const std::filesystem::path& root_path);

    // Remove a workspace from tracking
    bool removeWorkspace(const std::string& name);

    // Set the "active" context for the agent
    bool setActiveWorkspace(const std::string& name);

    // Get the path of the currently active workspace
    std::optional<std::filesystem::path> getActiveWorkspacePath() const;

    // List all registered workspaces (useful for the API)
    std::map<std::string, std::filesystem::path> getAllWorkspaces() const;

private:
    std::map<std::string, std::filesystem::path> workspaces_;
    std::string active_workspace_;
};

} // namespace atlas::core