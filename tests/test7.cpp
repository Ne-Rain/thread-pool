#include "thread_pool.hpp"
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

void test_1() {
    ThreadPool pool(1);

    std::mutex mtx;
    std::condition_variable cv;
    bool can_continue = false;
    int started = 0;
    auto f = pool.submit([&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            started++;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&can_continue]() { return can_continue == true; });
        return 1;
    });

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&started]() { return started == 1; });
    }

    std::vector<int> vec;
    std::mutex vec_mtx;
    pool.submit_priority(0, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(4);
    });
    pool.submit_priority(5, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(2);
    });
    pool.submit_priority(5, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(3);
    });
    pool.submit_priority(-1, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(5);
    });
    pool.submit_priority(10, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(1);
    });
    assert(pool.get_tasks_queued() == 5);
    {
        std::unique_lock<std::mutex> lock(mtx);
        can_continue = true;
    }
    cv.notify_all();
    pool.wait_for_tasks();
    assert(f.get() == 1);
    for (int i = 0; i < 5; i++) {
        assert(vec[i] == i + 1);
    }
}

void test_2() {
    ThreadPool pool(1);

    std::mutex mtx;
    std::condition_variable cv;
    bool can_continue = false;
    int started = 0;
    auto f = pool.submit([&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            started++;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&can_continue]() { return can_continue == true; });
        return 1;
    });

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&started]() { return started == 1; });
    }

    std::vector<int> vec;
    std::mutex vec_mtx;
    pool.submit_priority(5, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(1);
    });
    pool.submit_priority(5, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(2);
    });
    pool.submit_priority(5, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(3);
    });
    pool.submit_priority(5, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(4);
    });
    assert(pool.get_tasks_queued() == 4);
    {
        std::unique_lock<std::mutex> lock(mtx);
        can_continue = true;
    }
    cv.notify_all();
    pool.wait_for_tasks();
    assert(f.get() == 1);
    for (int i = 0; i < 4; i++) {
        assert(vec[i] == i + 1);
    }
}

void test_3() {
    ThreadPool pool(1);

    std::mutex mtx;
    std::condition_variable cv;
    bool can_continue = false;
    int started = 0;
    auto f = pool.submit([&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            started++;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&can_continue]() { return can_continue == true; });
        return 1;
    });

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&started]() { return started == 1; });
    }

    std::vector<int> vec;
    std::mutex vec_mtx;

    pool.submit([&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(2);
    });
    pool.submit_priority(10, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(1);
    });
    pool.submit([&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(3);
    });
    pool.submit_priority(-10, [&]() {
        std::unique_lock<std::mutex> lock(vec_mtx);
        vec.push_back(4);
    });
    assert(pool.get_tasks_queued() == 4);
    {
        std::unique_lock<std::mutex> lock(mtx);
        can_continue = true;
    }
    cv.notify_all();
    pool.wait_for_tasks();
    assert(f.get() == 1);
    for (int i = 0; i < 4; i++) {
        assert(vec[i] == i + 1);
    }
}

void test_4() {
    ThreadPool pool(1);
    auto f1 = pool.submit_priority(10, []() { throw std::runtime_error("boom"); });

    auto f2 = pool.submit([]() { return 2; });

    try {
        f1.get();
        assert(false);
    } catch (const std::runtime_error& e) {
        assert(std::string(e.what()) == "boom");
    } catch (...) {
        assert(false);
    }
    assert(f2.get() == 2);
}

void test_5() {
    ThreadPool pool(1);
    auto f = pool.submit([]() { return ThreadPool::get_thread_index(); });
    assert(f.get() == 0);
}

void test_6() { assert(ThreadPool::get_thread_index() == ThreadPool::npos); }

void test_7() {
    ThreadPool pool(4);
    std::vector<std::size_t> vec(4);
    std::mutex mtx;
    std::condition_variable cv;
    bool can_continue = false;
    int started = 0;
    for (int i = 0; i < 4; ++i) {
        pool.submit([&]() {
            {
                std::unique_lock<std::mutex> lock(mtx);
                started++;
                vec[ThreadPool::get_thread_index()] = 1;
            }
            cv.notify_all();
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&can_continue]() { return can_continue; });
        });
    }
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&started]() { return started == 4; });
        can_continue = true;
    }
    cv.notify_all();
    for (std::size_t i = 0; i < 4; ++i) {
        assert(vec[i] == 1);
    }
}

void test_8() {
    ThreadPool poolA(1);
    ThreadPool poolB(1);
    auto f1 = poolA.submit([]() { return ThreadPool::get_thread_index(); });
    auto f2 = poolB.submit([]() { return ThreadPool::get_thread_index(); });
    assert(f1.get() == 0);
    assert(f2.get() == 0);
}

void test_9() {
    ThreadPool pool(4);

    pool.wait_for_tasks();

    pool.reset(3);

    auto f1 = pool.submit([] { return ThreadPool::get_thread_index(); });

    auto index = f1.get();

    assert(index < 3);
}

void test_exception_worker_survival() {
    ThreadPool pool(1);

    pool.submit([] { throw std::runtime_error("error"); });

    pool.wait_for_tasks();

    auto f = pool.submit([] { return 42; });

    assert(f.get() == 42);
}

int main() {
    // test_1();
    // test_2();
    // test_3();
    // test_4();
    // test_5();
    // test_6();
    // test_7();
    // test_8();
    // test_9();
    test_exception_worker_survival();
    return 0;
}