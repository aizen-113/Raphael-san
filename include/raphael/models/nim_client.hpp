#pragma once

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <string_view>

namespace raphael {

class NimClient {
public:
    using TokenCallback = std::function<void(std::string_view)>;

    struct StreamResult {
        bool tool_called = false;
        std::string tool_name;
        std::string tool_args;
        std::string reasoning;
        std::string content;
        std::string tool_id;
    };

    explicit NimClient(std::string api_key);
    ~NimClient();

    StreamResult stream(
        std::string_view model,
        std::string_view system,
        const nlohmann::json& history,
        TokenCallback on_token,
        const nlohmann::json& tools = {},
        std::string_view reasoning_effort = {},
        bool no_thinking = false
    );

private:
    CURL* curl_;
    curl_slist* headers_;
};

}
