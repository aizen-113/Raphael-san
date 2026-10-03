#include "raphael/core/assistant.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using json = nlohmann::json;

namespace raphael {

static const json tools = json::array({
    {
        {"type", "function"},
        {"function", {
            {"name", "delegate_to_model"},
            {
                "description",
                "Delegate difficult reasoning, coding, debugging, "
                "or long-horizon work to Kimi-K3."
            },
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"model", {
                        {"type", "string"},
                        {"enum", {"kimi"}}
                    }}
                }},
                {"required", {"model"}}
            }}
        }}
    }
});

static constexpr auto controller_prompt = R"(
You are Raphael-san.

Answer the user directly when you can.

When the task needs substantially stronger reasoning,
difficult coding/debugging, complex analysis, or long-horizon work,
call delegate_to_model.

IMPORTANT:
When using delegate_to_model, emit ONLY the tool call.
Do not emit any answer text before or after the tool call.

When answering directly, do not use the tool.
Never explain delegation.
Never reveal internal reasoning.
)";

static constexpr auto kimi_prompt =
    "You are Raphael-san, a personal desktop AI assistant. "
    "Give the best possible answer. Be direct and useful.";

static bool wants_high_reasoning(std::string text) {
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    static constexpr std::string_view triggers[] = {
        "think deeply",
        "reason deeply",
        "deep reasoning",
        "reason carefully",
        "analyze in depth",
        "analyze deeply",
        "derive this",
        "prove this",
        "prove it",
        "step by step",
        "think through every step",
        "give detailed reasoning"
    };

    for (auto trigger : triggers) {
        if (text.find(trigger) != std::string::npos)
            return true;
    }

    return false;
}

Assistant::Assistant(const Config& config)
    : lightning_(config.nvidia_api_key),
      kimi_(config.nvidia_api_key),
      models_() {}

void Assistant::run(
    const Request& request,
    NimClient::TokenCallback on_token
) {
    auto request_history = history_;

    request_history.push_back({
        {"role", "user"},
        {"content", request.text}
    });

    std::string controller_output;

    auto capture = [&](std::string_view token) {
        controller_output += token;
        on_token(token);
    };

    auto result = lightning_.stream(
        models_.id("lightning"),
        controller_prompt,
        request_history,
        capture,
        tools,
        {},
        true
    );

    if (!result.tool_called) {
        history_ = request_history;

        history_.push_back({
            {"role", "assistant"},
            {"content", controller_output}
        });

        on_token(controller_output);
        return;
    }

    if (result.tool_name != "delegate_to_model")
        throw std::runtime_error("Unknown tool: " + result.tool_name);

    auto args = json::parse(result.tool_args);
    auto model = args.at("model").get<std::string>();

    if (!models_.contains(model))
        throw std::runtime_error("Invalid delegation target: " + model);

    std::cerr
        << "[DELEGATING TO: "
        << model
        << "]\n";

    on_token("\n[Searching for a better answer...]\n");

    auto history_snapshot = std::move(request_history);
    auto original_request = request.text;
    auto effort = wants_high_reasoning(request.text)
        ? "high"
        : "low";

    auto model_id = std::string(models_.id(model));

    jobs_.submit(
        original_request,
        [this,
         history = std::move(history_snapshot),
         model_id = std::move(model_id),
         effort]() mutable {

            std::string answer;

            auto capture = [&](std::string_view token) {
                answer += token;
            };

            kimi_.stream(
                model_id,
                kimi_prompt,
                history,
                capture,
                {},
                effort
            );

            return answer;
        }
    );
}

std::vector<BackgroundJob>
Assistant::new_background_results() {
    return jobs_.take_new_results();
}

std::optional<BackgroundJob>
Assistant::get_background_result(
    std::uint64_t id
) const {
    return jobs_.get(id);
}

}
