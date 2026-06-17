#include "atlas/tools/ReadFileTool.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

namespace atlas::tools {

ReadFileTool::ReadFileTool(const std::filesystem::path& base_directory)
    : base_directory_(base_directory) {}

std::string ReadFileTool::name() const {
    return "read_file";
}

std::string ReadFileTool::description() const {
    return "Reads the complete text content of a local file. Useful for inspecting code, logs, or configuration files.";
}

nlohmann::json ReadFileTool::parametersSchema() const {
    // This JSON Schema will be sent directly to the LLM
    return {
        {"type", "object"},
        {"properties", {
            {"filepath", {
                {"type", "string"},
                {"description", "The relative or absolute path to the file you want to read."}
            }}
        }},
        {"required", {"filepath"}}
    };
}

std::string ReadFileTool::execute(const nlohmann::json& arguments) {
    // 1. Validate that the LLM provided the correct arguments
    if (!arguments.contains("filepath") || !arguments["filepath"].is_string()) {
        return "Error: Missing or invalid 'filepath' argument. Expected a string.";
    }

    std::string filepath_str = arguments["filepath"].get<std::string>();
    std::filesystem::path target_path(filepath_str);

    // 2. Resolve relative paths against our base directory
    if (target_path.is_relative()) {
        target_path = base_directory_ / target_path;
    }

    // Note: In a production server, we would add strict path sanitization here
    // to prevent directory traversal attacks (e.g., reading "../../../etc/shadow").
    // For a local-first workspace, this is acceptable for Version 1.

    // 3. Verify the file exists and is readable
    if (!std::filesystem::exists(target_path)) {
        return "Error: File does not exist at path: " + target_path.string();
    }
    if (!std::filesystem::is_regular_file(target_path)) {
        return "Error: Path exists but is not a regular text file: " + target_path.string();
    }

    // 4. Read and return the file contents
    std::ifstream file(target_path);
    if (!file.is_open()) {
        return "Error: Could not open file for reading (permission denied?): " + target_path.string();
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace atlas::tools
