#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "threadpool/blocking_queue.hpp"

SCENARIO("priority_blocking_queue orders items by priority (max-heap)", "[priority_blocking_queue]") {
    GIVEN("an empty priority_blocking_queue with std::less compare") {
        tp::priority_blocking_queue<int, std::less<>> task_q;

        WHEN("items with different priorities are pushed") {
            task_q.try_push(3);
            task_q.try_push(1);
            task_q.try_push(5);
            task_q.try_push(2);

            THEN("the highest priority items are popped first") {
                REQUIRE(task_q.pop() == 5);
                REQUIRE(task_q.pop() == 3);
                REQUIRE(task_q.pop() == 2);
                REQUIRE(task_q.pop() == 1);
                REQUIRE(task_q.size() == 0);
            }
        }
    }
}

SCENARIO("priority_blocking_queue supports try_pop and size queries", "[priority_blocking_queue]") {
    GIVEN("an empty priority_blocking_queue") {
        tp::priority_blocking_queue<int, std::less<>> task_q;

        THEN("it reports empty") {
            REQUIRE(task_q.size() == 0);
        }

        WHEN("one item is pushed") {
            task_q.try_push(10);

            THEN("size is 1 and it is not empty") {
                REQUIRE(task_q.size() == 1);
                REQUIRE(task_q.size() != 0);
            }

            AND_WHEN("try_pop retrieves the item") {
                int item = 0;
                bool success = task_q.try_pop(item);

                THEN("it succeeds with the highest priority item") {
                    REQUIRE(success);
                    REQUIRE(item == 10);
                    REQUIRE(task_q.size() == 0);
                }
            }
        }
    }
}

SCENARIO("priority_blocking_queue timed_pop retrieves items", "[priority_blocking_queue]") {
    GIVEN("a priority_blocking_queue with one item") {
        tp::priority_blocking_queue<int, std::less<>> task_q;
        task_q.try_push(7);

        WHEN("timed_pop is called") {
            int item = 0;
            bool success = task_q.timed_pop(item, std::chrono::milliseconds(100));

            THEN("it returns the item successfully") {
                REQUIRE(success);
                REQUIRE(item == 7);
            }
        }
    }
}

SCENARIO("priority_blocking_queue timed_pop times out on empty queue", "[priority_blocking_queue]") {
    GIVEN("an empty priority_blocking_queue") {
        tp::priority_blocking_queue<int, std::less<>> task_q;

        WHEN("timed_pop is called with a short timeout") {
            int item = 0;
            bool success = task_q.timed_pop(item, std::chrono::milliseconds(50));

            THEN("it returns false") {
                REQUIRE_FALSE(success);
            }
        }
    }
}

SCENARIO("priority_blocking_queue with min-heap ordering", "[priority_blocking_queue]") {
    GIVEN("an empty priority_blocking_queue with std::greater compare") {
        tp::priority_blocking_queue<int, std::greater<>> task_q;

        WHEN("items with different priorities are pushed") {
            task_q.try_push(3);
            task_q.try_push(1);
            task_q.try_push(5);
            task_q.try_push(2);

            THEN("the lowest priority items are popped first") {
                REQUIRE(task_q.pop() == 1);
                REQUIRE(task_q.pop() == 2);
                REQUIRE(task_q.pop() == 3);
                REQUIRE(task_q.pop() == 5);
                REQUIRE(task_q.size() == 0);
            }
        }
    }
}

SCENARIO("priority_blocking_queue push blocks until space is available", "[priority_blocking_queue]") {
    GIVEN("a bounded priority_blocking_queue with capacity 2") {
        tp::priority_blocking_queue<int, std::less<>> task_q(2);

        // Fill the queue
        task_q.try_push(1);
        task_q.try_push(2);

        WHEN("push is called on a full queue from another thread") {
            std::atomic push_completed{false};
            std::jthread producer([&] {
                task_q.push(std::move(3));
                push_completed = true;
            });

            // Give the producer time to block
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            REQUIRE_FALSE(push_completed.load());

            THEN("push completes after an item is popped") {
                const auto val = task_q.pop();
                REQUIRE(val == 2); // max-heap: highest first

                producer.join();
                REQUIRE(push_completed.load());
            }
        }
    }
}

SCENARIO("priority_blocking_queue try_pop on empty queue returns false", "[priority_blocking_queue]") {
    GIVEN("an empty priority_blocking_queue") {
        tp::priority_blocking_queue<int, std::greater<>> task_q;

        WHEN("try_pop is called") {
            int item = -1;
            bool success = task_q.try_pop(item);

            THEN("it returns false") {
                REQUIRE_FALSE(success);
                REQUIRE(item == -1);
            }
        }
    }
}

SCENARIO("priority_blocking_queue timed_pop times out on empty queue (greater)", "[priority_blocking_queue]") {
    GIVEN("an empty priority_blocking_queue with std::greater") {
        tp::priority_blocking_queue<int, std::greater<>> task_q;

        WHEN("timed_pop is called with a short timeout") {
            int item = 0;
            bool success = task_q.timed_pop(item, std::chrono::milliseconds(50));

            THEN("it returns false") {
                REQUIRE_FALSE(success);
            }
        }
    }
}

SCENARIO("priority_blocking_queue empty returns correct state", "[priority_blocking_queue]") {
    GIVEN("an empty priority_blocking_queue") {
        tp::priority_blocking_queue<int, std::less<>> task_q;

        THEN("empty returns true") {
            REQUIRE(task_q.empty());
        }

        WHEN("an item is pushed") {
            task_q.try_push(5);

            THEN("empty returns false") {
                REQUIRE_FALSE(task_q.empty());
            }
        }
    }

    GIVEN("an empty priority_blocking_queue with std::greater") {
        tp::priority_blocking_queue<int, std::greater<>> task_q;

        THEN("empty returns true") {
            REQUIRE(task_q.empty());
        }

        WHEN("an item is pushed") {
            task_q.try_push(5);

            THEN("empty returns false") {
                REQUIRE_FALSE(task_q.empty());
            }
        }
    }
}

SCENARIO("priority_blocking_queue wake_all unblocks waiting threads", "[priority_blocking_queue]") {
    GIVEN("an empty priority_blocking_queue") {
        tp::priority_blocking_queue<int, std::less<>> task_q;

        WHEN("a thread is blocked on timed_pop and wake_all is called") {
            std::atomic pop_returned{false};
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

    GIVEN("an empty priority_blocking_queue with std::greater") {
        tp::priority_blocking_queue<int, std::greater<>> task_q;

        WHEN("a thread is blocked on timed_pop and wake_all is called") {
            std::atomic pop_returned{false};
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
