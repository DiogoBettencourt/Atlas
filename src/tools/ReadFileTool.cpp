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
    if (!arguments.contains("filepath") || !arguments["filepath"].is_string()) {
        return "Error: Missing or invalid 'filepath' argument.";
    }

    std::string filepath_str = arguments["filepath"].get<std::string>();

    // Remove potential stray quotes or whitespace often injected by LLM leakage
    filepath_str.erase(std::remove(filepath_str.begin(), filepath_str.end(), '\"'), filepath_str.end());
    filepath_str.erase(std::remove(filepath_str.begin(), filepath_str.end(), '\''), filepath_str.end());

    // Trim leading/trailing whitespace
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    };
    trim(filepath_str);

    std::filesystem::path target_path(filepath_str);

    // Force root lock logic as discussed...
    std::filesystem::path full_path = target_path.is_absolute()
                                     ? target_path
                                     : base_directory_ / target_path;

    full_path = std::filesystem::weakly_canonical(full_path);

    std::cout << "\n   [DEBUG: Final resolved path: " << full_path << "]\n";

    if (!std::filesystem::exists(full_path)) {
        return "Error: File does not exist at: " + full_path.string();
    }

    // Read and return the file contents
        std::ifstream file(full_path);
        if (!file.is_open()) {
            // DIAGNOSTIC: Check if the file is locked or if there's another issue
            int err = errno;
            return "Error: Could not open file! errno: " + std::to_string(err) +
                   " (Permission denied: " + (err == EACCES ? "Yes" : "No") + ")";
        }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace atlas::tools
