#include "atlas/core/Application.hpp"
#include "atlas/agent/LLMClient.hpp" // Ensure this matches your actual include path
#include "atlas/agent/Agent.hpp"
#include "atlas/api/APIServer.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        // 1. Initialize the Core Application
        atlas::core::Application app(argc, argv);

        // 2. Initialize the LLM Client
        atlas::agent::LLMClient llm_client;

        // 3. Initialize the Agent with the new constructor signature
        // We now pass: llm_client, tool_manager, session_manager, model_name
        atlas::agent::Agent agent(
            llm_client, 
            app.getToolManager(), 
            app.getSessionManager(), // <--- THIS IS THE MISSING ARGUMENT
            "gemma4:e4b"
        );

        // 4. Initialize and start the API Server
        atlas::api::APIServer api_server(agent, "127.0.0.1", 8080);
        api_server.start();

        // 4. Run the main application loop (This blocks until you press Ctrl+C)
        app.run();

        // 5. Cleanup when Ctrl+C is pressed
        api_server.stop();

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
