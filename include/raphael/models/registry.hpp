#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace raphael {

class ModelRegistry {
public:
    ModelRegistry();

    std::string_view id(std::string_view name) const;
    bool contains(std::string_view name) const;

private:
    std::unordered_map<std::string, std::string> models_;
};

}
