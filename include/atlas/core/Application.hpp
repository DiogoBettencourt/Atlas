#pragma once

#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace atlas::core {

/// Central application controller. Owns the event loop, config and signal handling.
/// Non-copyable/non-movable to keep io_context pinned to one location.
class Application {
public:
    /// Parse CLI args, load config, register signal handlers.
    Application(int argc, char* argv[]);

    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    /// Run the event loop. Blocks until stop() is called.
    void run();

    /// Gracefully stop the event loop.
    void stop();

private:
    void loadConfiguration();
    void setupSignalHandling();

    asio::io_context io_context_;
    nlohmann::json config_;
    asio::signal_set signals_;
};

} // namespace atlas::core
