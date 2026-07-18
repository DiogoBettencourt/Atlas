#include "atlas/core/SessionManager.hpp"
#include <random>
#include <sstream>
#include <iomanip>

namespace atlas::core {

std::string SessionManager::generateUUID() {
    // Simple UUID v4 generator for local session IDs
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    
    return ss.str();
}

std::string SessionManager::createSession() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string new_id = generateUUID();
    
    // Initialize with our autonomous system prompt
    sessions_[new_id] = {
        {
            {"role", "system"},
            {"content", "You are Atlas, an autonomous local AI workspace agent. "
                        "1. When asked to read a file, ALWAYS prioritize using the 'read_file' tool immediately. "
                        "2. DO NOT ask the user for the path if you can infer it. "
                        "3. Be concise, technical, and tool-oriented."}
        }
    };
    return new_id;
}

bool SessionManager::appendMessage(const std::string& session_id, const std::string& role, const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second.push_back({{"role", role}, {"content", content}});
        return true;
    }
    return false;
}

std::optional<std::vector<nlohmann::json>> SessionManager::getSessionHistory(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool SessionManager::deleteSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.erase(session_id) > 0;
}

} // namespace atlas::core