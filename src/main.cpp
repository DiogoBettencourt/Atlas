#include "atlas/core/Application.hpp"
#include "atlas/models/Workspace.hpp"
#include <iostream>
#include <exception>

void testModels() {
    std::cout << "\n--- Running Data Models Test ---\n";

    // 1. Create a strongly typed C++ Workspace
    atlas::models::Workspace ws;
    ws.id = "ws_test_001";
    ws.name = "My First Workspace";
    ws.description = "Testing the strictly typed C++ to JSON conversion.";
    ws.created_at = "2026-06-17T20:00:00Z";
    ws.updated_at = "2026-06-17T20:00:00Z";

    // Modify a setting (notice how default_model and system_prompt remain as defaults!)
    ws.settings.temperature = 0.5;

    // 2. Add a conversation
    atlas::models::Conversation conv;
    conv.id = "conv_001";
    conv.title = "Initialization Sequence";
    conv.created_at = "2026-06-17T20:01:00Z";
    conv.updated_at = "2026-06-17T20:01:00Z";

    // 3. Add some messages
    atlas::models::Message msg1;
    msg1.id = "msg_001";
    msg1.role = atlas::models::Role::User; // Type-safe enum!
    msg1.content = "Initialize core systems.";
    msg1.timestamp = "2026-06-17T20:01:05Z";

    atlas::models::Message msg2;
    msg2.id = "msg_002";
    msg2.role = atlas::models::Role::System;
    msg2.content = "Systems initialized. Atlas is ready.";
    msg2.timestamp = "2026-06-17T20:01:10Z";

    conv.messages.push_back(msg1);
    conv.messages.push_back(msg2);

    ws.conversations.push_back(conv);

    // ==========================================
    // THE MAGIC: Convert entire C++ structure to JSON
    // ==========================================
    nlohmann::json j = ws;

    std::cout << "[+] Serialization successful. Resulting JSON:\n";
    std::cout << j.dump(4) << "\n";
    std::cout << "--------------------------------\n\n";
}

int main(int argc, char* argv[]) {
    try {
        // Run our models test
        testModels();

        // Start the core application lifecycle
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
