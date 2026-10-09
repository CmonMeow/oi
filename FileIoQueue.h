#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

// Disk jobs run in FIFO order. Their completions run only when the application
// polls, so network and transfer state never cross thread boundaries.
class FileIoQueue {
public:
    using Completion = std::function<void()>;
    using Work = std::function<Completion()>;
private:
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Work> jobs;
    std::deque<Completion> completed;
    std::thread thread;
    size_t pending = 0;
    bool stopping = false;
    void run() {
        for (;;) {
            Work work;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock,[this]{return stopping || !jobs.empty();});
                if (jobs.empty()) return;
                work = std::move(jobs.front()); jobs.pop_front();
            }
            auto result = work();
            // Release job-owned handles before checking for shutdown again.
            work = nullptr;
            std::lock_guard<std::mutex> lock(mutex);
            if (result) completed.push_back(std::move(result));
            else --pending;
        }
    }
public:
    ~FileIoQueue() {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        wake.notify_one();
        if (thread.joinable()) thread.join();
    }
    bool submit(Work work, bool cleanup = false) {
        std::lock_guard<std::mutex> lock(mutex);
        // Include completed jobs in the bound: a paused UI cannot grow memory.
        // Handle cleanup must always be accepted, including during shutdown.
        if (!cleanup && pending >= 1024) return false;
        ++pending; jobs.push_back(std::move(work));
        if (!thread.joinable()) thread = std::thread([this]{run();});
        wake.notify_one(); return true;
    }
    void poll() {
        for (unsigned i=0; i<128; ++i) {
            Completion completion;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (completed.empty()) return;
                completion = std::move(completed.front()); completed.pop_front(); --pending;
            }
            completion();
        }
    }
};
