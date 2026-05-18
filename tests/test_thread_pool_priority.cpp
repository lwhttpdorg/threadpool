#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "threadpool/task_queue.hpp"
#include "threadpool/thread_pool.hpp"

SCENARIO("thread_pool executes queued tasks by priority", "[thread_pool]") {
    GIVEN("a single-threaded pool with a bounded priority task queue") {
        auto work_queue = std::make_unique<tp::priority_task_queue<tp::callable>>(3);
        tp::thread_pool pool(1, 2, std::chrono::seconds(1), std::move(work_queue));

        WHEN("tasks with different priorities are submitted out of order") {
            std::vector<int> execution_order;
            std::mutex order_mutex;
            std::mutex blocker_mutex;
            std::condition_variable blocker_cv;
            bool blocker_started = false;
            bool release_blocker = false;

            // Submit a blocker task to occupy the sole worker thread
            pool.execute([&]() {
                {
                    std::scoped_lock<std::mutex> lock(blocker_mutex);
                    blocker_started = true;
                }
                blocker_cv.notify_one();

                std::unique_lock<std::mutex> lock(blocker_mutex);
                blocker_cv.wait(lock, [&]() { return release_blocker; });
            });

            // Wait until the blocker task has actually started
            {
                std::unique_lock<std::mutex> lock(blocker_mutex);
                blocker_cv.wait(lock, [&]() { return blocker_started; });
            }

            // At this point the worker thread is blocked, so all new tasks go into the queue
            pool.execute(1, [&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(1);
            });
            pool.execute(3, [&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(3);
            });
            pool.execute(2, [&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(2);
            });

            // Release the blocker so the worker starts consuming the queue by priority
            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                release_blocker = true;
            }
            blocker_cv.notify_one();

            pool.shutdown();
            pool.await_termination(std::chrono::seconds(5));

            THEN("higher priority tasks execute first") {
                REQUIRE(execution_order.size() == 3);
                REQUIRE(execution_order[0] == 3);
                REQUIRE(execution_order[1] == 2);
                REQUIRE(execution_order[2] == 1);
            }
        }
    }
}

SCENARIO("thread_pool execute overrides default priority", "[thread_pool]") {
    GIVEN("a single-threaded pool with a bounded priority task queue") {
        auto work_queue = std::make_unique<tp::priority_task_queue<tp::callable>>(3);
        tp::thread_pool pool(1, 2, std::chrono::seconds(1), std::move(work_queue));

        WHEN("a default-priority task and a high-priority task are submitted") {
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

            pool.execute([&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(0);
            });
            pool.execute(5, [&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(5);
            });

            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                release_blocker = true;
            }
            blocker_cv.notify_one();

            pool.shutdown();
            pool.await_termination(std::chrono::seconds(5));

            THEN("the explicit high-priority task executes first") {
                REQUIRE(execution_order.size() == 2);
                REQUIRE(execution_order[0] == 5);
                REQUIRE(execution_order[1] == 0);
            }
        }
    }
}

SCENARIO("thread_pool spawns non-core worker when queue is full", "[thread_pool]") {
    GIVEN("a pool with core=1, max=2 and a bounded priority queue of capacity 1") {
        auto work_queue = std::make_unique<tp::priority_task_queue<tp::callable>>(1);
        tp::thread_pool pool(1, 2, std::chrono::seconds(1), std::move(work_queue));

        WHEN("a high-priority task is submitted while queue is full") {
            std::vector<int> execution_order;
            std::mutex order_mutex;
            std::mutex blocker_mutex;
            std::condition_variable blocker_cv;
            bool blocker_started = false;
            bool release_blocker = false;
            std::atomic<bool> high_priority_started{false};

            // Blocker occupies the core worker thread
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

            // Enqueue a low-priority task (queue becomes full)
            pool.execute(1, [&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(1);
            });

            // Queue is full, a non-core worker will be spawned to run the high-priority task directly
            pool.execute(3, [&]() {
                {
                    std::scoped_lock<std::mutex> lock(order_mutex);
                    execution_order.push_back(3);
                }
                high_priority_started.store(true);
            });

            // Wait until the non-core worker has actually started the high-priority task
            while (!high_priority_started.load()) {
                std::this_thread::yield();
            }

            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                release_blocker = true;
            }
            blocker_cv.notify_one();

            pool.shutdown();
            pool.await_termination(std::chrono::seconds(5));

            THEN("the high-priority task is executed by the non-core worker first") {
                REQUIRE(execution_order.size() == 2);
                REQUIRE(execution_order[0] == 3);
                REQUIRE(execution_order[1] == 1);
            }
        }
    }
}

SCENARIO("thread_pool rejects task when queue is full and max threads reached", "[thread_pool]") {
    GIVEN("a pool with core=1, max=1 and a bounded priority queue of capacity 1") {
        auto work_queue = std::make_unique<tp::priority_task_queue<tp::callable>>(1);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue),
                             tp::thread_pool::reject_policy::abort);

        WHEN("more tasks are submitted than queue can hold") {
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

            // Enqueue a task (queue becomes full)
            pool.execute(1, [&]() {});

            THEN("the next task is rejected with an exception") {
                REQUIRE_THROWS_AS(pool.execute(3, [&]() {}), tp::thread_pool::rejected_execution_exception);
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
}
