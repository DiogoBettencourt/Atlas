#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
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
class SymbolIndexer {
public:
    SymbolIndexer() = default;

    SymbolIndexer(const SymbolIndexer&) = delete;
    SymbolIndexer& operator=(const SymbolIndexer&) = delete;
    SymbolIndexer(SymbolIndexer&&) = delete;
    SymbolIndexer& operator=(SymbolIndexer&&) = delete;

    // Recursively scans `root` for recognized source extensions
    // (.hpp/.h/.cpp/.cc/.py/.js/.ts by default) and rebuilds the in-memory
    // index. Safe to call again to refresh after files change.
    void indexDirectory(const std::filesystem::path& root);

    // Returns every symbol whose name contains `query` (case-insensitive
    // substring match), across the most recently indexed directory.
    [[nodiscard]] std::vector<SymbolEntry> search(const std::string& query) const;

    [[nodiscard]] std::size_t symbolCount() const { return symbols_.size(); }

private:
    void indexFile(const std::filesystem::path& file, const std::filesystem::path& root);

    std::vector<SymbolEntry> symbols_;
};

} // namespace atlas::core
