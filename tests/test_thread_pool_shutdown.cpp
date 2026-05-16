#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "threadpool/task_queue.hpp"
#include "threadpool/thread_pool.hpp"

SCENARIO("thread_pool shutdown rejects new tasks", "[thread_pool]") {
    GIVEN("a running thread_pool with a busy worker and queued tasks") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::work_task>>(5);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        std::atomic<bool> blocker_active{true};
        pool.execute([&]() {
            while (blocker_active.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        for (int i = 0; i < 3; ++i) {
            pool.execute([&]() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
        }

        WHEN("shutdown is called") {
            pool.shutdown();

            THEN("new tasks are rejected") {
                REQUIRE_THROWS_AS(pool.execute([]() {}), tp::thread_pool::rejected_execution_exception);
            }
        }

        blocker_active = false;
    }
}

SCENARIO("thread_pool shutdown drains queued tasks", "[thread_pool]") {
    GIVEN("a running thread_pool with a busy worker and queued tasks") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::work_task>>(5);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        std::atomic<bool> blocker_active{true};
        pool.execute([&]() {
            while (blocker_active.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        std::atomic<int> executed{0};
        for (int i = 0; i < 3; ++i) {
            pool.execute([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++executed;
            });
        }

        WHEN("shutdown is called and the blocker is released") {
            pool.shutdown();
            blocker_active = false;

            THEN("queued tasks are eventually executed") {
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
                REQUIRE(executed.load() == 3);
                REQUIRE(pool.is_terminated());
            }
        }
    }
}

SCENARIO("thread_pool shutdown_now returns remaining tasks", "[thread_pool]") {
    GIVEN("a running thread_pool with a busy worker and queued tasks") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::work_task>>(5);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        std::atomic<bool> blocker_active{true};
        pool.execute([&]() {
            while (blocker_active.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        std::atomic<int> executed{0};
        for (int i = 0; i < 3; ++i) {
            pool.execute([&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++executed;
            });
        }

        WHEN("shutdown_now is called and the blocker is released") {
            auto remaining = pool.shutdown_now();
            blocker_active = false;

            THEN("all queued tasks are returned and not executed") {
                REQUIRE(remaining.size() == 3);
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
                REQUIRE(executed.load() == 0);
            }
        }
    }
}

SCENARIO("thread_pool shutdown and termination state transitions", "[thread_pool]") {
    GIVEN("a running thread_pool") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::work_task>>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        THEN("it is neither shutdown nor terminated") {
            REQUIRE_FALSE(pool.is_shutdown());
            REQUIRE_FALSE(pool.is_terminated());
        }

        WHEN("shutdown is called on an empty pool") {
            pool.shutdown();

            THEN("it is shutdown and eventually terminated") {
                REQUIRE(pool.is_shutdown());
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
                REQUIRE(pool.is_terminated());
            }
        }
    }
}
