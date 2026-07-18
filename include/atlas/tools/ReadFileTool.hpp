#pragma once
#include "atlas/tools/Tool.hpp"
#include "atlas/core/WorkspaceManager.hpp" // Add this include
#include <filesystem>

namespace atlas::tools {

class ReadFileTool : public Tool {
public:
    // Change the constructor to take the manager by reference
    explicit ReadFileTool(core::WorkspaceManager& workspace_manager);
    
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    std::string execute(const nlohmann::json& arguments) override;

private:
    core::WorkspaceManager& workspace_manager_; // Store the reference
};

} // namespace atlas::tools