#pragma once

#include "atlas/tools/Tool.hpp"
#include <filesystem>

namespace atlas::tools {

// Opens a pull request via the GitHub REST API. Like GitTool, this only
// operates inside the dedicated self-improvement workspace (checked
// against `allowed_repo_root`) and against the single `github_repo`
// ("owner/name") it was configured with at startup - it cannot be
// redirected to open PRs against an arbitrary repository from chat input.
//
// Requires the ATLAS_GITHUB_TOKEN environment variable to hold a GitHub
// personal access token (fine-grained, scoped to "Pull requests: write" on
// just this repo is strongly recommended). The tool never merges, closes,
// or force-pushes anything - it only ever creates a new PR for a
// human to review.
class GitHubPRTool final : public Tool {
public:
    GitHubPRTool(std::filesystem::path allowed_repo_root, std::string github_repo);

    [[nodiscard]] std::string name() const override { return "github_pr"; }

    [[nodiscard]] std::string description() const override {
        return "Opens a GitHub pull request from a feature branch (already "
               "pushed via the git tool) into the base branch, for human "
               "review. Never merges anything automatically.";
    }

    [[nodiscard]] nlohmann::json parametersSchema() const override;

    [[nodiscard]] nlohmann::json execute(const nlohmann::json& arguments,
                                          const std::string& workspace_root) const override;

private:
    [[nodiscard]] bool isAllowedRepo(const std::string& workspace_root) const;

    std::filesystem::path allowed_repo_root_;
    std::string github_repo_; // "owner/name"
};

} // namespace atlas::tools
