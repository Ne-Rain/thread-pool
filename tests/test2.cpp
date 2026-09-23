#include "thread_pool.hpp"
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std;

void task1() {
    ThreadPool pool(4);
    std::atomic<int> cont(0);
    for (int i = 0; i < 10; ++i) {
        pool.submit([&cont]() { ++cont; });
    }

    pool.shutdown();
    assert(cont == 10);
}

void task2() {
    ThreadPool pool(2);
    pool.shutdown();
    try {
        pool.submit([]() { return 1; });
    } catch (const std::runtime_error&) {
        assert(true);
    } catch (...) {
        assert(false);
    }
}

void task3() {
    ThreadPool pool(2);
    std::atomic<int> counter(0);
    pool.submit([&counter]() { ++counter; });
    pool.shutdown();
    assert(counter == 1);
    pool.shutdown();
    pool.shutdown();
    assert(counter == 1);
}

void task4() {
    ThreadPool pool(2);
    std::mutex mtx;
    std::condition_variable cv;
    bool can_continue = false;
    int started = 0, completed = 0;

    auto block_task = [&](){
        std::unique_lock<std::mutex> lock(mtx);
        ++started;
        cv.notify_all();

        cv.wait(lock, [&](){
            return can_continue;
        });

        ++completed;
    };

    pool.enqueue(block_task);
    pool.enqueue(block_task);

    for(int i = 0;i < 3;++i){
        pool.enqueue([&](){
            std::unique_lock<std::mutex> lock(mtx);
            ++completed;
        });
    }

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&](){
            return started == 2;
        });
    }

    std::thread shutdown_thread([&](){
        pool.shutdown();
    });

    {
        std::unique_lock<std::mutex> lock(mtx);
        can_continue = true;
    }
    cv.notify_all();
    shutdown_thread.join();

    assert(completed == 5);
}

int main() {

    task1();
    task2();
    task3();
    task4();

    return 0;
}