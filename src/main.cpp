#include "atlas/core/Application.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    // Handled before constructing Application, which would otherwise start
    // the server and create data directories just to report a version.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            std::cout << "Atlas " << ATLAS_VERSION << std::endl;
            return 0;
        }
    }

    try {
        atlas::core::Application app(argc, argv);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
