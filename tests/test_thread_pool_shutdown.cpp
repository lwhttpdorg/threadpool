#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "threadpool/blocking_queue.hpp"
#include "threadpool/thread_pool.hpp"

SCENARIO("thread_pool shutdown rejects new tasks", "[thread_pool]") {
    GIVEN("a running thread_pool with a busy worker and queued tasks") {
        std::mutex blocker_mutex;
        std::condition_variable blocker_cv;
        bool blocker_started = false;
        bool release_blocker = false;

        auto work_queue = std::make_unique<tp::fifo_task_queue>(5);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        pool.execute([&] {
            {
                std::scoped_lock lock(blocker_mutex);
                blocker_started = true;
            }
            blocker_cv.notify_one();
            std::unique_lock lock(blocker_mutex);
            blocker_cv.wait(lock, [&] { return release_blocker; });
        });

        {
            std::unique_lock lock(blocker_mutex);
            blocker_cv.wait(lock, [&] { return blocker_started; });
        }

        for (int i = 0; i < 3; ++i) {
            pool.execute([&] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
        }

        WHEN("shutdown is called") {
            pool.shutdown();

            THEN("new tasks are rejected") {
                REQUIRE_THROWS_AS(pool.execute([] {}), tp::thread_pool::rejected_execution_exception);
            }
        }

        {
            std::scoped_lock lock(blocker_mutex);
            release_blocker = true;
        }
        blocker_cv.notify_one();
    }
}

SCENARIO("thread_pool shutdown drains queued tasks", "[thread_pool]") {
    GIVEN("a running thread_pool with a busy worker and queued tasks") {
        std::mutex blocker_mutex;
        std::condition_variable blocker_cv;
        bool blocker_started = false;
        bool release_blocker = false;

        auto work_queue = std::make_unique<tp::fifo_task_queue>(5);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        pool.execute([&] {
            {
                std::scoped_lock lock(blocker_mutex);
                blocker_started = true;
            }
            blocker_cv.notify_one();
            std::unique_lock lock(blocker_mutex);
            blocker_cv.wait(lock, [&] { return release_blocker; });
        });

        {
            std::unique_lock lock(blocker_mutex);
            blocker_cv.wait(lock, [&] { return blocker_started; });
        }

        std::atomic executed{0};
        for (int i = 0; i < 3; ++i) {
            pool.execute([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++executed;
            });
        }

        WHEN("shutdown is called and the blocker is released") {
            pool.shutdown();
            {
                std::scoped_lock lock(blocker_mutex);
                release_blocker = true;
            }
            blocker_cv.notify_one();

            THEN("queued tasks are eventually executed") {
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
                REQUIRE(executed.load() == 3);
            }
        }

        // Ensure blocker is released for destructor path
        {
            std::scoped_lock lock(blocker_mutex);
            release_blocker = true;
        }
        blocker_cv.notify_one();
    }
}

SCENARIO("thread_pool shutdown_now returns remaining tasks", "[thread_pool]") {
    GIVEN("a running thread_pool with a busy worker and queued tasks") {
        std::mutex blocker_mutex;
        std::condition_variable blocker_cv;
        bool blocker_started = false;
        bool release_blocker = false;

        auto work_queue = std::make_unique<tp::fifo_task_queue>(5);
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        pool.execute([&] {
            {
                std::scoped_lock lock(blocker_mutex);
                blocker_started = true;
            }
            blocker_cv.notify_one();
            std::unique_lock lock(blocker_mutex);
            blocker_cv.wait(lock, [&] { return release_blocker; });
        });

        {
            std::unique_lock lock(blocker_mutex);
            blocker_cv.wait(lock, [&] { return blocker_started; });
        }

        std::atomic executed{0};
        for (int i = 0; i < 3; ++i) {
            pool.execute([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++executed;
            });
        }

        WHEN("shutdown_now is called and the blocker is released") {
            const auto remaining = pool.shutdown_now();
            {
                std::scoped_lock lock(blocker_mutex);
                release_blocker = true;
            }
            blocker_cv.notify_one();

            THEN("all queued tasks are returned and not executed") {
                REQUIRE(remaining.size() == 3);
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
                REQUIRE(executed.load() == 0);
            }
        }

        // Ensure blocker is released for destructor path
        {
            std::scoped_lock lock(blocker_mutex);
            release_blocker = true;
        }
        blocker_cv.notify_one();
    }
}

SCENARIO("thread_pool shutdown and termination state transitions", "[thread_pool]") {
    GIVEN("a running thread_pool") {
        auto work_queue = std::make_unique<tp::fifo_task_queue>();
        tp::thread_pool pool(1, 1, std::chrono::seconds(1), std::move(work_queue));

        THEN("it accepts new tasks") {
            REQUIRE_NOTHROW(pool.execute([] {}));
        }

        WHEN("shutdown is called on an empty pool") {
            pool.shutdown();

            THEN("new tasks are rejected and workers exit") {
                REQUIRE_THROWS_AS(pool.execute([] {}), tp::thread_pool::rejected_execution_exception);
                REQUIRE(pool.await_termination(std::chrono::seconds(5)));
            }
        }
    }
}
