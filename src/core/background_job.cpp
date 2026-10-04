#include "raphael/core/background_job.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
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

        BackgroundJob job;

        job.id = id;
        job.request = std::move(request);
        job.answer = {};
        job.reasoning = {};
        job.error = {};
        job.type = BackgroundJobType::Kimi;
        job.state = JobState::Queued;
        job.notified = false;
        job.delivered = false;

        jobs_[id] = std::move(job);

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
            auto output = pending.work();

            std::lock_guard lock(mutex_);

            auto& job = jobs_[pending.id];

            job.answer = std::move(output.answer);
            job.reasoning = std::move(output.reasoning);
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

std::optional<BackgroundJob>
BackgroundJobManager::search(std::string_view query) const {
    std::lock_guard lock(mutex_);

    BackgroundJob* best = nullptr;
    int best_score = -1;

    std::string q(query);

    std::transform(
        q.begin(),
        q.end(),
        q.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    for (auto& [id, job] : jobs_) {
        if (job.state != JobState::Completed)
            continue;

        if (q.empty()) {
            if (!best || id > best->id)
                best = const_cast<BackgroundJob*>(&job);
            continue;
        }

        std::string request = job.request;

        std::transform(
            request.begin(),
            request.end(),
            request.begin(),
            [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            }
        );

        int score = 0;

        if (request.find(q) != std::string::npos)
            score += 100;

        std::string word;

        for (char c : q) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!word.empty()) {
                    if (request.find(word) != std::string::npos)
                        score += 10;
                    word.clear();
                }
            } else {
                word += c;
            }
        }

        if (!word.empty() &&
            request.find(word) != std::string::npos)
            score += 10;

        if (score > best_score ||
            (score == best_score && best && id > best->id)) {

            best_score = score;
            best = const_cast<BackgroundJob*>(&job);
        }
    }

    if (!best)
        return std::nullopt;

    return *best;
}

void BackgroundJobManager::set_type(
    std::uint64_t id,
    BackgroundJobType type)
{
    std::lock_guard lock(mutex_);

    const auto it =
        jobs_.find(id);

    if (it == jobs_.end()) {
        return;
    }

    it->second.type = type;
}

void BackgroundJobManager::mark_delivered(std::uint64_t id) {
    std::lock_guard lock(mutex_);

    auto it = jobs_.find(id);

    if (it != jobs_.end())
        it->second.delivered = true;
}

}
