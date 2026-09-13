#include "atlas/tools/WriteFileTool.hpp"
#include <fstream>
#include <iostream>

namespace atlas::tools {

WriteFileTool::WriteFileTool(core::WorkspaceManager& workspace_manager)
    : workspace_manager_(workspace_manager) {}

std::string WriteFileTool::name() const {
    return "write_file";
}

std::string WriteFileTool::description() const {
    return "Creates a new file or completely overwrites an existing file with new content. Use this to write full files.";
}

nlohmann::json WriteFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "The relative path to the file (e.g., 'src/main.cpp')."}
            }},
            {"content", {
                {"type", "string"},
                {"description", "The complete source code or text to write into the file."}
            }}
        }},
        {"required", nlohmann::json::array({"path", "content"})}
    };
}

std::string WriteFileTool::execute(const nlohmann::json& arguments) {
    if (!arguments.contains("path") || !arguments["content"].is_string()) {
        return "Error: Missing 'path' or 'content'.";
    }

    std::string path_str = arguments["path"];
    std::string content = arguments["content"];

    try {
        // Enforce sandboxing: Hardcoded to "AtlasCore" workspace for now
        auto safe_path = workspace_manager_.resolveSafePath("AtlasCore", path_str);

        // Create parent directories if they don't exist
        if (safe_path.has_parent_path()) {
            std::filesystem::create_directories(safe_path.parent_path());
        }

        std::ofstream file(safe_path, std::ios::trunc);
        if (!file.is_open()) {
            return "Error: Could not open file for writing at " + safe_path.string();
        }

        file << content;
        return "Success: File written to " + safe_path.string();

    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

} // namespace atlas::tools