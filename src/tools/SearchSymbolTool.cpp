#include "atlas/tools/SearchSymbolTool.hpp"
#include <sstream>

namespace atlas::tools {

SearchSymbolTool::SearchSymbolTool(core::SymbolIndexer& indexer)
    : indexer_(indexer) {}

std::string SearchSymbolTool::name() const {
    return "search_symbols";
}

std::string SearchSymbolTool::description() const {
    return "Searches the codebase for classes, structs, functions, or methods matching a keyword. "
           "Returns the file path and line number of the match. Use this before trying to read files blindly.";
}

nlohmann::json SearchSymbolTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"query", {
                {"type", "string"},
                {"description", "The name of the symbol to search for (e.g., 'Application', 'chat', 'SymbolIndexer')."}
            }}
        }},
        {"required", nlohmann::json::array({"query"})}
    };
}

std::string SearchSymbolTool::execute(const nlohmann::json& arguments) {
    if (!arguments.contains("query") || !arguments["query"].is_string()) {
        return "Error: Missing or invalid 'query' parameter.";
    }

    std::string query = arguments["query"];
    auto results = indexer_.searchSymbols(query);

    if (results.empty()) {
        return "No symbols found matching query: " + query;
    }

    std::ostringstream ss;
    ss << "Found " << results.size() << " match(es) for '" << query << "':\n";
    for (const auto& sym : results) {
        // Outputting the full path makes it easy for the Agent to chain this directly into read_file
        ss << "- [" << sym.type << "] " << sym.name 
           << "\n  Path: " << sym.file_path.string() 
           << "\n  Line: " << sym.line_number << "\n";
    }

    return ss.str();
}

} // namespace atlas::tools