#include "thread_pool.hpp"
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <future>
#include <chrono>

using namespace std;

void test_1() {
    ThreadPool pool(4);
    std::mutex mtx;
    int cont = 0;
    for (int i = 0; i < 100; i++) {
        pool.submit([&]() {
            std::unique_lock<std::mutex> lock(mtx);
            ++cont;
        });
    }

    pool.wait_for_tasks();
    assert(cont == 100);
    assert(pool.get_tasks_queued() == 0);
    assert(pool.get_tasks_running() == 0);
    assert(pool.get_tasks_total() == 0);
}

void test_2() {
    ThreadPool pool(2);
    std::mutex mtx;
    std::condition_variable cv;
    int started = 0, cont = 0;
    bool can_continue = false;

    auto block_task = [&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            ++started;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return can_continue; });
        ++cont;
    };

    pool.submit(block_task);
    pool.submit(block_task);

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return started == 2; });
    }
    assert(pool.get_tasks_queued() == 0);
    assert(pool.get_tasks_running() == 2);
    assert(pool.get_tasks_total() == 2);

    // 在另一个线程调用 wait_for_tasks()
    auto wait_future = std::async(std::launch::async, [&]() { pool.wait_for_tasks(); });

    // 两个任务仍然被阻塞，因此 wait_for_tasks() 不能返回
    auto status = wait_future.wait_for(std::chrono::milliseconds(50));

    assert(status == std::future_status::timeout);
    {
        std::unique_lock<std::mutex> lock(mtx);
        can_continue = true;
    }
    cv.notify_all();
    status = wait_future.wait_for(std::chrono::seconds(1));

    assert(status == std::future_status::ready);
    wait_future.get();
    assert(cont == 2);
    assert(pool.get_tasks_queued() == 0);
    assert(pool.get_tasks_running() == 0);
    assert(pool.get_tasks_total() == 0);
}

void test_3() {
    ThreadPool pool(2);
    std::mutex mtx;
    std::condition_variable cv;
    int started = 0, cont = 0;
    bool can_continue = false;

    auto block_task = [&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            ++started;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return can_continue; });
        ++cont;
    };

    pool.submit(block_task);
    pool.submit(block_task);

    for (int i = 0; i < 3; ++i) {
        pool.submit([&]() {
            std::unique_lock<std::mutex> lock(mtx);
            ++cont;
        });
    }

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return started == 2; });
        assert(pool.get_tasks_total() == 5);
        assert(pool.get_tasks_queued() == 3);
        assert(pool.get_tasks_running() == 2);
        can_continue = true;
    }
    cv.notify_all();
    pool.wait_for_tasks();
    assert(cont == 5);
    assert(pool.get_tasks_queued() == 0);
    assert(pool.get_tasks_running() == 0);
    assert(pool.get_tasks_total() == 0);
}

int main() {
    test_1();
    test_2();
    test_3();
    return 0;
}