#include "atlas/core/Application.hpp"
#include "atlas/tools/ReadFileTool.hpp"
#include <iostream>
#include <exception>

void testTools() {
    std::cout << "\n--- Running Tool Engine Test ---\n";

    // Initialize the tool (it will use the current build directory as the base)
    atlas::tools::ReadFileTool read_tool(".."); // ".." goes up from build/ to the root Atlas folder

    std::cout << "[+] Tool Name: " << read_tool.name() << "\n";
    std::cout << "[+] Tool Schema:\n" << read_tool.parametersSchema().dump(2) << "\n\n";

    // Simulate an LLM calling the tool with JSON arguments
    nlohmann::json llm_arguments = {
        {"filepath", "CMakeLists.txt"}
    };

    std::cout << "[+] Simulating Execution...\n";
    std::string observation = read_tool.execute(llm_arguments);

    // Print the first 100 characters of the file to prove it worked
    std::cout << "[+] Result (First 100 chars):\n" << observation.substr(0, 100) << "...\n";
    std::cout << "--------------------------------\n\n";
}

int main(int argc, char* argv[]) {
    try {
        testTools();

        atlas::core::Application app(argc, argv);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
