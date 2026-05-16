#include <catch2/catch_test_macros.hpp>

#include "threadpool/task_queue.hpp"

SCENARIO("priority_task_queue orders items by priority (max-heap)", "[priority_task_queue]") {
    GIVEN("an empty priority_task_queue with std::less compare") {
        tp::priority_task_queue<int, std::less<>> task_q;

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

SCENARIO("priority_task_queue supports try_pop and size queries", "[priority_task_queue]") {
    GIVEN("an empty priority_task_queue") {
        tp::priority_task_queue<int, std::less<>> task_q;

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

SCENARIO("priority_task_queue pop_with_timeout retrieves items", "[priority_task_queue]") {
    GIVEN("a priority_task_queue with one item") {
        tp::priority_task_queue<int, std::less<>> task_q;
        task_q.try_push(7);

        WHEN("pop_with_timeout is called") {
            int item = 0;
            bool success = task_q.pop_with_timeout(item, std::chrono::milliseconds(100));

            THEN("it returns the item successfully") {
                REQUIRE(success);
                REQUIRE(item == 7);
            }
        }
    }
}

SCENARIO("priority_task_queue pop_with_timeout times out on empty queue", "[priority_task_queue]") {
    GIVEN("an empty priority_task_queue") {
        tp::priority_task_queue<int, std::less<>> task_q;

        WHEN("pop_with_timeout is called with a short timeout") {
            int item = 0;
            bool success = task_q.pop_with_timeout(item, std::chrono::milliseconds(50));

            THEN("it returns false") {
                REQUIRE_FALSE(success);
            }
        }
    }
}

SCENARIO("priority_task_queue with min-heap ordering", "[priority_task_queue]") {
    GIVEN("an empty priority_task_queue with std::greater compare") {
        tp::priority_task_queue<int, std::greater<>> task_q;

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
