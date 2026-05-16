#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <vector>

namespace tp {
    template<typename T>
    class task_queue {
    public:
        virtual ~task_queue() = default;

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
        explicit fifo_task_queue(size_t capacity = std::numeric_limits<size_t>::max()) : _capacity(capacity) {
        }

        // Non-blocking push (move): returns false if queue is full
        bool try_push(T &&item) override {
            {
                std::unique_lock<std::mutex> lock(_q_mutex);
                if (_capacity != std::numeric_limits<size_t>::max() && _queue.size() >= _capacity) {
                    return false;
                }
                _queue.push_back(std::move(item));
            }
            _t_cond.notify_one();
            return true;
        }

        // Blocking pop: waits until an item is available
        T pop() override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            while (_queue.empty()) {
                _t_cond.wait(lock, [this] { return !_queue.empty(); });
            }
            T item = std::move(_queue.front());
            _queue.pop_front();
            lock.unlock();
            _t_cond.notify_one();
            return item;
        }

        // Non-blocking pop: returns false if no item available
        bool try_pop(T &item) override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            if (_queue.empty()) {
                return false;
            }
            item = std::move(_queue.front());
            _queue.pop_front();
            lock.unlock();
            _t_cond.notify_one();
            return true;
        }

        // Blocking pop with timeout: returns false if timeout
        bool pop_with_timeout(T &item, std::chrono::milliseconds timeout) override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            bool has_item = _t_cond.wait_for(lock, timeout, [this] { return !_queue.empty(); });
            if (!has_item) {
                return false;
            }
            item = std::move(_queue.front());
            _queue.pop_front();
            lock.unlock();
            _t_cond.notify_one();
            return true;
        }

        // Get total size of queue (snapshot only)
        size_t size() const override {
            std::lock_guard<std::mutex> lock(_q_mutex);
            return _queue.size();
        }

        // Wake up all threads waiting on pop/pop_with_timeout
        void wake_all() override {
            _t_cond.notify_all();
        }

    private:
        std::deque<T> _queue;
        mutable std::mutex _q_mutex;
        std::condition_variable _t_cond;
        size_t _capacity = std::numeric_limits<size_t>::max();
    };

    // Priority blocking task queue
    template<typename T, typename Compare = std::less<T>>
    class priority_task_queue: public task_queue<T> {
    public:
        explicit priority_task_queue(size_t capacity = std::numeric_limits<size_t>::max()) : _capacity(capacity) {
        }

        // Non-blocking push (move): returns false if queue is full
        bool try_push(T &&item) override {
            {
                std::unique_lock<std::mutex> lock(_q_mutex);
                if (_capacity != std::numeric_limits<size_t>::max() && _queue.size() >= _capacity) {
                    return false;
                }
                _queue.push_back(std::move(item));
                std::push_heap(_queue.begin(), _queue.end(), _compare);
            }
            _t_cond.notify_one();
            return true;
        }

        // Blocking pop: waits until an item is available
        T pop() override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            while (_queue.empty()) {
                _t_cond.wait(lock, [this] { return !_queue.empty(); });
            }
            std::pop_heap(_queue.begin(), _queue.end(), _compare);
            T item = std::move(_queue.back());
            _queue.pop_back();
            lock.unlock();
            _t_cond.notify_one();
            return item;
        }

        // Non-blocking pop: returns false if no item available
        bool try_pop(T &item) override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            if (_queue.empty()) {
                return false;
            }
            std::pop_heap(_queue.begin(), _queue.end(), _compare);
            item = std::move(_queue.back());
            _queue.pop_back();
            lock.unlock();
            _t_cond.notify_one();
            return true;
        }

        // Blocking pop with timeout: returns false if timeout
        bool pop_with_timeout(T &item, std::chrono::milliseconds timeout) override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            bool has_item = _t_cond.wait_for(lock, timeout, [this] { return !_queue.empty(); });
            if (!has_item) {
                return false;
            }
            std::pop_heap(_queue.begin(), _queue.end(), _compare);
            item = std::move(_queue.back());
            _queue.pop_back();
            lock.unlock();
            _t_cond.notify_one();
            return true;
        }

        // Get total size of queue (snapshot only)
        size_t size() const override {
            std::lock_guard<std::mutex> lock(_q_mutex);
            return _queue.size();
        }

        // Wake up all threads waiting on pop/pop_with_timeout
        void wake_all() override {
            _t_cond.notify_all();
        }

    private:
        std::vector<T> _queue;
        Compare _compare;
        mutable std::mutex _q_mutex;
        std::condition_variable _t_cond;
        size_t _capacity = std::numeric_limits<size_t>::max();
    };
} // namespace tp
