#pragma once

#include <string>

namespace raphael {

struct RouteDecision {
    bool escalate = false;
    bool reasoning_needed = false;
    bool coding_needed = false;
    std::string model = "lightning";
    std::string response;
};

}
