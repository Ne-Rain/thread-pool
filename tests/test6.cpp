#include "thread_pool.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>

void test_1() {
    ThreadPool pool(2);
    bool logo = false;
    logo = pool.wait_for(std::chrono::milliseconds(100));
    assert(logo);
}

void test_2() {
    ThreadPool pool(2);
    pool.submit([]() -> int { return 0; });
    bool logo = false;
    logo = pool.wait_for(std::chrono::seconds(1));
    assert(logo && pool.get_tasks_total() == 0);
}

void test_3() {
    ThreadPool pool(2);
    std::mutex mtx;
    std::condition_variable cv;
    int started = 0;
    bool can_continue = false;

    auto block_task = [&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            ++started;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return can_continue; });
    };

    pool.submit(block_task);

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return started == 1; });
    }
    bool logo = pool.wait_for(std::chrono::milliseconds(50));
    assert(logo == false);
    {
        std::unique_lock<std::mutex> lock(mtx);
        can_continue = true;
    }
    cv.notify_all();
}

void test_4() {
    ThreadPool pool(2);
    std::mutex mtx;
    std::condition_variable cv;
    int started = 0;
    bool can_continue = false;

    auto block_task = [&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            ++started;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return can_continue; });
    };

    pool.submit(block_task);

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return started == 1; });
    }
    bool logo = pool.wait_for(std::chrono::milliseconds(50));
    assert(logo == false);
    {
        std::unique_lock<std::mutex> lock(mtx);
        can_continue = true;
    }
    cv.notify_all();
    logo = pool.wait_for(std::chrono::seconds(1));
    assert(logo);
}

void test_5() {
    ThreadPool pool(2);
    std::mutex mtx;
    std::condition_variable cv;
    int started = 0;
    bool can_continue = false;

    auto block_task = [&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            ++started;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return can_continue; });
    };

    pool.submit(block_task);

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return started == 1; });
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    bool logo = pool.wait_until(deadline);
    assert(logo == false);
    {
        std::unique_lock<std::mutex> lock(mtx);
        can_continue = true;
    }
    cv.notify_all();
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    logo = pool.wait_until(deadline);
    assert(logo);
}

void test_6() {
    ThreadPool pool(2);
    std::mutex mtx;
    std::condition_variable cv;
    int started = 0;
    bool can_continue = false;

    auto block_task = [&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            ++started;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return can_continue; });
    };

    pool.submit(block_task);
    pool.submit(block_task);
    std::future<int> future;
    for (int i = 0; i < 5; ++i) {
        future = pool.submit([]() { return 0; });
    }

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return started == 2; });
    }
    assert(pool.get_tasks_queued() == 5);
    assert(pool.get_tasks_running() == 2);
    pool.purge();
    assert(pool.get_tasks_running() == 2);
    assert(pool.get_tasks_queued() == 0);

    try {
        future.get(); // 测试被purge的任务
        assert(false);
    } catch (const std::future_error& e) {
        assert(e.code() == std::make_error_code(std::future_errc::broken_promise));
    } catch (...) {
        assert(false);
    }

    try {
        pool.purge(); // purge空队列
    } catch (...) {
        assert(false);
    }

    try {
        auto future2 = pool.submit([]() { return 1; }); // purge后提交
        {
            std::unique_lock<std::mutex> lock(mtx);
            can_continue = true;
        }
        cv.notify_all();
        assert(future2.get() == 1);
    } catch (...) {
        assert(false);
    }
}

void test_7() {
    ThreadPool pool(2);
    std::mutex mtx;
    std::condition_variable cv;
    int started = 0;
    bool can_continue = false;

    auto block_task = [&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            ++started;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return can_continue; });
        return 1;
    };
    auto test_future = pool.submit(block_task);

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return started == 1; });
        pool.pause();
        can_continue = true;
    }
    cv.notify_all();
    pool.wait_for_tasks();
    assert(test_future.get() == 1);

    auto future = pool.submit([]() { return 1; });
    assert(pool.get_tasks_queued() == 1);
    assert(pool.get_tasks_running() == 0);

    auto future2 = pool.submit([]() { return 2; });
    assert(pool.get_tasks_queued() == 2);

    pool.resume();
    assert(future.get() == 1);
    assert(future2.get() == 2);

    pool.pause();
    auto future3 = pool.submit([]() { return 3; });
    pool.shutdown();
    assert(future3.get() == 3);
}

void test_8() {
    ThreadPool pool(2);
    pool.reset(4);

    auto future = pool.submit([]() { return 42; });

    pool.wait_for_tasks();
    assert(pool.get_tasks_total() == 0);
    assert(future.get() == 42);
}

void test_9() {
    ThreadPool pool(2);
    std::mutex mtx;
    std::condition_variable cv;
    int started = 0;
    bool can_continue = false;

    auto block_task = [&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            ++started;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return can_continue; });
        return 1;
    };
    pool.submit(block_task);
    pool.submit(block_task);
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&started]() { return started == 2; });
    }
    for (int i = 0; i < 3; i++) {
        pool.submit([i]() { return i; });
    }
    assert(pool.get_tasks_total() == 5);

    auto future = std::async(std::launch::async, [&]() { pool.reset(4); });
    auto status = future.wait_for(std::chrono::seconds(1));
    assert(status == std::future_status::timeout);

    {
        std::unique_lock<std::mutex> lock(mtx);
        can_continue = true;
    }
    cv.notify_all();
    status = future.wait_for(std::chrono::seconds(1));
    assert(status == std::future_status::ready);
    future.get();
    assert(pool.get_tasks_total() == 0);
}

void test_10() {
    ThreadPool pool(2);
    pool.pause();

    auto f1 = pool.submit([]() { return 1; });
    auto f2 = pool.submit([]() { return 2; });
    assert(pool.get_tasks_queued() == 2);
    assert(pool.get_tasks_running() == 0);

    pool.reset(4);
    auto f3 = pool.submit([]() { return 3; });
    assert(f1.get() == 1);
    assert(f2.get() == 2);
    assert(f3.get() == 3);
}

void test_11() {
    ThreadPool pool(2);
    try {
        pool.reset(0);
        assert(false);
    } catch (const std::invalid_argument& e) {
        assert(std::string(e.what()) == "thread_count can't be set 0");
    } catch (...) {
        assert(false);
    }
    auto f = pool.submit([]() { return 1; });
    assert(f.get() == 1);
}

void test_12() {
    ThreadPool pool(2);
    pool.reset(4);
    auto f1 = pool.submit([]() { return 1; });
    pool.reset(1);
    auto f2 = pool.submit([]() { return 2; });
    pool.reset(3);
    auto f3 = pool.submit([]() { return 3; });
    assert(f1.get() == 1);
    assert(f2.get() == 2);
    assert(f3.get() == 3);
}

int main() {

    test_1();
    test_2();
    test_3();
    test_4();
    test_5();
    test_6();
    test_7();
    test_8();
    test_9();
    test_10();
    test_11();
    test_12();

    return 0;
}