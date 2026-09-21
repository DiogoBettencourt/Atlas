#pragma once

#include "atlas/tools/Tool.hpp"

namespace atlas::tools {

// Creates or overwrites a workspace-relative file with the given content.
// Parent directories are created automatically. Sandboxed via
// WorkspaceManager::resolveSafe.
class WriteFileTool final : public Tool {
public:
    [[nodiscard]] std::string name() const override { return "write_file"; }

    [[nodiscard]] std::string description() const override {
        return "Creates a new file or fully overwrites an existing one within "
               "the active workspace. Prefer edit_file for surgical changes "
               "to an existing file.";
    }

    [[nodiscard]] nlohmann::json parametersSchema() const override;

    [[nodiscard]] nlohmann::json execute(const nlohmann::json& arguments,
                                          const std::string& workspace_root) const override;
};

} // namespace atlas::tools
