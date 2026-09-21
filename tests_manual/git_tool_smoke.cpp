#include "atlas/tools/GitTool.hpp"
#include "atlas/tools/GitHubPRTool.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

int main() {
    namespace fs = std::filesystem;
    fs::path repo = "/tmp/fake_repo";
    fs::path wrong_workspace = "/tmp/not_the_repo";
    fs::create_directories(wrong_workspace);

    atlas::tools::GitTool git(repo);

    std::cout << "== wrong workspace should be refused ==\n";
    std::cout << git.execute({{"action", "status"}}, wrong_workspace.string()).dump(2) << "\n\n";

    std::cout << "== correct workspace: status ==\n";
    std::cout << git.execute({{"action", "status"}}, repo.string()).dump(2) << "\n\n";

    std::cout << "== attempt checkout_branch main (must be refused) ==\n";
    std::cout << git.execute({{"action", "checkout_branch"}, {"branch", "main"}}, repo.string()).dump(2) << "\n\n";

    std::cout << "== checkout_branch feature/atlas-test (should create it) ==\n";
    std::cout << git.execute({{"action", "checkout_branch"}, {"branch", "feature/atlas-test"}}, repo.string()).dump(2) << "\n\n";

    {
        std::ofstream f(repo / "new_file.txt");
        f << "written by the agent\n";
    }

    std::cout << "== add ==\n";
    std::cout << git.execute({{"action", "add"}}, repo.string()).dump(2) << "\n\n";

    std::cout << "== commit ==\n";
    std::cout << git.execute({{"action", "commit"}, {"message", "test: add new_file.txt"}}, repo.string()).dump(2) << "\n\n";

    std::cout << "== push (expected to fail: no 'origin' remote configured) ==\n";
    std::cout << git.execute({{"action", "push"}}, repo.string()).dump(2) << "\n\n";

    std::cout << "== attempt push while on main (must be refused before even trying) ==\n";
    (void)git.execute({{"action", "checkout_branch"}, {"branch", "main"}}, repo.string());
    std::cout << git.execute({{"action", "push"}}, repo.string()).dump(2) << "\n\n";

    std::cout << "== GitHubPRTool: disabled (no token) ==\n";
    atlas::tools::GitHubPRTool pr(repo, "example-owner/example-repo");
    std::cout << pr.execute({{"title", "test"}, {"head", "feature/atlas-test"}}, repo.string()).dump(2) << "\n\n";

    std::cout << "== GitHubPRTool: wrong workspace refused ==\n";
    std::cout << pr.execute({{"title", "test"}, {"head", "feature/atlas-test"}}, wrong_workspace.string()).dump(2) << "\n";

    return 0;
}
