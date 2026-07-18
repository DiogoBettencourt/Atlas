#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace atlas::agent {

class LLMClient {
public:
    LLMClient();
    ~LLMClient();

    // The updated method signature our Agent is looking for
    std::string generateResponse(const std::vector<nlohmann::json>& messages, 
                                 const std::string& model_name,
                                 const nlohmann::json& tools = nlohmann::json::array());
};

} // namespace atlas::agent