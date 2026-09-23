#include "thread_pool.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace std;

int main() {

    // test1
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 1000; ++i) {
            pool.enqueue([&counter]() { ++counter; });
        }
    }
    assert(counter == 1000);

    // test2
    std::mutex mtx;
    std::condition_variable started_cv;
    std::condition_variable release_cv;
    int started = 0;
    bool release = false;
    {
        ThreadPool pool(4);
        for (int i = 0; i < 4; ++i) {
            pool.enqueue([&]() {
                std::unique_lock<std::mutex> lock(mtx);
                ++started;
                started_cv.notify_one();

                release_cv.wait(lock, [&]() { return release; });
            });
        }

        std::unique_lock<std::mutex> lock(mtx);

        const bool all_started = started_cv.wait_for(lock, std::chrono::seconds(2),
                                                     [&]() { return started == 4; });
        release = true;
        lock.unlock();
        release_cv.notify_all();
        assert(all_started);
    }

    // test3
    std::vector<int> test3_vec;
    {
        ThreadPool pool(1);
        for (int i = 0; i < 100; ++i) {
            pool.enqueue([i, &test3_vec]() { test3_vec.push_back(i); });
        }
    }
    for (int i = 0; i < 100; ++i) {
        assert(test3_vec[i] == i);
    }

    // test4
    std::atomic<int> completed{0};
    {
        ThreadPool pool(1);
        for (int i = 0; i < 10; ++i) {
            pool.enqueue([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ++completed;
            });
        }
    }
    assert(completed == 10);

    // test5
    { ThreadPool pool(5); }
    return 0;
}