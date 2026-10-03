#include "raphael/core/background_job.hpp"

#include <utility>

namespace raphael {

BackgroundJobManager::BackgroundJobManager()
    : worker_(&BackgroundJobManager::worker_loop, this) {}

BackgroundJobManager::~BackgroundJobManager() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }

    cv_.notify_one();

    if (worker_.joinable())
        worker_.join();
}

std::uint64_t BackgroundJobManager::submit(
    std::string request,
    Work work
) {
    const auto id = next_id_++;

    {
        std::lock_guard lock(mutex_);

        jobs_[id] = {
            id,
            std::move(request),
            {},
            {},
            JobState::Queued
        };

        queue_.push({id, std::move(work)});
    }

    cv_.notify_one();
    return id;
}

void BackgroundJobManager::worker_loop() {
    while (true) {
        Pending pending;

        {
            std::unique_lock lock(mutex_);

            cv_.wait(lock, [&] {
                return stopping_ || !queue_.empty();
            });

            if (stopping_ && queue_.empty())
                return;

            pending = std::move(queue_.front());
            queue_.pop();

            jobs_[pending.id].state = JobState::Running;
        }

        try {
            auto answer = pending.work();

            std::lock_guard lock(mutex_);

            auto& job = jobs_[pending.id];
            job.answer = std::move(answer);
            job.state = JobState::Completed;

        } catch (const std::exception& e) {

            std::lock_guard lock(mutex_);

            auto& job = jobs_[pending.id];
            job.error = e.what();
            job.state = JobState::Failed;
        }
    }
}

std::vector<BackgroundJob>
BackgroundJobManager::take_new_results() {
    std::vector<BackgroundJob> results;

    std::lock_guard lock(mutex_);

    for (auto& [id, job] : jobs_) {
        if (!job.notified &&
            (job.state == JobState::Completed ||
             job.state == JobState::Failed)) {

            job.notified = true;
            results.push_back(job);
        }
    }

    return results;
}

std::optional<BackgroundJob>
BackgroundJobManager::get(std::uint64_t id) const {
    std::lock_guard lock(mutex_);

    auto it = jobs_.find(id);

    if (it == jobs_.end())
        return std::nullopt;

    return it->second;
}

}
