#include "thread_pool.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <gtest/gtest.h>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

TEST(ThreadPoolStressTest, ExecutesAllSubmittedTasks) {
    ThreadPool pool(8);
    std::atomic<std::size_t> executed{0};
    const std::size_t task_count = 100000;
    for (std::size_t i = 0; i < task_count; i++) {
        pool.submit([&executed]() { ++executed; });
    }
    pool.wait_for_tasks();
    EXPECT_EQ(executed.load(), task_count);
    EXPECT_EQ(pool.get_tasks_total(), 0u);
}

TEST(ThreadPoolStressTest, ExecutesEveryTaskExactlyOnce) {
    ThreadPool pool(8);
    const std::size_t task_count = 100000;
    std::vector<std::atomic<int>> executions(task_count);
    for (std::size_t i = 0; i < task_count; ++i) {
        executions[i].store(0);
    }
    for (std::size_t i = 0; i < task_count; ++i) {
        pool.submit([&executions, i]() { ++executions[i]; });
    }
    pool.wait_for_tasks();
    for (std::size_t i = 0; i < task_count; ++i) {
        EXPECT_EQ(executions[i].load(), 1);
    }
    EXPECT_EQ(pool.get_tasks_total(), 0u);
}

TEST(ThreadPoolStressTest, ConcurrentSubmitters) {
    ThreadPool pool(8);

    const std::size_t submitter_count = 4;
    const std::size_t tasks_per_submitter = 25000;

    std::vector<std::thread> submitters;
    submitters.reserve(submitter_count);
    std::atomic<std::size_t> executed{0};
    for (std::size_t i = 0; i < submitter_count; i++) {
        submitters.emplace_back([&]() {
            for (std::size_t j = 0; j < tasks_per_submitter; j++) {
                pool.submit([&executed]() { ++executed; });
            }
        });
    }
    for (auto& s : submitters) {
        s.join();
    }
    pool.wait_for_tasks();
    EXPECT_EQ(executed, submitter_count * tasks_per_submitter);
    EXPECT_EQ(pool.get_tasks_total(), 0u);
}

TEST(ThreadPoolStressTest, ConcurrentSubmittersExactlyOnce) {
    ThreadPool pool(8);

    const std::size_t submitter_count = 4;
    const std::size_t tasks_per_submitter = 25000;

    const std::size_t total_tasks = submitter_count * tasks_per_submitter;
    std::vector<std::atomic<int>> executions(total_tasks);
    std::vector<std::thread> submitters;
    submitters.reserve(submitter_count);

    for (std::size_t i = 0; i < total_tasks; ++i) {
        executions[i].store(0);
    }

    for (std::size_t s = 0; s < submitter_count; ++s) {
        submitters.emplace_back([&pool, &executions, s, tasks_per_submitter]() {
            for (std::size_t j = 0; j < tasks_per_submitter; ++j) {
                const std::size_t task_id = s * tasks_per_submitter + j;
                pool.submit([&executions, task_id]() { ++executions[task_id]; });
            }
        });
    }

    for (auto& t : submitters) {
        t.join();
    }
    pool.wait_for_tasks();
    for (std::size_t i = 0; i < total_tasks; ++i) {
        EXPECT_EQ(executions[i].load(), 1);
    }

    EXPECT_EQ(pool.get_tasks_total(), 0u);
}

TEST(ThreadPoolStressTest, TestCancelRace) {
    const std::size_t task_count = 10000;
    ThreadPool pool(8);
    pool.pause();
    const std::size_t canceller_count = 4;
    std::vector<std::atomic<int>> executions(task_count);

    for (std::size_t i = 0; i < task_count; ++i) {
        executions[i].store(0);
    }
    std::vector<TaskHandle<std::size_t>> handles;
    handles.reserve(task_count);
    for (std::size_t i = 0; i < task_count; ++i) {
        auto handle = pool.submit_with_handle([&executions, i]() {
            ++executions[i];
            std::this_thread::yield();
            return i;
        });
        handles.push_back(std::move(handle));
    }

    std::mutex start_mtx;
    std::condition_variable start_cv;
    bool start = false;

    std::vector<int> cancel_results(task_count, 0);

    std::vector<std::thread> cancellers;
    cancellers.reserve(canceller_count);

    for (std::size_t c = 0; c < canceller_count; ++c) {
        cancellers.emplace_back([&handles, &cancel_results, &start_cv, &start_mtx, &start, c,
                                 canceller_count, task_count]() {
            {
                std::unique_lock<std::mutex> lock(start_mtx);
                start_cv.wait(lock, [&]() { return start; });
            }
            for (std::size_t i = c; i < task_count; i += canceller_count) {

                if (handles[i].cancel()) {
                    cancel_results[i] = 1;
                } else {
                    cancel_results[i] = 2;
                }
            }
        });
    }

    std::thread resumer([&]() {
        {
            std::unique_lock<std::mutex> lock(start_mtx);
            start_cv.wait(lock, [&]() { return start; });
        }

        pool.resume();
    });
    {
        std::lock_guard<std::mutex> lock(start_mtx);
        start = true;
    }
    start_cv.notify_all();

    for (auto& t : cancellers) {
        t.join();
    }
    resumer.join();
    pool.wait_for_tasks();
    std::size_t cancelled_count = 0;
    std::size_t executed_count = 0;
    for (std::size_t i = 0; i < task_count; ++i) {
        if (cancel_results[i] == 1) {
            ++cancelled_count;

            EXPECT_EQ(executions[i].load(), 0);
            EXPECT_TRUE(handles[i].is_cancelled());

            EXPECT_THROW(handles[i].get(), TaskCancelledException);
        } else {
            ++executed_count;

            EXPECT_EQ(executions[i].load(), 1);
            EXPECT_FALSE(handles[i].is_cancelled());

            EXPECT_EQ(handles[i].get(), i);
        }
    }
    EXPECT_EQ(cancelled_count + executed_count, task_count);
    EXPECT_EQ(pool.get_tasks_total(), 0u);
}

TEST(ThreadPoolStressTest, RandomConcurrentOperations) {
    ThreadPool pool(8);

    const std::size_t actor_count = 4;
    const std::size_t operations_per_actor = 5000;

    std::atomic<std::size_t> executed{0};

    std::vector<std::thread> actors;
    actors.reserve(actor_count);

    for (std::size_t id = 0; id < actor_count; ++id) {
        actors.emplace_back([&pool, &executed, id, operations_per_actor]() {
            // 每个 actor 使用独立、固定 seed 的随机数生成器
            // 随机的同时尽量保持测试可复现
            std::mt19937 rng(static_cast<unsigned int>(12345 + id));

            std::uniform_int_distribution<int> action(0, 4);

            for (std::size_t i = 0; i < operations_per_actor; ++i) {

                switch (action(rng)) {
                case 0:
                    // 普通提交
                    pool.submit([&executed]() { ++executed; });
                    break;

                case 1:
                    // 暂停 worker 领取新任务
                    pool.pause();
                    break;

                case 2:
                    // 恢复 worker
                    pool.resume();
                    break;

                case 3:
                    // 清除仍在队列中的任务
                    pool.purge();
                    break;

                case 4: {
                    // 制造 cancel 与 worker 的竞争
                    auto handle = pool.submit_with_handle([&executed]() {
                        ++executed;
                        return 1;
                    });

                    handle.cancel();
                    break;
                }

                default:
                    break;
                }

                // 增加不同 actor / worker 之间的调度交错
                // correctness 不依赖 yield
                std::this_thread::yield();
            }
        });
    }
    for (auto& actor : actors) {
        actor.join();
    }

    // 最后一个随机操作可能恰好是 pause()
    // 如果不恢复，队列中若还有任务，wait_for_tasks()
    pool.resume();

    pool.wait_for_tasks();

    EXPECT_EQ(pool.get_tasks_queued(), 0u);
    EXPECT_EQ(pool.get_tasks_running(), 0u);
    EXPECT_EQ(pool.get_tasks_total(), 0u);
}