#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <limits>
#include <mutex>
#include <queue>
#include <type_traits>
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
                if (_capacity != std::numeric_limits<size_t>::max()
                    && _read_buffer.size() + _write_buffer.size() >= _capacity) {
                    return false;
                }
                _write_buffer.push_back(std::move(item));
            }
            _t_cond.notify_one();
            return true;
        }

        // Blocking pop: waits until an item is available
        T pop() override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            while (_read_buffer.empty() && _write_buffer.empty()) {
                _t_cond.wait(lock, [this] { return !_read_buffer.empty() || !_write_buffer.empty(); });
            }
            if (_read_buffer.empty()) {
                std::swap(_read_buffer, _write_buffer);
            }
            T item = std::move(_read_buffer.front());
            _read_buffer.pop_front();
            lock.unlock();
            _t_cond.notify_one();
            return item;
        }

        // Non-blocking pop: returns false if no item available
        bool try_pop(T &item) override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            if (_read_buffer.empty()) {
                if (!_write_buffer.empty()) {
                    std::swap(_read_buffer, _write_buffer);
                }
                else {
                    return false;
                }
            }
            item = std::move(_read_buffer.front());
            _read_buffer.pop_front();
            lock.unlock();
            _t_cond.notify_one();
            return true;
        }

        // Blocking pop with timeout: returns false if timeout
        bool pop_with_timeout(T &item, std::chrono::milliseconds timeout) override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            bool has_item =
                _t_cond.wait_for(lock, timeout, [this] { return !_read_buffer.empty() || !_write_buffer.empty(); });
            if (!has_item) {
                return false;
            }

            if (_read_buffer.empty()) {
                std::swap(_read_buffer, _write_buffer);
            }
            item = std::move(_read_buffer.front());
            _read_buffer.pop_front();
            lock.unlock();
            _t_cond.notify_one();
            return true;
        }

        // Get total size of queue (snapshot only)
        size_t size() const override {
            std::lock_guard<std::mutex> lock(_q_mutex);
            return _read_buffer.size() + _write_buffer.size();
        }

    private:
        std::deque<T> _write_buffer;
        std::deque<T> _read_buffer;
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
                if (_capacity != std::numeric_limits<size_t>::max()
                    && _read_buffer.size() + _write_buffer.size() >= _capacity) {
                    return false;
                }
                push_heap(_write_buffer, std::move(item));
            }
            _t_cond.notify_one();
            return true;
        }

        // Blocking pop: waits until an item is available
        T pop() override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            while (_read_buffer.empty() && _write_buffer.empty()) {
                _t_cond.wait(lock, [this] { return !_read_buffer.empty() || !_write_buffer.empty(); });
            }
            if (_read_buffer.empty()) {
                _read_buffer.swap(_write_buffer);
            }
            T item = pop_heap(_read_buffer);
            lock.unlock();
            _t_cond.notify_one();
            return item;
        }

        // Non-blocking pop: returns false if no item available
        bool try_pop(T &item) override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            if (_read_buffer.empty()) {
                if (!_write_buffer.empty()) {
                    _read_buffer.swap(_write_buffer);
                }
                else {
                    return false;
                }
            }
            item = pop_heap(_read_buffer);
            lock.unlock();
            _t_cond.notify_one();
            return true;
        }

        // Blocking pop with timeout: returns false if timeout
        bool pop_with_timeout(T &item, std::chrono::milliseconds timeout) override {
            std::unique_lock<std::mutex> lock(_q_mutex);
            bool has_item =
                _t_cond.wait_for(lock, timeout, [this] { return !_read_buffer.empty() || !_write_buffer.empty(); });
            if (!has_item) {
                return false;
            }

            if (_read_buffer.empty()) {
                _read_buffer.swap(_write_buffer);
            }
            item = pop_heap(_read_buffer);
            lock.unlock();
            _t_cond.notify_one();
            return true;
        }

        // Get total size of queue (snapshot only)
        size_t size() const override {
            std::lock_guard<std::mutex> lock(_q_mutex);
            return _read_buffer.size() + _write_buffer.size();
        }

    private:
        std::vector<T> _write_buffer;
        std::vector<T> _read_buffer;
        mutable std::mutex _q_mutex;
        std::condition_variable _t_cond;
        Compare _compare;
        size_t _capacity = std::numeric_limits<size_t>::max();

        void push_heap(std::vector<T> &buf, T &&item) {
            buf.push_back(std::move(item));
            std::push_heap(buf.begin(), buf.end(), _compare);
        }

        T pop_heap(std::vector<T> &buf) {
            std::pop_heap(buf.begin(), buf.end(), _compare);
            T item = std::move(buf.back());
            buf.pop_back();
            return item;
        }
    };

} // namespace tp
