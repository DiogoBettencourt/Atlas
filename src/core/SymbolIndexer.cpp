#include "atlas/core/SymbolIndexer.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace atlas::core {

namespace fs = std::filesystem;

void to_json(nlohmann::json& j, const SymbolEntry& s) {
    j = nlohmann::json{
        {"kind", s.kind},
        {"name", s.name},
        {"file", s.file},
        {"line", s.line}
    };
}

namespace {

bool hasRecognizedExtension(const fs::path& p) {
    static const std::vector<std::string> kExtensions = {
        ".hpp", ".h", ".hh", ".cpp", ".cc", ".cxx", ".py", ".js", ".ts", ".tsx"
    };
    auto ext = p.extension().string();
    return std::find(kExtensions.begin(), kExtensions.end(), ext) != kExtensions.end();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

void SymbolIndexer::indexDirectory(const fs::path& root) {
    symbols_.clear();

    if (!fs::exists(root) || !fs::is_directory(root)) {
        return;
    }

    for (const auto& entry : fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file() || !hasRecognizedExtension(entry.path())) {
            continue;
        }
        // Skip common noise directories.
        auto path_str = entry.path().generic_string();
        if (path_str.find("/build/") != std::string::npos ||
            path_str.find("/.git/") != std::string::npos ||
            path_str.find("/node_modules/") != std::string::npos) {
            continue;
        }
        indexFile(entry.path(), root);
    }
}

void SymbolIndexer::indexFile(const fs::path& file, const fs::path& root) {
    std::ifstream in(file);
    if (!in) {
        return;
    }

    // Coarse patterns covering the common declaration shapes we care about.
    static const std::regex class_re(R"(^\s*(class|struct)\s+([A-Za-z_]\w*))");
    static const std::regex cpp_func_re(
        R"(^\s*(?:[\w:<>,\s\*&]+?\s+)([A-Za-z_]\w*(?:::[A-Za-z_]\w*)?)\s*\([^;{]*\)\s*(?:const)?\s*\{?\s*$)");
    static const std::regex py_def_re(R"(^\s*def\s+([A-Za-z_]\w*)\s*\()");
    static const std::regex py_class_re(R"(^\s*class\s+([A-Za-z_]\w*))");
    static const std::regex js_func_re(
        R"(^\s*(?:export\s+)?(?:async\s+)?function\s+([A-Za-z_]\w*)\s*\()");

    std::string relative = fs::relative(file, root).generic_string();
    std::string line;
    unsigned int line_no = 0;
    std::smatch match;

    while (std::getline(in, line)) {
        ++line_no;

        if (std::regex_search(line, match, class_re)) {
            symbols_.push_back(SymbolEntry{match[1].str(), match[2].str(), relative, line_no});
            continue;
        }
        if (std::regex_search(line, match, py_class_re)) {
            symbols_.push_back(SymbolEntry{"class", match[1].str(), relative, line_no});
            continue;
        }
        if (std::regex_search(line, match, py_def_re)) {
            symbols_.push_back(SymbolEntry{"function", match[1].str(), relative, line_no});
            continue;
        }
        if (std::regex_search(line, match, js_func_re)) {
            symbols_.push_back(SymbolEntry{"function", match[1].str(), relative, line_no});
            continue;
        }
        // C++ free function / method definitions - kept last since it's the
        // broadest pattern and most prone to false positives.
        if (line.find('(') != std::string::npos && line.find(';') == std::string::npos &&
            std::regex_search(line, match, cpp_func_re)) {
            const std::string& candidate = match[1].str();
            // Filter out obvious non-declarations (control flow keywords).
            static const std::vector<std::string> kExcluded = {
                "if", "for", "while", "switch", "catch", "return"
            };
            if (std::find(kExcluded.begin(), kExcluded.end(), candidate) == kExcluded.end()) {
                std::string kind = candidate.find("::") != std::string::npos ? "method" : "function";
                symbols_.push_back(SymbolEntry{kind, candidate, relative, line_no});
            }
        }
    }
}

std::vector<SymbolEntry> SymbolIndexer::search(const std::string& query) const {
    std::vector<SymbolEntry> results;
    std::string needle = lower(query);
    for (const auto& sym : symbols_) {
        if (lower(sym.name).find(needle) != std::string::npos) {
            results.push_back(sym);
        }
    }
    return results;
}

} // namespace atlas::core
