#include "atlas/tools/WriteFileTool.hpp"
#include "atlas/core/WorkspaceManager.hpp"

#include <fstream>

namespace atlas::tools {

namespace fs = std::filesystem;

nlohmann::json WriteFileTool::parametersSchema() const {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Workspace-relative path to write."}
            }},
            {"content", {
                {"type", "string"},
                {"description", "Full text content to write to the file."}
            }}
        }},
        {"required", nlohmann::json::array({"path", "content"})}
    };
}

nlohmann::json WriteFileTool::execute(const nlohmann::json& arguments,
                                       const std::string& workspace_root) const {
    if (!arguments.contains("path") || !arguments["path"].is_string() ||
        !arguments.contains("content") || !arguments["content"].is_string()) {
        return nlohmann::json{{"error", "missing required arguments: path, content"}};
    }

    try {
        auto resolved = core::WorkspaceManager::resolveSafe(workspace_root, arguments["path"]);

        std::error_code ec;
        fs::create_directories(resolved.parent_path(), ec);

        std::ofstream out(resolved, std::ios::binary | std::ios::trunc);
        if (!out) {
            return nlohmann::json{{"error", "unable to open file for writing"}};
        }
        std::string content = arguments["content"].get<std::string>();
        out << content;
        out.close();

        return nlohmann::json{
            {"path", arguments["path"]},
            {"bytes_written", content.size()},
            {"status", "ok"}
        };
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}};
    }
}

} // namespace atlas::tools
