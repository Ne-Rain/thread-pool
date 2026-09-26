#include "thread_pool.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

const std::size_t TASK_COUNT = 100000;
const std::size_t WARMUP_TASK_COUNT = 1000;
const std::size_t ROUNDS = 10;

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());

    const std::size_t n = values.size();

    if (n % 2 == 1) {
        return values[n / 2];
    }

    return (values[n / 2 - 1] +
            values[n / 2]) /
           2.0;
}

double average(const std::vector<double>& values) {
    const double total =
        std::accumulate(
            values.begin(),
            values.end(),
            0.0);

    return total /
           static_cast<double>(values.size());
}

void warm_up(ThreadPool& pool) {
    for (std::size_t i = 0;
         i < WARMUP_TASK_COUNT;
         ++i) {
        pool.submit([]() {});
    }

    pool.wait_for_tasks();
}

void print_summary(
    const std::string& name,
    std::size_t workers,
    const std::vector<double>& times) {

    const double average_time =
        average(times);

    const double median_time =
        median(times);

    const double average_throughput =
        static_cast<double>(TASK_COUNT) /
        average_time;

    const double median_throughput =
        static_cast<double>(TASK_COUNT) /
        median_time;

    std::cout
        << "\n"
        << name
        << " summary"
        << "\nworkers = "
        << workers
        << '\n'
        << "average time       = "
        << average_time
        << " s\n"
        << "median time        = "
        << median_time
        << " s\n"
        << "average throughput = "
        << average_throughput
        << " tasks/s\n"
        << "median throughput  = "
        << median_throughput
        << " tasks/s\n";
}

/*
 * 1. End-to-end
 *
 * 测量：
 *
 * submit
 *   +
 * enqueue
 *   +
 * worker dequeue
 *   +
 * task execution
 *   +
 * wait_for_tasks
 */
std::vector<double>
benchmark_end_to_end(std::size_t workers) {
    std::vector<double> times;
    times.reserve(ROUNDS);

    for (std::size_t round = 0;
         round < ROUNDS;
         ++round) {

        ThreadPool pool(workers);

        warm_up(pool);

        const auto start =
            std::chrono::steady_clock::now();

        for (std::size_t i = 0;
             i < TASK_COUNT;
             ++i) {

            pool.submit([]() {});
        }

        pool.wait_for_tasks();

        const auto end =
            std::chrono::steady_clock::now();

        const double seconds =
            std::chrono::duration<double>(
                end - start)
                .count();

        times.push_back(seconds);

        std::cout
            << "EndToEnd"
            << " workers=" << workers
            << " round=" << (round + 1)
            << " time=" << seconds
            << " s\n";
    }

    return times;
}

/*
 * 2. Enqueue-only
 *
 * pool.pause() 后 worker 不会消费任务。
 *
 * 所以计时区间主要包含：
 *
 * submit
 *   ↓
 * packaged_task / future
 *   ↓
 * std::function
 *   ↓
 * mutex
 *   ↓
 * priority_queue::push
 *
 * resume + drain 不计入时间。
 */
std::vector<double>
benchmark_enqueue_only(std::size_t workers) {
    std::vector<double> times;
    times.reserve(ROUNDS);

    for (std::size_t round = 0;
         round < ROUNDS;
         ++round) {

        ThreadPool pool(workers);

        warm_up(pool);

        pool.pause();

        const auto start =
            std::chrono::steady_clock::now();

        for (std::size_t i = 0;
             i < TASK_COUNT;
             ++i) {

            pool.submit([]() {});
        }

        const auto end =
            std::chrono::steady_clock::now();

        const double seconds =
            std::chrono::duration<double>(
                end - start)
                .count();

        times.push_back(seconds);

        /*
         * 清理本轮任务。
         *
         * 注意：
         * 这两步必须放在计时结束之后，
         * 否则就不再是 enqueue-only。
         */
        pool.resume();
        pool.wait_for_tasks();

        std::cout
            << "EnqueueOnly"
            << " workers=" << workers
            << " round=" << (round + 1)
            << " time=" << seconds
            << " s\n";
    }

    return times;
}

/*
 * 3. Drain-only
 *
 * 先暂停 pool，
 * 把所有任务提前放进队列。
 *
 * 正式计时只包含：
 *
 * resume
 *   ↓
 * workers竞争队列
 *   ↓
 * dequeue
 *   ↓
 * 执行空任务
 *   ↓
 * running_tasks_维护
 *   ↓
 * wait_for_tasks
 */
std::vector<double>
benchmark_drain_only(std::size_t workers) {
    std::vector<double> times;
    times.reserve(ROUNDS);

    for (std::size_t round = 0;
         round < ROUNDS;
         ++round) {

        ThreadPool pool(workers);

        warm_up(pool);

        pool.pause();

        /*
         * 提前准备好所有任务。
         * 这一段不计时。
         */
        for (std::size_t i = 0;
             i < TASK_COUNT;
             ++i) {

            pool.submit([]() {});
        }

        const auto start =
            std::chrono::steady_clock::now();

        pool.resume();
        pool.wait_for_tasks();

        const auto end =
            std::chrono::steady_clock::now();

        const double seconds =
            std::chrono::duration<double>(
                end - start)
                .count();

        times.push_back(seconds);

        std::cout
            << "DrainOnly"
            << " workers=" << workers
            << " round=" << (round + 1)
            << " time=" << seconds
            << " s\n";
    }

    return times;
}

} // namespace

int main() {
    const std::vector<std::size_t>
        worker_counts{
            1,
            2,
            4,
            8
        };

    std::cout
        << std::fixed
        << std::setprecision(6);

    /*
     * 第一组：
     * End-to-end
     */
    std::cout
        << "\n"
        << "========================================\n"
        << "END-TO-END BENCHMARK\n"
        << "========================================\n";

    for (std::size_t workers :
         worker_counts) {

        const std::vector<double> times =
            benchmark_end_to_end(workers);

        print_summary(
            "EndToEnd",
            workers,
            times);
    }

    /*
     * 第二组：
     * Enqueue-only
     */
    std::cout
        << "\n"
        << "========================================\n"
        << "ENQUEUE-ONLY BENCHMARK\n"
        << "========================================\n";

    for (std::size_t workers :
         worker_counts) {

        const std::vector<double> times =
            benchmark_enqueue_only(workers);

        print_summary(
            "EnqueueOnly",
            workers,
            times);
    }

    /*
     * 第三组：
     * Drain-only
     */
    std::cout
        << "\n"
        << "========================================\n"
        << "DRAIN-ONLY BENCHMARK\n"
        << "========================================\n";

    for (std::size_t workers :
         worker_counts) {

        const std::vector<double> times =
            benchmark_drain_only(workers);

        print_summary(
            "DrainOnly",
            workers,
            times);
    }

    return 0;
}