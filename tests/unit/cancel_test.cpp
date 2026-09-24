#include "thread_pool.hpp"
#include <atomic>
#include <gtest/gtest.h>
#include <stdexcept>

TEST(TaskCancelTest, TestCancel) {
    ThreadPool pool(1);
    pool.pause();

    std::atomic<int> executed{0};
    auto handle = pool.submit_with_handle([&]() {
        ++executed;
        return 23;
    });
    bool res = handle.cancel();
    EXPECT_TRUE(res);
    EXPECT_TRUE(handle.is_cancelled());

    EXPECT_THROW(handle.get(), TaskCancelledException);

    pool.resume();
    pool.wait_for_tasks();
    EXPECT_EQ(executed.load(), 0);
}

TEST(TaskCancelTest, TestWorker) {
    ThreadPool pool(1);

    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool can_finish = false;

    auto handle = pool.submit_with_handle([&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            started = true;
            cv.notify_all();
            cv.wait(lock, [&]() { return can_finish; });
        }
        return 42;
    });
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return started; });
    }
    bool result = handle.cancel();
    EXPECT_FALSE(result);
    EXPECT_TRUE(!handle.is_cancelled());
    {
        std::unique_lock<std::mutex> lock(mtx);
        can_finish = true;
    }
    cv.notify_all();
    EXPECT_EQ(handle.get(), 42);
}

TEST(TaskCancelTest, TestRun) {
    ThreadPool pool(2);
    auto handle = pool.submit_with_handle([]() { return 123; });
    EXPECT_EQ(handle.get(), 123);
    EXPECT_TRUE(!handle.valid());
    EXPECT_TRUE(!handle.is_cancelled());
}

TEST(TaskCacelTest, TestException) {
    ThreadPool pool(1);
    auto handle = pool.submit_with_handle([]() -> int { throw std::runtime_error("boom"); });
    EXPECT_THROW(handle.get(), std::runtime_error);
    EXPECT_TRUE(!handle.is_cancelled());
}

TEST(TaskCancelTest, TestPurge) {
    ThreadPool pool(1);
    pool.pause();
    std::atomic<int> executed{0};
    auto handle = pool.submit_with_handle([&]() {
        ++executed;
        return 10;
    });
    pool.purge();
    EXPECT_TRUE(handle.is_cancelled());
    EXPECT_EQ(executed.load(), 0);
    EXPECT_THROW(handle.get(), TaskCancelledException);
    pool.wait_for_tasks();
    EXPECT_EQ(pool.get_tasks_total(), 0);
}

TEST(TaskCancelTest, TestMove) {
    ThreadPool pool(1);
    pool.pause();
    auto h1 = pool.submit_with_handle([]() { return 10; });
    auto h2 = std::move(h1);
    EXPECT_TRUE(!h1.valid());
    EXPECT_FALSE(h1.cancel());
    EXPECT_TRUE(h2.valid());
    EXPECT_TRUE(h2.cancel());
    EXPECT_TRUE(h2.is_cancelled());
}