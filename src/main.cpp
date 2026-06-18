#include "atlas/core/Application.hpp"
#include "atlas/agent/LLMClient.hpp"
#include <iostream>
#include <exception>

void testLLM() {
    std::cout << "\n--- Running Local LLM Test ---\n";

    // Connect to the default Ollama port
    atlas::agent::LLMClient llm("localhost", 11434);

    // Create the conversation history JSON
    nlohmann::json messages = nlohmann::json::array({
        {{"role", "user"}, {"content", "Hello Atlas! Respond with a short, 1-sentence greeting telling me you are online."}}
    });

    std::cout << "[+] Sending HTTP POST request to local Ollama instance...\n";
    std::cout << "[+] Waiting for AI to think (this might take a few seconds)...\n";

    try {
        // IMPORTANT: If you downloaded a different model (like phi3 or llama3.2), change the name here!
        nlohmann::json response = llm.generateChatResponse("qwen2.5-coder:7b", messages);

        // Extract the actual text response from the JSON
        if (response.contains("message") && response["message"].contains("content")) {
            std::string ai_text = response["message"]["content"].get<std::string>();
            std::cout << "\n[+] SUCCESS! Atlas says:\n\n";
            std::cout << "    \"" << ai_text << "\"\n\n";
        } else {
            std::cout << "[-] Received a response, but it didn't contain a standard message structure.\n";
            std::cout << response.dump(4) << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "\n[-] LLM Connection Failed: " << e.what() << "\n";
        std::cerr << "[-] Make sure Ollama is running and you have pulled the 'llama3' model!\n";
    }

    std::cout << "--------------------------------\n\n";
}

int main(int argc, char* argv[]) {
    try {
        // Run the AI connection test
        testLLM();

        atlas::core::Application app(argc, argv);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
