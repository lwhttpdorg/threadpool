#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "threadpool/callable.hpp"
#include "threadpool/task_queue.hpp"

namespace tp {
    constexpr const char *THREAD_POOL_NOT_RUN = "thread pool is not running";
    constexpr const char *THREAD_POOL_FULL = "queue is full and max threads reached";

    // C++ thread pool modeled after java.util.concurrent.ThreadPoolExecutor.
    class thread_pool {
    public:
        class rejected_execution_exception: public std::runtime_error {
        public:
            explicit rejected_execution_exception(const char *msg) : std::runtime_error(msg) {
            }
            explicit rejected_execution_exception(const std::string &msg) : std::runtime_error(msg) {
            }
        };

        enum class state { running, shutdown, stop };

        enum class reject_policy {
            abort,         // throw rejected_execution_exception
            caller_runs,   // execute the task in the calling thread
            discard,       // silently drop the task
            discard_oldest // remove the oldest queued task and retry enqueue
        };

        thread_pool(unsigned int _core_pool_size, unsigned int _max_pool_size, std::chrono::seconds _keep_alive_time,
                    std::unique_ptr<task_queue<callable>> _work_queue,
                    reject_policy _rej_policy = reject_policy::abort) :
            core_pool_size(_core_pool_size), max_pool_size(_max_pool_size), keep_alive_time(_keep_alive_time),
            work_queue(std::move(_work_queue)), rej_policy(_rej_policy) {
            if (_core_pool_size > _max_pool_size) {
                throw std::invalid_argument("core pool size must be less than or equal to max pool size");
            }
            start_core_threads();
        }

        thread_pool(unsigned int _core_pool_size, unsigned int _max_pool_size, std::chrono::minutes _keep_alive_time,
                    std::unique_ptr<task_queue<callable>> _work_queue,
                    reject_policy _rej_policy = reject_policy::abort) :
            core_pool_size(_core_pool_size), max_pool_size(_max_pool_size),
            keep_alive_time(std::chrono::duration_cast<std::chrono::seconds>(_keep_alive_time)),
            work_queue(std::move(_work_queue)), rej_policy(_rej_policy) {
            if (_core_pool_size > _max_pool_size) {
                throw std::invalid_argument("core pool size must be less than or equal to max pool size");
            }
            start_core_threads();
        }

        // The destructor calls shutdown_now() and unconditionally joins all worker
        // threads. No threads are ever detached, preventing use-after-free and
        // memory leaks. If tasks are deadlocked, the destructor will block
        // indefinitely — callers must ensure tasks are well-behaved or call
        // shutdown()/await_termination() explicitly before destruction.
        ~thread_pool() {
            shutdown_now();
            join_all_threads();
        }

        thread_pool(const thread_pool &) = delete;
        thread_pool &operator=(const thread_pool &) = delete;
        thread_pool(thread_pool &&) = delete;
        thread_pool &operator=(thread_pool &&) = delete;

        // execute(Runnable command)
        void execute(callable task) {
            submit_task(std::move(task));
        }

        template<class F, class... Args,
                 typename = std::enable_if_t<!(std::is_integral_v<std::decay_t<F>> && sizeof...(Args) >= 1)>>
        void execute(F &&f, Args &&...args) {
            auto fn = [f = std::forward<F>(f), tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                std::apply(std::move(f), std::move(tup));
            };
            submit_task(callable(std::move(fn)));
        }

        template<class F, class... Args>
        void execute(unsigned int priority, F &&f, Args &&...args) {
            auto fn = [f = std::forward<F>(f), tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                std::apply(std::move(f), std::move(tup));
            };
            submit_task(callable(std::move(fn), priority));
        }

        void shutdown() {
            state expected = state::running;
            if (pool_state.compare_exchange_strong(expected, state::shutdown)) {
                // Push low-priority poison pills so real tasks are consumed first.
                push_poison_pills(callable(callable::LOWEST_PRIORITY));
            }
        }

        std::vector<callable> shutdown_now() {
            pool_state.store(state::stop);

            // Drain remaining tasks BEFORE waking workers, so that poison pills
            // are not consumed by the drain loop.
            std::vector<callable> remaining;
            {
                std::lock_guard<std::mutex> lock(threads_mutex);
                callable task(callable::LOWEST_PRIORITY);
                while (work_queue->try_pop(task)) {
                    if (!task.is_poison_pill()) {
                        remaining.push_back(std::move(task));
                    }
                }
            }

            // Push high-priority poison pills for immediate worker exit.
            push_poison_pills(callable(callable::HIGHEST_PRIORITY));
            return remaining;
        }

        bool await_termination(std::chrono::seconds timeout) {
            std::unique_lock<std::mutex> lock(threads_mutex);
            auto all_workers_stopped = [this] { return active_worker_count == 0; };
            bool terminated = false;
            if (timeout.count() < 0) {
                termination.wait(lock, all_workers_stopped);
                terminated = true;
            }
            else {
                terminated = termination.wait_for(lock, timeout, all_workers_stopped);
            }
            if (terminated) {
                join_all_workers_locked(lock);
            }
            return terminated;
        }

    private:
        // Eagerly create all core threads at construction time.
        void start_core_threads() {
            std::lock_guard<std::mutex> lock(threads_mutex);
            core_threads.reserve(core_pool_size);
            for (unsigned int i = 0; i < core_pool_size; ++i) {
                core_threads.emplace_back([this]() {
                    core_worker_loop();
                    {
                        std::lock_guard<std::mutex> worker_lock(threads_mutex);
                        --active_worker_count;
                    }
                    termination.notify_all();
                });
                ++active_worker_count;
            }
        }

        // Pushes one poison pill per active worker into the queue, then wakes all
        // blocked consumers. If the queue is full some pills may fail to enqueue,
        // but wake_all() ensures workers re-check the pool state and exit.
        void push_poison_pills(callable pill) {
            std::lock_guard<std::mutex> lock(threads_mutex);
            for (unsigned int i = 0; i < active_worker_count; ++i) {
                work_queue->try_push(callable(pill));
            }
            work_queue->wake_all();
        }

        bool try_start_non_core_worker(callable &task) {
            const state s = pool_state.load();
            if (s != state::running) {
                return false;
            }
            try {
                std::lock_guard<std::mutex> lock(threads_mutex);
                if (active_worker_count >= max_pool_size) {
                    return false;
                }
                non_core_threads.emplace_back([this, t = std::move(task)]() mutable {
                    non_core_worker_loop(std::move(t));
                    {
                        std::lock_guard<std::mutex> worker_lock(threads_mutex);
                        --active_worker_count;
                    }
                    termination.notify_all();
                });
                ++active_worker_count;
                return true;
            }
            catch (...) {
                return false;
            }
        }

        void submit_task(callable task) {
            if (pool_state.load() != state::running) {
                reject_task(task, THREAD_POOL_NOT_RUN);
                return;
            }

            // Step 1: try to enqueue
            if (work_queue->try_push(std::move(task))) {
                return;
            }

            // Step 2: queue full — try to start a non-core worker
            if (try_start_non_core_worker(task)) {
                return;
            }

            // Step 3: apply rejection policy
            if (rej_policy == reject_policy::discard_oldest && pool_state.load() == state::running) {
                callable oldest(callable::LOWEST_PRIORITY);
                if (work_queue->try_pop(oldest)) {
                    // Retry enqueue once after discarding oldest
                    if (work_queue->try_push(std::move(task))) {
                        return;
                    }
                }
            }

            reject_task(task, THREAD_POOL_FULL);
        }

        void join_all_threads() {
            std::unique_lock<std::mutex> lock(threads_mutex);
            auto to_join = extract_and_unlock_threads(lock);
            for (auto &t: to_join) {
                if (t.joinable()) {
                    t.join();
                }
            }
        }

        void join_all_workers_locked(std::unique_lock<std::mutex> &lock) {
            auto to_join = extract_and_unlock_threads(lock);
            for (auto &t: to_join) {
                if (t.joinable()) {
                    t.join();
                }
            }
            lock.lock();
        }

        // Helper to move threads out of the pool and unlock the mutex so joining doesn't block others.
        std::vector<std::thread> extract_and_unlock_threads(std::unique_lock<std::mutex> &lock) {
            std::vector<std::thread> to_join;
            to_join.reserve(core_threads.size() + non_core_threads.size());
            for (auto &t: core_threads) {
                to_join.push_back(std::move(t));
            }
            core_threads.clear();
            for (auto &t: non_core_threads) {
                to_join.push_back(std::move(t));
            }
            non_core_threads.clear();
            lock.unlock();
            return to_join;
        }

        // Core worker: blocks indefinitely on the queue, exits only on shutdown/stop.
        void core_worker_loop() {
            for (;;) {
                callable task = work_queue->pop();

                const state s = pool_state.load();
                if (s == state::stop) {
                    break;
                }

                if (task.is_poison_pill()) {
                    // Poison pill — exit signal
                    break;
                }

                try {
                    task();
                }
                catch (...) {
                    // Swallow user exceptions to prevent std::terminate.
                }
            }
        }

        // Non-core worker: executes its initial task, then polls with timeout.
        void non_core_worker_loop(callable task) {
            if (!task.is_poison_pill()) {
                try {
                    task();
                }
                catch (...) {
                }
            }

            for (;;) {
                const state s = pool_state.load();
                if (s == state::stop) {
                    break;
                }

                // Timed wait — exit if idle too long
                task = callable(callable::LOWEST_PRIORITY);
                const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(keep_alive_time);
                if (!work_queue->pop_with_timeout(task, timeout)) {
                    break; // idle timeout, non-core thread exits
                }
                if (task.is_poison_pill()) {
                    // Poison pill — exit signal
                    break;
                }

                try {
                    task();
                }
                catch (...) {
                }
            }
        }

        void reject_task(callable &task, const std::string &reason) {
            switch (rej_policy) {
                case reject_policy::abort:
                    throw rejected_execution_exception("Task rejected: " + reason);
                case reject_policy::caller_runs:
                    if (pool_state.load() == state::running && !task.is_poison_pill()) {
                        task();
                    }
                    break;
                case reject_policy::discard:
                    task = callable(callable::LOWEST_PRIORITY);
                    break;
                case reject_policy::discard_oldest:
                    task = callable(callable::LOWEST_PRIORITY);
                    break;
            }
        }

        unsigned int core_pool_size;
        unsigned int max_pool_size;
        std::chrono::seconds keep_alive_time;

        std::vector<std::thread> core_threads;     // created eagerly at construction
        std::vector<std::thread> non_core_threads; // created on demand, exit after keep_alive_time
        unsigned int active_worker_count{0};

        std::unique_ptr<task_queue<callable>> work_queue;
        reject_policy rej_policy;

        std::atomic<state> pool_state{state::running};
        mutable std::mutex threads_mutex;
        std::condition_variable termination;
    };

} // namespace tp
