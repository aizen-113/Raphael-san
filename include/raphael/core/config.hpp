#pragma once

#include <string>

namespace raphael {

struct Config {
    std::string nvidia_api_key;

    static Config from_env();
};

}
