#pragma once

#include "raphael/core/background_job.hpp"
#include "raphael/core/config.hpp"
#include "raphael/core/request.hpp"
#include "raphael/models/nim_client.hpp"
#include "raphael/models/registry.hpp"
#include "raphael/core/web_search.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace raphael {

class Assistant {
public:
    explicit Assistant(const Config&);

    void run(const Request&, NimClient::TokenCallback);

    std::vector<BackgroundJob> new_background_results();

    std::optional<BackgroundJob>
    get_background_result(std::uint64_t id) const;

    bool has_active_background_jobs() const;
    
    void elaborate_web_result(
        std::uint64_t id,
        NimClient::TokenCallback on_token
    );

    void note_assistant_turn();

private:
    NimClient lightning_;
    NimClient kimi_;

    std::optional<std::uint64_t> recent_web_result_id_;
    std::size_t recent_web_turns_ = 0;

    ModelRegistry models_;
    BackgroundJobManager jobs_;
    WebSearchClient web_search_;

    nlohmann::json history_ = nlohmann::json::array();

    std::size_t active_background_jobs_ = 0;
};

}
