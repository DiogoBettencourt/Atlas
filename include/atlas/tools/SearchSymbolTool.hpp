#pragma once

#include "atlas/core/SymbolIndexer.hpp"
#include "atlas/tools/Tool.hpp"

namespace atlas::tools {

// Queries the SymbolIndexer for classes/functions/methods whose name
// matches a substring, scoped to the calling request's workspace. This is
// the "Librarian" entry point agents should reach for before reading whole
// files. Lazily indexes the workspace on first use if it hasn't been
// indexed yet (e.g. a workspace created via the API after startup).
class SearchSymbolTool final : public Tool {
public:
    explicit SearchSymbolTool(core::SymbolIndexer& indexer) : indexer_(indexer) {}

    [[nodiscard]] std::string name() const override { return "search_symbol"; }

    [[nodiscard]] std::string description() const override {
        return "Searches the indexed codebase for classes, structs, functions, "
               "or methods whose name contains the query string. Returns file "
               "and line number for each match so you can read_file a small, "
               "targeted range instead of the whole file.";
    }

    [[nodiscard]] nlohmann::json parametersSchema() const override;

    [[nodiscard]] nlohmann::json execute(const nlohmann::json& arguments,
                                          const std::string& workspace_root) const override;

private:
    core::SymbolIndexer& indexer_;
};

} // namespace atlas::tools
