#include <atomic>
#include <chrono>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "task_queue.hpp"
#include "thread_pool.hpp"

SCENARIO("thread_pool reject policy abort throws exception", "[thread_pool]") {
    GIVEN("a thread_pool configured with abort policy") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue),
                             tp::thread_pool::reject_policy::abort);

        WHEN("shutdown is called and a new task is submitted") {
            pool.shutdown();

            THEN("submitting a task throws runtime_error") {
                REQUIRE_THROWS_AS(pool.execute([]() {}), tp::thread_pool::rejected_execution_exception);
            }
        }
    }
}

SCENARIO("thread_pool reject policy caller_runs executes in caller thread", "[thread_pool]") {
    GIVEN("a thread_pool configured with caller_runs policy") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue),
                             tp::thread_pool::reject_policy::caller_runs);

        WHEN("shutdown is called and a new task is submitted") {
            pool.shutdown();

            THEN("the task runs synchronously in the caller thread") {
                std::atomic<bool> ran{false};
                pool.execute([&]() { ran = true; });
                REQUIRE(ran);
            }
        }
    }
}

SCENARIO("thread_pool reject policy discard silently drops tasks", "[thread_pool]") {
    GIVEN("a thread_pool configured with discard policy") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue),
                             tp::thread_pool::reject_policy::discard);

        WHEN("shutdown is called and a new task is submitted") {
            pool.shutdown();

            THEN("the task is silently discarded") {
                std::atomic<bool> ran{false};
                pool.execute([&]() { ran = true; });
                REQUIRE_FALSE(ran);
            }
        }
    }
}
