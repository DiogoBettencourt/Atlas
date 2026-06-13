#include "atlas/core/Application.hpp"
#include "atlas/storage/FileStorageManager.hpp"
#include <iostream>
#include <exception>

void testStorage() {
    std::cout << "\n--- Running Storage Test ---\n";

    // Create a storage manager that saves files to a "local_workspaces" folder
    atlas::storage::FileStorageManager storage("local_workspaces");

    if (storage.initialize()) {
        std::cout << "[+] Storage initialized successfully.\n";

        // Create some dummy JSON data
        nlohmann::json test_data = {
            {"name", "Test Workspace"},
            {"description", "A temporary workspace for testing the storage layer."},
            {"created_at", "2026-06-13"}
        };

        std::string ws_id = "test_ws_001";

        // Test Saving
        if (storage.saveWorkspace(ws_id, test_data)) {
            std::cout << "[+] Workspace saved successfully.\n";
        }

        // Test Loading
        auto loaded_data = storage.loadWorkspace(ws_id);
        if (loaded_data) {
            std::cout << "[+] Workspace loaded successfully. Content:\n"
                      << loaded_data->dump(4) << "\n";
        } else {
            std::cerr << "[-] Failed to load workspace.\n";
        }

        // Test Listing
        auto workspaces = storage.listWorkspaces();
        std::cout << "[+] Found " << workspaces.size() << " workspace(s): ";
        for (const auto& ws : workspaces) {
            std::cout << ws << " ";
        }
        std::cout << "\n";
    } else {
        std::cerr << "[-] Failed to initialize storage.\n";
    }
    std::cout << "----------------------------\n\n";
}

int main(int argc, char* argv[]) {
    try {
        // 1. Run our quick storage test
        testStorage();

        // 2. Start the core application lifecycle
        atlas::core::Application app(argc, argv);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred." << std::endl;
        return 1;
    }

    return 0;
}
