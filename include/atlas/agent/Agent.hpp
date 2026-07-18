#pragma once

#include "atlas/agent/LLMClient.hpp"
#include "atlas/tools/ToolManager.hpp"
#include "atlas/core/SessionManager.hpp"
#include <string>

namespace atlas::agent {

class Agent {
public:
    Agent(LLMClient& llm_client, 
          tools::ToolManager& tool_manager, 
          core::SessionManager& session_manager, 
          const std::string& model_name);

    // Main interaction endpoint
    std::string chat(const std::string& message, const std::string& session_id);

    // Required by APIServer to auto-create sessions if none is provided in the request
    core::SessionManager& getSessionManager() { return session_manager_; }

private:
    LLMClient& llm_client_;
    tools::ToolManager& tool_manager_;
    core::SessionManager& session_manager_;
    std::string model_name_;
};

} // namespace atlas::agent