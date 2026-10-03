#include "raphael/core/config.hpp"

#include <cstdlib>
#include <stdexcept>

namespace raphael {

Config Config::from_env() {
    const char* key = std::getenv("NVIDIA_API_KEY");

    if (!key || !*key)
        throw std::runtime_error("NVIDIA_API_KEY is not set");

    return {key};
}

}
