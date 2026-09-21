#pragma once

#include "atlas/tools/Tool.hpp"

namespace atlas::tools {

// Lists the immediate contents (one level, non-recursive) of a
// workspace-relative directory. Complements search_symbol: search_symbol
// finds *code symbols* by name, but has no way to answer "what files exist
// here" - project config, docs, empty files, or anything in a language the
// regex patterns don't recognize are all invisible to it. Without this
// tool an agent exploring an unfamiliar workspace (in particular the
// self-improvement workspace, where it needs to find README.md,
// CMakeLists.txt, etc.) has no way to discover the repo's layout at all.
class ListDirectoryTool final : public Tool {
public:
    [[nodiscard]] std::string name() const override { return "list_directory"; }

    [[nodiscard]] std::string description() const override {
        return "Lists the immediate files and subdirectories (one level deep, "
               "not recursive) of a directory within the active workspace. "
               "Use this to discover what exists before read_file/search_symbol; "
               "call it again with a subdirectory's path to go deeper.";
    }

    [[nodiscard]] nlohmann::json parametersSchema() const override;

    [[nodiscard]] nlohmann::json execute(const nlohmann::json& arguments,
                                          const std::string& workspace_root) const override;
};

} // namespace atlas::tools
