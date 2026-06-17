#pragma once

#include "atlas/tools/Tool.hpp"
#include <filesystem>

namespace atlas::tools {

class ReadFileTool : public Tool {
public:
    // We pass a base directory so the tool knows where to look for relative paths.
    // By default, it will use the current working directory.
    explicit ReadFileTool(const std::filesystem::path& base_directory = std::filesystem::current_path());
    ~ReadFileTool() override = default;

    // Delete copy/move semantics for tool singletons
    ReadFileTool(const ReadFileTool&) = delete;
    ReadFileTool& operator=(const ReadFileTool&) = delete;

    // Implementing the Tool interface
    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    std::string execute(const nlohmann::json& arguments) override;

private:
    std::filesystem::path base_directory_;
};

} // namespace atlas::tools
