#pragma once

#include <curl/curl.h>

#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <sys/types.h>

namespace raphael {

struct WebSearchResult {
    std::string title;
    std::string url;
    std::string snippet;
};

class WebSearchClient {
public:
    explicit WebSearchClient(
        std::string base_url = "http://127.0.0.1:4479"
    );

    ~WebSearchClient();

    WebSearchClient(const WebSearchClient&) = delete;
    WebSearchClient& operator=(const WebSearchClient&) = delete;

    std::vector<WebSearchResult> search(
        std::string_view query,
        int max_results = 5,
        std::string_view timelimit = {}
    );

    /*
     * Called once after each visible Raphael response.
     *
     * After 3 responses without another web search,
     * an instance of DDGS that Raphael started will be stopped.
     */
    void note_assistant_turn();

private:
    bool server_healthy_locked();

    void start_server_locked();

    void stop_server_locked();

    bool process_alive_locked();

    CURL* curl_ = nullptr;

    curl_slist* headers_ = nullptr;

    std::string base_url_;

    /*
     * If true, Raphael started this DDGS process itself
     * and is therefore allowed to shut it down.
     */
    bool owns_server_ = false;

    pid_t ddgs_pid_ = -1;

    /*
     * Set whenever a search happens.
     *
     * note_assistant_turn() uses this to determine whether
     * the current assistant turn actually needed web search.
     */
    bool web_used_since_turn_ = false;

    int idle_turns_ = 0;

    mutable std::mutex mutex_;
};

} // namespace raphael
