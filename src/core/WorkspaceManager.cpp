#include "atlas/core/WorkspaceManager.hpp"

#include <stdexcept>

namespace atlas::core {

namespace fs = std::filesystem;

WorkspaceManager::WorkspaceManager(fs::path workspaces_root)
    : workspaces_root_(std::move(workspaces_root)) {
    std::error_code ec;
    fs::create_directories(workspaces_root_, ec);
}

fs::path WorkspaceManager::createOrGetWorkspace(const std::string& name,
                                                 const fs::path& physical_path) {
    if (auto existing = workspaces_.find(name); existing != workspaces_.end()) {
        return existing->second;
    }

    fs::path root = physical_path.empty() ? (workspaces_root_ / name) : physical_path;
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        throw std::runtime_error("WorkspaceManager: failed to create workspace '" + name +
                                  "': " + ec.message());
    }

    fs::path canonical = fs::weakly_canonical(root);
    workspaces_.emplace(name, canonical);
    return canonical;
}

std::optional<fs::path> WorkspaceManager::rootFor(const std::string& name) const {
    if (auto it = workspaces_.find(name); it != workspaces_.end()) {
        return it->second;
    }
    return std::nullopt;
}

fs::path WorkspaceManager::resolveSafe(const fs::path& workspace_root,
                                        const std::string& relative_path) {
    // Disallow absolute inputs outright; every tool-facing path must be
    // workspace-relative.
    fs::path requested(relative_path);
    if (requested.is_absolute()) {
        throw std::runtime_error("path traversal rejected: absolute paths are not allowed");
    }

    fs::path combined = workspace_root / requested;
    fs::path normalized = fs::weakly_canonical(combined);
    fs::path canonical_root = fs::weakly_canonical(workspace_root);

    // Ensure normalized path is inside canonical_root (string-prefix check
    // on the canonical, separator-normalized paths).
    auto root_str = canonical_root.native();
    auto norm_str = normalized.native();

    // A naive prefix check would treat a sibling directory whose name
    // merely starts with the same characters as the workspace root (e.g.
    // root ".../workspaces/default" vs ".../workspaces/default-evil") as
    // "inside" it. Require an exact match or that the next character is a
    // path separator, so containment is checked on whole path components.
    bool within = norm_str.size() >= root_str.size() &&
                  std::equal(root_str.begin(), root_str.end(), norm_str.begin()) &&
                  (norm_str.size() == root_str.size() ||
                   norm_str[root_str.size()] == fs::path::preferred_separator);

    if (!within) {
        throw std::runtime_error("path traversal rejected: '" + relative_path +
                                  "' escapes workspace sandbox");
    }

    return normalized;
}

} // namespace atlas::core
