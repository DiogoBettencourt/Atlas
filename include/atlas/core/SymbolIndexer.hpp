#pragma once

#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace atlas::core {

// A single discovered symbol (class, struct, function, or method).
struct SymbolEntry {
    std::string kind;       // "class", "struct", "function", "method"
    std::string name;       // e.g. "WorkspaceManager" or "WorkspaceManager::resolveSafe"
    std::string file;       // path relative to the indexed root
    unsigned int line = 0;  // 1-based line number
};

void to_json(nlohmann::json& j, const SymbolEntry& s);

// Builds a coarse, regex-based in-memory graph of classes/structs and
// free/member function declarations across a source tree. This is
// intentionally not a full C++ parser (no libclang dependency): the goal
// is "good enough" low-token symbol mapping so an agent operating under a
// tight context budget can jump straight to a definition instead of
// reading whole files.
//
// One SymbolIndexer instance is shared across every workspace (see
// Application::symbol_indexer_), so it keeps a separate symbol table per
// workspace root rather than a single global one - otherwise indexing a
// second workspace (e.g. enabling --self-repo) would silently replace the
// first workspace's index and search_symbol would return the wrong
// workspace's results with no error. Thread-safe: the API server dispatches
// requests concurrently, and either indexing or searching can happen from
// any of those request threads.
class SymbolIndexer {
public:
    SymbolIndexer() = default;

    SymbolIndexer(const SymbolIndexer&) = delete;
    SymbolIndexer& operator=(const SymbolIndexer&) = delete;
    SymbolIndexer(SymbolIndexer&&) = delete;
    SymbolIndexer& operator=(SymbolIndexer&&) = delete;

    // Recursively scans `root` for recognized source extensions
    // (.hpp/.h/.cpp/.cc/.py/.js/.ts by default) and rebuilds the in-memory
    // index for that specific root, replacing any previous index for it.
    // Other workspaces' indices are untouched. Safe to call again to
    // refresh after files change.
    void indexDirectory(const std::filesystem::path& root);

    // True if `root` has an index built (via indexDirectory) already.
    [[nodiscard]] bool isIndexed(const std::filesystem::path& root) const;

    // Returns every symbol whose name contains `query` (case-insensitive
    // substring match) within the index for `workspace_root`. Returns an
    // empty vector (not an error) if `workspace_root` hasn't been indexed.
    [[nodiscard]] std::vector<SymbolEntry> search(const std::string& query,
                                                   const std::filesystem::path& workspace_root) const;

    [[nodiscard]] std::size_t symbolCount(const std::filesystem::path& workspace_root) const;

private:
    void indexFile(const std::filesystem::path& file, const std::filesystem::path& root,
                   std::vector<SymbolEntry>& out) const;

    // Canonicalized, separator-normalized string used as the map key so
    // the same workspace root always hashes the same way regardless of
    // trailing slashes or "." components.
    [[nodiscard]] static std::string keyFor(const std::filesystem::path& root);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<SymbolEntry>> symbols_by_root_;
};

} // namespace atlas::core
