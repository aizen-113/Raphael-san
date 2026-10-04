#include "raphael/core/assistant.hpp"
#include "raphael/core/config.hpp"
#include "raphael/core/request.hpp"

#include "linenoise.hpp"

#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

namespace {

std::string make_request_id()
{
    const auto now =
        std::chrono::system_clock::now();

    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();

    return std::to_string(millis);
}

bool is_yes(const std::string& input)
{
    std::string text;

    for (unsigned char c : input) {

        if (std::isspace(c)) {
            continue;
        }

        text.push_back(
            static_cast<char>(
                std::tolower(c)
            )
        );
    }

    if (text == "y" ||
        text == "yes" ||
        text == "yeah" ||
        text == "yep") {

        return true;
    }

    /*
     * Also accept stretched forms such as:
     *
     * yessssss
     * yyyyyyes
     */
    std::size_t y_count = 0;

    while (y_count < text.size() &&
           text[y_count] == 'y') {

        ++y_count;
    }

    if (y_count == 0 ||
        y_count >= text.size()) {

        return false;
    }

    std::size_t e_count = y_count;

    while (e_count < text.size() &&
           text[e_count] == 'e') {

        ++e_count;
    }

    if (e_count == y_count ||
        e_count >= text.size()) {

        return false;
    }

    return text.substr(e_count) ==
           "s";
}

bool is_no(const std::string& input)
{
    std::string text;

    for (unsigned char c : input) {

        if (std::isspace(c)) {
            continue;
        }

        text.push_back(
            static_cast<char>(
                std::tolower(c)
            )
        );
    }

    if (text == "n" ||
        text == "no" ||
        text == "nope" ||
        text == "nah") {

        return true;
    }

    /*
     * Accept stretched forms such as:
     *
     * nnnno
     * nooooo
     * nnnnnooooooooo
     */
    std::size_t n_count = 0;

    while (n_count < text.size() &&
           text[n_count] == 'n') {

        ++n_count;
    }

    if (n_count == 0 ||
        n_count >= text.size()) {

        return false;
    }

    std::size_t o_count =
        n_count;

    while (o_count < text.size() &&
           text[o_count] == 'o') {

        ++o_count;
    }

    return o_count > n_count &&
           o_count == text.size();
}

std::string lowercase(
    std::string_view text)
{
    std::string result;
    result.reserve(text.size());

    for (unsigned char c : text) {

        result.push_back(
            static_cast<char>(
                std::tolower(c)
            )
        );
    }

    return result;
}

bool is_background_status_question(
    std::string_view text)
{
    const std::string q =
        lowercase(text);

    return
        q.find("do u got") != std::string::npos ||
        q.find("do you got") != std::string::npos ||
        q.find("did u get") != std::string::npos ||
        q.find("did you get") != std::string::npos ||
        q.find("did u find") != std::string::npos ||
        q.find("did you find") != std::string::npos ||
        q.find("do u have") != std::string::npos ||
        q.find("do you have") != std::string::npos ||
        q.find("got the data") != std::string::npos ||
        q.find("got the result") != std::string::npos ||
        q.find("got the answer") != std::string::npos ||
        q.find("is it done") != std::string::npos ||
        q.find("is it finished") != std::string::npos ||
        q.find("done yet") != std::string::npos ||
        q.find("finished yet") != std::string::npos ||
        q.find("any result") != std::string::npos ||
        q.find("any results") != std::string::npos ||
        q.find("is it ready") != std::string::npos;
}

} // namespace

volatile std::sig_atomic_t
g_shutdown_requested = 0;

void handle_signal(int)
{
    g_shutdown_requested = 1;
}

int main()
{
    linenoise::EditState editor;
    int old_flags = -1;

    try {
        std::signal(
            SIGINT,
            handle_signal
        );

        std::signal(
            SIGTERM,
            handle_signal
        );

        const auto config =
            raphael::Config::from_env();

        raphael::Assistant assistant(
            config
        );

        old_flags =
            fcntl(
                STDIN_FILENO,
                F_GETFL,
                0
            );

        if (old_flags < 0) {
            throw std::runtime_error(
                "Failed to read stdin flags."
            );
        }

        if (fcntl(
                STDIN_FILENO,
                F_SETFL,
                old_flags | O_NONBLOCK
            ) < 0) {

            throw std::runtime_error(
                "Failed to set stdin non-blocking."
            );
        }

        if (!linenoise::EditStart(
                editor,
                "You: ",
                STDIN_FILENO,
                STDOUT_FILENO)) {

            throw std::runtime_error(
                "Failed to initialize terminal editor."
            );
        }

        std::optional<std::uint64_t>
            pending_background_result;

        std::string input;

        while (true) {

            if (g_shutdown_requested) {
                break;
            }

            /*
             * Check completed background jobs.
             */
            const auto results =
                assistant.new_background_results();

            for (const auto& job : results) {

                if (job.state ==
                    raphael::JobState::Completed) {

                    linenoise::EditHide(
                        editor
                    );

                    pending_background_result =
                        job.id;

                    if (job.type ==
                        raphael::BackgroundJobType::WebSearch) {

                        std::cout
                            << "\n● Found the results of this search: \""
                            << job.request
                            << "\"\n"
                            << "Show them? (yes/no)\n";
                    }
                    else {

                        std::cout
                            << "\n● Got a better answer. "
                            << "Show it? (yes/no)\n";
                    }

                    linenoise::EditShow(
                        editor
                    );
                }
                else if (
                    job.state ==
                    raphael::JobState::Failed) {

                    linenoise::EditHide(
                        editor
                    );

                    std::cout
                        << "\n● Background analysis failed: "
                        << job.error
                        << '\n';

                    linenoise::EditShow(
                        editor
                    );
                }
            }

            /*
             * Wait for either keyboard input or a timeout.
             */
            struct pollfd pfd {};

            pfd.fd =
                STDIN_FILENO;

            pfd.events =
                POLLIN;

            const int poll_result =
                poll(
                    &pfd,
                    1,
                    100
                );

            if (g_shutdown_requested) {
                break;
            }

            if (poll_result < 0) {

                if (errno == EINTR) {
                    continue;
                }

                throw std::runtime_error(
                    "stdin polling failed."
                );
            }

            if (poll_result == 0) {
                continue;
            }

            if (!(pfd.revents &
                  (POLLIN | POLLHUP))) {

                continue;
            }

            /*
             * Feed keyboard input to linenoise.
             */
            const auto status =
                linenoise::EditFeed(
                    editor,
                    input
                );

            if (status ==
                linenoise::EditStatus::More) {

                continue;
            }

            /*
             * A complete line, cancel, EOF, or error
             * finishes this editing session.
             */
            linenoise::EditStop(
                editor
            );

            if (status ==
                    linenoise::EditStatus::Cancel ||
                status ==
                    linenoise::EditStatus::Eof) {

                break;
            }

            if (status ==
                linenoise::EditStatus::Error) {

                throw std::runtime_error(
                    "Terminal input failed."
                );
            }

            /*
             * Only process a completed line.
             */
            if (status !=
                linenoise::EditStatus::Done) {

                continue;
            }

            if (!input.empty()) {

                linenoise::AddHistory(
                    input.c_str()
                );
            }

            /*
             * Handle completed background result.
             */
            if (pending_background_result.has_value() &&
                is_yes(input)) {

                const auto job =
                    assistant.get_background_result(
                        *pending_background_result
                    );

                if (job.has_value() &&
                    job->state ==
                        raphael::JobState::Completed) {

                    if (job->type ==
                        raphael::BackgroundJobType::WebSearch) {

                        std::cout
                            << "● ";

                        assistant.elaborate_web_result(
                            job->id,
                            [](std::string_view token) {
                                std::cout
                                    << token
                                    << std::flush;
                            }
                        );

                        std::cout
                            << '\n';
                    }
                    else {

                        std::cout
                            << "● "
                            << job->answer
                            << '\n';
                    }
                }

                pending_background_result.reset();

                if (!linenoise::EditStart(
                        editor,
                        "You: ",
                        STDIN_FILENO,
                        STDOUT_FILENO)) {

                    throw std::runtime_error(
                        "Failed to restart terminal editor."
                    );
                }

                continue;
            }

            /*
             * Keep the result stored when the Master says no.
             */
            if (pending_background_result.has_value() &&
                is_no(input)) {

                std::cout
                    << "● Understood, Master.\n";

                pending_background_result.reset();

                if (!linenoise::EditStart(
                        editor,
                        "You: ",
                        STDIN_FILENO,
                        STDOUT_FILENO)) {

                    throw std::runtime_error(
                        "Failed to restart terminal editor."
                    );
                }

                continue;
            }

            /*
             * Any other message dismisses the visible
             * yes/no notification, but the result itself
             * remains stored inside BackgroundJobManager.
             */
            pending_background_result.reset();

            /*
             * Handle exit.
             */
            if (input == "exit" ||
                input == "quit" ||
                input == "/exit" ||
                input == "/quit") {

                break;
            }

            /*
             * Ask about a background job without creating
             * another AI request.
             */
            if (assistant.has_active_background_jobs() &&
                is_background_status_question(input)) {

                std::cout
                    << "● Still processing, Master.\n";

                if (!linenoise::EditStart(
                        editor,
                        "You: ",
                        STDIN_FILENO,
                        STDOUT_FILENO)) {

                    throw std::runtime_error(
                        "Failed to restart terminal editor."
                    );
                }

                continue;
            }

            /*
             * Normal Raphael request.
             */
            raphael::Request request;

            request.request_id =
                make_request_id();

            request.timestamp =
                std::chrono::system_clock::now();

            request.source =
                raphael::RequestSource::CLI;

            request.text =
                input;

            try {
                std::cout
                    << "● ";

                assistant.run(
                    request,
                    [](std::string_view token) {
                        std::cout
                            << token
                            << std::flush;
                    }
                );

                assistant.note_assistant_turn();

                std::cout
                    << '\n';
            }
            catch (const std::exception& e) {

                std::cout
                    << "\n● Failed.\n"
                    << e.what()
                    << '\n';
            }

            /*
             * Start the editor again for the next message.
             */
            if (!linenoise::EditStart(
                    editor,
                    "You: ",
                    STDIN_FILENO,
                    STDOUT_FILENO)) {

                throw std::runtime_error(
                    "Failed to restart terminal editor."
                );
            }
        }

        /*
         * Clean up terminal state.
         */
        linenoise::EditStop(
            editor
        );

        if (old_flags >= 0) {

            fcntl(
                STDIN_FILENO,
                F_SETFL,
                old_flags
            );
        }

        if (g_shutdown_requested) {

            std::cout
                << "\n● Shutting down, Master.\n";
        }

        return 0;
    }
    catch (const std::exception& e) {

        std::cerr
            << "Fatal error: "
            << e.what()
            << '\n';

        return 1;
    }
}