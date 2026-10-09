#include "thread_pool.hpp"

#include <stdexcept>
#include <utility>

ThreadPool::ThreadPool(std::size_t threadCount)
{
    if (threadCount == 0)
        throw std::invalid_argument("ThreadPool needs at least one worker");

    for (std::size_t i = 0; i < threadCount; ++i)
    {
        workers.emplace_back([this]
        {
            while (true)
            {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(mutex);
                    condition.wait(lock, [this]
                    {
                        return stopping || !tasks.empty();
                    });

                    if (stopping && tasks.empty())
                        return;

                    task = std::move(tasks.front());
                    tasks.pop();
                }

                task();
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }

    condition.notify_all();

    for (auto& worker : workers)
        if (worker.joinable()) worker.join();
}

void ThreadPool::enqueue(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping) return;
        tasks.push(std::move(task));
    }

    condition.notify_one();
}
