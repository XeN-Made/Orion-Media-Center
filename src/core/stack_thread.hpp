// ORION Media Center - A worker thread with a stack of a chosen size.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <pthread.h>

#include <cstddef>
#include <functional>
#include <utility>

namespace hui
{

// std::thread cannot be given a stack size, and a console thread's is far
// smaller than a PC's: deep library code (an archive's decoder) overflows it
// and the app ends with no word. This runs one job on a thread with the
// stack asked for.
//
//   StackThread worker;
//   worker.start([this] { unpack(); }, 2u << 20);
//   ...
//   worker.join();
class StackThread
{
  public:
    StackThread() = default;
    StackThread(const StackThread &) = delete;
    StackThread &operator=(const StackThread &) = delete;
    ~StackThread()
    {
        join();
    }

    // False when the thread could not be made (the job does not run) or one
    // is still joinable.
    bool start(std::function<void()> work, std::size_t stack = 2u << 20)
    {
        if (started_)
            return false;
        work_ = std::move(work);
        pthread_attr_t attr;
        if (pthread_attr_init(&attr) != 0)
            return false;
        pthread_attr_setstacksize(&attr, stack);
        started_ = pthread_create(&thread_, &attr, &StackThread::main, this) == 0;
        pthread_attr_destroy(&attr);
        return started_;
    }
    bool joinable() const
    {
        return started_;
    }
    void join()
    {
        if (!started_)
            return;
        pthread_join(thread_, nullptr);
        started_ = false;
    }

  private:
    static void *main(void *self)
    {
        static_cast<StackThread *>(self)->work_();
        return nullptr;
    }

    pthread_t thread_{};
    bool started_ = false;
    std::function<void()> work_;
};

} // namespace hui
