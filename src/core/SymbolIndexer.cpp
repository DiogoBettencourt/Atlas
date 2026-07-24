#include "atlas/core/SymbolIndexer.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <unordered_set>

namespace atlas::core {

void SymbolIndexer::indexWorkspace(const std::filesystem::path& root_path) {
    root_path_ = root_path;
    symbol_table_.clear();

    if (!std::filesystem::exists(root_path_)) return;

    // 1. Directories we DO NOT want to scan (Performance Guard)
    std::unordered_set<std::string> ignored_dirs = {
        ".git", "build", "node_modules", "out", "dist", "bin", "obj"
    };

    // 2. Walk the entire workspace, skipping ignored directories
    for (auto it = std::filesystem::recursive_directory_iterator(root_path_); 
         it != std::filesystem::recursive_directory_iterator(); 
         ++it) {
        
        if (it->is_directory()) {
            if (ignored_dirs.count(it->path().filename().string())) {
                it.disable_recursion_pending(); // Skip this entire folder
            }
            continue;
        }

        if (it->is_regular_file()) {
            parseFile(it->path());
        }
    }

    std::cout << "[SymbolIndexer] Indexed generic workspace symbols from: " << root_path_ << "\n";
}

void SymbolIndexer::parseFile(const std::filesystem::path& file_path) {
    std::string ext = file_path.extension().string();
    
    // 3. Determine which Regex to use based on file extension
    std::regex symbol_regex;
    bool supported_file = false;

    if (ext == ".hpp" || ext == ".h" || ext == ".cpp") {
        // C++: Matches 'class X' or 'struct Y'
        symbol_regex = std::regex(R"(\b(class|struct)\s+(\w+))");
        supported_file = true;
    } else if (ext == ".py") {
        // Python: Matches 'class X:' or 'def Y:'
        symbol_regex = std::regex(R"(^\s*(class|def)\s+(\w+))");
        supported_file = true;
    } else if (ext == ".js" || ext == ".ts") {
        // JS/TS: Matches 'class X' or 'function Y'
        symbol_regex = std::regex(R"(\b(class|function)\s+(\w+))");
        supported_file = true;
    }

    if (!supported_file) return; // Skip files we don't know how to parse yet

    std::ifstream file(file_path);
    if (!file.is_open()) return;

    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;
        std::smatch match;
        if (std::regex_search(line, match, symbol_regex)) {
            if (match.size() > 2) {
                std::string kind = match[1].str();
                std::string name = match[2].str();

                SymbolInfo info{name, kind, file_path, line_num};
                symbol_table_[name].push_back(info);
            }
        }
    }
}

// ... searchSymbols and getSymbolGraphSummary remain exactly the same ...
std::vector<SymbolInfo> SymbolIndexer::searchSymbols(const std::string& query) const {
    std::vector<SymbolInfo> results;
    for (const auto& [name, symbols] : symbol_table_) {
        if (name.find(query) != std::string::npos) {
            for (const auto& sym : symbols) {
                results.push_back(sym);
            }
        }
    }
    return results;
}

std::string SymbolIndexer::getSymbolGraphSummary() const {
    std::ostringstream ss;
    ss << "Indexed Codebase Symbols:\n";
    for (const auto& [name, symbols] : symbol_table_) {
        for (const auto& sym : symbols) {
            ss << "- [" << sym.type << "] " << sym.name << " (in " << sym.file_path.filename().string() << ")\n";
        }
    }
    return ss.str();
}

} // namespace atlas::core