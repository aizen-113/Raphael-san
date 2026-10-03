#pragma once

#include "raphael/core/background_job.hpp"
#include "raphael/core/config.hpp"
#include "raphael/core/request.hpp"
#include "raphael/models/nim_client.hpp"
#include "raphael/models/registry.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace raphael {

class Assistant {
public:
    explicit Assistant(const Config& config);

    void run(
        const Request& request,
        NimClient::TokenCallback on_token
    );

    std::vector<BackgroundJob> new_background_results();
    std::optional<BackgroundJob> get_background_result(
        std::uint64_t id
    ) const;

private:
    NimClient lightning_;
    NimClient kimi_;

    ModelRegistry models_;
    BackgroundJobManager jobs_;

    nlohmann::json history_ = nlohmann::json::array();
};

}
