#pragma once

#include "atlas/tools/Tool.hpp"
#include <filesystem>

namespace atlas::tools {

// Exposes a narrow, safety-checked subset of git to the agent so it can
// work on its OWN source tree: status, diff, add, commit, checkout_branch,
// push, pull, abort_merge. This tool is deliberately not general-purpose:
//
//   - It only operates when the request's workspace_root canonically
//     matches the single `allowed_repo_root` it was constructed with (set
//     from --self-repo at startup). Any other workspace gets a hard
//     refusal, so a normal user workspace can never trigger a git push.
//   - checkout_branch refuses "main"/"master"/"HEAD" as a target name.
//   - push refuses to push while HEAD is on "main" or "master".
//   - pull is always --ff-only, so this tool itself can never leave the
//     repo mid-merge - but a merge/rebase conflict can still exist if one
//     was left behind by a previous run, or by a human poking at the same
//     checkout (see docs/specs/self-improvement-safety.md on why
//     --self-repo should be a dedicated clone, not daily-driver one).
//     add/commit/checkout_branch/push/pull all refuse to run while one is
//     in progress (see inProgressOperation()) - add/commit refusing
//     specifically matters because blindly `git add`-ing a conflicted
//     file stages its literal `<<<<<<<` markers as if resolved, and a
//     commit on top of that bakes broken code into history. abort_merge
//     is the explicit, safe way out: it never fires on its own, only when
//     the agent (or a human directing it) asks for it.
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
               "repository: status, diff, add, commit, checkout_branch, push, "
               "pull, abort_merge. Only works inside the dedicated "
               "self-improvement workspace. Pull (fast-forward only) before "
               "branching so new work starts from the latest remote state. "
               "Never pushes to main/master directly - always work on a "
               "feature branch, then use github_pr to open a pull request. "
               "If a merge or rebase is left unresolved (conflict markers in "
               "a file, 'status' showing unmerged paths), add/commit/"
               "checkout_branch/push/pull will all refuse until it's dealt "
               "with - call abort_merge to cleanly back out to the state "
               "before the merge/rebase started.";
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

    // Returns "merge", "rebase", or "" (clean) depending on whether the
    // repo is currently sitting mid-conflict. Resolved via
    // `git rev-parse --git-path <name>` rather than hand-assuming `.git`
    // is a plain directory, so this still works correctly when
    // allowed_repo_root_ is a `git worktree` checkout (the setup
    // docs/specs/self-improvement-safety.md recommends), where `.git` is
    // a file pointing elsewhere.
    [[nodiscard]] std::string inProgressOperation() const;

    std::filesystem::path allowed_repo_root_;
};

} // namespace atlas::tools
