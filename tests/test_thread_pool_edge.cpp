#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "threadpool/task_queue.hpp"
#include "threadpool/thread_pool.hpp"

SCENARIO("thread_pool constructor rejects invalid pool sizes", "[thread_pool]") {
    GIVEN("core_pool_size > max_pool_size") {
        THEN("construction throws std::invalid_argument") {
            auto wq = std::make_unique<tp::fifo_task_queue<tp::callable>>();
            REQUIRE_THROWS_AS(tp::thread_pool(4, 2, std::chrono::seconds(1), std::move(wq)), std::invalid_argument);
        }
    }
}

SCENARIO("thread_pool await_termination with negative timeout waits indefinitely", "[thread_pool]") {
    GIVEN("a running thread_pool") {
        auto wq = std::make_unique<tp::fifo_task_queue<tp::callable>>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(wq));

        WHEN("shutdown is called and await_termination uses negative timeout") {
            pool.execute([] {});
            pool.shutdown();

            THEN("await_termination blocks until all workers exit and returns true") {
                bool ok = pool.await_termination(std::chrono::seconds(-1));
                REQUIRE(ok);
            }
        }
    }
}

SCENARIO("thread_pool swallows exceptions thrown by user tasks", "[thread_pool]") {
    GIVEN("a running thread_pool") {
        auto wq = std::make_unique<tp::fifo_task_queue<tp::callable>>();
        tp::thread_pool pool(2, 2, std::chrono::seconds(1), std::move(wq));

        WHEN("tasks throw exceptions") {
            std::atomic<int> counter{0};

            pool.execute([] { throw std::runtime_error("boom"); });
            pool.execute([&] { ++counter; });
            pool.execute([] { throw 42; });
            pool.execute([&] { ++counter; });

            pool.shutdown();
            pool.await_termination(std::chrono::seconds(5));

            THEN("subsequent tasks still execute normally") {
                REQUIRE(counter.load() == 2);
            }
        }
    }
}

SCENARIO("thread_pool discard_oldest falls through to reject when retry fails", "[thread_pool]") {
    GIVEN("a pool with discard_oldest policy, core=1, max=1, queue capacity=1") {
        auto wq = std::make_unique<tp::fifo_task_queue<tp::callable>>(1);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(wq),
                             tp::thread_pool::reject_policy::discard_oldest);

        std::mutex blocker_mutex;
        std::condition_variable blocker_cv;
        bool blocker_started = false;
        bool release_blocker = false;

        // Block the sole worker
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

        // Fill the queue
        pool.execute([&]() {});

        WHEN("another task is submitted triggering discard_oldest") {
            std::atomic<bool> ran{false};
            // discard_oldest pops the oldest, retries push — should succeed
            pool.execute([&]() { ran = true; });

            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                release_blocker = true;
            }
            blocker_cv.notify_one();

            pool.shutdown();
            pool.await_termination(std::chrono::seconds(5));

            THEN("the new task was enqueued and executed") {
                REQUIRE(ran.load());
            }
        }
    }
}

SCENARIO("thread_pool non-core worker exits on shutdown_now (stop state)", "[thread_pool]") {
    GIVEN("a pool with core=1, max=2, queue capacity=1") {
        auto wq = std::make_unique<tp::fifo_task_queue<tp::callable>>(1);
        tp::thread_pool pool(1, 2, std::chrono::seconds(60), std::move(wq));

        std::mutex blocker_mutex;
        std::condition_variable blocker_cv;
        bool blocker_started = false;
        bool release_blocker = false;

        // Block the core worker
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

        // Fill the queue
        pool.execute([] {});

        // This triggers a non-core worker
        std::atomic<bool> non_core_ran{false};
        pool.execute([&]() {
            non_core_ran = true;
            // Sleep so the non-core worker is still alive when shutdown_now is called
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });

        // Wait for non-core worker to start
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        WHEN("shutdown_now is called") {
            auto remaining = pool.shutdown_now();

            {
                std::scoped_lock<std::mutex> lock(blocker_mutex);
                release_blocker = true;
            }
            blocker_cv.notify_one();

            THEN("all workers exit and the pool terminates") {
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
                REQUIRE(non_core_ran.load());
            }
        }
    }
}
