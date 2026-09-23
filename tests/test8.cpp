#include "thread_pool.hpp"
#include <atomic>
#include <cassert>

void test_1() {
    ThreadPool pool(1);
    pool.pause();

    std::atomic<int> executed{0};
    auto handle = pool.submit_with_handle([&]() {
        ++executed;
        return 23;
    });
    bool res = handle.cancel();
    assert(res == true);
    assert(handle.is_cancelled());

    bool caught = false;
    try {
        handle.get();
    } catch (const TaskCancelledException&) {
        caught = true;
    }
    assert(caught);
    pool.resume();
    pool.wait_for_tasks();
    assert(executed == 0);
}

void test_2() {
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
    assert(result == false);
    assert(!handle.is_cancelled());
    {
        std::unique_lock<std::mutex> lock(mtx);
        can_finish = true;
    }
    cv.notify_all();
    assert(handle.get() == 42);
}

void test_3() {
    ThreadPool pool(2);
    auto handle = pool.submit_with_handle([]() { return 123; });
    assert(handle.get() == 123);
    assert(!handle.is_cancelled());
}

void test_4() {
    ThreadPool pool(1);
    auto handle = pool.submit_with_handle([]() -> int { throw std::runtime_error("boom"); });
    bool caught = false;
    try {
        handle.get();
    } catch (const std::runtime_error& e) {
        caught = true;
    }
    assert(caught);
    assert(!handle.is_cancelled());
}

void test_5() {
    ThreadPool pool(1);
    pool.pause();
    std::atomic<int> executed{0};
    auto handle = pool.submit_with_handle([&]() {
        ++executed;
        return 10;
    });
    pool.purge();
    assert(handle.is_cancelled());
    assert(executed.load() == 0);
    try {
        handle.get();
        assert(false);
    } catch (const TaskCancelledException&) {
    }
}

void test_6() {
    ThreadPool pool(1);
    pool.pause();
    auto h1 = pool.submit_with_handle([]() { return 10; });
    auto h2 = std::move(h1);
    assert(!h1.valid());
    assert(h1.cancel() == false);
    assert(h2.valid());
    assert(h2.cancel() == true);
    assert(h2.is_cancelled());
}

int main() {
    test_1();
    test_2();
    test_3();
    test_4();
    test_5();
    test_6();
}