#pragma once

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace raphael {

enum class JobState {
    Queued,
    Running,
    Completed,
    Failed
};

enum class BackgroundJobType {
    Kimi,
    WebSearch
};

struct BackgroundOutput {
    std::string answer;
    std::string reasoning;
};

struct BackgroundJob {
    std::uint64_t id;

    std::string request;
    std::string answer;
    std::string reasoning;
    std::string error;

    BackgroundJobType type = BackgroundJobType::Kimi;

    JobState state = JobState::Queued;

    bool notified = false;
    bool delivered = false;
};

class BackgroundJobManager {
public:
    using Work = std::function<BackgroundOutput()>;

    BackgroundJobManager();
    ~BackgroundJobManager();

    std::uint64_t submit(
        std::string request,
        Work work
    );

    std::vector<BackgroundJob>
    take_new_results();

    std::optional<BackgroundJob>
    get(std::uint64_t id) const;

    std::optional<BackgroundJob>
    search(std::string_view query) const;

    void mark_delivered(
        std::uint64_t id
    );

    void set_type(
        std::uint64_t id,
        BackgroundJobType type
    );

private:
    struct Pending {
        std::uint64_t id;
        Work work;
    };

    void worker_loop();

    std::atomic<std::uint64_t>
        next_id_{1};

    mutable std::mutex mutex_;

    std::condition_variable cv_;

    std::queue<Pending> queue_;

    std::unordered_map<
        std::uint64_t,
        BackgroundJob
    > jobs_;

    std::thread worker_;

    bool stopping_ = false;
};

} // namespace raphael
