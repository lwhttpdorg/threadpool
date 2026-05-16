#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <thread>

#include "task_queue.hpp"
#include "thread_pool.hpp"

SCENARIO("thread_pool shutdown_now returns remaining tasks", "[thread_pool]") {
    GIVEN("a thread_pool with 1 core thread and many pending tasks") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>();
        tp::thread_pool pool(1, 2, std::chrono::seconds(1), std::move(work_queue));

        WHEN("10 tasks are submitted and shutdown_now is called") {
            std::atomic<int> executed{0};
            for (int i = 0; i < 10; ++i) {
                pool.execute([&]() {
                    ++executed;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                });
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            auto remaining = pool.shutdown_now();

            THEN("remaining tasks plus executed equals total submitted") {
                REQUIRE(remaining.size() + executed.load() == 10);
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
            }
        }
    }
}

SCENARIO("thread_pool shutdown and termination state", "[thread_pool]") {
    GIVEN("a running thread_pool") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>();
        tp::thread_pool pool(1, 2, std::chrono::seconds(1), std::move(work_queue));

        THEN("it is neither shutdown nor terminated") {
            REQUIRE_FALSE(pool.is_shutdown());
            REQUIRE_FALSE(pool.is_terminated());
        }

        WHEN("shutdown is called") {
            pool.shutdown();

            THEN("it is shutdown and eventually terminated") {
                REQUIRE(pool.is_shutdown());
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
                REQUIRE(pool.is_terminated());
            }
        }
    }
}
