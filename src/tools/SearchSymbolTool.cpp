#include "atlas/tools/SearchSymbolTool.hpp"

namespace atlas::tools {

nlohmann::json SearchSymbolTool::parametersSchema() const {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"query", {
                {"type", "string"},
                {"description", "Substring to search for within symbol names (case-insensitive)."}
            }}
        }},
        {"required", nlohmann::json::array({"query"})}
    };
}

nlohmann::json SearchSymbolTool::execute(const nlohmann::json& arguments,
                                          const std::string& workspace_root) const {
    if (!arguments.contains("query") || !arguments["query"].is_string()) {
        return nlohmann::json{{"error", "missing required argument: query"}};
    }

    std::filesystem::path root(workspace_root);
    if (!indexer_.isIndexed(root)) {
        // Workspaces created after startup (e.g. via a /chat request naming
        // a new `workspace`) never went through Application's initial
        // indexDirectory() calls. Index on first use instead of silently
        // returning zero matches forever.
        indexer_.indexDirectory(root);
    }

    auto matches = indexer_.search(arguments["query"].get<std::string>(), root);

    nlohmann::json results = nlohmann::json::array();
    for (const auto& match : matches) {
        results.push_back(match);
    }

    return nlohmann::json{
        {"query", arguments["query"]},
        {"match_count", results.size()},
        {"matches", results}
    };
}

} // namespace atlas::tools
