#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "threadpool/blocking_queue.hpp"

SCENARIO("array_blocking_queue maintains first-in-first-out ordering", "[array_blocking_queue]") {
    GIVEN("an empty array_blocking_queue") {
        tp::array_blocking_queue<int> task_q;

        WHEN("multiple items are pushed in sequence") {
            task_q.try_push(1);
            task_q.try_push(2);
            task_q.try_push(3);

            THEN("items are popped in the same order") {
                REQUIRE(task_q.pop() == 1);
                REQUIRE(task_q.pop() == 2);
                REQUIRE(task_q.pop() == 3);
                REQUIRE(task_q.size() == 0);
            }
        }
    }
}

SCENARIO("array_blocking_queue try_pop on empty queue", "[array_blocking_queue]") {
    GIVEN("an empty array_blocking_queue") {
        tp::array_blocking_queue<int> task_q;

        WHEN("try_pop is called") {
            int item = 0;
            bool success = task_q.try_pop(item);

            THEN("it returns false and item is unchanged") {
                REQUIRE_FALSE(success);
                REQUIRE(item == 0);
            }
        }
    }
}

SCENARIO("array_blocking_queue timed_pop behavior", "[array_blocking_queue]") {
    GIVEN("an empty array_blocking_queue") {
        tp::array_blocking_queue<int> task_q;

        WHEN("timed_pop is called with a short timeout") {
            int item = 0;
            bool success = task_q.timed_pop(item, std::chrono::milliseconds(50));

            THEN("it returns false because the queue is empty") {
                REQUIRE_FALSE(success);
            }
        }
    }

    GIVEN("a array_blocking_queue with one item") {
        tp::array_blocking_queue<int> task_q;
        task_q.try_push(42);

        WHEN("timed_pop is called") {
            int item = 0;
            bool success = task_q.timed_pop(item, std::chrono::milliseconds(100));

            THEN("it returns true with the correct item") {
                REQUIRE(success);
                REQUIRE(item == 42);
            }
        }
    }
}

SCENARIO("array_blocking_queue tracks size correctly", "[array_blocking_queue]") {
    GIVEN("an empty array_blocking_queue") {
        tp::array_blocking_queue<int> task_q;

        THEN("its size is 0") {
            REQUIRE(task_q.size() == 0);
        }

        WHEN("two items are pushed") {
            task_q.try_push(1);
            task_q.try_push(2);

            THEN("its size is 2") {
                REQUIRE(task_q.size() == 2);
            }

            AND_WHEN("one item is popped") {
                task_q.pop();

                THEN("its size is 1") {
                    REQUIRE(task_q.size() == 1);
                }
            }
        }
    }
}

SCENARIO("array_blocking_queue supports batch push", "[array_blocking_queue]") {
    GIVEN("an empty array_blocking_queue") {
        tp::array_blocking_queue<int> task_q;

        WHEN("a batch of items is pushed") {
            task_q.try_push(1);
            task_q.try_push(2);
            task_q.try_push(3);

            THEN("all items are available in order") {
                REQUIRE(task_q.pop() == 1);
                REQUIRE(task_q.pop() == 2);
                REQUIRE(task_q.pop() == 3);
            }
        }
    }
}

SCENARIO("array_blocking_queue handles concurrent push and pop", "[array_blocking_queue]") {
    GIVEN("an empty array_blocking_queue and a producer-consumer pair") {
        tp::array_blocking_queue<int> task_q;
        const int count = 1000;

        WHEN("producer pushes 1000 items while consumer pops them") {
            std::jthread producer([&]() {
                for (int i = 0; i < count; ++i) {
                    task_q.try_push(std::move(i));
                }
            });

            std::jthread consumer([&]() {
                for (int i = 0; i < count; ++i) {
                    auto val = task_q.pop();
                    (void)val;
                }
            });

            producer.join();
            consumer.join();

            THEN("the queue is empty after all operations") {
                REQUIRE(task_q.size() == 0);
            }
        }
    }
}

SCENARIO("array_blocking_queue push blocks until space is available", "[array_blocking_queue]") {
    GIVEN("a bounded array_blocking_queue with capacity 2") {
        tp::array_blocking_queue<int> task_q(2);

        // Fill the queue
        task_q.try_push(1);
        task_q.try_push(2);

        WHEN("push is called on a full queue from another thread") {
            std::atomic<bool> push_completed{false};
            std::jthread producer([&] {
                task_q.push(std::move(3));
                push_completed = true;
            });

            // Give the producer time to block
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            REQUIRE_FALSE(push_completed.load());

            THEN("push completes after an item is popped") {
                auto val = task_q.pop();
                REQUIRE(val == 1);

                producer.join();
                REQUIRE(push_completed.load());

                // Verify the pushed item is in the queue
                REQUIRE(task_q.pop() == 2);
                REQUIRE(task_q.pop() == 3);
            }
        }
    }
}

SCENARIO("array_blocking_queue empty returns correct state", "[array_blocking_queue]") {
    GIVEN("an empty array_blocking_queue") {
        tp::array_blocking_queue<int> task_q;

        THEN("empty returns true") {
            REQUIRE(task_q.empty());
        }

        WHEN("an item is pushed") {
            task_q.try_push(42);

            THEN("empty returns false") {
                REQUIRE_FALSE(task_q.empty());
            }

            AND_WHEN("the item is popped") {
                task_q.pop();

                THEN("empty returns true again") {
                    REQUIRE(task_q.empty());
                }
            }
        }
    }
}

SCENARIO("array_blocking_queue wake_all unblocks waiting threads", "[array_blocking_queue]") {
    GIVEN("an empty array_blocking_queue") {
        tp::array_blocking_queue<int> task_q;

        WHEN("a thread is blocked on timed_pop and wake_all is called") {
            std::atomic<bool> pop_returned{false};
            std::jthread consumer([&] {
                int item = 0;
                task_q.timed_pop(item, std::chrono::seconds(10));
                pop_returned = true;
            });

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            REQUIRE_FALSE(pop_returned.load());

            task_q.wake_all();

            consumer.join();

            THEN("the blocked thread is unblocked") {
                REQUIRE(pop_returned.load());
            }
        }
    }
}
