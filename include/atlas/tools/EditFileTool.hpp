#pragma once

#include "atlas/tools/Tool.hpp"

namespace atlas::tools {

// Performs a surgical find-and-replace edit within an existing
// workspace-relative file. `old_text` must match exactly once; this
// mirrors the "surgical file reads/writes" philosophy in the Librarian
// architecture, avoiding full-file rewrites for small changes.
class EditFileTool final : public Tool {
public:
    [[nodiscard]] std::string name() const override { return "edit_file"; }

    [[nodiscard]] std::string description() const override {
        return "Replaces an exact, unique block of text ('old_text') with "
               "'new_text' inside an existing file. Fails if old_text is "
               "missing or appears more than once, to avoid ambiguous edits.";
    }

    [[nodiscard]] nlohmann::json parametersSchema() const override;

    [[nodiscard]] nlohmann::json execute(const nlohmann::json& arguments,
                                          const std::string& workspace_root) const override;
};

} // namespace atlas::tools
