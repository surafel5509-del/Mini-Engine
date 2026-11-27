#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include "mini/core/Types.h"

namespace mini {

// Simple thread-pool for background work (asset import, physics, etc.).
class JobSystem {
public:
    explicit JobSystem(u32 threadCount = 0);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Run a job and return a future for its result.
    template <typename F>
    auto run(F&& fn) -> std::future<decltype(fn())> {
        using Ret = decltype(fn());
        auto task = std::make_shared<std::packaged_task<Ret()>>(
            std::forward<F>(fn));
        std::future<Ret> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mQueue.emplace([task] { (*task)(); });
        }
        mCv.notify_one();
        return fut;
    }

    void waitIdle();
    u32 workerCount() const { return static_cast<u32>(mWorkers.size()); }

private:
    void workerLoop();

    std::vector<std::thread> mWorkers;
    std::queue<std::function<void()>> mQueue;
    std::mutex mMutex;
    std::condition_variable mCv;
    bool mStop = false;
};

} // namespace mini
