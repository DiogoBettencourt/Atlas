#include "atlas/tools/EditFileTool.hpp"
#include <fstream>
#include <sstream>

namespace atlas::tools {

EditFileTool::EditFileTool(core::WorkspaceManager& workspace_manager)
    : workspace_manager_(workspace_manager) {}

std::string EditFileTool::name() const {
    return "edit_file";
}

std::string EditFileTool::description() const {
    return "Replaces a specific block of text in an existing file. Exact string matching is used for 'search_string'.";
}

nlohmann::json EditFileTool::parametersSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Relative path to the file."}
            }},
            {"search_string", {
                {"type", "string"},
                {"description", "The exact block of code to find and replace. Must match the file exactly."}
            }},
            {"replace_string", {
                {"type", "string"},
                {"description", "The new code that will replace the search_string."}
            }}
        }},
        {"required", nlohmann::json::array({"path", "search_string", "replace_string"})}
    };
}

std::string EditFileTool::execute(const nlohmann::json& arguments) {
    if (!arguments.contains("path") || !arguments.contains("search_string") || !arguments.contains("replace_string")) {
        return "Error: Missing parameters.";
    }

    try {
        auto safe_path = workspace_manager_.resolveSafePath("AtlasCore", arguments["path"]);

        std::ifstream in_file(safe_path);
        if (!in_file.is_open()) return "Error: Could not read file.";
        
        std::stringstream buffer;
        buffer << in_file.rdbuf();
        std::string content = buffer.str();
        in_file.close();

        std::string search_str = arguments["search_string"];
        std::string replace_str = arguments["replace_string"];

        size_t pos = content.find(search_str);
        if (pos == std::string::npos) {
            return "Error: search_string not found in file. Ensure exact matching (including whitespace).";
        }

        content.replace(pos, search_str.length(), replace_str);

        std::ofstream out_file(safe_path, std::ios::trunc);
        out_file << content;
        
        return "Success: File updated.";
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

} // namespace atlas::tools