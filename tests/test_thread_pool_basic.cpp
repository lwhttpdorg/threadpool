#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <thread>

#include "threadpool/task_queue.hpp"
#include "threadpool/thread_pool.hpp"

SCENARIO("thread_pool executes submitted tasks", "[thread_pool]") {
    GIVEN("a thread_pool with 2 core and 4 max threads") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::work_task>>();
        tp::thread_pool pool(2, 4, std::chrono::seconds(1), std::move(work_queue));

        WHEN("several tasks are submitted") {
            std::atomic<int> counter{0};
            for (int i = 0; i < 5; ++i) {
                pool.execute([&]() { ++counter; });
            }

            THEN("all tasks are executed after shutdown") {
                pool.shutdown();
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
                REQUIRE(counter == 5);
            }
        }
    }
}

SCENARIO("thread_pool keeps core threads alive", "[thread_pool]") {
    GIVEN("a thread_pool with 2 core threads") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::work_task>>();
        tp::thread_pool pool(2, 4, std::chrono::seconds(1), std::move(work_queue));

        WHEN("tasks are executed and complete") {
            std::atomic<int> counter{0};
            pool.execute([&]() { ++counter; });
            pool.execute([&]() { ++counter; });

            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            THEN("core threads remain active") {
                REQUIRE(pool.active_count() == 2);
            }

            AND_WHEN("the pool is shut down") {
                pool.shutdown();

                THEN("it terminates successfully") {
                    REQUIRE(pool.await_termination(std::chrono::seconds(5)));
                }
            }
        }
    }
}

SCENARIO("thread_pool queue_size reflects pending tasks", "[thread_pool]") {
    GIVEN("a thread_pool with 1 core thread") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::work_task>>();
        tp::thread_pool pool(1, 2, std::chrono::seconds(1), std::move(work_queue));

        WHEN("multiple slow tasks are submitted") {
            for (int i = 0; i < 5; ++i) {
                pool.execute([]() { std::this_thread::sleep_for(std::chrono::seconds(1)); });
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            THEN("the queue contains pending tasks") {
                REQUIRE(pool.queue_size() > 0);
            }

            pool.shutdown();
            REQUIRE(pool.await_termination(std::chrono::seconds(10)));
        }
    }
}
