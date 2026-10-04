#include "raphael/models/nim_client.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using json = nlohmann::json;

namespace raphael {

struct StreamState {
    std::string buffer;
    NimClient::TokenCallback& on_token;

    std::string tool_name;
    std::string tool_args;
    std::string tool_id;
    std::string reasoning;
    std::string content;
    std::string error;
};

static size_t stream_callback(
    char* ptr,
    size_t size,
    size_t count,
    void* data
) {
    auto& state = *static_cast<StreamState*>(data);
    state.buffer.append(ptr, size * count);

    try {
        size_t end;

        while ((end = state.buffer.find('\n')) != std::string::npos) {
            auto line = state.buffer.substr(0, end);
            state.buffer.erase(0, end + 1);

            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (line.rfind("data: ", 0) != 0) {
                if (!line.empty())
                    state.error += line;
                continue;
            }

            auto payload = line.substr(6);

            if (payload == "[DONE]")
                continue;

            auto j = json::parse(payload);

            if (!j.contains("choices") ||
                !j["choices"].is_array() ||
                j["choices"].empty())
                continue;

            const auto& choice = j["choices"][0];

            if (!choice.contains("delta") ||
                !choice["delta"].is_object())
                continue;

            const auto& delta = choice["delta"];

            if (delta.contains("reasoning_content") &&
                delta["reasoning_content"].is_string()) {
                state.reasoning +=
                    delta["reasoning_content"].get<std::string>();
            }

            if (delta.contains("tool_calls") &&
                delta["tool_calls"].is_array()) {

                for (const auto& call : delta["tool_calls"]) {
                    if (!call.is_object() ||
                        !call.contains("function") ||
                        !call["function"].is_object())
                        continue;

                    if (state.tool_name.empty() &&
                        call.contains("id") &&
                        call["id"].is_string()) {

                        state.tool_id = call["id"].get<std::string>();
                    }

                    const auto& function = call["function"];

                    if (function.contains("name") &&
                        function["name"].is_string()) {

                        state.tool_name +=
                            function["name"].get<std::string>();
                    }

                    if (function.contains("arguments") &&
                        function["arguments"].is_string()) {

                        state.tool_args +=
                            function["arguments"].get<std::string>();
                    }
                }

                continue;
            }

            if (delta.contains("content") &&
                delta["content"].is_string()) {

                auto token = delta["content"].get<std::string>();

                state.content += token;
                state.on_token(token);
            }
        }
    } catch (const std::exception& e) {
        state.error = e.what();
        return 0;
    }

    return size * count;
}

NimClient::NimClient(std::string api_key)
    : curl_(curl_easy_init()),
      headers_(nullptr) {

    if (!curl_)
        throw std::runtime_error("Failed to initialize curl");

    headers_ = curl_slist_append(
        headers_,
        ("Authorization: Bearer " + api_key).c_str()
    );

    headers_ = curl_slist_append(
        headers_,
        "Content-Type: application/json"
    );

    headers_ = curl_slist_append(
        headers_,
        "Accept: text/event-stream"
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_URL,
        "https://integrate.api.nvidia.com/v1/chat/completions"
    );

    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 15L);

    // Don't kill genuinely long generations,
    // but abort if the connection completely stalls.
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl_, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl_, CURLOPT_LOW_SPEED_TIME, 120L);

    curl_easy_setopt(
        curl_,
        CURLOPT_HTTP_VERSION,
        CURL_HTTP_VERSION_2TLS
    );

    curl_easy_setopt(curl_, CURLOPT_TCP_KEEPALIVE, 1L);
}

NimClient::~NimClient() {
    curl_slist_free_all(headers_);
    curl_easy_cleanup(curl_);
}

NimClient::StreamResult NimClient::stream(
    std::string_view model,
    std::string_view system,
    const json& history,
    TokenCallback on_token,
    const json& tools,
    std::string_view reasoning_effort,
    bool no_thinking
) {
    // std::cerr << "\n[Model: " << model << "]\n";

    json messages = json::array();

    messages.push_back({
        {"role", "system"},
        {"content", system}
    });

    for (const auto& message : history) {
        if (!message.is_object())
            continue;

        auto copy = message;

        // Kimi needs reasoning history.
        // Other models don't need it.
        if (model != "moonshotai/kimi-k3")
            copy.erase("reasoning_content");

        messages.push_back(std::move(copy));
    }

    json body = {
        {"model", std::string(model)},
        {"messages", messages},
        {"max_tokens", 4096},
        {"temperature", 0.2},
        {"stream", true}
    };

    if (!reasoning_effort.empty())
        body["reasoning_effort"] = std::string(reasoning_effort);

    if (no_thinking) {
        body["chat_template_kwargs"] = {
            {"enable_thinking", false}
        };
    }

    if (!tools.empty()) {
        body["tools"] = tools;
        body["tool_choice"] = "auto";
        body["parallel_tool_calls"] = false;
    }

    auto payload = body.dump();

    StreamState state{{}, on_token, {}, {}, {}, {}, {}};

    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(
        curl_,
        CURLOPT_POSTFIELDS,
        payload.c_str()
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_WRITEFUNCTION,
        stream_callback
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_WRITEDATA,
        &state
    );

    auto result = curl_easy_perform(curl_);

    if (result == CURLE_OPERATION_TIMEDOUT) {
        curl_easy_setopt(curl_, CURLOPT_FRESH_CONNECT, 1L);
        result = curl_easy_perform(curl_);
        curl_easy_setopt(curl_, CURLOPT_FRESH_CONNECT, 0L);
    }    

    long status = 0;
    curl_easy_getinfo(
        curl_,
        CURLINFO_RESPONSE_CODE,
        &status
    );

    if (!state.error.empty())
        throw std::runtime_error(state.error);

    if (result != CURLE_OK)
        throw std::runtime_error(
            curl_easy_strerror(result)
        );

    if (status >= 400) {
        std::string error =
            "NIM HTTP error: " + std::to_string(status);

        if (!state.error.empty())
            error += ": " + state.error;

        throw std::runtime_error(error);
    }

    return {
        !state.tool_name.empty(),
        std::move(state.tool_name),
        std::move(state.tool_args),
        std::move(state.tool_id),
        std::move(state.reasoning),
        std::move(state.content)
    };
}

}
