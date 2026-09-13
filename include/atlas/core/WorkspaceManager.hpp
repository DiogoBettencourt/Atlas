#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace atlas::core {

// Context-aware router that maps logical workspace names to physical
// directories on disk, and enforces path-traversal-safe resolution of any
// relative path an agent (or tool) requests within a workspace.
class WorkspaceManager {
public:
    explicit WorkspaceManager(std::filesystem::path workspaces_root);

    WorkspaceManager(const WorkspaceManager&) = delete;
    WorkspaceManager& operator=(const WorkspaceManager&) = delete;
    WorkspaceManager(WorkspaceManager&&) = delete;
    WorkspaceManager& operator=(WorkspaceManager&&) = delete;

    // Registers (and creates on disk if needed) a workspace with the given
    // logical name, rooted at `physical_path`. If `physical_path` is empty,
    // defaults to <workspaces_root>/<name>.
    std::filesystem::path createOrGetWorkspace(const std::string& name,
                                                const std::filesystem::path& physical_path = {});

    // Returns the sandbox root for a previously created workspace, or
    // std::nullopt if unknown.
    [[nodiscard]] std::optional<std::filesystem::path> rootFor(const std::string& name) const;

    // Resolves `relative_path` against `workspace_root`, guaranteeing the
    // result stays within the sandbox. Throws std::runtime_error on any
    // attempt to escape the sandbox (e.g. via "../../" traversal or an
    // absolute path pointing elsewhere).
    [[nodiscard]] static std::filesystem::path resolveSafe(
        const std::filesystem::path& workspace_root,
        const std::string& relative_path);

    [[nodiscard]] const std::filesystem::path& workspacesRoot() const { return workspaces_root_; }

private:
    std::filesystem::path workspaces_root_;
    std::unordered_map<std::string, std::filesystem::path> workspaces_;
};

} // namespace atlas::core
