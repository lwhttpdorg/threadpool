#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "task_queue.hpp"
#include "thread_pool.hpp"

SCENARIO("thread_pool executes tasks by priority", "[thread_pool]") {
    GIVEN("a single-threaded pool with a priority task queue") {
        auto work_queue = std::make_unique<tp::priority_task_queue<tp::task, tp::task_priority_compare>>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        WHEN("tasks with different priorities are submitted out of order") {
            std::vector<int> execution_order;
            std::mutex order_mutex;
            std::mutex blocker_mutex;
            std::condition_variable blocker_cv;
            bool blocker_started = false;
            bool release_blocker = false;

            // 用一个阻塞任务占住唯一的工作线程
            pool.execute([&]() {
                {
                    std::lock_guard<std::mutex> lock(blocker_mutex);
                    blocker_started = true;
                }
                blocker_cv.notify_one();

                std::unique_lock<std::mutex> lock(blocker_mutex);
                blocker_cv.wait(lock, [&]() { return release_blocker; });
            });

            // 等待阻塞任务确实开始运行
            {
                std::unique_lock<std::mutex> lock(blocker_mutex);
                blocker_cv.wait(lock, [&]() { return blocker_started; });
            }

            // 此时工作线程被占住，所有新任务都会进入队列排队
            pool.execute_with_priority(1, [&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(1);
            });
            pool.execute_with_priority(3, [&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(3);
            });
            pool.execute_with_priority(2, [&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(2);
            });

            // 释放阻塞任务，工作线程开始按优先级消费队列
            {
                std::lock_guard<std::mutex> lock(blocker_mutex);
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

SCENARIO("thread_pool execute_with_priority overrides default priority", "[thread_pool]") {
    GIVEN("a single-threaded pool with a priority task queue") {
        auto work_queue = std::make_unique<tp::priority_task_queue<tp::task, tp::task_priority_compare>>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        WHEN("a default-priority task and a high-priority task are submitted") {
            std::vector<int> execution_order;
            std::mutex order_mutex;
            std::mutex blocker_mutex;
            std::condition_variable blocker_cv;
            bool blocker_started = false;
            bool release_blocker = false;

            pool.execute([&]() {
                {
                    std::lock_guard<std::mutex> lock(blocker_mutex);
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
            pool.execute_with_priority(5, [&]() {
                std::scoped_lock<std::mutex> lock(order_mutex);
                execution_order.push_back(5);
            });

            {
                std::lock_guard<std::mutex> lock(blocker_mutex);
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
