#include "thread_pool.hpp"
#include <cassert>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std;

void test_failed() {
    ThreadPool pool(1);
    auto future = pool.submit([]() -> int {
        throw std::runtime_error("task failed");
        return 0;
    });

    try {
        future.get();
        assert(false && "Expected exception was not thrown");
    } catch (const std::runtime_error& e) {
        assert(std::string(e.what()) == "task failed");
    } catch (...) {
        assert(false && "Caught unexpected exception type");
    }
}

void test_failed2() {
    ThreadPool pool(1);
    auto future = pool.submit([]() -> int {
        throw std::runtime_error("task failed");
        return 0;
    });

    auto future2 = pool.submit([]() -> int { return 42; });

    try {
        future.get();
        assert(false && "Expected exception was not thrown");
    } catch (const std::runtime_error& e) {
        assert(std::string(e.what()) == "task failed");
    } catch (...) {
        assert(false && "Caught unexpected exception type");
    }

    assert(future2.get() == 42);
}

void test_value() {
    int value = 10;
    ThreadPool pool(2);
    auto future = pool.submit(
        [](int x) {
            x = 20;
            return x;
        },
        value);
    future.get();
    assert(value == 10);
}

void test_ref() {
    int value = 10;
    ThreadPool pool(1);
    auto future = pool.submit([](int& x) { x = 20; }, std::ref(value));
    future.get();
    assert(value == 20);
}

struct Multiplier {
    int factor = 2;

    int operator()(int value) const { return factor * value; }
};
void test_obj() {
    Multiplier m;
    ThreadPool pool(1);
    auto future = pool.submit(m, 3);
    assert(future.get() == 6);
}

struct Calculator {
    int multiply(int lhs, int rhs) const { return lhs * rhs; }
};
void test_class() {
    Calculator c;
    ThreadPool pool(1);
    auto future = pool.submit(&Calculator::multiply, &c, 1, 2);
    assert(future.get() == 2);
}

void test_move_value() {
    ThreadPool pool(1);
    auto future = pool.submit([]() { return std::unique_ptr<int>(new int(2026)); });

    std::unique_ptr<int> res = future.get();
    assert(*res == 2026);
}

void comuser(ThreadPool& pool, std::vector<std::vector<std::future<int>>>& vec, std::mutex& mtx) {
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 300; i++) {
        auto future = pool.submit([i]() { return i; });
        futures.push_back(std::move(future));
    }
    std::unique_lock<std::mutex> lock(mtx);
    vec.push_back(std::move(futures));
}

void producer(int producer_id, ThreadPool& pool,
              std::vector<std::vector<std::future<int>>>& results, std::mutex& results_mutex) {

    std::vector<std::future<int>> futures;
    futures.reserve(300);

    for (int task_id = 0; task_id < 300; ++task_id) {
        futures.push_back(
            pool.submit([producer_id, task_id]() { return producer_id * 1000 + task_id; }));
    }

    std::lock_guard<std::mutex> lock(results_mutex);
    results[producer_id] = std::move(futures);
}
void test_mutli() {
    std::vector<std::thread> threads;
    std::mutex mtx;
    threads.reserve(8);
    ThreadPool pool(4);
    std::vector<std::vector<std::future<int>>> results(8);
    for (int producer_id = 0; producer_id < 8; ++producer_id) {
        threads.emplace_back(producer, producer_id, std::ref(pool), std::ref(results),
                             std::ref(mtx));
    }
    for (auto& t : threads) {
        t.join();
    }
    for (int producer_id = 0; producer_id < 8; ++producer_id) {
        for (int task_id = 0; task_id < 300; ++task_id) {
            assert(results[producer_id][task_id].get() == producer_id * 1000 + task_id);
        }
    }
}

void test_pressure() {
    std::vector<std::future<int>> vec;
    {
        ThreadPool pool(4);
        for (int i = 0; i < 200; ++i) {
            if (i % 10 == 0) {
                auto future = pool.submit([i]() {
                    throw std::runtime_error("task failed");
                    return 0;
                });
                vec.push_back(std::move(future));
            } else {
                auto future = pool.submit([i]() { return i; });
                vec.push_back(std::move(future));
            }
        }
    }

    for (int i = 0; i < 200; ++i) {
        if (i % 10 == 0) {
            try {
                vec[i].get();
                assert(false && "Expected exception was not thrown");
            } catch (const std::runtime_error& e) {
                assert(std::string(e.what()) == "task failed");
            } catch (...) {
                assert(false && "Caught unexpected exception type");
            }
        } else {
            assert(vec[i].get() == i);
        }
    }
}

int main() {
    test_failed();
    test_failed2();
    test_value();
    test_ref();
    test_obj();
    test_class();
    test_move_value();
    test_mutli();
    test_pressure();
    return 0;
}
