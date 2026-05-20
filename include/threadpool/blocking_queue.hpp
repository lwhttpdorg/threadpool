#pragma once

#include <algorithm>          // for std::push_heap, std::pop_heap
#include <chrono>             // for std::chrono::milliseconds
#include <condition_variable> // for std::condition_variable
#include <deque>              // for std::deque
#include <mutex>              // for std::mutex, std::lock_guard, std::unique_lock
#include <optional>           // for std::optional, std::nullopt
#include <type_traits>        // for std::is_nothrow_move_constructible_v
#include <vector>             // for std::vector

#include "threadpool/callable.hpp"

namespace tp {

    /**
     * @class blocking_queue
     * @brief Abstract interface for a thread-safe blocking queue.
     * @tparam T The element type stored in the queue.
     */
    template<typename T>
    class blocking_queue {
    public:
        virtual ~blocking_queue() = default;

        /**
         * @brief Blocking push: waits until space is available, then enqueues the item.
         * @param item The item to enqueue (moved).
         */
        virtual void push(T &&item) = 0;

        /**
         * @brief Non-blocking push: attempts to enqueue without waiting.
         * @param item The item to enqueue (moved).
         * @return true if the item was enqueued, false if the queue is full.
         */
        virtual bool try_push(T &&item) = 0;

        /**
         * @brief Blocking pop: waits until an item is available, then dequeues it.
         * @return The dequeued item.
         */
        virtual T pop() = 0;

        /**
         * @brief Non-blocking pop: attempts to dequeue without waiting.
         * @param[out] item The dequeued item if successful.
         * @return true if an item was dequeued, false if the queue is empty.
         */
        virtual bool try_pop(T &item) = 0;

        /**
         * @brief Blocking pop with timeout.
         * @param[out] item The dequeued item if successful.
         * @param timeout Maximum duration to wait.
         * @return true if an item was dequeued, false if the timeout expired.
         */
        virtual bool timed_pop(T &item, std::chrono::milliseconds timeout) = 0;

        /**
         * @brief Returns the current number of items in the queue (snapshot).
         * @return The queue size.
         */
        virtual size_t size() const = 0;

        /**
         * @brief Checks whether the queue is empty (snapshot).
         * @return true if empty.
         */
        virtual bool empty() const = 0;

        /**
         * @brief Wakes all threads blocked on pop/timed_pop.
         *
         * Used during shutdown to unblock waiting worker threads.
         */
        virtual void wake_all() = 0;
    };

    /**
     * @class array_blocking_queue
     * @brief Thread-safe FIFO blocking queue with optional bounded capacity.
     * @tparam T The element type. Must be nothrow move constructible.
     */
    template<typename T>
    class array_blocking_queue: public blocking_queue<T> {
    public:
        /**
         * @brief Constructs a FIFO task queue.
         * @param _capacity Optional maximum capacity. If nullopt, the queue is unbounded.
         */
        explicit array_blocking_queue(std::optional<size_t> _capacity = std::nullopt) : capacity(_capacity) {
            static_assert(std::is_nothrow_move_constructible_v<T>,
                          "array_blocking_queue error: Type T must be noexcept move constructible to prevent task loss "
                          "during pop().");
        }

        /// @copydoc blocking_queue::push
        void push(T &&item) override {
            std::unique_lock<std::mutex> lock(q_mutex);
            cv_not_full.wait(lock, [this] { return !capacity.has_value() || task_q.size() < *capacity; });
            task_q.push_back(std::move(item));
            lock.unlock();
            cv_not_empty.notify_one();
        }

        /// @copydoc blocking_queue::try_push
        bool try_push(T &&item) override {
            {
                std::unique_lock<std::mutex> lock(q_mutex);
                if (capacity.has_value() && task_q.size() >= *capacity) {
                    return false;
                }
                task_q.push_back(std::move(item));
            }
            cv_not_empty.notify_one();
            return true;
        }

        /// @copydoc blocking_queue::pop
        T pop() override {
            std::unique_lock<std::mutex> lock(q_mutex);
            cv_not_empty.wait(lock, [this] { return !task_q.empty(); });
            T item = std::move(task_q.front());
            task_q.pop_front();
            lock.unlock();
            cv_not_full.notify_one();
            return item;
        }

        /// @copydoc blocking_queue::try_pop
        bool try_pop(T &item) override {
            std::unique_lock<std::mutex> lock(q_mutex);
            if (task_q.empty()) {
                return false;
            }
            item = std::move(task_q.front());
            task_q.pop_front();
            lock.unlock();
            cv_not_full.notify_one();
            return true;
        }

        /// @copydoc blocking_queue::timed_pop
        bool timed_pop(T &item, std::chrono::milliseconds timeout) override {
            std::unique_lock<std::mutex> lock(q_mutex);
            bool has_item = cv_not_empty.wait_for(lock, timeout, [this] { return !task_q.empty(); });
            if (!has_item) {
                return false;
            }
            item = std::move(task_q.front());
            task_q.pop_front();
            lock.unlock();
            cv_not_full.notify_one();
            return true;
        }

        /// @copydoc blocking_queue::size
        size_t size() const override {
            std::lock_guard<std::mutex> lock(q_mutex);
            return task_q.size();
        }

        /// @copydoc blocking_queue::empty
        bool empty() const override {
            std::lock_guard<std::mutex> lock(q_mutex);
            return task_q.empty();
        }

        /// @copydoc blocking_queue::wake_all
        void wake_all() override {
            cv_not_empty.notify_all();
            cv_not_full.notify_all();
        }

    private:
        std::deque<T> task_q;
        mutable std::mutex q_mutex;
        std::condition_variable cv_not_empty;
        std::condition_variable cv_not_full;
        std::optional<size_t> capacity;
    };

    /**
     * @class priority_blocking_queue
     * @brief Thread-safe priority blocking queue (max-heap by default).
     * @tparam T The element type. Must be nothrow move constructible.
     * @tparam Compare Comparator type for heap ordering. Defaults to callable_priority_less (max-heap).
     */
    template<typename T, typename Compare = callable_priority_less>
    class priority_blocking_queue: public blocking_queue<T> {
    public:
        /**
         * @brief Constructs a priority task queue.
         * @param _capacity Optional maximum capacity. If nullopt, the queue is unbounded.
         */
        explicit priority_blocking_queue(std::optional<size_t> _capacity = std::nullopt) : capacity(_capacity) {
            static_assert(
                std::is_nothrow_move_constructible_v<T>,
                "priority_blocking_queue error: Type T must be noexcept move constructible to prevent task loss "
                "during pop().");
            if (capacity.has_value()) {
                task_q.reserve(*capacity);
            }
        }

        /// @copydoc blocking_queue::push
        void push(T &&item) override {
            std::unique_lock<std::mutex> lock(q_mutex);
            cv_not_full.wait(lock, [this] { return !capacity.has_value() || task_q.size() < *capacity; });
            task_q.push_back(std::move(item));
            std::push_heap(task_q.begin(), task_q.end(), task_compare);
            lock.unlock();
            cv_not_empty.notify_one();
        }

        /// @copydoc blocking_queue::try_push
        bool try_push(T &&item) override {
            {
                std::unique_lock<std::mutex> lock(q_mutex);
                if (capacity.has_value() && task_q.size() >= *capacity) {
                    return false;
                }
                task_q.push_back(std::move(item));
                std::push_heap(task_q.begin(), task_q.end(), task_compare);
            }
            cv_not_empty.notify_one();
            return true;
        }

        /// @copydoc blocking_queue::pop
        T pop() override {
            std::unique_lock<std::mutex> lock(q_mutex);
            cv_not_empty.wait(lock, [this] { return !task_q.empty(); });
            std::pop_heap(task_q.begin(), task_q.end(), task_compare);
            T item = std::move(task_q.back());
            task_q.pop_back();
            lock.unlock();
            cv_not_full.notify_one();
            return item;
        }

        /// @copydoc blocking_queue::try_pop
        bool try_pop(T &item) override {
            std::unique_lock<std::mutex> lock(q_mutex);
            if (task_q.empty()) {
                return false;
            }
            std::pop_heap(task_q.begin(), task_q.end(), task_compare);
            item = std::move(task_q.back());
            task_q.pop_back();
            lock.unlock();
            cv_not_full.notify_one();
            return true;
        }

        /// @copydoc blocking_queue::timed_pop
        bool timed_pop(T &item, std::chrono::milliseconds timeout) override {
            std::unique_lock<std::mutex> lock(q_mutex);
            bool has_item = cv_not_empty.wait_for(lock, timeout, [this] { return !task_q.empty(); });
            if (!has_item) {
                return false;
            }
            std::pop_heap(task_q.begin(), task_q.end(), task_compare);
            item = std::move(task_q.back());
            task_q.pop_back();
            lock.unlock();
            cv_not_full.notify_one();
            return true;
        }

        /// @copydoc blocking_queue::size
        size_t size() const override {
            std::lock_guard<std::mutex> lock(q_mutex);
            return task_q.size();
        }

        /// @copydoc blocking_queue::empty
        bool empty() const override {
            std::lock_guard<std::mutex> lock(q_mutex);
            return task_q.empty();
        }

        /// @copydoc blocking_queue::wake_all
        void wake_all() override {
            cv_not_empty.notify_all();
            cv_not_full.notify_all();
        }

    private:
        std::vector<T> task_q;
        Compare task_compare;
        mutable std::mutex q_mutex;
        std::condition_variable cv_not_empty;
        std::condition_variable cv_not_full;
        std::optional<size_t> capacity;
    };
} // namespace tp
