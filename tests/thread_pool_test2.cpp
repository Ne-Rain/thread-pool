#include "thread_pool.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <future>
#include <thread>
#include <vector>

using namespace std;

bool com_equal(double a, double b) {
    if (std::abs(a - b) < 1e-9)
        return true;
    return false;
}

double div_test(double a, double b) { return a / b; }

void test_submit_returns_value() {
    ThreadPool pool(2);
    std::future<int> future = pool.submit([]() { return 2026; });
    assert(future.get() == 2026);
}

void test_submit_with_arguments() {
    ThreadPool pool(4);
    int base = 40;
    auto future = pool.submit([base](int value) { return base + value; }, 2);
    assert(future.get() == 42);
}

void test_submit_with_value() {
    ThreadPool pool(1);
    auto future =
        pool.submit(div_test, 10.0, 8.0); // 编译错误：No matching member function for call
    assert(com_equal(future.get(), 1.25));
}

void test_submit_var() {
    std::atomic<bool> completed{false};
    ThreadPool pool(1);
    auto future = pool.submit([&completed]() { completed = true; });

    future.get();
    assert(completed);
}

void test_submit_mutli_tasks() {
    std::vector<std::future<int>> vec;
    ThreadPool pool(4);
    for (int i = 0; i < 100; ++i) {
        auto future = pool.submit([i]() { return i * i; });
        vec.push_back(std::move(future));
    }
    for (int i = 0; i < 100; ++i) {
        assert(vec[i].get() == i * i);
    }
}

void test_submit_destructor_waits() {
    std::vector<std::future<int>> futures;
    {
        ThreadPool pool(2);
        for (int i = 0; i < 10; ++i) {
            futures.push_back(pool.submit([i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                return i;
            }));
        }
    }
    for (int i = 0; i < 10; ++i) {
        assert(futures[i].get() == i);
    }
}

void test_submit_die() {
    std::vector<std::future<int>> futures;
    {
        ThreadPool pool(2);
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto future = pool.submit([i]() { return i; });
            futures.push_back(std::move(future));
        }
    }
    for (int i = 0; i < 10; ++i) {
        assert(futures[i].get() == i);
    }
}

void test_submit_double() {
    std::vector<std::future<int>> sfutures;
    std::atomic<int> completed{0};
    {
        ThreadPool pool(2);

        for (int i = 0; i < 10; ++i) {
            auto future = pool.submit([i]() { return i; });
            sfutures.push_back(std::move(future));
            pool.enqueue([&completed]() { ++completed; });
        }
        for (int i = 0; i < 10; ++i) {
            assert(sfutures[i].get() == i);
        }
    }
    assert(completed == 10);
}

int main() {
    test_submit_returns_value();
    test_submit_with_arguments();
    test_submit_with_value();
    test_submit_var();
    test_submit_mutli_tasks();
    test_submit_destructor_waits();
    test_submit_die();
    test_submit_double();
    return 0;
}