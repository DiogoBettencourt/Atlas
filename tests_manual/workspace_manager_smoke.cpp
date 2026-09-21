// Manual smoke test for WorkspaceManager::resolveSafe, following the same
// pattern as git_tool_smoke.cpp (build + run by hand; not yet wired into
// CTest). Exercises the sandbox-boundary bug where a sibling directory
// whose name merely starts with the workspace name (e.g. "default" vs
// "default-evil") used to be incorrectly treated as "inside" the sandbox
// by a naive string-prefix check.
#include "atlas/core/WorkspaceManager.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using atlas::core::WorkspaceManager;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void expectThrows(const std::string& label, const fs::path& root, const std::string& rel) {
    try {
        auto resolved = WorkspaceManager::resolveSafe(root, rel);
        std::cout << "FAIL (expected throw): " << label << " -> resolved to " << resolved << "\n";
        ++failures;
    } catch (const std::exception&) {
        std::cout << "ok (rejected): " << label << "\n";
    }
}

void expectOk(const std::string& label, const fs::path& root, const std::string& rel,
              const fs::path& expected) {
    try {
        auto resolved = WorkspaceManager::resolveSafe(root, rel);
        if (resolved != expected) {
            std::cout << "FAIL (wrong result): " << label << " -> " << resolved
                       << " (expected " << expected << ")\n";
            ++failures;
        } else {
            std::cout << "ok (resolved): " << label << " -> " << resolved << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "FAIL (unexpected throw): " << label << " -> " << e.what() << "\n";
        ++failures;
    }
}

} // namespace

int main() {
    fs::path base = fs::temp_directory_path() / "atlas_wsm_smoke";
    fs::remove_all(base);

    fs::path ws_root = base / "workspaces" / "default";
    fs::path sibling = base / "workspaces" / "default-evil"; // starts with "default"
    fs::create_directories(ws_root);
    fs::create_directories(sibling);
    { std::ofstream(sibling / "secret.txt") << "should never be reachable from ws_root\n"; }

    std::cout << "== ordinary in-sandbox path ==\n";
    expectOk("plain file", ws_root, "notes.txt", fs::weakly_canonical(ws_root / "notes.txt"));

    std::cout << "\n== classic '../' traversal, still rejected ==\n";
    expectThrows("dotdot escape", ws_root, "../../etc/passwd");

    std::cout << "\n== absolute path, still rejected ==\n";
    expectThrows("absolute path", ws_root, fs::path("/etc/passwd").string());

    std::cout << "\n== sibling-prefix escape (the bug this test guards against) ==\n";
    // "default-evil" starts with "default": a naive prefix check on the
    // canonical string would let this through even though it's a
    // completely different directory tree.
    expectThrows("sibling prefix escape", ws_root, "../default-evil/secret.txt");

    fs::remove_all(base);

    std::cout << "\n" << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures))
              << "\n";
    return failures == 0 ? 0 : 1;
}
