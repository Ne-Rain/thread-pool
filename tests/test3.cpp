#include "thread_pool.hpp"
#include <cassert>
#include <stdexcept>
#include <string>

using namespace std;

void test_1() {
    ThreadPool pool(2);

    auto future = pool.submit([]() { return 91; });

    assert(future.get() == 91);
}

int add(int a, int b) { return a + b; }
void test_2() {
    ThreadPool pool(2);
    auto future = pool.submit(add, 10, 20);
    assert(future.get() == 30);
}

void test_3() {
    ThreadPool pool(3);
    auto future = pool.submit([]() { return 91; });
    auto future2 = pool.submit([]() { return 3.5; });
    auto future3 = pool.submit([]() {
        string str = "hello";
        return str;
    });

    assert(future.get() == 91);
    assert(future2.get() == 3.5);
    assert(future3.get() == "hello");
}

void test_4() {
    ThreadPool pool(2);
    int cont = 0;
    auto future = pool.submit([&]() { cont++; });
    try {
        future.get();
        assert(cont == 1);
    } catch (...) {
        assert(false);
    }
}

void test_5() {
    ThreadPool pool(2);
    auto future = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    try {
        future.get();
        assert(false);
    } catch (const std::runtime_error& e) {
        assert(string(e.what()) == "boom");
    } catch (...) {
        assert(false);
    }

    auto another = pool.submit([] { return 100; });
    assert(another.get() == 100);
}

void test_6() {
    ThreadPool pool(2);
    pool.shutdown();
    try {
        pool.submit([]() { return 1; });
        assert(false);
    } catch (const std::runtime_error&) {
        assert(true);
    } catch (...) {
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

    return 0;
}