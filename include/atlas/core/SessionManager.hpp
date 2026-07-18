#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>

namespace atlas::core {

class SessionManager {
public:
    SessionManager() = default;

    // Creates a new session and returns its unique ID
    std::string createSession();

    // Appends a message to a specific session
    bool appendMessage(const std::string& session_id, const std::string& role, const std::string& content);

    // Retrieves the full history for a session
    std::optional<std::vector<nlohmann::json>> getSessionHistory(const std::string& session_id);

    // Deletes a session to free up memory
    bool deleteSession(const std::string& session_id);

private:
    // Generates a simple random ID
    std::string generateUUID();

    // Maps session_id to a list of OpenAI-formatted messages
    std::map<std::string, std::vector<nlohmann::json>> sessions_;
    
    // Mutex for thread-safety (essential since the APIServer runs on a background thread)
    std::mutex mutex_;
};

} // namespace atlas::core