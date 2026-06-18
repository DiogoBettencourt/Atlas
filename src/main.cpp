#include "atlas/core/Application.hpp"
#include "atlas/agent/LLMClient.hpp" // Ensure this matches your actual include path
#include "atlas/agent/Agent.hpp"
#include "atlas/api/APIServer.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        // 1. Initialize the Core Workspace (Storage, Tools, Paths)
        atlas::core::Application app(argc, argv);

        // 2. Initialize the AI Brain
        atlas::agent::LLMClient llm_client;

        // We pass the ToolManager from the Application directly to the Agent
        atlas::agent::Agent agent(llm_client, app.getToolManager(), "qwen2.5-coder:7b");

        // 3. Initialize and start the API Server
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
