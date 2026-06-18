#include "atlas/core/Application.hpp"
#include "atlas/agent/LLMClient.hpp"
#include "atlas/agent/Agent.hpp"
#include <iostream>
#include <string>
#include <exception>

void runInteractiveTerminal(atlas::core::Application& app) {
    std::cout << "\n============================================\n";
    std::cout << " Atlas Interactive AI Workspace [v0.1]\n";
    std::cout << " Type 'exit' to quit.\n";
    std::cout << "============================================\n";

    // Initialize our HTTP client and our Agent
    atlas::agent::LLMClient llm("localhost", 11434);

    // Note: Change "llama3" here if you downloaded a different model like "qwen2.5-coder:3b"
    atlas::agent::Agent agent(llm, app.getToolManager(), "qwen2.5-coder:3b");

    std::string user_input;
    while (true) {
        std::cout << "\n> You: ";
        std::getline(std::cin, user_input);

        if (user_input == "exit" || user_input == "quit") {
            break;
        }
        if (user_input.empty()) continue;

        std::cout << "  Atlas: (Thinking...)\r";

        try {
            // This triggers the autonomous loop!
            std::string response = agent.chat(user_input);
            std::cout << "  Atlas: " << response << "\n";
        } catch (const std::exception& e) {
            std::cerr << "\n  [!] Error: " << e.what() << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        atlas::core::Application app(argc, argv);

        // Start the interactive chat interface
        runInteractiveTerminal(app);

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
