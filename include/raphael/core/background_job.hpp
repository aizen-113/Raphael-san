#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <condition_variable>
#include <mutex>

namespace raphael {

enum class JobState {
    Queued,
    Running,
    Completed,
    Failed
};

struct BackgroundJob {
    std::uint64_t id;
    std::string request;
    std::string answer;
    std::string error;
    JobState state = JobState::Queued;
    bool notified = false;
};

class BackgroundJobManager {
public:
    using Work = std::function<std::string()>;

    BackgroundJobManager();
    ~BackgroundJobManager();

    std::uint64_t submit(std::string request, Work work);

    std::vector<BackgroundJob> take_new_results();
    std::optional<BackgroundJob> get(std::uint64_t id) const;

private:
    struct Pending {
        std::uint64_t id;
        Work work;
    };

    void worker_loop();

    std::atomic<std::uint64_t> next_id_{1};

    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::queue<Pending> queue_;
    std::unordered_map<std::uint64_t, BackgroundJob> jobs_;

    std::thread worker_;
    bool stopping_ = false;
};

}
