#include "atlas/core/WorkspaceManager.hpp"
#include <iostream>

namespace atlas::core {

bool WorkspaceManager::addWorkspace(const std::string& name, const std::filesystem::path& root_path) {
    // Only add if the directory actually exists
    if (std::filesystem::exists(root_path) && std::filesystem::is_directory(root_path)) {
        workspaces_[name] = std::filesystem::weakly_canonical(root_path);
        
        // If this is the first workspace added, make it active by default
        if (active_workspace_.empty()) {
            active_workspace_ = name;
        }
        return true;
    }
    return false;
}

bool WorkspaceManager::removeWorkspace(const std::string& name) {
    auto it = workspaces_.find(name);
    if (it != workspaces_.end()) {
        workspaces_.erase(it);
        // If we deleted the active workspace, fallback to another one or empty
        if (active_workspace_ == name) {
            active_workspace_ = workspaces_.empty() ? "" : workspaces_.begin()->first;
        }
        return true;
    }
    return false;
}

bool WorkspaceManager::setActiveWorkspace(const std::string& name) {
    if (workspaces_.find(name) != workspaces_.end()) {
        active_workspace_ = name;
        return true;
    }
    return false;
}

std::optional<std::filesystem::path> WorkspaceManager::getActiveWorkspacePath() const {
    if (active_workspace_.empty()) return std::nullopt;
    
    auto it = workspaces_.find(active_workspace_);
    if (it != workspaces_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::map<std::string, std::filesystem::path> WorkspaceManager::getAllWorkspaces() const {
    return workspaces_;
}

std::filesystem::path WorkspaceManager::resolveSafePath(const std::string& workspace_name, const std::string& relative_path) const {
    auto it = workspaces_.find(workspace_name);
    if (it == workspaces_.end()) {
        throw std::runtime_error("Workspace not found: " + workspace_name);
    }

    std::filesystem::path root_dir = std::filesystem::canonical(it->second);
    std::filesystem::path target_path = std::filesystem::weakly_canonical(root_dir / relative_path);

    // Sandbox check: Ensure the resolved target path starts with the workspace root path
    if (target_path.string().find(root_dir.string()) != 0) {
        throw std::runtime_error("Security Error: Path traversal attempt outside workspace boundaries.");
    }

    return target_path;
}

} // namespace atlas::core