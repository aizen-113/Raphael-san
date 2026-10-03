#pragma once

#include "raphael/core/request.hpp"
#include "raphael/core/task.hpp"
#include "raphael/models/nim_client.hpp"

namespace raphael {

class Router {
public:
    explicit Router(NimClient& model);

    RouteDecision route(const Request& request) const;

private:
    NimClient& model_;
};

}
