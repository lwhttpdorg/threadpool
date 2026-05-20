#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "threadpool/blocking_queue.hpp"
#include "threadpool/thread_pool.hpp"

SCENARIO("thread_pool executes submitted tasks", "[thread_pool]") {
    GIVEN("a thread_pool with 2 core and 4 max threads") {
        auto work_queue = std::make_unique<tp::fifo_task_queue>();
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
        auto work_queue = std::make_unique<tp::fifo_task_queue>();
        tp::thread_pool pool(2, 4, std::chrono::seconds(1), std::move(work_queue));

        WHEN("tasks are executed and complete") {
            std::atomic<int> counter{0};
            pool.execute([&]() { ++counter; });
            pool.execute([&]() { ++counter; });

            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            THEN("subsequent tasks are picked up promptly") {
                std::atomic<bool> third_ran{false};
                pool.execute([&]() { third_ran = true; });
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                REQUIRE(third_ran);
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
        auto work_queue = std::make_unique<tp::fifo_task_queue>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        WHEN("multiple slow tasks are submitted") {
            std::atomic<int> started{0};
            std::mutex blocker_mutex;
            std::condition_variable blocker_cv;
            bool blocker_started = false;
            bool release_blocker = false;

            // Block the sole worker thread deterministically
            pool.execute([&]() {
                ++started;
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

            // Submit more tasks that will queue up
            for (int i = 0; i < 4; ++i) {
                pool.execute([&]() { ++started; });
            }

            THEN("not all tasks have started yet") {
                REQUIRE(started.load() == 1);
            }

            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                release_blocker = true;
            }
            blocker_cv.notify_one();

            pool.shutdown();
            REQUIRE(pool.await_termination(std::chrono::seconds(10)));
        }
    }
}

// --- Free functions for submit tests ---

static std::atomic<bool> func_no_arg_called{false};

void func_no_arg() {
    func_no_arg_called = true;
}

static std::atomic<int> func_with_args_result{0};

void func_with_args(int x, int y) {
    func_with_args_result = x + y;
}

SCENARIO("thread_pool executes a no-arg free function", "[thread_pool]") {
    GIVEN("a running thread pool with fifo queue") {
        auto wq = std::make_unique<tp::fifo_task_queue>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(wq));

        func_no_arg_called = false;

        WHEN("a no-arg function is submitted") {
            pool.execute(func_no_arg);

            pool.shutdown();
            REQUIRE(pool.await_termination(std::chrono::seconds(5)));

            THEN("the function is executed") {
                REQUIRE(func_no_arg_called.load());
            }
        }
    }
}

SCENARIO("thread_pool executes a free function with arguments", "[thread_pool]") {
    GIVEN("a running thread pool with fifo queue") {
        auto wq = std::make_unique<tp::fifo_task_queue>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(wq));

        func_with_args_result = 0;

        WHEN("a function with arguments is submitted") {
            pool.execute(func_with_args, 17, 25);

            pool.shutdown();
            REQUIRE(pool.await_termination(std::chrono::seconds(5)));

            THEN("the function receives the correct arguments") {
                REQUIRE(func_with_args_result.load() == 42);
            }
        }
    }
}
