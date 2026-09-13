#include "atlas/tools/EditFileTool.hpp"
#include "atlas/core/WorkspaceManager.hpp"

#include <fstream>
#include <sstream>

namespace atlas::tools {

nlohmann::json EditFileTool::parametersSchema() const {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Workspace-relative path to the file to edit."}
            }},
            {"old_text", {
                {"type", "string"},
                {"description", "Exact text to find. Must appear exactly once in the file."}
            }},
            {"new_text", {
                {"type", "string"},
                {"description", "Text to replace old_text with."}
            }}
        }},
        {"required", nlohmann::json::array({"path", "old_text", "new_text"})}
    };
}

nlohmann::json EditFileTool::execute(const nlohmann::json& arguments,
                                      const std::string& workspace_root) const {
    if (!arguments.contains("path") || !arguments.contains("old_text") ||
        !arguments.contains("new_text")) {
        return nlohmann::json{{"error", "missing required arguments: path, old_text, new_text"}};
    }

    try {
        auto resolved = core::WorkspaceManager::resolveSafe(workspace_root, arguments["path"]);

        std::ifstream in(resolved, std::ios::binary);
        if (!in) {
            return nlohmann::json{{"error", "file not found: " + arguments["path"].get<std::string>()}};
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        std::string content = buffer.str();
        in.close();

        std::string old_text = arguments["old_text"].get<std::string>();
        std::string new_text = arguments["new_text"].get<std::string>();

        std::size_t first = content.find(old_text);
        if (first == std::string::npos) {
            return nlohmann::json{{"error", "old_text not found in file"}};
        }
        std::size_t second = content.find(old_text, first + 1);
        if (second != std::string::npos) {
            return nlohmann::json{{"error", "old_text is not unique in file (matches multiple locations)"}};
        }

        content.replace(first, old_text.size(), new_text);

        std::ofstream out(resolved, std::ios::binary | std::ios::trunc);
        if (!out) {
            return nlohmann::json{{"error", "unable to open file for writing"}};
        }
        out << content;
        out.close();

        return nlohmann::json{
            {"path", arguments["path"]},
            {"status", "ok"}
        };
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}};
    }
}

} // namespace atlas::tools
