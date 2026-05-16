#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "task_queue.hpp"

SCENARIO("fifo_task_queue maintains first-in-first-out ordering", "[fifo_task_queue]") {
    GIVEN("an empty fifo_task_queue") {
        tp::fifo_task_queue<int> task_q;

        WHEN("multiple items are pushed in sequence") {
            task_q.push(1);
            task_q.push(2);
            task_q.push(3);

            THEN("items are popped in the same order") {
                REQUIRE(task_q.pop() == 1);
                REQUIRE(task_q.pop() == 2);
                REQUIRE(task_q.pop() == 3);
                REQUIRE(task_q.empty());
            }
        }
    }
}

SCENARIO("fifo_task_queue try_pop on empty queue", "[fifo_task_queue]") {
    GIVEN("an empty fifo_task_queue") {
        tp::fifo_task_queue<int> task_q;

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

SCENARIO("fifo_task_queue pop_with_timeout behavior", "[fifo_task_queue]") {
    GIVEN("an empty fifo_task_queue") {
        tp::fifo_task_queue<int> task_q;

        WHEN("pop_with_timeout is called with a short timeout") {
            int item = 0;
            bool success = task_q.pop_with_timeout(item, std::chrono::milliseconds(50));

            THEN("it returns false because the queue is empty") {
                REQUIRE_FALSE(success);
            }
        }
    }

    GIVEN("a fifo_task_queue with one item") {
        tp::fifo_task_queue<int> task_q;
        task_q.push(42);

        WHEN("pop_with_timeout is called") {
            int item = 0;
            bool success = task_q.pop_with_timeout(item, std::chrono::milliseconds(100));

            THEN("it returns true with the correct item") {
                REQUIRE(success);
                REQUIRE(item == 42);
            }
        }
    }
}

SCENARIO("fifo_task_queue tracks size correctly", "[fifo_task_queue]") {
    GIVEN("an empty fifo_task_queue") {
        tp::fifo_task_queue<int> task_q;

        THEN("its size is 0") {
            REQUIRE(task_q.size() == 0);
        }

        WHEN("two items are pushed") {
            task_q.push(1);
            task_q.push(2);

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

SCENARIO("fifo_task_queue supports batch push", "[fifo_task_queue]") {
    GIVEN("an empty fifo_task_queue") {
        tp::fifo_task_queue<int> task_q;

        WHEN("a batch of items is pushed") {
            std::vector<int> items = {1, 2, 3};
            task_q.push(std::move(items));

            THEN("all items are available in order") {
                REQUIRE(task_q.pop() == 1);
                REQUIRE(task_q.pop() == 2);
                REQUIRE(task_q.pop() == 3);
            }
        }
    }
}

SCENARIO("fifo_task_queue handles concurrent push and pop", "[fifo_task_queue]") {
    GIVEN("an empty fifo_task_queue and a producer-consumer pair") {
        tp::fifo_task_queue<int> task_q;
        const int count = 1000;

        WHEN("producer pushes 1000 items while consumer pops them") {
            std::thread producer([&]() {
                for (int i = 0; i < count; ++i) {
                    task_q.push(std::move(i));
                }
            });

            std::thread consumer([&]() {
                for (int i = 0; i < count; ++i) {
                    auto val = task_q.pop();
                    (void)val;
                }
            });

            producer.join();
            consumer.join();

            THEN("the queue is empty after all operations") {
                REQUIRE(task_q.empty());
            }
        }
    }
}
