#include "thread_pool.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

void test_1() {
    ThreadPool pool(4);
    std::vector<int> vec(100, 0);
    std::mutex mtx;
    pool.parallel_for(0, 100, [&](int i) {
        std::unique_lock<std::mutex> lock(mtx);
        vec[i]++;
    });
    for (auto t : vec) {
        if (t != 1)
            assert(false);
    }
}

void test_2() {
    ThreadPool pool(3);
    std::vector<int> vec(10, 0);
    std::mutex mtx;
    pool.parallel_for(0, 10, [&](int i) {
        std::unique_lock<std::mutex> lock(mtx);
        vec[i]++;
    });
    for (auto t : vec) {
        if (t != 1)
            assert(false);
    }
}

void test_3() {
    ThreadPool pool(8);
    std::vector<int> vec(2, 0);
    std::mutex mtx;
    pool.parallel_for(0, 2, [&](int i) {
        std::unique_lock<std::mutex> lock(mtx);
        vec[i]++;
    });
    for (auto t : vec) {
        if (t != 1)
            assert(false);
    }
}

void test_4() {
    ThreadPool pool(4);
    std::vector<int> vec(10, 0);
    std::mutex mtx;
    pool.parallel_for(-5, 5, [&](int i) {
        std::unique_lock<std::mutex> lock(mtx);
        vec[i + 5]++;
    });
    for (auto t : vec) {
        if (t != 1)
            assert(false);
    }
}

void test_5() {
    ThreadPool pool(4);
    std::vector<int> vec(10, 0);
    std::mutex mtx;
    pool.parallel_for(5, 5, [&](int i) {
        std::unique_lock<std::mutex> lock(mtx);
        vec[i]++;
    });
    for (auto t : vec) {
        if (t != 0)
            assert(false);
    }
}

void test_6() {
    ThreadPool pool(4);
    std::vector<int> vec(10, 0);
    std::mutex mtx;
    try {
        pool.parallel_for(8, 5, [&](int i) {
            std::unique_lock<std::mutex> lock(mtx);
            vec[i]++;
        });
        assert(false);
    } catch (const std::invalid_argument& e) {
        assert(std::string(e.what()) == "last must not be less than first");
    } catch (...) {
        assert(false);
    }
}

void test_7() {
    ThreadPool pool(4);
    std::mutex task_mtx;
    std::condition_variable cv;
    bool can_continue = false;
    pool.submit([&]() {
        std::unique_lock<std::mutex> lock(task_mtx);
        cv.wait(lock, [&can_continue]() { return can_continue; });
    });

    std::vector<int> vec(100, 0);
    std::mutex mtx;
    auto future = std::async(std::launch::async, [&]() {
        pool.parallel_for(0, 100, [&](int i) {
            std::unique_lock<std::mutex> lock(mtx);
            vec[i]++;
        });
    });

    auto status = future.wait_for(std::chrono::seconds(1));
    assert(status == std::future_status::ready);
    future.get();

    for (auto t : vec) {
        if (t != 1)
            assert(false);
    }

    {
        std::unique_lock<std::mutex> lock(task_mtx);
        can_continue = true;
    }
    cv.notify_all();
}

void test_8() {
    ThreadPool pool(2);
    std::mutex task_mtx;
    std::condition_variable cv;
    bool can_continue = false;
    bool block_started = false;

    auto future = std::async(std::launch::async, [&]() {
        pool.parallel_for(0, 2, [&](int i) {
            if (i == 0)
                throw std::runtime_error("boom");
            std::unique_lock<std::mutex> lock(task_mtx);
            block_started = true;
            cv.notify_all();
            cv.wait(lock, [&]() { return can_continue; });
        });
    });

    {
        std::unique_lock<std::mutex> lock(task_mtx);
        cv.wait(lock, [&]() { return block_started; });
    }
    auto status = future.wait_for(std::chrono::seconds(1));
    assert(status == std::future_status::timeout);

    {
        std::unique_lock<std::mutex> lock(task_mtx);
        can_continue = true;
    }
    cv.notify_all();
    status = future.wait_for(std::chrono::seconds(1));
    assert(status == std::future_status::ready);
    try {
        future.get();
        assert(false);
    } catch (const std::runtime_error& e) {
        assert(std::string(e.what()) == "boom");
    } catch (...) {
        assert(false);
    }
}

class MoveOnlyCallable {
public:
    MoveOnlyCallable() : ptr(new int(10)) {}

    MoveOnlyCallable(const MoveOnlyCallable&) = delete;
    MoveOnlyCallable& operator=(const MoveOnlyCallable&) = delete;

    MoveOnlyCallable(MoveOnlyCallable&&) = default;
    MoveOnlyCallable& operator=(MoveOnlyCallable&&) = default;

    void operator()(int i) {}

private:
    std::unique_ptr<int> ptr;
};
void test_9() {
    ThreadPool pool(2);
    MoveOnlyCallable m;
    try{
        pool.parallel_for(0, 100, std::move(m));
    }catch(...){
        assert(false);
    }
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
    return 0;
}