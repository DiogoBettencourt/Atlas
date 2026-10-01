#include "atlas/tools/GitTool.hpp"
#include "atlas/tools/GitHubPRTool.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace {

int g_failures = 0;

// This file predates assertions (it was a pure print-and-eyeball smoke
// test - see the git history), which meant it silently "passed" for a
// long time even though its fixture repo was never actually created, so
// every git call inside failed with a chdir() error and nobody noticed.
// `check` is deliberately small and loud rather than pulling in a test
// framework: print PASS/FAIL per check, keep going so one failure
// doesn't hide the next one, and let main()'s exit code reflect whether
// anything actually failed.
void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "  [PASS] " << label << "\n";
    } else {
        std::cout << "  [FAIL] " << label << "\n";
        ++g_failures;
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Runs `git <args>` via plain system() to build the fixture repo itself
// - GitTool's own execute() is what's under test, so fixture setup
// deliberately doesn't go through it. Cross-platform on purpose: the
// caller's cwd (set once via fs::current_path(), never a shell `cd`)
// decides which repo this runs against, so `args` only ever needs to be
// plain ASCII git arguments - no path interpolation, no quoting that
// would need to differ between POSIX sh and cmd.exe. A caller that needs
// a value with a space in it (a commit message, a config value) wraps
// it in double quotes itself - the one quoting convention both shells
// agree on for simple content.
void sh(const std::string& args) {
    int rc = std::system(("git " + args).c_str());
    if (rc != 0) {
        std::cerr << "fixture setup command failed (" << rc << "): git " << args << "\n";
        std::exit(1);
    }
}

// Same as sh(), but for a command that's *expected* to possibly fail -
// creating a merge/rebase conflict on purpose, or aborting one that may
// or may not exist. Its output isn't suppressed (no POSIX-only
// `>/dev/null 2>&1` - there's no cross-platform equivalent worth the
// trouble) - seeing git's own conflict message in the test log is useful
// signal, not noise.
void shAllowFail(const std::string& args) {
    int rc = std::system(("git " + args).c_str());
    (void)rc; // exit status intentionally unchecked: the caller expects this to possibly fail
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream f(path, std::ios::trunc);
    f << content;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    fs::path repo = fs::temp_directory_path() / "atlas_git_tool_smoke_repo";
    fs::path wrong_workspace = fs::temp_directory_path() / "atlas_git_tool_smoke_not_the_repo";

    std::error_code ec;
    fs::remove_all(repo, ec);
    fs::remove_all(wrong_workspace, ec);
    fs::create_directories(repo);
    fs::create_directories(wrong_workspace);

    // Every git call below runs with this as cwd - set once, portably,
    // via the filesystem API rather than a shell `cd` (whose quoting
    // rules differ between POSIX sh and cmd.exe, and whose separator
    // conventions don't both agree on `/tmp/...`-style forward slashes).
    fs::current_path(repo);

    // A real repo, not just an empty directory - every action below
    // exercises actual git, not a chdir() failure that happens to return
    // a JSON object nobody checked the shape of.
    sh("init -q -b main");
    sh("config user.email test@example.com");
    sh("config user.name \"Atlas Test\"");
    writeFile(repo / "shared.txt", "line one\n");
    sh("add shared.txt");
    sh("commit -q -m \"initial commit\"");

    atlas::tools::GitTool git(repo);

    std::cout << "== wrong workspace should be refused ==\n";
    auto wrong_ws_result = git.execute({{"action", "status"}}, wrong_workspace.string());
    std::cout << wrong_ws_result.dump(2) << "\n";
    check(wrong_ws_result.contains("error"), "wrong workspace refused");

    std::cout << "\n== correct workspace: status ==\n";
    auto status_result = git.execute({{"action", "status"}}, repo.string());
    std::cout << status_result.dump(2) << "\n";
    check(status_result.value("exit_code", -1) == 0, "status succeeds against the real repo");

    std::cout << "\n== attempt checkout_branch main (must be refused) ==\n";
    auto main_refused = git.execute({{"action", "checkout_branch"}, {"branch", "main"}}, repo.string());
    std::cout << main_refused.dump(2) << "\n";
    check(main_refused.contains("error"), "checkout_branch main refused");

    std::cout << "\n== checkout_branch feature/atlas-test (should create it) ==\n";
    auto checkout_result = git.execute({{"action", "checkout_branch"}, {"branch", "feature/atlas-test"}}, repo.string());
    std::cout << checkout_result.dump(2) << "\n";
    check(checkout_result.value("exit_code", -1) == 0, "checkout_branch creates a new branch");

    writeFile(repo / "new_file.txt", "written by the agent\n");

    std::cout << "\n== add ==\n";
    auto add_result = git.execute({{"action", "add"}}, repo.string());
    std::cout << add_result.dump(2) << "\n";
    check(add_result.value("exit_code", -1) == 0, "add succeeds");

    std::cout << "\n== commit ==\n";
    auto commit_result = git.execute({{"action", "commit"}, {"message", "test: add new_file.txt"}}, repo.string());
    std::cout << commit_result.dump(2) << "\n";
    check(commit_result.value("exit_code", -1) == 0, "commit succeeds");

    std::cout << "\n== push (expected to fail: no 'origin' remote configured) ==\n";
    auto push_no_remote = git.execute({{"action", "push"}}, repo.string());
    std::cout << push_no_remote.dump(2) << "\n";
    check(push_no_remote.value("exit_code", -1) != 0, "push fails cleanly with no remote configured");

    std::cout << "\n== attempt push while on main (must be refused before even trying) ==\n";
    // checkout_branch("main") is itself refused by the tool (main is a
    // protected name - see the earlier check) - this tool can never check
    // itself out onto main. To actually test push's defense-in-depth
    // main/master guard, land on main the way something outside the
    // tool's control would, bypassing GitTool entirely via the raw
    // fixture helper, then confirm push still refuses regardless of how
    // we got onto main.
    sh("checkout -q main");
    auto push_on_main = git.execute({{"action", "push"}}, repo.string());
    std::cout << push_on_main.dump(2) << "\n";
    check(push_on_main.contains("error"), "push refused while on main");

    // --- Issue #16: merge-conflict detection and abort_merge recovery ---
    //
    // Set up a genuine conflicting merge: two branches that each change
    // the same line of shared.txt differently, then merge one into the
    // other. `git merge` exits nonzero and leaves the repo mid-merge with
    // conflict markers written into shared.txt - exactly the state the
    // old version of this tool had no way to detect or recover from.
    std::cout << "\n== setting up a real merge conflict ==\n";
    sh("checkout -q main");
    sh("checkout -q -b conflict-a");
    writeFile(repo / "shared.txt", "line one, changed on A\n");
    sh("commit -q -am \"change on conflict-a\"");
    sh("checkout -q main");
    sh("checkout -q -b conflict-b");
    writeFile(repo / "shared.txt", "line one, changed on B\n");
    sh("commit -q -am \"change on conflict-b\"");
    shAllowFail("merge conflict-a");

    auto status_mid_conflict = git.execute({{"action", "status"}}, repo.string());
    std::cout << "status while mid-conflict:\n" << status_mid_conflict.dump(2) << "\n";
    check(status_mid_conflict.value("exit_code", -1) == 0, "status itself still works mid-conflict (read-only)");

    std::cout << "\n== add must refuse while a merge is unresolved ==\n";
    auto add_during_conflict = git.execute({{"action", "add"}}, repo.string());
    std::cout << add_during_conflict.dump(2) << "\n";
    check(add_during_conflict.contains("error"), "add refused during an unresolved merge");
    check(contains(add_during_conflict.value("error", ""), "abort_merge"),
          "add's refusal points the agent at abort_merge");

    std::cout << "\n== commit must also refuse while a merge is unresolved ==\n";
    auto commit_during_conflict = git.execute({{"action", "commit"}, {"message", "oops"}}, repo.string());
    std::cout << commit_during_conflict.dump(2) << "\n";
    check(commit_during_conflict.contains("error"), "commit refused during an unresolved merge");

    std::cout << "\n== checkout_branch must also refuse while a merge is unresolved ==\n";
    auto checkout_during_conflict = git.execute({{"action", "checkout_branch"}, {"branch", "escape-hatch"}}, repo.string());
    std::cout << checkout_during_conflict.dump(2) << "\n";
    check(checkout_during_conflict.contains("error"), "checkout_branch refused during an unresolved merge");

    // The actual hazard this guard exists to prevent: confirm the
    // conflict markers are genuinely still sitting in the working tree
    // (i.e. the refusal above is doing real work, not just reacting to a
    // false positive).
    {
        std::ifstream f(repo / "shared.txt");
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check(contains(content, "<<<<<<<"), "conflict markers are genuinely present in the working tree");
    }

    std::cout << "\n== abort_merge with nothing in progress is a clean no-op ==\n";
    sh("merge --abort");
    auto noop_abort = git.execute({{"action", "abort_merge"}}, repo.string());
    std::cout << noop_abort.dump(2) << "\n";
    check(!noop_abort.contains("error"), "abort_merge with nothing in progress doesn't error");
    check(noop_abort.value("exit_code", -1) == 0, "abort_merge no-op reports exit_code 0");

    // Re-create the exact same conflict to test abort_merge actually
    // doing the recovery, not just the no-op path above.
    shAllowFail("merge conflict-a");
    auto status_mid_conflict_2 = git.execute({{"action", "status"}}, repo.string());
    check(contains(status_mid_conflict_2.value("output", ""), "shared.txt") ||
          status_mid_conflict_2.value("exit_code", -1) == 0,
          "conflict re-created for the recovery test");

    std::cout << "\n== abort_merge actually recovers ==\n";
    auto abort_result = git.execute({{"action", "abort_merge"}}, repo.string());
    std::cout << abort_result.dump(2) << "\n";
    check(abort_result.value("exit_code", -1) == 0, "abort_merge succeeds");

    {
        std::ifstream f(repo / "shared.txt");
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check(!contains(content, "<<<<<<<"), "conflict markers are gone after abort_merge");
    }

    std::cout << "\n== normal actions work again after abort_merge ==\n";
    auto status_after_abort = git.execute({{"action", "status"}}, repo.string());
    std::cout << status_after_abort.dump(2) << "\n";
    check(status_after_abort.value("exit_code", -1) == 0, "status succeeds after recovery");

    auto add_after_abort = git.execute({{"action", "add"}}, repo.string());
    check(add_after_abort.value("exit_code", -1) == 0, "add succeeds again after recovery (no longer blocked)");

    // --- Issue #16: same guard, but for a rebase conflict rather than a
    // merge one - a distinct code path in inProgressOperation() (checks
    // rebase-merge/rebase-apply instead of MERGE_HEAD), so the merge test
    // above doesn't actually cover it.
    std::cout << "\n== setting up a real rebase conflict ==\n";
    sh("checkout -q conflict-b");
    shAllowFail("rebase conflict-a");

    auto status_mid_rebase = git.execute({{"action", "status"}}, repo.string());
    std::cout << status_mid_rebase.dump(2) << "\n";

    std::cout << "\n== commit must refuse while a rebase is unresolved ==\n";
    auto commit_during_rebase = git.execute({{"action", "commit"}, {"message", "oops"}}, repo.string());
    std::cout << commit_during_rebase.dump(2) << "\n";
    check(contains(commit_during_rebase.value("error", ""), "rebase"),
          "commit refused with a message naming the rebase specifically");

    std::cout << "\n== abort_merge recovers from a rebase too (git rebase --abort) ==\n";
    auto rebase_abort_result = git.execute({{"action", "abort_merge"}}, repo.string());
    std::cout << rebase_abort_result.dump(2) << "\n";
    check(rebase_abort_result.value("exit_code", -1) == 0, "abort_merge succeeds against an in-progress rebase");

    auto status_after_rebase_abort = git.execute({{"action", "status"}}, repo.string());
    check(status_after_rebase_abort.value("exit_code", -1) == 0, "status succeeds after rebase recovery");
    auto commit_after_rebase_abort = git.execute({{"action", "commit"}, {"message", "oops"}}, repo.string());
    // runGit()'s result shape never includes an "error" key, win or lose -
    // only this tool's own guards (argument validation, the conflict
    // guard above) add one. So "no 'error' key" here specifically means
    // "the conflict guard didn't fire" - the request reached real git,
    // which then fails on its own, ordinary terms (nothing to commit).
    check(!commit_after_rebase_abort.contains("error"),
          "commit no longer blocked by the (now-resolved) rebase - reaches real git, not the guard");
    check(commit_after_rebase_abort.value("exit_code", -1) != 0,
          "...and real git correctly refuses too, for an ordinary reason (nothing to commit)");

    std::cout << "\n== GitHubPRTool: disabled (no token, or built without OpenSSL) ==\n";
    atlas::tools::GitHubPRTool pr(repo, "example-owner/example-repo");
    std::cout << pr.execute({{"title", "test"}, {"head", "feature/atlas-test"}}, repo.string()).dump(2) << "\n\n";

    std::cout << "== GitHubPRTool: wrong workspace refused ==\n";
    std::cout << pr.execute({{"title", "test"}, {"head", "feature/atlas-test"}}, wrong_workspace.string()).dump(2) << "\n";

    // Step out of `repo` before deleting it - on Windows a directory
    // that's still a live process's cwd can't be removed.
    fs::current_path(fs::temp_directory_path());
    fs::remove_all(repo, ec);
    fs::remove_all(wrong_workspace, ec);

    std::cout << "\n" << (g_failures == 0 ? "ALL CHECKS PASSED" : std::to_string(g_failures) + " CHECK(S) FAILED") << "\n";
    return g_failures == 0 ? 0 : 1;
}
