#pragma once

#include "atlas/tools/Tool.hpp"
#include "atlas/core/SymbolIndexer.hpp"

namespace atlas::tools {

class SearchSymbolTool : public Tool {
public:
    explicit SearchSymbolTool(core::SymbolIndexer& indexer);

    std::string name() const override;
    std::string description() const override;
    nlohmann::json parametersSchema() const override;
    std::string execute(const nlohmann::json& arguments) override;

private:
    core::SymbolIndexer& indexer_;
};

} // namespace atlas::tools