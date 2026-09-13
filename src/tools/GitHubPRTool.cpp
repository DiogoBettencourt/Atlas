#include "atlas/tools/GitHubPRTool.hpp"

#include <cstdlib>
#include <httplib.h>
#include <regex>

namespace atlas::tools {

namespace fs = std::filesystem;

namespace {
bool isValidBranchName(const std::string& name) {
    static const std::regex valid(R"(^[A-Za-z0-9][A-Za-z0-9._/-]*$)");
    if (name.empty() || name.size() > 200) return false;
    if (name.find("..") != std::string::npos) return false;
    return std::regex_match(name, valid);
}
} // namespace

GitHubPRTool::GitHubPRTool(fs::path allowed_repo_root, std::string github_repo)
    : allowed_repo_root_(std::move(allowed_repo_root)), github_repo_(std::move(github_repo)) {
    if (!allowed_repo_root_.empty()) {
        std::error_code ec;
        allowed_repo_root_ = fs::weakly_canonical(allowed_repo_root_, ec);
    }
}

bool GitHubPRTool::isAllowedRepo(const std::string& workspace_root) const {
    if (allowed_repo_root_.empty()) return false;
    std::error_code ec;
    fs::path requested = fs::weakly_canonical(fs::path(workspace_root), ec);
    return !ec && requested == allowed_repo_root_;
}

nlohmann::json GitHubPRTool::parametersSchema() const {
    return nlohmann::json{
        {"type", "object"},
        {"properties", {
            {"title", {{"type", "string"}, {"description", "Pull request title."}}},
            {"body", {{"type", "string"}, {"description", "Pull request description (markdown)."}}},
            {"head", {{"type", "string"}, {"description", "Feature branch containing your changes (already pushed)."}}},
            {"base", {{"type", "string"}, {"description", "Branch to merge into. Defaults to 'main'."}}}
        }},
        {"required", nlohmann::json::array({"title", "head"})}
    };
}

nlohmann::json GitHubPRTool::execute(const nlohmann::json& arguments,
                                      const std::string& workspace_root) const {
    if (allowed_repo_root_.empty() || github_repo_.empty()) {
        return nlohmann::json{{"error",
            "github_pr tool is disabled: start Atlas with --self-repo=<path> and "
            "--github-repo=<owner/name> to enable it"}};
    }
    if (!isAllowedRepo(workspace_root)) {
        return nlohmann::json{{"error",
            "github_pr tool refused: this workspace is not the configured self-repo"}};
    }
    if (!arguments.contains("title") || !arguments["title"].is_string() ||
        !arguments.contains("head") || !arguments["head"].is_string()) {
        return nlohmann::json{{"error", "missing required arguments: title, head"}};
    }

    std::string head = arguments["head"].get<std::string>();
    std::string base = arguments.value("base", std::string("main"));
    if (!isValidBranchName(head) || !isValidBranchName(base)) {
        return nlohmann::json{{"error", "invalid head/base branch name"}};
    }
    if (head == base) {
        return nlohmann::json{{"error", "head and base branches must differ"}};
    }

    const char* token_env = std::getenv("ATLAS_GITHUB_TOKEN");
    if (token_env == nullptr || std::string(token_env).empty()) {
        return nlohmann::json{{"error",
            "ATLAS_GITHUB_TOKEN environment variable is not set; cannot authenticate to GitHub"}};
    }

    nlohmann::json payload{
        {"title", arguments["title"]},
        {"head", head},
        {"base", base},
        {"body", arguments.value("body", std::string{})}
    };

    httplib::SSLClient client("api.github.com", 443);
    client.set_connection_timeout(10, 0);
    client.set_read_timeout(30, 0);

    httplib::Headers headers = {
        {"Authorization", std::string("Bearer ") + token_env},
        {"Accept", "application/vnd.github+json"},
        {"User-Agent", "Atlas-Agent/0.1"},
        {"X-GitHub-Api-Version", "2022-11-28"}
    };

    std::string path = "/repos/" + github_repo_ + "/pulls";
    auto response = client.Post(path, headers, payload.dump(), "application/json");

    if (!response) {
        return nlohmann::json{{"error", "failed to reach api.github.com"}};
    }

    auto parsed = nlohmann::json::parse(response->body, nullptr, false);

    if (response->status == 201 && !parsed.is_discarded()) {
        return nlohmann::json{
            {"status", "created"},
            {"pr_url", parsed.value("html_url", std::string{})},
            {"pr_number", parsed.value("number", 0)}
        };
    }

    std::string message = (!parsed.is_discarded() && parsed.contains("message"))
        ? parsed["message"].get<std::string>()
        : response->body;
    return nlohmann::json{
        {"error", "GitHub API returned HTTP " + std::to_string(response->status) + ": " + message}
    };
}

} // namespace atlas::tools
