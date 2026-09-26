#include "thread_pool.hpp"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <vector>

int main() {
    const std::size_t task_count = 100000;

    std::vector<std::size_t> worker_counts{
        1, 2, 4, 8
    };

    for (std::size_t workers : worker_counts) {
        ThreadPool pool(workers);

        const auto start =
            std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < task_count; ++i) {
            pool.submit([]() {});
        }

        pool.wait_for_tasks();

        const auto end =
            std::chrono::steady_clock::now();

        const auto elapsed =
            std::chrono::duration_cast<
                std::chrono::microseconds>(
                end - start);

        const double seconds =
            elapsed.count() / 1000000.0;

        const double throughput =
            task_count / seconds;

        std::cout
            << "workers = " << workers
            << ", time = " << seconds << " s"
            << ", throughput = "
            << throughput << " tasks/s"
            << '\n';
    }
}