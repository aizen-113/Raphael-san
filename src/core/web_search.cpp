#include "raphael/core/web_search.hpp"

#include <nlohmann/json.hpp>

#include <sys/prctl.h>
#include <sys/types.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

namespace raphael {
namespace {

std::size_t write_callback(
    char* ptr,
    std::size_t size,
    std::size_t count,
    void* userdata)
{
    auto* output =
        static_cast<std::string*>(userdata);

    const std::size_t total =
        size * count;

    output->append(ptr, total);

    return total;
}

std::size_t discard_callback(
    char*,
    std::size_t size,
    std::size_t count,
    void*)
{
    return size * count;
}

std::string get_ddgs_binary()
{
    if (const char* env =
            std::getenv("RAPHAEL_DDGS_BIN")) {

        if (*env != '\0') {
            return env;
        }
    }

    const char* home =
        std::getenv("HOME");

    if (!home || *home == '\0') {
        throw std::runtime_error(
            "HOME environment variable is not set."
        );
    }

    return std::string(home) +
           "/.local/share/raphael-ddgs/bin/ddgs";
}

std::string string_field(
    const nlohmann::json& object,
    const char* key)
{
    if (!object.is_object()) {
        return {};
    }

    if (!object.contains(key)) {
        return {};
    }

    if (!object[key].is_string()) {
        return {};
    }

    return object[key].get<std::string>();
}

} // namespace

WebSearchClient::WebSearchClient(
    std::string base_url)
    : base_url_(std::move(base_url))
{
    curl_ = curl_easy_init();

    if (!curl_) {
        throw std::runtime_error(
            "Failed to initialize web search client."
        );
    }

    headers_ = curl_slist_append(
        headers_,
        "Content-Type: application/json"
    );

    headers_ = curl_slist_append(
        headers_,
        "Accept: application/json"
    );

    if (!headers_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;

        throw std::runtime_error(
            "Failed to initialize web search headers."
        );
    }
}

WebSearchClient::~WebSearchClient()
{
    std::lock_guard lock(mutex_);

    stop_server_locked();

    if (headers_) {
        curl_slist_free_all(headers_);
    }

    if (curl_) {
        curl_easy_cleanup(curl_);
    }
}

bool WebSearchClient::process_alive_locked()
{
    if (ddgs_pid_ <= 0) {
        return false;
    }

    int status = 0;

    const pid_t result =
        waitpid(
            ddgs_pid_,
            &status,
            WNOHANG
        );

    if (result == 0) {
        return true;
    }

    if (result == ddgs_pid_) {
        ddgs_pid_ = -1;
        owns_server_ = false;
        return false;
    }

    return false;
}

bool WebSearchClient::server_healthy_locked()
{
    curl_easy_reset(curl_);

    const std::string url =
        base_url_ + "/health";

    curl_easy_setopt(
        curl_,
        CURLOPT_URL,
        url.c_str()
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_HTTPHEADER,
        headers_
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_HTTPGET,
        1L
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_WRITEFUNCTION,
        discard_callback
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_WRITEDATA,
        nullptr
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_CONNECTTIMEOUT_MS,
        250L
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_TIMEOUT_MS,
        500L
    );

    const CURLcode result =
        curl_easy_perform(curl_);

    if (result != CURLE_OK) {
        return false;
    }

    long status = 0;

    curl_easy_getinfo(
        curl_,
        CURLINFO_RESPONSE_CODE,
        &status
    );

    return status == 200;
}

void WebSearchClient::start_server_locked()
{
    if (server_healthy_locked()) {
        /*
         * Something is already serving DDGS on the port.
         * We did not create it, so we must not kill it later.
         */
        owns_server_ = false;
        return;
    }

    const std::string binary =
        get_ddgs_binary();

    if (access(binary.c_str(), X_OK) != 0) {
        throw std::runtime_error(
            "DDGS executable not found:\n" +
            binary
        );
    }

    const pid_t parent_pid = getpid();

    const pid_t pid = fork();

    if (pid < 0) {
        throw std::runtime_error(
            "Failed to start DDGS."
        );
    }

    if (pid == 0) {
        /*
        * If Raphael dies unexpectedly, Linux will automatically
        * send SIGTERM to DDGS as well.
        */
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0) {
            _exit(127);
        }

        /*
        * Protect against the tiny race where the parent dies
        * between fork() and prctl().
        */
        if (getppid() != parent_pid) {
            _exit(127);
        }

        /*
        * DDGS is an internal implementation detail.
        * Keep its logs out of Raphael's terminal.
        */
        const int devnull =
            open(
                "/dev/null",
                O_RDWR
            );

        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);

            if (devnull > STDERR_FILENO) {
                close(devnull);
            }
        }

        execl(
            binary.c_str(),
            binary.c_str(),
            "api",
            "--host",
            "127.0.0.1",
            "--port",
            "4479",
            static_cast<char*>(nullptr)
        );

        _exit(127);
    }

    ddgs_pid_ = pid;
    owns_server_ = true;

    for (int i = 0; i < 200; ++i) {

        std::this_thread::sleep_for(
            std::chrono::milliseconds(50)
        );

        if (server_healthy_locked()) {
            idle_turns_ = 0;
            web_used_since_turn_ = false;
            return;
        }

        if (!process_alive_locked()) {
            break;
        }
    }

    stop_server_locked();

    throw std::runtime_error(
        "DDGS server failed to become ready."
    );
}

void WebSearchClient::stop_server_locked()
{
    if (!owns_server_ || ddgs_pid_ <= 0) {
        ddgs_pid_ = -1;
        owns_server_ = false;
        return;
    }

    /*
     * Ask DDGS to terminate cleanly.
     */
    kill(
        ddgs_pid_,
        SIGTERM
    );

    /*
     * Give it a moment to exit.
     */
    for (int i = 0; i < 20; ++i) {

        int status = 0;

        const pid_t result =
            waitpid(
                ddgs_pid_,
                &status,
                WNOHANG
            );

        if (result == ddgs_pid_) {
            ddgs_pid_ = -1;
            owns_server_ = false;
            return;
        }

        if (result < 0) {
            ddgs_pid_ = -1;
            owns_server_ = false;
            return;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(25)
        );
    }

    /*
     * It didn't exit cleanly.
     */
    kill(
        ddgs_pid_,
        SIGKILL
    );

    waitpid(
        ddgs_pid_,
        nullptr,
        0
    );

    ddgs_pid_ = -1;
    owns_server_ = false;
}

std::vector<WebSearchResult>
WebSearchClient::search(
    std::string_view query,
    int max_results,
    std::string_view timelimit)
{
    if (query.empty()) {
        throw std::invalid_argument(
            "Web search query cannot be empty."
        );
    }

    max_results =
        std::clamp(
            max_results,
            1,
            8
        );

    std::lock_guard lock(mutex_);

    /*
     * Start DDGS only when the first search is actually requested.
     */
    if (!server_healthy_locked()) {
        start_server_locked();
    }

    /*
     * This search counts as web usage for the current
     * assistant-turn window.
     */
    web_used_since_turn_ = true;
    idle_turns_ = 0;

    nlohmann::json request = {
        {"query", std::string(query)},
        {"region", "in-en"},
        {"safesearch", "moderate"},
        {"max_results", max_results},
        {"page", 1},
        {"backend", "auto"}
    };

    if (!timelimit.empty()) {
        request["timelimit"] =
            std::string(timelimit);
    }

    const std::string request_body =
        request.dump();

    std::string response_body;

    curl_easy_reset(curl_);

    const std::string url =
        base_url_ + "/search/text";

    curl_easy_setopt(
        curl_,
        CURLOPT_URL,
        url.c_str()
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_HTTPHEADER,
        headers_
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_POST,
        1L
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_POSTFIELDS,
        request_body.c_str()
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_POSTFIELDSIZE,
        static_cast<long>(
            request_body.size()
        )
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_WRITEFUNCTION,
        write_callback
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_WRITEDATA,
        &response_body
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_CONNECTTIMEOUT_MS,
        1000L
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_TIMEOUT_MS,
        7000L
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_LOW_SPEED_LIMIT,
        1L
    );

    curl_easy_setopt(
        curl_,
        CURLOPT_LOW_SPEED_TIME,
        5L
    );

    const CURLcode result =
        curl_easy_perform(curl_);

    if (result != CURLE_OK) {
        throw std::runtime_error(
            std::string(
                "Web search request failed: "
            ) +
            curl_easy_strerror(result)
        );
    }

    long status = 0;

    curl_easy_getinfo(
        curl_,
        CURLINFO_RESPONSE_CODE,
        &status
    );

    if (status < 200 || status >= 300) {
        throw std::runtime_error(
            "DDGS API returned HTTP " +
            std::to_string(status)
        );
    }

    nlohmann::json response;

    try {
        response =
            nlohmann::json::parse(
                response_body
            );
    }
    catch (
        const nlohmann::json::exception& e
    ) {
        throw std::runtime_error(
            std::string(
                "Invalid DDGS response: "
            ) +
            e.what()
        );
    }

    if (!response.contains("results") ||
        !response["results"].is_array()) {

        throw std::runtime_error(
            "DDGS response does not contain results."
        );
    }

    std::vector<WebSearchResult> results;

    results.reserve(
        response["results"].size()
    );

    for (const auto& item :
         response["results"]) {

        WebSearchResult result;

        result.title =
            string_field(
                item,
                "title"
            );

        result.url =
            string_field(
                item,
                "href"
            );

        result.snippet =
            string_field(
                item,
                "body"
            );

        if (result.url.empty()) {
            continue;
        }

        /*
         * Keep model context under control.
         */
        if (result.title.size() > 200) {
            result.title.resize(197);
            result.title += "...";
        }

        if (result.snippet.size() > 700) {
            result.snippet.resize(697);
            result.snippet += "...";
        }

        results.push_back(
            std::move(result)
        );
    }

    return results;
}

void WebSearchClient::note_assistant_turn()
{
    std::lock_guard lock(mutex_);

    if (!owns_server_) {
        return;
    }

    /*
     * A search happened during this turn.
     * Do not count this turn as idle.
     */
    if (web_used_since_turn_) {
        web_used_since_turn_ = false;
        idle_turns_ = 0;
        return;
    }

    ++idle_turns_;

    if (idle_turns_ >= 3) {
        stop_server_locked();
        idle_turns_ = 0;
    }
}

} // namespace raphael
