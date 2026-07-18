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

} // namespace atlas::core