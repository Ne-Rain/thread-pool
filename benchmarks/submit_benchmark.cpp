#include "thread_pool.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

const std::size_t TASK_COUNT = 1000;
const std::size_t WORK_PER_TASK = 100000;
const std::size_t ROUNDS = 5;
const std::size_t WARMUP_TASKS = 100;

/*
 * 一个纯 CPU-bound 任务。
 *
 * 特点：
 * 1. 没有 mutex
 * 2. 没有 sleep
 * 3. 没有 IO
 * 4. 没有动态内存分配
 * 5. 每个任务完全独立
 *
 * uint64_t 的溢出是有定义的，
 * 不会引入 signed overflow UB。
 */
std::uint64_t cpu_work(std::uint64_t seed) {
    std::uint64_t x = seed + 0x9e3779b97f4a7c15ULL;

    for (std::size_t i = 0; i < WORK_PER_TASK; ++i) {

        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;

        x *= 2685821657736338717ULL;

        x += static_cast<std::uint64_t>(i) + 0x9e3779b97f4a7c15ULL;
    }

    return x;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());

    const std::size_t n = values.size();

    if (n % 2 == 1) {
        return values[n / 2];
    }

    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

double average(const std::vector<double>& values) {

    const double total = std::accumulate(values.begin(), values.end(), 0.0);

    return total / static_cast<double>(values.size());
}

/*
 * 防止编译器认为计算结果没有用途。
 *
 * 同时后面也可以利用 checksum
 * 验证并行版本和串行版本计算结果一致。
 */
std::uint64_t checksum(const std::vector<std::uint64_t>& results) {

    std::uint64_t value = 0;

    for (std::size_t i = 0; i < results.size(); ++i) {

        value ^= results[i] + 0x9e3779b97f4a7c15ULL + (value << 6) + (value >> 2);
    }

    return value;
}

void warm_up(ThreadPool& pool) {
    for (std::size_t i = 0; i < WARMUP_TASKS; ++i) {

        pool.submit([]() {});
    }

    pool.wait_for_tasks();
}

/*
 * 串行基线
 */
std::vector<double> benchmark_serial(std::uint64_t& expected_checksum) {

    std::vector<double> times;
    times.reserve(ROUNDS);

    for (std::size_t round = 0; round < ROUNDS; ++round) {

        std::vector<std::uint64_t> results(TASK_COUNT);

        const auto start = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < TASK_COUNT; ++i) {

            results[i] = cpu_work(static_cast<std::uint64_t>(i + 1));
        }

        const auto end = std::chrono::steady_clock::now();

        const double seconds = std::chrono::duration<double>(end - start).count();

        times.push_back(seconds);

        const std::uint64_t current_checksum = checksum(results);

        if (round == 0) {
            expected_checksum = current_checksum;
        } else if (current_checksum != expected_checksum) {

            throw std::runtime_error("serial checksum mismatch");
        }

        std::cout << "Serial" << " round=" << (round + 1) << " time=" << seconds << " s\n";
    }

    return times;
}

/*
 * ThreadPool CPU benchmark
 */
std::vector<double> benchmark_thread_pool(std::size_t workers, std::uint64_t expected_checksum) {

    std::vector<double> times;
    times.reserve(ROUNDS);

    for (std::size_t round = 0; round < ROUNDS; ++round) {

        ThreadPool pool(workers);

        /*
         * 只确保 worker 已经真正启动。
         * warm-up 不进入正式计时。
         */
        warm_up(pool);

        std::vector<std::uint64_t> results(TASK_COUNT);

        const auto start = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < TASK_COUNT; ++i) {

            pool.submit(
                [i, &results]() { results[i] = cpu_work(static_cast<std::uint64_t>(i + 1)); });
        }

        pool.wait_for_tasks();

        const auto end = std::chrono::steady_clock::now();

        const double seconds = std::chrono::duration<double>(end - start).count();

        times.push_back(seconds);

        const std::uint64_t current_checksum = checksum(results);

        if (current_checksum != expected_checksum) {

            throw std::runtime_error("parallel checksum mismatch");
        }

        std::cout << "ThreadPool" << " workers=" << workers << " round=" << (round + 1)
                  << " time=" << seconds << " s\n";
    }

    return times;
}

void print_pool_summary(std::size_t workers, const std::vector<double>& times,
                        double serial_median) {

    const double average_time = average(times);

    const double median_time = median(times);

    const double speedup = serial_median / median_time;

    const double efficiency = speedup / static_cast<double>(workers);

    const double throughput = static_cast<double>(TASK_COUNT) / median_time;

    std::cout << "\nThreadPool summary\n"
              << "workers            = " << workers << '\n'

              << "average time       = " << average_time << " s\n"

              << "median time        = " << median_time << " s\n"

              << "throughput         = " << throughput << " tasks/s\n"

              << "speedup            = " << speedup << "x\n"

              << "parallel efficiency= " << efficiency * 100.0 << "%\n";
}

} // namespace

int main() {
    const std::vector<std::size_t> worker_counts{1, 2, 4, 8, 12, 16, 24, 32};

    std::cout << std::fixed << std::setprecision(6);

    std::cout << "hardware_concurrency = " << std::thread::hardware_concurrency() << "\n";

    std::cout << "task_count           = " << TASK_COUNT << "\n";

    std::cout << "work_per_task        = " << WORK_PER_TASK << "\n\n";

    /*
     * 先测真正的串行版本。
     */
    std::uint64_t expected_checksum = 0;

    const std::vector<double> serial_times = benchmark_serial(expected_checksum);

    const double serial_average = average(serial_times);

    const double serial_median = median(serial_times);

    std::cout << "\nSerial summary\n"
              << "average time = " << serial_average << " s\n"
              << "median time  = " << serial_median << " s\n"
              << "checksum     = " << expected_checksum << "\n";

    /*
     * 再测不同 worker 数。
     */
    for (std::size_t workers : worker_counts) {

        std::cout << "\n========================================\n"
                  << "workers = " << workers << "\n"
                  << "========================================\n";

        const std::vector<double> times = benchmark_thread_pool(workers, expected_checksum);

        print_pool_summary(workers, times, serial_median);
    }

    return 0;
}