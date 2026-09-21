#include "atlas/tools/ReadFileTool.hpp"
#include "atlas/core/WorkspaceManager.hpp"

#include <fstream>
#include <sstream>

namespace atlas::tools {

namespace {
constexpr std::size_t kMaxPreviewBytes = 32 * 1024; // 32KB guard for context budget
}

nlohmann::json ReadFileTool::parametersSchema() const {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Workspace-relative path to the file to read."}
            }}
        }},
        {"required", nlohmann::json::array({"path"})}
    };
}

nlohmann::json ReadFileTool::execute(const nlohmann::json& arguments,
                                      const std::string& workspace_root) const {
    if (!arguments.contains("path") || !arguments["path"].is_string()) {
        return nlohmann::json{{"error", "missing required argument: path"}};
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

        bool truncated = false;
        if (content.size() > kMaxPreviewBytes) {
            content = content.substr(0, kMaxPreviewBytes);
            truncated = true;
        }

        return nlohmann::json{
            {"path", arguments["path"]},
            {"content", content},
            {"truncated", truncated}
        };
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}};
    }
}

} // namespace atlas::tools
