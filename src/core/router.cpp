#include "raphael/core/router.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

using json = nlohmann::json;

namespace raphael {

Router::Router(NimClient& model) : model_(model) {}

RouteDecision Router::route(const Request& request) const {
    static constexpr auto system = R"(
You are Raphael's first-response controller.

Decide whether the request can be answered well by Nemotron Lightning.

If it is simple, conversational, factual, or otherwise easy:
- action = "answer"
- answer the user directly.

If it needs substantial reasoning, complex coding, debugging,
deep analysis, long-horizon work, or specialized multimodal ability:
- action = "escalate"
- do not answer the user
- choose the best model.

Available models:
lightning = simple/general
super = medium/advanced general tasks
glm = deep reasoning
deepseek = coding and multimodal
kimi = heavy long-horizon and multimodal work

Return ONLY JSON in exactly this shape:
{
  "action": "answer" or "escalate",
  "reasoning_needed": true or false,
  "coding_needed": true or false,
  "model": "lightning" | "super" | "glm" | "deepseek" | "kimi",
  "response": "answer text, or empty when escalating"
}

Never explain the decision.
)";

    auto raw = model_.complete(
        "nvidia/nemotron-3.5-lightning-30b-a3b",
        system,
        request.text
    );

    auto j = json::parse(raw);

    RouteDecision d{
        j.value("action", "answer") == "escalate",
        j.value("reasoning_needed", false),
        j.value("coding_needed", false),
        j.value("model", "lightning"),
        j.value("response", "")
    };

    if (d.escalate && d.model == "lightning")
        d.model = "super";

    return d;
}

}
