#pragma once

#include "atlas/tools/Tool.hpp"

namespace atlas::tools {

// Reads the full contents of a workspace-relative file. Enforces the
// WorkspaceManager sandbox and returns a truncated preview for very large
// files to protect the LLM's context budget.
class ReadFileTool final : public Tool {
public:
    [[nodiscard]] std::string name() const override { return "read_file"; }

    [[nodiscard]] std::string description() const override {
        return "Reads the contents of a file within the active workspace. "
               "Use search_symbol first to find the right file/line instead "
               "of reading whole directories blindly.";
    }

    [[nodiscard]] nlohmann::json parametersSchema() const override;

    [[nodiscard]] nlohmann::json execute(const nlohmann::json& arguments,
                                          const std::string& workspace_root) const override;
};

} // namespace atlas::tools
