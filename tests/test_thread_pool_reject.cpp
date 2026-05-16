#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "task_queue.hpp"
#include "thread_pool.hpp"

SCENARIO("thread_pool reject policy abort throws exception", "[thread_pool]") {
    GIVEN("a running thread_pool with abort policy and a full queue") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>(1);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue),
                             tp::thread_pool::reject_policy::abort);

        std::mutex blocker_mutex;
        std::condition_variable blocker_cv;
        bool blocker_started = false;
        bool release_blocker = false;

        pool.execute([&]() {
            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                blocker_started = true;
            }
            blocker_cv.notify_one();
            std::unique_lock<std::mutex> lock(blocker_mutex);
            blocker_cv.wait(lock, [&]() { return release_blocker; });
        });

        {
            std::unique_lock<std::mutex> lock(blocker_mutex);
            blocker_cv.wait(lock, [&]() { return blocker_started; });
        }

        // Fill the queue with a task that takes non-zero time
        pool.execute([&]() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });

        WHEN("a task is submitted while the queue is full") {
            THEN("submitting a task throws rejected_execution_exception") {
                REQUIRE_THROWS_AS(pool.execute([]() {}), tp::thread_pool::rejected_execution_exception);
            }
        }

        {
            std::scoped_lock<std::mutex> lock(blocker_mutex);
            release_blocker = true;
        }
        blocker_cv.notify_one();
        pool.shutdown();
        pool.await_termination(std::chrono::seconds(5));
    }
}

SCENARIO("thread_pool reject policy caller_runs executes in caller thread", "[thread_pool]") {
    GIVEN("a running thread_pool with caller_runs policy and a full queue") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>(1);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue),
                             tp::thread_pool::reject_policy::caller_runs);

        std::mutex blocker_mutex;
        std::condition_variable blocker_cv;
        bool blocker_started = false;
        bool release_blocker = false;

        pool.execute([&]() {
            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                blocker_started = true;
            }
            blocker_cv.notify_one();
            std::unique_lock<std::mutex> lock(blocker_mutex);
            blocker_cv.wait(lock, [&]() { return release_blocker; });
        });

        {
            std::unique_lock<std::mutex> lock(blocker_mutex);
            blocker_cv.wait(lock, [&]() { return blocker_started; });
        }

        // Fill the queue with a task that takes non-zero time
        pool.execute([&]() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });

        WHEN("a task is submitted while the queue is full") {
            THEN("the task runs synchronously in the caller thread") {
                auto caller_id = std::this_thread::get_id();
                std::thread::id runner_id;
                pool.execute([&]() { runner_id = std::this_thread::get_id(); });
                REQUIRE(runner_id == caller_id);
            }
        }

        {
            std::scoped_lock<std::mutex> lock(blocker_mutex);
            release_blocker = true;
        }
        blocker_cv.notify_one();
        pool.shutdown();
        pool.await_termination(std::chrono::seconds(5));
    }
}

SCENARIO("thread_pool reject policy discard silently drops tasks", "[thread_pool]") {
    GIVEN("a running thread_pool with discard policy and a full queue") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>(1);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue),
                             tp::thread_pool::reject_policy::discard);

        std::mutex blocker_mutex;
        std::condition_variable blocker_cv;
        bool blocker_started = false;
        bool release_blocker = false;

        pool.execute([&]() {
            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                blocker_started = true;
            }
            blocker_cv.notify_one();
            std::unique_lock<std::mutex> lock(blocker_mutex);
            blocker_cv.wait(lock, [&]() { return release_blocker; });
        });

        {
            std::unique_lock<std::mutex> lock(blocker_mutex);
            blocker_cv.wait(lock, [&]() { return blocker_started; });
        }

        // Fill the queue with a task that takes non-zero time
        pool.execute([&]() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });

        WHEN("a task is submitted while the queue is full") {
            THEN("the task is silently discarded") {
                std::atomic<bool> ran{false};
                pool.execute([&]() { ran = true; });
                REQUIRE_FALSE(ran);
            }
        }

        {
            std::scoped_lock<std::mutex> lock(blocker_mutex);
            release_blocker = true;
        }
        blocker_cv.notify_one();
        pool.shutdown();
        pool.await_termination(std::chrono::seconds(5));
    }
}

SCENARIO("thread_pool reject policy discard_oldest removes oldest queued task", "[thread_pool]") {
    GIVEN("a running thread_pool with discard_oldest policy and a full queue") {
        auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>(2);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue),
                             tp::thread_pool::reject_policy::discard_oldest);

        std::vector<int> execution_order;
        std::mutex order_mutex;
        std::mutex blocker_mutex;
        std::condition_variable blocker_cv;
        bool blocker_started = false;
        bool release_blocker = false;

        pool.execute([&]() {
            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                blocker_started = true;
            }
            blocker_cv.notify_one();
            std::unique_lock<std::mutex> lock(blocker_mutex);
            blocker_cv.wait(lock, [&]() { return release_blocker; });
        });

        {
            std::unique_lock<std::mutex> lock(blocker_mutex);
            blocker_cv.wait(lock, [&]() { return blocker_started; });
        }

        // Fill the queue with two tasks
        pool.execute([&]() {
            std::scoped_lock<std::mutex> lock(order_mutex);
            execution_order.push_back(1);
        });
        pool.execute([&]() {
            std::scoped_lock<std::mutex> lock(order_mutex);
            execution_order.push_back(2);
        });

        // Submit a third task: oldest (task 1) should be discarded, task 3 enters the queue
        pool.execute([&]() {
            std::scoped_lock<std::mutex> lock(order_mutex);
            execution_order.push_back(3);
        });

        {
            std::scoped_lock<std::mutex> lock(blocker_mutex);
            release_blocker = true;
        }
        blocker_cv.notify_one();

        pool.shutdown();
        pool.await_termination(std::chrono::seconds(5));

        THEN("the oldest queued task is discarded and the new task is executed") {
            REQUIRE(execution_order.size() == 2);
            REQUIRE(execution_order[0] == 2);
            REQUIRE(execution_order[1] == 3);
        }
    }
}
