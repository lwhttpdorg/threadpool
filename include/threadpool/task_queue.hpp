#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

#include "threadpool/callable.hpp"

namespace tp {
    template<typename T>
    class task_queue {
    public:
        virtual ~task_queue() = default;

        // Blocking push: waits until space is available
        virtual void push(T &&item) = 0;

        // Non-blocking push (move): returns false if queue is full
        virtual bool try_push(T &&item) = 0;

        // Blocking pop: waits until an item is available
        virtual T pop() = 0;

        // Non-blocking pop: returns false if no item available
        virtual bool try_pop(T &item) = 0;

        // Blocking pop with timeout: returns false if timeout
        virtual bool pop_with_timeout(T &item, std::chrono::milliseconds timeout) = 0;

        // Get total size of queue (snapshot only)
        virtual size_t size() const = 0;

        // Wake up all threads waiting on pop/pop_with_timeout
        virtual void wake_all() = 0;
    };

    // FIFO blocking task queue
    template<typename T>
    class fifo_task_queue: public task_queue<T> {
    public:
        explicit fifo_task_queue(std::optional<size_t> _capacity = std::nullopt) : capacity(_capacity) {
            static_assert(
                std::is_nothrow_move_constructible_v<T>,
                "fifo_task_queue error: Type T must be noexcept move constructible to prevent task loss during pop().");
        }

        // Blocking push: waits until space is available
        void push(T &&item) override {
            std::unique_lock<std::mutex> lock(q_mutex);
            cv_not_full.wait(lock, [this] { return !capacity.has_value() || task_q.size() < *capacity; });
            task_q.push_back(std::move(item));
            lock.unlock();
            cv_not_empty.notify_one();
        }

        // Non-blocking push (move): returns false if queue is full
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

        // Blocking pop: waits until an item is available
        T pop() override {
            std::unique_lock<std::mutex> lock(q_mutex);
            cv_not_empty.wait(lock, [this] { return !task_q.empty(); });
            T item = std::move(task_q.front());
            task_q.pop_front();
            lock.unlock();
            cv_not_full.notify_one(); // notify producers waiting for space
            return item;
        }

        // Non-blocking pop: returns false if no item available
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

        // Blocking pop with timeout: returns false if timeout
        bool pop_with_timeout(T &item, std::chrono::milliseconds timeout) override {
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

        // Get total size of queue (snapshot only)
        size_t size() const override {
            std::lock_guard<std::mutex> lock(q_mutex);
            return task_q.size();
        }

        // Wake up all threads waiting on pop/pop_with_timeout
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

    // Priority blocking task queue
    template<typename T, typename Compare = callable_priority_less>
    class priority_task_queue: public task_queue<T> {
    public:
        explicit priority_task_queue(std::optional<size_t> _capacity = std::nullopt) : capacity(_capacity) {
            static_assert(std::is_nothrow_move_constructible_v<T>,
                          "priority_task_queue error: Type T must be noexcept move constructible to prevent task loss "
                          "during pop().");
            if (capacity.has_value()) {
                task_q.reserve(*capacity);
            }
        }

        // Blocking push: waits until space is available
        void push(T &&item) override {
            std::unique_lock<std::mutex> lock(q_mutex);
            cv_not_full.wait(lock, [this] { return !capacity.has_value() || task_q.size() < *capacity; });
            task_q.push_back(std::move(item));
            std::push_heap(task_q.begin(), task_q.end(), task_compare);
            lock.unlock();
            cv_not_empty.notify_one();
        }

        // Non-blocking push (move): returns false if queue is full
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

        // Blocking pop: waits until an item is available
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

        // Non-blocking pop: returns false if no item available
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

        // Blocking pop with timeout: returns false if timeout
        bool pop_with_timeout(T &item, std::chrono::milliseconds timeout) override {
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

        // Get total size of queue (snapshot only)
        size_t size() const override {
            std::lock_guard<std::mutex> lock(q_mutex);
            return task_q.size();
        }

        // Wake up all threads waiting on pop/pop_with_timeout
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
