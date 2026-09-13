#pragma once

#include "atlas/tools/Tool.hpp"
#include "atlas/core/WorkspaceManager.hpp"

namespace atlas::tools {

class EditFileTool : public Tool {
public:
    explicit EditFileTool(core::WorkspaceManager& workspace_manager);

    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    std::string execute(const nlohmann::json& arguments) override;

private:
    core::WorkspaceManager& workspace_manager_;
};

} // namespace atlas::tools