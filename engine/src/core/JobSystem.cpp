#include "mini/core/JobSystem.h"
#include <algorithm>

namespace mini {

JobSystem::JobSystem(u32 threadCount) {
    if (threadCount == 0) {
        threadCount = std::max(1u, std::thread::hardware_concurrency());
    }
    for (u32 i = 0; i < threadCount; ++i) {
        mWorkers.emplace_back([this] { workerLoop(); });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStop = true;
    }
    mCv.notify_all();
    for (auto& t : mWorkers) {
        if (t.joinable()) t.join();
    }
}

void JobSystem::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCv.wait(lock, [this] { return mStop || !mQueue.empty(); });
            if (mStop && mQueue.empty()) return;
            job = std::move(mQueue.front());
            mQueue.pop();
        }
        job();
    }
}

void JobSystem::waitIdle() {
    std::lock_guard<std::mutex> lock(mMutex);
    // Note: jobs currently executing are not tracked here; callers that
    // need completion should await the future returned by run().
}

} // namespace mini
