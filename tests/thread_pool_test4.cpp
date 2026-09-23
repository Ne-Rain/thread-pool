#include "thread_pool.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std;

void test_empty_pool_is_idle() {
    ThreadPool pool(2);

    assert(pool.get_tasks_queued() == 0);
    assert(pool.get_tasks_running() == 0);
    assert(pool.get_tasks_total() == 0);

    auto waiter = async(launch::async, [&pool]() { pool.wait_for_tasks(); });
    assert(waiter.wait_for(chrono::seconds(2)) == future_status::ready);
}

void test_task_counters() {
    ThreadPool pool(2);

    mutex mtx;
    condition_variable started_cv;
    condition_variable release_cv;
    int started = 0;
    bool release = false;

    for (int i = 0; i < 2; ++i) {
        pool.enqueue([&]() {
            unique_lock<mutex> lock(mtx);
            ++started;
            started_cv.notify_one();
            release_cv.wait(lock, [&]() { return release; });
        });
    }

    {
        unique_lock<mutex> lock(mtx);
        const bool both_started =
            started_cv.wait_for(lock, chrono::seconds(2), [&]() { return started == 2; });
        assert(both_started);
    }

    // 两个工作线程均被占用，因此下面三个任务应稳定地留在队列中。
    for (int i = 0; i < 3; ++i) {
        pool.enqueue([]() {});
    }

    assert(pool.get_tasks_running() == 2);
    assert(pool.get_tasks_queued() == 3);
    assert(pool.get_tasks_total() == 5);

    {
        lock_guard<mutex> lock(mtx);
        release = true;
    }
    release_cv.notify_all();

    pool.wait_for_tasks();

    assert(pool.get_tasks_running() == 0);
    assert(pool.get_tasks_queued() == 0);
    assert(pool.get_tasks_total() == 0);
}

void test_wait_does_not_confuse_empty_queue_with_idle_pool() {
    ThreadPool pool(1);

    mutex mtx;
    condition_variable started_cv;
    condition_variable release_cv;
    bool started = false;
    bool release = false;

    pool.enqueue([&]() {
        unique_lock<mutex> lock(mtx);
        started = true;
        started_cv.notify_one();
        release_cv.wait(lock, [&]() { return release; });
    });

    {
        unique_lock<mutex> lock(mtx);
        const bool task_started =
            started_cv.wait_for(lock, chrono::seconds(2), [&]() { return started; });
        assert(task_started);
    }

    // 此时任务已从队列中取出：queued == 0，但 running == 1。
    assert(pool.get_tasks_queued() == 0);
    assert(pool.get_tasks_running() == 1);

    promise<void> waiter_started_promise;
    future<void> waiter_started = waiter_started_promise.get_future();

    auto waiter = async(launch::async, [&]() {
        waiter_started_promise.set_value();
        pool.wait_for_tasks();
    });

    waiter_started.wait();
    assert(waiter.wait_for(chrono::milliseconds(100)) == future_status::timeout);

    {
        lock_guard<mutex> lock(mtx);
        release = true;
    }
    release_cv.notify_one();

    assert(waiter.wait_for(chrono::seconds(2)) == future_status::ready);
    assert(pool.get_tasks_total() == 0);
}

void test_wait_supports_multiple_waiters() {
    ThreadPool pool(1);

    mutex mtx;
    condition_variable started_cv;
    condition_variable release_cv;
    bool started = false;
    bool release = false;

    pool.enqueue([&]() {
        unique_lock<mutex> lock(mtx);
        started = true;
        started_cv.notify_one();
        release_cv.wait(lock, [&]() { return release; });
    });

    {
        unique_lock<mutex> lock(mtx);
        const bool task_started =
            started_cv.wait_for(lock, chrono::seconds(2), [&]() { return started; });
        assert(task_started);
    }

    auto waiter1 = async(launch::async, [&]() { pool.wait_for_tasks(); });
    auto waiter2 = async(launch::async, [&]() { pool.wait_for_tasks(); });

    assert(waiter1.wait_for(chrono::milliseconds(100)) == future_status::timeout);
    assert(waiter2.wait_for(chrono::milliseconds(100)) == future_status::timeout);

    {
        lock_guard<mutex> lock(mtx);
        release = true;
    }
    release_cv.notify_one();

    assert(waiter1.wait_for(chrono::seconds(2)) == future_status::ready);
    assert(waiter2.wait_for(chrono::seconds(2)) == future_status::ready);
}

void test_pool_can_be_reused_after_wait() {
    ThreadPool pool(4);
    atomic<int> completed{0};

    for (int i = 0; i < 200; ++i) {
        pool.enqueue([&completed]() { ++completed; });
    }

    pool.wait_for_tasks();
    assert(completed.load() == 200);
    assert(pool.get_tasks_total() == 0);

    for (int i = 0; i < 300; ++i) {
        pool.enqueue([&completed]() { ++completed; });
    }

    pool.wait_for_tasks();
    assert(completed.load() == 500);
    assert(pool.get_tasks_total() == 0);
}

void test_wait_after_failed_submitted_task() {
    ThreadPool pool(2);
    atomic<bool> second_task_completed{false};

    auto failed = pool.submit([]() -> int { throw runtime_error("stage4 failure"); });

    auto succeeded = pool.submit([&second_task_completed]() {
        second_task_completed = true;
        return 42;
    });

    pool.wait_for_tasks();

    assert(second_task_completed.load());
    assert(succeeded.get() == 42);

    try {
        (void)failed.get();
        assert(false && "Expected exception was not thrown");
    } catch (const runtime_error& e) {
        assert(string(e.what()) == "stage4 failure");
    } catch (...) {
        assert(false && "Caught unexpected exception type");
    }

    assert(pool.get_tasks_total() == 0);
}

void producer(ThreadPool& pool, atomic<int>& completed, int task_count) {
    for (int i = 0; i < task_count; ++i) {
        pool.enqueue([&completed]() { ++completed; });
    }
}

void test_wait_after_concurrent_producers_finish() {
    const int producer_count = 8;
    const int tasks_per_producer = 500;

    ThreadPool pool(4);
    atomic<int> completed{0};
    vector<thread> producers;
    producers.reserve(producer_count);

    for (int i = 0; i < producer_count; ++i) {
        producers.emplace_back(producer, ref(pool), ref(completed), tasks_per_producer);
    }

    for (auto& producer_thread : producers) {
        producer_thread.join();
    }

    // 阶段 4 的基础语义：所有生产者停止提交后，再等待线程池进入空闲状态。
    pool.wait_for_tasks();

    assert(completed.load() == producer_count * tasks_per_producer);
    assert(pool.get_tasks_total() == 0);
}

int main() {
    test_empty_pool_is_idle();
    test_task_counters();
    test_wait_does_not_confuse_empty_queue_with_idle_pool();
    test_wait_supports_multiple_waiters();
    test_pool_can_be_reused_after_wait();
    test_wait_after_failed_submitted_task();
    test_wait_after_concurrent_producers_finish();
    return 0;
}