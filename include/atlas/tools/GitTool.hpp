#pragma once

#include "atlas/tools/Tool.hpp"
#include <filesystem>

namespace atlas::tools {

// Exposes a narrow, safety-checked subset of git to the agent so it can
// work on its OWN source tree: status, diff, add, commit, checkout_branch,
// push. This tool is deliberately not general-purpose:
//
//   - It only operates when the request's workspace_root canonically
//     matches the single `allowed_repo_root` it was constructed with (set
//     from --self-repo at startup). Any other workspace gets a hard
//     refusal, so a normal user workspace can never trigger a git push.
//   - checkout_branch refuses "main"/"master"/"HEAD" as a target name.
//   - push refuses to push while HEAD is on "main" or "master".
//   - Every git invocation runs via fork()+execvp() on POSIX or
//     CreateProcess() on Windows with an explicit, individually-quoted
//     argument list - never a shell (cmd.exe/sh) - so arguments from the
//     LLM cannot be used for shell injection regardless of their content.
//   - If `allowed_repo_root` is empty, the tool is fully disabled and
//     always returns an error explaining how to enable it.
class GitTool final : public Tool {
public:
    explicit GitTool(std::filesystem::path allowed_repo_root);

    [[nodiscard]] std::string name() const override { return "git"; }

    [[nodiscard]] std::string description() const override {
        return "Runs a restricted set of git operations against Atlas's own "
               "repository: status, diff, add, commit, checkout_branch, push. "
               "Only works inside the dedicated self-improvement workspace. "
               "Never pushes to main/master directly - always work on a "
               "feature branch, then use github_pr to open a pull request.";
    }

    [[nodiscard]] nlohmann::json parametersSchema() const override;

    [[nodiscard]] nlohmann::json execute(const nlohmann::json& arguments,
                                          const std::string& workspace_root) const override;

private:
    [[nodiscard]] bool isAllowedRepo(const std::string& workspace_root) const;

    // Runs `git <args>` with cwd = allowed_repo_root_, capturing combined
    // stdout+stderr. Returns {"exit_code": N, "output": "..."}.
    [[nodiscard]] nlohmann::json runGit(const std::vector<std::string>& args) const;

    [[nodiscard]] std::string currentBranch() const;

    std::filesystem::path allowed_repo_root_;
};

} // namespace atlas::tools
