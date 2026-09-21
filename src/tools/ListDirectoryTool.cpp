#include "atlas/tools/ListDirectoryTool.hpp"
#include "atlas/core/WorkspaceManager.hpp"

#include <algorithm>
#include <vector>

namespace atlas::tools {

namespace fs = std::filesystem;

nlohmann::json ListDirectoryTool::parametersSchema() const {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description",
                 "Workspace-relative directory to list. Omit or use \".\" for the "
                 "workspace root."}
            }}
        }},
        {"required", nlohmann::json::array()}
    };
}

nlohmann::json ListDirectoryTool::execute(const nlohmann::json& arguments,
                                           const std::string& workspace_root) const {
    std::string requested_path = arguments.value("path", std::string("."));

    try {
        auto resolved = core::WorkspaceManager::resolveSafe(workspace_root, requested_path);

        if (!fs::exists(resolved)) {
            return nlohmann::json{{"error", "directory not found: " + requested_path}};
        }
        if (!fs::is_directory(resolved)) {
            return nlohmann::json{{"error", requested_path + " is not a directory"}};
        }

        struct Entry {
            std::string name;
            std::string type;
            std::uintmax_t size = 0;
        };
        std::vector<Entry> entries;

        std::error_code ec;
        for (const auto& child : fs::directory_iterator(
                 resolved, fs::directory_options::skip_permission_denied, ec)) {
            Entry entry;
            entry.name = child.path().filename().string();
            if (child.is_directory()) {
                entry.type = "directory";
            } else if (child.is_regular_file()) {
                entry.type = "file";
                std::error_code size_ec;
                entry.size = fs::file_size(child.path(), size_ec);
            } else {
                entry.type = "other";
            }
            entries.push_back(std::move(entry));
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.type != b.type) return a.type == "directory"; // directories first
            return a.name < b.name;
        });

        nlohmann::json result_entries = nlohmann::json::array();
        for (const auto& entry : entries) {
            nlohmann::json e{{"name", entry.name}, {"type", entry.type}};
            if (entry.type == "file") {
                e["size"] = entry.size;
            }
            result_entries.push_back(std::move(e));
        }

        return nlohmann::json{
            {"path", requested_path},
            {"entry_count", result_entries.size()},
            {"entries", result_entries}
        };
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", e.what()}};
    }
}

} // namespace atlas::tools
