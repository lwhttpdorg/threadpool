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
            abort,         // ThreadPoolExecutor.AbortPolicy
            caller_runs,   // ThreadPoolExecutor.CallerRunsPolicy
            discard,       // ThreadPoolExecutor.DiscardPolicy
            discard_oldest // ThreadPoolExecutor.DiscardOldestPolicy
        };

        thread_pool(int _core_pool_size, int _max_pool_size, std::chrono::seconds _keep_alive_time,
                    std::unique_ptr<task_queue<callable>> _work_queue,
                    reject_policy _rej_policy = reject_policy::abort) :
            core_pool_size(_core_pool_size), max_pool_size(_max_pool_size), keep_alive_time(_keep_alive_time),
            work_queue(std::move(_work_queue)), rej_policy(_rej_policy) {
            if (_core_pool_size > _max_pool_size) {
                throw std::invalid_argument("core pool size must be less than or equal to max pool size");
            }
        }

        thread_pool(int _core_pool_size, int _max_pool_size, std::chrono::minutes _keep_alive_time,
                    std::unique_ptr<task_queue<callable>> _work_queue,
                    reject_policy _rej_policy = reject_policy::abort) :
            core_pool_size(_core_pool_size), max_pool_size(_max_pool_size),
            keep_alive_time(std::chrono::duration_cast<std::chrono::seconds>(_keep_alive_time)),
            work_queue(std::move(_work_queue)), rej_policy(_rej_policy) {
            if (_core_pool_size > _max_pool_size) {
                throw std::invalid_argument("core pool size must be less than or equal to max pool size");
            }
        }

        ~thread_pool() {
            shutdown();
            if (!await_termination(std::chrono::seconds(30))) {
                // Force cleanup to avoid std::terminate on dangling joinable threads.
                shutdown_now();
                std::lock_guard<std::mutex> lock(threads_mutex);
                for (auto &t: worker_threads) {
                    if (t.joinable()) {
                        t.detach();
                    }
                }
                worker_threads.clear();
            }
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
            if constexpr (sizeof...(Args) == 0) {
                submit_task(callable(std::function<void()>(std::forward<F>(f))));
            }
            else {
                auto fn = [f = std::forward<F>(f), tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                    std::apply(std::move(f), std::move(tup));
                };
                submit_task(callable(std::function<void()>(std::move(fn))));
            }
        }

        template<class F, class... Args>
        void execute(unsigned int priority, F &&f, Args &&...args) {
            if constexpr (sizeof...(Args) == 0) {
                submit_task(callable(std::function<void()>(std::forward<F>(f)), priority));
            }
            else {
                auto fn = [f = std::forward<F>(f), tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                    std::apply(std::move(f), std::move(tup));
                };
                submit_task(callable(std::function<void()>(std::move(fn)), priority));
            }
        }

        void shutdown() {
            state expected = state::running;
            if (pool_state.compare_exchange_strong(expected, state::shutdown)) {
                wake_idle_workers();
            }
        }

        std::vector<callable> shutdown_now() {
            pool_state.store(state::stop);
            wake_idle_workers();

            std::vector<callable> remaining;
            callable task{};
            while (work_queue->try_pop(task)) {
                if (task) {
                    remaining.push_back(std::move(task));
                }
            }
            return remaining;
        }

        bool await_termination(std::chrono::seconds timeout) {
            std::unique_lock<std::mutex> lock(threads_mutex);
            auto all_workers_stopped = [this] { return worker_threads.empty(); };
            bool terminated = false;
            if (timeout.count() < 0) {
                termination.wait(lock, all_workers_stopped);
                terminated = true;
            }
            else {
                terminated = termination.wait_for(lock, timeout, all_workers_stopped);
            }
            if (terminated) {
                join_all_workers_locked();
            }
            return terminated;
        }

    private:
        int pool_size() const {
            std::lock_guard<std::mutex> lock(threads_mutex);
            return static_cast<int>(worker_threads.size());
        }

        void wake_idle_workers() {
            std::lock_guard<std::mutex> lock(threads_mutex);
            const int work_thread_count = static_cast<int>(worker_threads.size());
            for (int i = 0; i < work_thread_count; ++i) {
                work_queue->try_push(callable{});
            }
            work_queue->wake_all();
        }

        void remove_current_worker_locked() {
            const auto id = std::this_thread::get_id();
            for (auto it = worker_threads.begin(); it != worker_threads.end(); ++it) {
                if (it->get_id() == id) {
                    if (it->joinable()) {
                        it->detach();
                    }
                    worker_threads.erase(it);
                    break;
                }
            }
        }

        bool try_start_worker(callable &task) {
            const state s = pool_state.load();
            if (s != state::running) {
                return false;
            }
            if (task && s == state::shutdown) {
                return false;
            }
            try {
                std::lock_guard<std::mutex> lock(threads_mutex);
                if (static_cast<int>(worker_threads.size()) >= max_pool_size) {
                    return false;
                }
                worker_threads.emplace_back([this, t = std::move(task)]() mutable {
                    worker_loop(std::move(t));
                    std::lock_guard<std::mutex> worker_lock(threads_mutex);
                    remove_current_worker_locked();
                    termination.notify_all();
                });
                return true;
            }
            catch (...) {
                // If thread creation fails (e.g. resource limit), the task has
                // already been moved into the lambda capture and is lost. This
                // is an extreme out-of-memory situation.
                return false;
            }
        }

        // Mirrors ThreadPoolExecutor.execute() submission path.
        void submit_task(callable task) {
            state s = pool_state.load();
            if (s != state::running) {
                reject(task, "thread pool is not running");
                return;
            }

            bool discard_oldest_attempted = false;
            for (;;) {
                const int current_size = pool_size();

                if (current_size < core_pool_size && try_start_worker(task)) {
                    return;
                }

                if (work_queue->try_push(std::move(task))) {
                    if (pool_size() == 0 && pool_state.load() == state::running) {
                        callable empty_task{};
                        try_start_worker(empty_task);
                    }
                    return;
                }

                if (current_size < max_pool_size && try_start_worker(task)) {
                    return;
                }

                if (rej_policy == reject_policy::discard_oldest && !discard_oldest_attempted
                    && pool_state.load() == state::running) {
                    callable oldest{};
                    if (work_queue->try_pop(oldest)) {
                        discard_oldest_attempted = true;
                        continue;
                    }
                }

                reject(task, "queue is full and max threads reached");
                return;
            }
        }

        void join_all_workers_locked() {
            for (auto &t: worker_threads) {
                if (t.joinable()) {
                    t.join();
                }
            }
            worker_threads.clear();
        }

        void worker_loop(callable task) {
            for (;;) {
                if (task) {
                    try {
                        task();
                    }
                    catch (...) {
                        // Swallow user exceptions to prevent std::terminate.
                    }
                }

                const state s = pool_state.load();
                if (s == state::stop) {
                    break;
                }

                task = callable{};

                if (s == state::shutdown) {
                    if (!fetch_task(task, fetch_mode::non_blocking)) {
                        break;
                    }
                    continue;
                }

                if (pool_size() > core_pool_size) {
                    const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(keep_alive_time);
                    if (fetch_task(task, fetch_mode::timed, timeout)) {
                        continue;
                    }
                    if (pool_size() <= core_pool_size) {
                        continue;
                    }
                    break;
                }

                if (!fetch_task(task, fetch_mode::blocking)) {
                    break;
                }
            }
        }

        enum class fetch_mode { non_blocking, blocking, timed };

        bool fetch_task(callable &task, fetch_mode mode,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
            bool got = false;
            switch (mode) {
                case fetch_mode::non_blocking:
                    got = work_queue->try_pop(task);
                    break;
                case fetch_mode::blocking:
                    task = work_queue->pop();
                    got = true;
                    break;
                case fetch_mode::timed:
                    got = work_queue->pop_with_timeout(task, timeout);
                    break;
            }
            return got && static_cast<bool>(task);
        }

        void reject(callable &task, const std::string &reason) {
            switch (rej_policy) {
                case reject_policy::abort:
                    throw rejected_execution_exception("Task rejected: " + reason);
                case reject_policy::caller_runs:
                    if (pool_state.load() == state::running && task) {
                        task();
                    }
                    break;
                case reject_policy::discard:
                case reject_policy::discard_oldest:
                    break;
            }
        }

        int core_pool_size;
        int max_pool_size;
        std::chrono::seconds keep_alive_time;
        std::vector<std::thread> worker_threads;
        std::unique_ptr<task_queue<callable>> work_queue;
        reject_policy rej_policy;

        std::atomic<state> pool_state{state::running};
        mutable std::mutex threads_mutex;
        std::condition_variable termination;
    };

} // namespace tp
