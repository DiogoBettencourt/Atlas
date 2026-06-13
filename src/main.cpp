#include <iostream>
#include <nlohmann/json.hpp>
#include <asio.hpp>

using json = nlohmann::json;

int main() {
    std::cout << "Starting Atlas AI Workspace..." << std::endl;

    // Verify nlohmann/json integration
    json config = {
        {"name", "Atlas"},
        {"version", "0.1.0"},
        {"local_first", true}
    };
    std::cout << "Configuration loaded:\n" << config.dump(4) << std::endl;

    // Verify Asio integration
    asio::io_context io_context;
    std::cout << "Asio event loop initialized." << std::endl;

    return 0;
}
