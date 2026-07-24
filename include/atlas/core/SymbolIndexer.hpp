#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <map>

namespace atlas::core {

struct SymbolInfo {
    std::string name;
    std::string type; // e.g., "class", "method", "function", "def"
    std::filesystem::path file_path;
    int line_number;
};

class SymbolIndexer {
public:
    SymbolIndexer() = default;

    // Scans a directory recursively for supported code files and indexes symbols
    void indexWorkspace(const std::filesystem::path& root_path);

    // Search for symbols matching a query string
    std::vector<SymbolInfo> searchSymbols(const std::string& query) const;

    // Get a summary text of all indexed symbols (to inject into agent context if needed)
    std::string getSymbolGraphSummary() const;

private:
    // UPDATED: Now generic to handle multiple file extensions instead of just headers
    void parseFile(const std::filesystem::path& file_path);

    // Maps symbol name to its detailed info
    std::map<std::string, std::vector<SymbolInfo>> symbol_table_;
    std::filesystem::path root_path_;
};

} // namespace atlas::core