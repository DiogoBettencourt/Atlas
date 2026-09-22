#include "atlas/tools/GitTool.hpp"

#include <array>
#include <cstring>
#include <regex>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace atlas::tools {

namespace fs = std::filesystem;

namespace {

// Conservative allow-list for branch names: alphanumerics, dot, dash,
// underscore, and forward slash (for "feature/x" style names). Rejects
// anything that could be interpreted as a git option (leading '-') or
// path traversal.
// Shape-only validation shared by every branch-name argument: safe
// characters, bounded length, no ".." component. Does NOT reject
// main/master/HEAD - callers that must protect those branches (checkout,
// push) check for them separately.
bool isSafeRefShape(const std::string& name) {
    static const std::regex valid(R"(^[A-Za-z0-9][A-Za-z0-9._/-]*$)");
    if (name.empty() || name.size() > 200) return false;
    if (name.find("..") != std::string::npos) return false;
    return std::regex_match(name, valid);
}

bool isValidBranchName(const std::string& name) {
    if (name == "main" || name == "master" || name == "HEAD") return false;
    return isSafeRefShape(name);
}

#if defined(_WIN32)
// Quotes a single argument per the rules CommandLineToArgvW (and every
// well-behaved Windows C runtime) uses to split a command line back into
// argv, so building the command line here and having git.exe's CRT parse
// it back apart round-trips exactly - this is what keeps arguments from
// smuggling extra "arguments" the way naive quoting would. Based on the
// well-known algorithm Microsoft documents for this exact purpose (also
// used by e.g. Python's subprocess.list2cmdline).
std::string quoteWindowsArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg; // no special characters, no quoting needed
    }

    std::string out = "\"";
    for (auto it = arg.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == '\\') {
            ++it;
            ++backslashes;
        }

        if (it == arg.end()) {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (*it == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(*it);
        }
    }
    out.push_back('"');
    return out;
}
#endif

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
                    {"status", "diff", "add", "commit", "checkout_branch", "push", "pull"})},
                {"description", "Which git operation to perform."}
            }},
            {"branch", {
                {"type", "string"},
                {"description", "Branch name for checkout_branch (never 'main' or 'master') "
                                "or, for pull, the remote branch to fast-forward from "
                                "(defaults to the current branch)."}
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

#if defined(_WIN32)

nlohmann::json GitTool::runGit(const std::vector<std::string>& args) const {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!CreatePipe(&read_handle, &write_handle, &sa, 0)) {
        return nlohmann::json{{"exit_code", -1}, {"output", "CreatePipe() failed"}};
    }
    // Only the child should inherit the write end.
    SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);

    // Build "git.exe" arg1 arg2 ... as a single command line, quoting each
    // argument so the started process's own argv parsing round-trips it
    // exactly - never invoking cmd.exe, so no shell metacharacter
    // interpretation happens at any point.
    std::string command_line = "git.exe";
    for (const auto& arg : args) {
        command_line += ' ';
        command_line += quoteWindowsArg(arg);
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = write_handle;
    si.hStdError = write_handle;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::string cwd = allowed_repo_root_.string();

    // CreateProcessA requires a mutable command-line buffer.
    std::vector<char> mutable_cmd(command_line.begin(), command_line.end());
    mutable_cmd.push_back('\0');

    BOOL ok = CreateProcessA(
        nullptr,               // resolve "git.exe" via PATH, not a fixed module path
        mutable_cmd.data(),
        nullptr, nullptr,
        TRUE,                  // inherit handles (needed for the pipe)
        0,
        nullptr,                // inherit parent's environment
        cwd.c_str(),
        &si, &pi);

    CloseHandle(write_handle);

    if (!ok) {
        CloseHandle(read_handle);
        return nlohmann::json{{"exit_code", -1}, {"output", "CreateProcess() failed to launch git.exe"}};
    }

    std::string output;
    std::array<char, 4096> buffer{};
    DWORD bytes_read = 0;
    while (ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) &&
           bytes_read > 0) {
        output.append(buffer.data(), bytes_read);
    }
    CloseHandle(read_handle);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    constexpr std::size_t kMaxOutput = 16 * 1024;
    bool truncated = false;
    if (output.size() > kMaxOutput) {
        output = output.substr(0, kMaxOutput);
        truncated = true;
    }

    return nlohmann::json{
        {"exit_code", static_cast<int>(exit_code)}, {"output", output}, {"truncated", truncated}};
}

#else

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

#endif

nlohmann::json GitTool::execute(const nlohmann::json& arguments,
                                 const std::string& workspace_root) const {
    // Defensive: every other Tool guards its execute() with a
    // try/catch (see ListDirectoryTool, ReadFileTool, EditFileTool,
    // WriteFileTool) so a thrown exception becomes a JSON {"error":...}
    // instead of propagating out of the ReAct loop uncaught. GitTool was
    // the one tool missing this net - added after a Windows-only crash
    // (list_directory_and_pull_smoke, STATUS_STACK_BUFFER_OVERRUN /
    // 0xC0000409) that could not be reproduced or root-caused from a
    // non-Windows environment; an uncaught C++ exception reaching the
    // CRT's default terminate handler is exactly what surfaces as that
    // status code on Windows, so this closes the most likely gap even
    // though the precise throw site couldn't be confirmed.
    try {
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

        if (action == "pull") {
            // Unlike checkout_branch/push, pull never writes to the remote, so
            // main/master are allowed here - syncing local main from origin
            // before branching off it is exactly what this is for.
            std::string branch = arguments.value("branch", std::string{});
            if (branch.empty()) {
                branch = currentBranch();
            }
            if (branch.empty()) {
                return nlohmann::json{{"error", "unable to determine branch to pull"}};
            }
            if (!isSafeRefShape(branch)) {
                return nlohmann::json{{"error", "invalid branch name: " + branch}};
            }
            // --ff-only: never fabricates a merge commit or rewrites history;
            // fails cleanly if the local branch has diverged, which is exactly
            // the case where an agent should stop and ask rather than guess.
            return runGit({"pull", "--ff-only", "origin", branch});
        }

        return nlohmann::json{{"error", "unknown action: " + action}};
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("git tool exception: ") + e.what()}};
    } catch (...) {
        return nlohmann::json{{"error", "git tool: unknown exception"}};
    }
}

} // namespace atlas::tools
