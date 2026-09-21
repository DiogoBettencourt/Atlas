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

std::string SymbolIndexer::keyFor(const fs::path& root) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(root, ec);
    return (ec ? root : canonical).generic_string();
}

bool SymbolIndexer::isIndexed(const fs::path& root) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return symbols_by_root_.find(keyFor(root)) != symbols_by_root_.end();
}

void SymbolIndexer::indexDirectory(const fs::path& root) {
    std::vector<SymbolEntry> discovered;

    if (fs::exists(root) && fs::is_directory(root)) {
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
            indexFile(entry.path(), root, discovered);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    symbols_by_root_[keyFor(root)] = std::move(discovered);
}

void SymbolIndexer::indexFile(const fs::path& file, const fs::path& root,
                               std::vector<SymbolEntry>& out) const {
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
            out.push_back(SymbolEntry{match[1].str(), match[2].str(), relative, line_no});
            continue;
        }
        if (std::regex_search(line, match, py_class_re)) {
            out.push_back(SymbolEntry{"class", match[1].str(), relative, line_no});
            continue;
        }
        if (std::regex_search(line, match, py_def_re)) {
            out.push_back(SymbolEntry{"function", match[1].str(), relative, line_no});
            continue;
        }
        if (std::regex_search(line, match, js_func_re)) {
            out.push_back(SymbolEntry{"function", match[1].str(), relative, line_no});
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
                out.push_back(SymbolEntry{kind, candidate, relative, line_no});
            }
        }
    }
}

std::vector<SymbolEntry> SymbolIndexer::search(const std::string& query,
                                                const fs::path& workspace_root) const {
    std::string needle = lower(query);
    std::vector<SymbolEntry> results;

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = symbols_by_root_.find(keyFor(workspace_root));
    if (it == symbols_by_root_.end()) {
        return results; // not indexed (yet) - empty, not an error
    }
    for (const auto& sym : it->second) {
        if (lower(sym.name).find(needle) != std::string::npos) {
            results.push_back(sym);
        }
    }
    return results;
}

std::size_t SymbolIndexer::symbolCount(const fs::path& workspace_root) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = symbols_by_root_.find(keyFor(workspace_root));
    return it == symbols_by_root_.end() ? 0 : it->second.size();
}

} // namespace atlas::core
