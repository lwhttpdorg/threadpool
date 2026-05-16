#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "threadpool/runnable.hpp"
#include "threadpool/task_queue.hpp"

namespace tp {
    // Compare tasks by runnable::priority() (max-heap: higher priority first)
    struct work_task_priority_compare {
        bool operator()(const work_task &lhs, const work_task &rhs) const {
            if (!lhs && !rhs) {
                return false;
            }
            if (!lhs) {
                return true;
            }
            if (!rhs) {
                return false;
            }
            return lhs->priority() < rhs->priority();
        }
    };

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
            caller_runs,   // execute task in caller thread
            discard,       // silently discard task
            discard_oldest // discard oldest task and retry
        };

        thread_pool(int core_pool_size, int maximum_pool_size, std::chrono::seconds keep_alive_time,
                    std::unique_ptr<task_queue<work_task>> work_queue, reject_policy policy = reject_policy::abort) :
            _core_pool_size(core_pool_size), _maximum_pool_size(maximum_pool_size),
            _keep_alive_time(std::chrono::duration_cast<std::chrono::seconds>(keep_alive_time)),
            _work_queue(std::move(work_queue)), _reject_policy(policy) {
            if (_core_pool_size < 0 || _maximum_pool_size <= 0 || _maximum_pool_size < _core_pool_size) {
                throw std::invalid_argument("Invalid pool size parameters");
            }
        }

        thread_pool(int core_pool_size, int maximum_pool_size, std::chrono::minutes keep_alive_time,
                    std::unique_ptr<task_queue<work_task>> work_queue, reject_policy policy = reject_policy::abort) :
            _core_pool_size(core_pool_size), _maximum_pool_size(maximum_pool_size),
            _keep_alive_time(std::chrono::duration_cast<std::chrono::seconds>(keep_alive_time)),
            _work_queue(std::move(work_queue)), _reject_policy(policy) {
            if (_core_pool_size < 0 || _maximum_pool_size <= 0 || _maximum_pool_size < _core_pool_size) {
                throw std::invalid_argument("Invalid pool size parameters");
            }
        }

        ~thread_pool() {
            shutdown();
            await_termination(std::chrono::seconds(-1));
        }

        // Submit any callable (lambda, function object, std::bind, etc.) for execution
        template<class F, class... Args>
        void execute(F &&f, Args &&...args) {
            auto task = make_runnable(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
            execute_internal(std::move(task));
        }

        // Submit a callable with explicit priority (higher value = higher priority)
        template<class F, class... Args>
        void execute_with_priority(unsigned int priority, F &&f, Args &&...args) {
            auto task = make_runnable(priority, std::bind(std::forward<F>(f), std::forward<Args>(args)...));
            execute_internal(std::move(task));
        }

        // Initiates an orderly shutdown: no new tasks accepted, but queued tasks are executed
        void shutdown() {
            _state.store(state::shutdown);
            int wc = _worker_count.load();
            for (int i = 0; i < wc; ++i) {
                _work_queue->try_push(nullptr);
            }
            _work_queue->wake_all();
        }

        // Attempts to stop all actively executing tasks and returns a list of tasks not executed
        std::vector<work_task> shutdown_now() {
            _state.store(state::stop);
            int wc = _worker_count.load();
            for (int i = 0; i < wc; ++i) {
                _work_queue->try_push(nullptr);
            }
            _work_queue->wake_all();

            std::vector<work_task> remaining;
            work_task task;
            while (_work_queue->try_pop(task)) {
                if (task) {
                    remaining.push_back(std::move(task));
                }
            }
            return remaining;
        }

        bool is_shutdown() const {
            return _state.load() != state::running;
        }

        bool is_terminated() const {
            return _state.load() != state::running && _worker_count.load() == 0;
        }

        bool await_termination(std::chrono::seconds timeout) {
            std::unique_lock<std::mutex> lock(_main_lock);
            if (timeout.count() < 0) {
                _termination.wait(lock, [this] { return _worker_count.load() == 0; });
                return true;
            }
            return _termination.wait_for(lock, timeout, [this] { return _worker_count.load() == 0; });
        }

        int active_count() const {
            return _worker_count.load();
        }

        size_t queue_size() const {
            return _work_queue->size();
        }

    private:
        void execute_internal(work_task task) {
            if (_state.load() != state::running) {
                reject(task, "thread pool is not running");
                return;
            }

            int c = _worker_count.load();
            if (c < _core_pool_size) {
                task = add_worker(std::move(task));
                if (!task) {
                    return;
                }
            }

            if (_work_queue->try_push(std::move(task))) {
                int recheck = _worker_count.load();
                if (_state.load() != state::running && _work_queue->try_pop(task)) {
                    reject(task, "thread pool is not running");
                    return;
                }
                if (recheck == 0) {
                    add_worker(nullptr);
                }
                return;
            }

            c = _worker_count.load();
            if (c < _maximum_pool_size) {
                task = add_worker(std::move(task));
                if (!task) {
                    return;
                }
            }

            reject(task, "queue is full and max threads reached");
        }

        work_task add_worker(work_task first_task) {
            int wc = _worker_count.load();
            while (wc < _maximum_pool_size) {
                if (_state.load() != state::running && (first_task || _state.load() != state::shutdown)) {
                    return first_task;
                }
                if (_worker_count.compare_exchange_weak(wc, wc + 1)) {
                    break;
                }
            }

            if (wc >= _maximum_pool_size) {
                return first_task;
            }

            try {
                std::thread t(
                    [this, first_task = std::move(first_task)]() mutable { run_worker(std::move(first_task)); });
                t.detach();
                return nullptr;
            }
            catch (...) {
                _worker_count--;
                return first_task;
            }
        }

        void run_worker(work_task first_task) {
            work_task task = std::move(first_task);

            while (task || (task = get_task())) {
                if (!task) {
                    break;
                }
                task->run();
                task.reset();
            }

            _worker_count--;
            _termination.notify_all();
        }

        work_task get_task() {
            while (true) {
                if (_state.load() == state::stop) {
                    return nullptr;
                }

                if (_state.load() == state::shutdown) {
                    work_task task;
                    if (_work_queue->try_pop(task)) {
                        return task;
                    }
                    return nullptr;
                }

                bool timed = _worker_count.load() > _core_pool_size;
                if (timed) {
                    work_task task;
                    if (_work_queue->pop_with_timeout(task, _keep_alive_time)) {
                        return task;
                    }
                    // Race condition fix: if other threads exited and worker_count dropped to core_pool_size or below,
                    // this thread should stay alive as a core thread instead of exiting.
                    if (_worker_count.load() <= _core_pool_size) {
                        continue;
                    }
                    return nullptr;
                }

                return _work_queue->pop();
            }
        }

        void reject(work_task &task, const std::string &reason) {
            switch (_reject_policy) {
                case reject_policy::abort:
                    throw rejected_execution_exception("Task rejected: " + reason);
                case reject_policy::caller_runs:
                    if (task) {
                        task->run();
                    }
                    break;
                case reject_policy::discard:
                    break;
                case reject_policy::discard_oldest: {
                    work_task oldest_task;
                    if (_work_queue->try_pop(oldest_task)) {
                        execute_internal(std::move(task));
                        return;
                    }
                    else {
                        if (task) {
                            task->run();
                        }
                    }
                    break;
                }
            }
        }

        int _core_pool_size;
        int _maximum_pool_size;
        std::chrono::seconds _keep_alive_time;
        std::unique_ptr<task_queue<work_task>> _work_queue;
        reject_policy _reject_policy;

        std::atomic<state> _state{state::running};
        std::atomic<int> _worker_count{0};
        std::mutex _main_lock;
        std::condition_variable _termination;
    };
} // namespace tp
