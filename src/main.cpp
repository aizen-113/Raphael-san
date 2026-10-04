#include "raphael/core/assistant.hpp"
#include "raphael/core/config.hpp"
#include "raphael/core/request.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

std::string make_request_id()
{
    const auto now = std::chrono::system_clock::now();

    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();

    return std::to_string(millis);
}

bool is_yes(const std::string& input)
{
    return input == "y" ||
           input == "Y" ||
           input == "yes" ||
           input == "Yes" ||
           input == "YES";
}

bool is_no(const std::string& input)
{
    return input == "n" ||
           input == "N" ||
           input == "no" ||
           input == "No" ||
           input == "NO";
}

} // namespace

int main()
{
    try {
        const auto config = raphael::Config::from_env();
        raphael::Assistant assistant(config);

        std::optional<std::uint64_t> pending_background_result;

        while (true) {
            /*
             * Check for completed background jobs before waiting
             * for the next request.
             */
            const auto completed_jobs =
                assistant.new_background_results();

            for (const auto& job : completed_jobs) {
                if (job.state == raphael::JobState::Completed) {
                    pending_background_result = job.id;

                    std::cout
                        << "\n● Got a better answer. Show it? (yes/no)\n";
                }
                else if (job.state == raphael::JobState::Failed) {
                    std::cout
                        << "\n● Background analysis failed: "
                        << job.error
                        << "\n";
                }
            }

            std::cout << "You: ";

            std::string input;

            if (!std::getline(std::cin, input)) {
                break;
            }

            if (input == "exit" ||
                input == "quit" ||
                input == "/exit" ||
                input == "/quit") {
                break;
            }

            /*
             * Handle the notification locally.
             *
             * This prevents "yes" or "no" from accidentally becoming
             * a brand-new message sent to Lightning.
             */
            if (pending_background_result.has_value() &&
                is_yes(input)) {

                const auto job =
                    assistant.get_background_result(
                        *pending_background_result
                    );

                if (job.has_value() &&
                    job->state == raphael::JobState::Completed) {

                    std::cout
                        << "● "
                        << job->answer
                        << "\n";
                }

                pending_background_result.reset();
                continue;
            }

            if (pending_background_result.has_value() &&
                is_no(input)) {

                /*
                 * The result stays inside BackgroundJobManager.
                 * It can still be retrieved later through the
                 * recall_background_answer tool.
                 */
                std::cout
                    << "● Understood, Master.\n";

                pending_background_result.reset();
                continue;
            }

            raphael::Request request;

            request.request_id = make_request_id();
            request.timestamp = std::chrono::system_clock::now();
            request.source = raphael::RequestSource::CLI;
            request.text = input;

            try {
                std::cout << "● ";

                assistant.run(
                    request,
                    [](std::string_view token) {
                        std::cout << token << std::flush;
                    }
                );

                std::cout << "\n";
            }
            catch (const std::exception& e) {
                std::cout
                    << "\n● Failed.\n"
                    << e.what()
                    << "\n";
            }
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
