#include "thread_pool.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {

const std::size_t WORKERS = 16;
const std::size_t TOTAL_WORK = 100000000;
const std::size_t ROUNDS = 5;
const std::size_t WARMUP_TASKS = 100;

struct BenchmarkCase {
    std::size_t task_count;
    std::size_t work_per_task;
};

std::uint64_t cpu_work(std::uint64_t seed, std::size_t work_count) {

    std::uint64_t x = seed + 0x9e3779b97f4a7c15ULL;

    for (std::size_t i = 0; i < work_count; ++i) {

        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;

        x *= 2685821657736338717ULL;

        x += static_cast<std::uint64_t>(i) + 0x9e3779b97f4a7c15ULL;
    }

    return x;
}

double average(const std::vector<double>& values) {

    const double total = std::accumulate(values.begin(), values.end(), 0.0);

    return total / static_cast<double>(values.size());
}

double median(std::vector<double> values) {

    std::sort(values.begin(), values.end());

    const std::size_t n = values.size();

    if (n % 2 == 1) {
        return values[n / 2];
    }

    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

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
 * 串行执行。
 *
 * 每种任务粒度都单独测一遍，
 * 用来得到这一组 workload 的串行基线。
 */
std::vector<double> benchmark_serial(const BenchmarkCase& benchmark_case,
                                     std::uint64_t& expected_checksum) {

    std::vector<double> times;
    times.reserve(ROUNDS);

    for (std::size_t round = 0; round < ROUNDS; ++round) {

        std::vector<std::uint64_t> results(benchmark_case.task_count);

        const auto start = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < benchmark_case.task_count; ++i) {

            results[i] = cpu_work(static_cast<std::uint64_t>(i + 1), benchmark_case.work_per_task);
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
    }

    return times;
}

/*
 * 固定 16 workers，
 * 只改变 task_count / work_per_task。
 */
std::vector<double> benchmark_thread_pool(const BenchmarkCase& benchmark_case,
                                          std::uint64_t expected_checksum) {

    std::vector<double> times;
    times.reserve(ROUNDS);

    for (std::size_t round = 0; round < ROUNDS; ++round) {

        ThreadPool pool(WORKERS);

        warm_up(pool);

        std::vector<std::uint64_t> results(benchmark_case.task_count);

        const auto start = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < benchmark_case.task_count; ++i) {

            pool.submit([i, &results, work_per_task = benchmark_case.work_per_task]() {
                results[i] = cpu_work(static_cast<std::uint64_t>(i + 1), work_per_task);
            });
        }

        pool.wait_for_tasks();

        const auto end = std::chrono::steady_clock::now();

        const double seconds = std::chrono::duration<double>(end - start).count();

        times.push_back(seconds);

        const std::uint64_t current_checksum = checksum(results);

        if (current_checksum != expected_checksum) {

            throw std::runtime_error("parallel checksum mismatch");
        }
    }

    return times;
}

void print_summary(const BenchmarkCase& benchmark_case, const std::vector<double>& serial_times,
                   const std::vector<double>& pool_times) {

    const double serial_average = average(serial_times);

    const double serial_median = median(serial_times);

    const double pool_average = average(pool_times);

    const double pool_median = median(pool_times);

    const double speedup = serial_median / pool_median;

    const double efficiency = speedup / static_cast<double>(WORKERS);

    const double task_rate = static_cast<double>(benchmark_case.task_count) / pool_median;

    std::cout << "\n========================================\n"
              << "task_count    = " << benchmark_case.task_count << '\n'
              << "work_per_task = " << benchmark_case.work_per_task << '\n'
              << "total_work    = " << benchmark_case.task_count * benchmark_case.work_per_task
              << '\n'
              << "----------------------------------------\n"
              << "serial average = " << serial_average << " s\n"
              << "serial median  = " << serial_median << " s\n"
              << '\n'
              << "pool average   = " << pool_average << " s\n"
              << "pool median    = " << pool_median << " s\n"
              << '\n'
              << "speedup        = " << speedup << "x\n"
              << "efficiency     = " << efficiency * 100.0 << "%\n"
              << "task rate      = " << task_rate << " tasks/s\n";
}

} // namespace

int main() {
    /*
     * 每组：
     *
     * task_count * work_per_task
     * = 100,000,000
     *
     * 所以总 CPU 工作量基本保持一致。
     */
    const std::vector<BenchmarkCase> cases{{100000, 1000}, {10000, 10000}, {1000, 100000},
                                           {100, 1000000}, {16, 6250000},  {8, 12500000}};

    std::cout << std::fixed << std::setprecision(6);

    std::cout << "workers    = " << WORKERS << '\n' << "total work = " << TOTAL_WORK << '\n';

    for (const BenchmarkCase& benchmark_case : cases) {

        if (benchmark_case.task_count * benchmark_case.work_per_task != TOTAL_WORK) {

            throw std::runtime_error("invalid benchmark case");
        }

        std::uint64_t expected_checksum = 0;

        const std::vector<double> serial_times =
            benchmark_serial(benchmark_case, expected_checksum);

        const std::vector<double> pool_times =
            benchmark_thread_pool(benchmark_case, expected_checksum);

        print_summary(benchmark_case, serial_times, pool_times);
    }

    return 0;
}