#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace raphael {

enum class RequestSource {
    CLI,
    Voice,
    GUI,
    System
};

using Metadata = std::unordered_map<std::string, std::string>;

struct Request {
    std::string request_id;
    std::chrono::system_clock::time_point timestamp;
    RequestSource source = RequestSource::CLI;

    std::string text;
    std::vector<std::string> images;
    std::string audio;

    std::string conversation_id;
    Metadata metadata;
};

}
