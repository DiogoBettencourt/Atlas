#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace atlas::models {

// 1. Define strictly typed Roles for AI interaction
enum class Role {
    System,
    User,
    Assistant,
    Tool
};

// nlohmann macro to safely serialize enums to lowercase string values
NLOHMANN_JSON_SERIALIZE_ENUM(Role, {
    {Role::System, "system"},
    {Role::User, "user"},
    {Role::Assistant, "assistant"},
    {Role::Tool, "tool"}
})

// 2. A single message in a conversation
struct Message {
    std::string id;
    Role role;
    std::string content;
    std::string timestamp; // ISO 8601 format string
};
// Auto-generates to_json and from_json functions
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Message, id, role, content, timestamp)

// 3. A conversation containing multiple messages
struct Conversation {
    std::string id;
    std::string title;
    std::vector<Message> messages;
    std::string created_at;
    std::string updated_at;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Conversation, id, title, messages, created_at, updated_at)

// 4. Configuration specific to a single workspace
struct WorkspaceSettings {
    std::string default_model = "llama3:latest";
    std::string system_prompt = "You are a helpful AI assistant.";
    double temperature = 0.7;
};
// WITH_DEFAULT handles missing JSON fields by falling back to the C++ defaults above
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(WorkspaceSettings, default_model, system_prompt, temperature)

// 5. The Root Workspace Model
struct Workspace {
    std::string id;
    std::string name;
    std::string description;
    WorkspaceSettings settings;
    std::vector<Conversation> conversations;
    std::string created_at;
    std::string updated_at;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Workspace, id, name, description, settings, conversations, created_at, updated_at)

} // namespace atlas::models
