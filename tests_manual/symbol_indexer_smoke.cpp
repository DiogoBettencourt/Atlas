// Manual smoke test for SymbolIndexer's per-workspace isolation. Reproduces
// the bug where indexing a second workspace (e.g. enabling --self-repo)
// silently replaced the first workspace's index, so search_symbol on
// workspace A would return workspace B's symbols with no error.
#include "atlas/core/SymbolIndexer.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

using atlas::core::SymbolIndexer;
namespace fs = std::filesystem;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}
} // namespace

int main() {
    fs::path base = fs::temp_directory_path() / "atlas_symidx_smoke";
    fs::remove_all(base);
    fs::path ws_a = base / "workspace_a";
    fs::path ws_b = base / "workspace_b";
    fs::create_directories(ws_a);
    fs::create_directories(ws_b);

    { std::ofstream f(ws_a / "a.cpp"); f << "void alphaOnlyFunction() {\n}\n"; }
    { std::ofstream f(ws_b / "b.cpp"); f << "void betaOnlyFunction() {\n}\n"; }

    SymbolIndexer indexer;

    std::cout << "== index workspace A, then workspace B ==\n";
    indexer.indexDirectory(ws_a);
    indexer.indexDirectory(ws_b);

    auto a_results = indexer.search("OnlyFunction", ws_a);
    auto b_results = indexer.search("OnlyFunction", ws_b);

    expect(a_results.size() == 1 && a_results[0].name == "alphaOnlyFunction",
           "workspace A search still finds alphaOnlyFunction after B was indexed");
    expect(b_results.size() == 1 && b_results[0].name == "betaOnlyFunction",
           "workspace B search finds betaOnlyFunction");

    std::cout << "\n== unindexed workspace returns empty, not another workspace's data ==\n";
    fs::path ws_c = base / "workspace_c";
    fs::create_directories(ws_c);
    auto c_results = indexer.search("OnlyFunction", ws_c);
    expect(c_results.empty(), "never-indexed workspace C returns no matches");

    fs::remove_all(base);
    std::cout << "\n" << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures))
              << "\n";
    return failures == 0 ? 0 : 1;
}
