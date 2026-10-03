#include "raphael/models/registry.hpp"

#include <stdexcept>

namespace raphael {

ModelRegistry::ModelRegistry() {
    models_ = {
        {"lightning", "nvidia/nemotron-3.5-lightning-30b-a3b"},
        {"kimi",      "moonshotai/kimi-k3"}
    };
}

std::string_view ModelRegistry::id(std::string_view name) const {
    auto it = models_.find(std::string(name));

    if (it == models_.end())
        throw std::runtime_error("Unknown model: " + std::string(name));

    return it->second;
}

bool ModelRegistry::contains(std::string_view name) const {
    return models_.contains(std::string(name));
}

}
