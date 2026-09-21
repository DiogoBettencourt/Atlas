// Manual smoke test for ListDirectoryTool and GitTool's new `pull` action.
#include "atlas/tools/GitTool.hpp"
#include "atlas/tools/ListDirectoryTool.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}

int run(const std::string& cmd) { return std::system(cmd.c_str()); }
} // namespace

int main() {
    // ---------------------------------------------------------------
    // list_directory
    // ---------------------------------------------------------------
    fs::path ws = fs::temp_directory_path() / "atlas_listdir_smoke";
    fs::remove_all(ws);
    fs::create_directories(ws / "subdir");
    { std::ofstream(ws / "a.txt") << "hello"; }
    { std::ofstream(ws / "b.txt") << "world!!"; }

    atlas::tools::ListDirectoryTool list_tool;

    std::cout << "== list_directory: root ==\n";
    auto result = list_tool.execute(json::object(), ws.string());
    std::cout << result.dump(2) << "\n";
    expect(result.value("entry_count", 0) == 3, "root has 3 entries (subdir, a.txt, b.txt)");
    expect(result["entries"][0]["type"] == "directory", "directories sort before files");

    std::cout << "\n== list_directory: sandbox escape rejected ==\n";
    auto escape = list_tool.execute({{"path", "../"}}, ws.string());
    std::cout << escape.dump(2) << "\n";
    expect(escape.contains("error"), "'../' is rejected, not silently listed");

    fs::remove_all(ws);

    // ---------------------------------------------------------------
    // git pull (fast-forward from a real local "origin")
    // ---------------------------------------------------------------
    fs::path base = fs::temp_directory_path() / "atlas_git_pull_smoke";
    fs::remove_all(base);
    fs::create_directories(base);
    fs::path origin = base / "origin.git";
    fs::path clone_a = base / "clone_a";
    fs::path clone_b = base / "clone_b";

    run("git init --bare -q \"" + origin.string() + "\"");
    run("git clone -q \"" + origin.string() + "\" \"" + clone_a.string() + "\"");
    run("git -C \"" + clone_a.string() + "\" -c user.email=a@a -c user.name=a "
        "checkout -q -b main");
    { std::ofstream(clone_a / "file1.txt") << "v1\n"; }
    run("git -C \"" + clone_a.string() + "\" add file1.txt");
    run("git -C \"" + clone_a.string() + "\" -c user.email=a@a -c user.name=a "
        "commit -q -m init");
    run("git -C \"" + clone_a.string() + "\" push -q -u origin main");

    run("git clone -q \"" + origin.string() + "\" \"" + clone_b.string() + "\"");
    // The bare origin's HEAD symref still points at the never-created
    // "master" (git init --bare doesn't know "main" exists until told),
    // so a plain clone leaves clone_b on an unborn HEAD. Real self-repo
    // clones always have an actual branch checked out, so make that true
    // here too rather than exercising an unrealistic edge case.
    run("git -C \"" + clone_b.string() + "\" checkout -q main");

    atlas::tools::GitTool git_b(clone_b);

    std::cout << "\n== git pull: invalid branch name rejected ==\n";
    auto bad = git_b.execute({{"action", "pull"}, {"branch", "-x"}}, clone_b.string());
    std::cout << bad.dump(2) << "\n";
    expect(bad.contains("error"), "'-x' is rejected as an unsafe ref name");

    std::cout << "\n== git pull: already up to date ==\n";
    auto up_to_date = git_b.execute({{"action", "pull"}}, clone_b.string());
    std::cout << up_to_date.dump(2) << "\n";
    expect(up_to_date.value("exit_code", -1) == 0, "pull with no new commits succeeds");

    // New commit lands on origin via clone_a...
    { std::ofstream(clone_a / "file2.txt") << "v2\n"; }
    run("git -C \"" + clone_a.string() + "\" add file2.txt");
    run("git -C \"" + clone_a.string() + "\" -c user.email=a@a -c user.name=a "
        "commit -q -m second");
    run("git -C \"" + clone_a.string() + "\" push -q origin main");

    std::cout << "\n== git pull: fast-forwards clone_b from origin ==\n";
    auto pulled = git_b.execute({{"action", "pull"}, {"branch", "main"}}, clone_b.string());
    std::cout << pulled.dump(2) << "\n";
    expect(pulled.value("exit_code", -1) == 0, "pull succeeds");
    expect(fs::exists(clone_b / "file2.txt"), "file2.txt appears in clone_b's working tree after pull");

    fs::remove_all(base);

    std::cout << "\n" << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures))
              << "\n";
    return failures == 0 ? 0 : 1;
}
