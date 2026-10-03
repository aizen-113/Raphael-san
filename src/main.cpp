#include "raphael/core/assistant.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

int main() {
    try {
        auto config = raphael::Config::from_env();
        raphael::Assistant assistant(config);

        std::uint64_t request_number = 0;
        const std::string conversation_id = "cli-1";

        while (true) {
            for (const auto& job : assistant.new_background_results()) {
                if (job.state == raphael::JobState::Completed) {
                    std::cout
                        << "\n[Got a better answer. Show it? yes/no]\n";
                } else {
                    std::cout
                        << "\n[Background job "
                        << job.id
                        << " failed: "
                        << job.error
                        << "]\n";
                }
            }
            std::cout << "You: ";

            std::string prompt;

            if (!std::getline(std::cin, prompt) ||
                prompt == "/exit")
                break;

            if (prompt.empty())
                continue;

            raphael::Request request{
                .request_id =
                    "REQ-" +
                    std::to_string(++request_number),

                .timestamp =
                    std::chrono::system_clock::now(),

                .source =
                    raphael::RequestSource::CLI,

                .text = prompt,

                .conversation_id =
                    conversation_id
            };

            try {
                std::cout << "Raphael: ";

                assistant.run(
                    request,
                    [](std::string_view token) {
                        std::cout
                            << token
                            << std::flush;
                    }
                );

                std::cout << "\n\n";

            } catch (const std::exception& e) {
                std::cerr
                    << "\n[Request error] "
                    << e.what()
                    << "\n\n";
            }
        }

    } catch (const std::exception& e) {
        std::cerr
            << "Fatal error: "
            << e.what()
            << '\n';

        return 1;
    }

    return 0;
}
