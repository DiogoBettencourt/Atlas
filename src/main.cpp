#include "atlas/core/Application.hpp"
#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    try {
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
