#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include <thread>

#include <threadpool/blocking_queue.hpp>
#include <threadpool/thread_pool.hpp>

static std::string tid() {
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    return oss.str();
}

void example_function() {
    printf("[Thread %s] %s\n", tid().c_str(), __func__);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    printf("[Thread %s] This is a regular function.\n", tid().c_str());
}

void example_function_with_args(int x) {
    printf("[Thread %s] %s\n", tid().c_str(), __func__);
    std::this_thread::sleep_for(std::chrono::seconds(3));
    printf("[Thread %s] This is a regular function with arguments: %d\n", tid().c_str(), x);
}

int main() {
    auto queue = std::make_unique<tp::fifo_task_queue>(2);
    tp::thread_pool pool(1, 4, std::chrono::seconds(60), std::move(queue));

    pool.execute([] {
        printf("[Thread %s] parameterless lambda\n", tid().c_str());
        std::this_thread::sleep_for(std::chrono::seconds(3));
        printf("[Thread %s] This is a parameterless lambda.\n", tid().c_str());
    });

    pool.execute(
        [](int x) {
            printf("[Thread %s] parameterized lambda\n", tid().c_str());
            std::this_thread::sleep_for(std::chrono::seconds(3));
            printf("[Thread %s] This is a parameterized lambda: %d\n", tid().c_str(), x);
        },
        42);

    pool.execute(5, [] {
        printf("[Thread %s] prioritized lambda\n", tid().c_str());
        std::this_thread::sleep_for(std::chrono::seconds(3));
        printf("[Thread %s] This is a prioritized lambda.\n", tid().c_str());
    });

    pool.execute(example_function);
    pool.execute(example_function_with_args, 99);

    pool.shutdown();
    pool.await_termination(std::chrono::seconds(0));
    return 0;
}
