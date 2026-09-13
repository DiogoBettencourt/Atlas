#include "atlas/tools/GitTool.hpp"

#include <array>
#include <cstring>
#include <fcntl.h>
#include <regex>
#include <sys/wait.h>
#include <unistd.h>

namespace atlas::tools {

namespace fs = std::filesystem;

namespace {

// Conservative allow-list for branch names: alphanumerics, dot, dash,
// underscore, and forward slash (for "feature/x" style names). Rejects
// anything that could be interpreted as a git option (leading '-') or
// path traversal.
bool isValidBranchName(const std::string& name) {
    static const std::regex valid(R"(^[A-Za-z0-9][A-Za-z0-9._/-]*$)");
    if (name.empty() || name.size() > 200) return false;
    if (name == "main" || name == "master" || name == "HEAD") return false;
    if (name.find("..") != std::string::npos) return false;
    return std::regex_match(name, valid);
}

} // namespace

GitTool::GitTool(fs::path allowed_repo_root) : allowed_repo_root_(std::move(allowed_repo_root)) {
    if (!allowed_repo_root_.empty()) {
        std::error_code ec;
        allowed_repo_root_ = fs::weakly_canonical(allowed_repo_root_, ec);
    }
}

bool GitTool::isAllowedRepo(const std::string& workspace_root) const {
    if (allowed_repo_root_.empty()) return false;
    std::error_code ec;
    fs::path requested = fs::weakly_canonical(fs::path(workspace_root), ec);
    return !ec && requested == allowed_repo_root_;
}

nlohmann::json GitTool::parametersSchema() const {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"action", {
                {"type", "string"},
                {"enum", nlohmann::json::array(
                    {"status", "diff", "add", "commit", "checkout_branch", "push"})},
                {"description", "Which git operation to perform."}
            }},
            {"branch", {
                {"type", "string"},
                {"description", "Branch name for checkout_branch. Never 'main' or 'master'."}
            }},
            {"message", {
                {"type", "string"},
                {"description", "Commit message, required for action=commit."}
            }},
            {"paths", {
                {"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "Files to stage for action=add. Omit or use ['.'] for everything."}
            }}
        }},
        {"required", nlohmann::json::array({"action"})}
    };
}

std::string GitTool::currentBranch() const {
    auto result = runGit({"rev-parse", "--abbrev-ref", "HEAD"});
    std::string branch = result.value("output", std::string{});
    while (!branch.empty() && (branch.back() == '\n' || branch.back() == '\r')) {
        branch.pop_back();
    }
    return branch;
}

nlohmann::json GitTool::runGit(const std::vector<std::string>& args) const {
    int stdout_pipe[2];
    if (pipe(stdout_pipe) != 0) {
        return nlohmann::json{{"exit_code", -1}, {"output", "pipe() failed"}};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return nlohmann::json{{"exit_code", -1}, {"output", "fork() failed"}};
    }

    if (pid == 0) {
        // Child: redirect stdout+stderr into the pipe, chdir into the repo,
        // then exec git directly (no shell involved at any point).
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

        if (chdir(allowed_repo_root_.c_str()) != 0) {
            _exit(127);
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>("git"));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp("git", argv.data());
        _exit(127); // only reached if execvp fails
    }

    // Parent
    close(stdout_pipe[1]);
    std::string output;
    std::array<char, 4096> buffer{};
    ssize_t n;
    while ((n = read(stdout_pipe[0], buffer.data(), buffer.size())) > 0) {
        output.append(buffer.data(), static_cast<std::size_t>(n));
    }
    close(stdout_pipe[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    // Guard against unbounded output flooding the agent's context.
    constexpr std::size_t kMaxOutput = 16 * 1024;
    bool truncated = false;
    if (output.size() > kMaxOutput) {
        output = output.substr(0, kMaxOutput);
        truncated = true;
    }

    return nlohmann::json{{"exit_code", exit_code}, {"output", output}, {"truncated", truncated}};
}

nlohmann::json GitTool::execute(const nlohmann::json& arguments,
                                 const std::string& workspace_root) const {
    if (allowed_repo_root_.empty()) {
        return nlohmann::json{{"error",
            "self-improvement git tool is disabled: start Atlas with --self-repo=<path> to enable it"}};
    }
    if (!isAllowedRepo(workspace_root)) {
        return nlohmann::json{{"error",
            "git tool refused: this workspace is not the configured self-repo. "
            "Switch to the self-improvement workspace to use this tool."}};
    }
    if (!arguments.contains("action") || !arguments["action"].is_string()) {
        return nlohmann::json{{"error", "missing required argument: action"}};
    }

    const std::string action = arguments["action"].get<std::string>();

    if (action == "status") {
        return runGit({"status", "--short", "--branch"});
    }

    if (action == "diff") {
        return runGit({"diff"});
    }

    if (action == "add") {
        std::vector<std::string> args{"add"};
        if (arguments.contains("paths") && arguments["paths"].is_array() &&
            !arguments["paths"].empty()) {
            for (const auto& p : arguments["paths"]) {
                if (!p.is_string()) {
                    return nlohmann::json{{"error", "paths must be an array of strings"}};
                }
                std::string path_str = p.get<std::string>();
                if (path_str.rfind('-', 0) == 0) {
                    return nlohmann::json{{"error", "path may not begin with '-': " + path_str}};
                }
                args.push_back(path_str);
            }
        } else {
            args.push_back(".");
        }
        return runGit(args);
    }

    if (action == "commit") {
        if (!arguments.contains("message") || !arguments["message"].is_string() ||
            arguments["message"].get<std::string>().empty()) {
            return nlohmann::json{{"error", "missing required argument: message"}};
        }
        return runGit({"commit", "-m", arguments["message"].get<std::string>()});
    }

    if (action == "checkout_branch") {
        if (!arguments.contains("branch") || !arguments["branch"].is_string()) {
            return nlohmann::json{{"error", "missing required argument: branch"}};
        }
        std::string branch = arguments["branch"].get<std::string>();
        if (!isValidBranchName(branch)) {
            return nlohmann::json{{"error",
                "invalid or disallowed branch name (main/master/HEAD are protected): " + branch}};
        }
        // Try switching to an existing branch first; fall back to creating it.
        auto attempt = runGit({"checkout", branch});
        if (attempt.value("exit_code", -1) != 0) {
            attempt = runGit({"checkout", "-b", branch});
        }
        return attempt;
    }

    if (action == "push") {
        std::string branch = currentBranch();
        if (branch.empty()) {
            return nlohmann::json{{"error", "unable to determine current branch"}};
        }
        if (branch == "main" || branch == "master") {
            return nlohmann::json{{"error",
                "refusing to push directly to '" + branch +
                "'. Use checkout_branch to create a feature branch first."}};
        }
        return runGit({"push", "-u", "origin", branch});
    }

    return nlohmann::json{{"error", "unknown action: " + action}};
}

} // namespace atlas::tools
