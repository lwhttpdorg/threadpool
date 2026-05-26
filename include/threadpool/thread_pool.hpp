#pragma once

#include <atomic>             // for std::atomic
#include <chrono>             // for std::chrono::seconds, milliseconds
#include <condition_variable> // for std::condition_variable
#include <memory>             // for std::unique_ptr
#include <mutex>              // for std::mutex, std::scoped_lock, std::unique_lock
#include <stdexcept>          // for std::runtime_error, std::invalid_argument
#include <string>             // for std::string
#include <thread>             // for std::jthread, std::thread::id
#include <tuple>              // for std::tuple, std::make_tuple, std::apply
#include <type_traits>        // for std::enable_if_t, std::is_integral_v, std::decay_t
#include <unordered_map>      // for std::unordered_map
#include <utility>            // for std::move, std::forward
#include <vector>             // for std::vector

#include "threadpool/blocking_queue.hpp"
#include "threadpool/callable.hpp"

namespace tp {
    constexpr const char *THREAD_POOL_NOT_RUN = "thread pool is not running";
    constexpr const char *THREAD_POOL_FULL = "queue is full and max threads reached";

    using fifo_task_queue = array_blocking_queue<callable>;
    using priority_task_queue = priority_blocking_queue<callable>;

    /**
     * @class thread_pool
     * @brief A dynamic thread pool with core/non-core workers and configurable rejection policies.
     *
     * Core threads are created eagerly at construction and remain alive until shutdown.
     * Non-core threads are spawned on demand when the queue is full and are reclaimed
     * after being idle for the configured keep-alive duration.
     *
     * Lifecycle states: running → shutdown (graceful) or stop (immediate).
     */
    class thread_pool {
    public:
        /**
         * @class rejected_execution_exception
         * @brief Thrown when a task cannot be accepted by the pool.
         */
        class rejected_execution_exception: public std::runtime_error {
        public:
            explicit rejected_execution_exception(const char *msg) : std::runtime_error(msg) {
            }
            explicit rejected_execution_exception(const std::string &msg) : std::runtime_error(msg) {
            }
        };

        /**
         * @enum pool_fsm_state
         * @brief The finite state machine states of the thread pool.
         */
        enum class pool_fsm_state : uint8_t {
            running,  ///< Accepting and executing tasks.
            shutdown, ///< No new tasks accepted; queued tasks will be drained.
            stop      ///< No new tasks accepted; in-progress tasks finish but queued tasks are discarded.
        };

        /**
         * @enum reject_policy
         * @brief Policy applied when a task cannot be enqueued and no new worker can be created.
         */
        enum class reject_policy : uint8_t {
            abort,         ///< Throw rejected_execution_exception.
            caller_runs,   ///< Execute the task in the calling thread.
            discard,       ///< Silently drop the task.
            discard_oldest ///< Remove the oldest queued task and retry enqueue.
        };

        /**
         * @brief Constructs a thread pool.
         * @param _core_pool_size Number of core worker threads (created eagerly).
         * @param _max_pool_size Maximum total worker threads (core + non-core).
         * @param _keep_alive_time Idle timeout for non-core workers before they exit.
         * @param _work_queue The task queue implementation to use.
         * @param _rej_policy Rejection policy when the pool is saturated.
         * @throws std::invalid_argument if core_pool_size > max_pool_size.
         */
        thread_pool(const unsigned int _core_pool_size, const unsigned int _max_pool_size,
                    const std::chrono::seconds _keep_alive_time, std::unique_ptr<blocking_queue<callable>> _work_queue,
                    const reject_policy _rej_policy = reject_policy::abort) :
            core_pool_size(_core_pool_size), max_pool_size(_max_pool_size), keep_alive_time(_keep_alive_time),
            work_queue(std::move(_work_queue)), rej_policy(_rej_policy) {
            if (_core_pool_size > _max_pool_size) {
                throw std::invalid_argument("core pool size must be less than or equal to max pool size");
            }
            watcher_thread = std::jthread([this] { watcher_thread_loop(); });
            start_core_threads();
        }

        /// @overload
        thread_pool(const unsigned int _core_pool_size, const unsigned int _max_pool_size,
                    const std::chrono::minutes _keep_alive_time, std::unique_ptr<blocking_queue<callable>> _work_queue,
                    const reject_policy _rej_policy = reject_policy::abort) :
            core_pool_size(_core_pool_size), max_pool_size(_max_pool_size),
            keep_alive_time(std::chrono::duration_cast<std::chrono::seconds>(_keep_alive_time)),
            work_queue(std::move(_work_queue)), rej_policy(_rej_policy) {
            if (_core_pool_size > _max_pool_size) {
                throw std::invalid_argument("core pool size must be less than or equal to max pool size");
            }
            watcher_thread = std::jthread([this] { watcher_thread_loop(); });
            start_core_threads();
        }

        /**
         * @brief Destructor. Initiates shutdown if still running, then joins all threads.
         */
        ~thread_pool() {
            const pool_fsm_state st = pool_state.load();
            if (pool_fsm_state::running == st) {
                shutdown();
            }
            join_all_threads();
            watcher_thread.join();
        }

        thread_pool(const thread_pool &) = delete;
        thread_pool &operator=(const thread_pool &) = delete;
        thread_pool(thread_pool &&) = delete;
        thread_pool &operator=(thread_pool &&) = delete;

        /**
         * @brief Submits a pre-built callable task for execution.
         * @param task The callable to execute.
         */
        void execute(callable task) {
            submit_task(std::move(task));
        }

        /**
         * @brief Submits a function with arguments for execution at default priority.
         * @tparam F Callable type.
         * @tparam Args Argument types.
         * @param f The function to execute.
         * @param args Arguments to forward to f.
         */
        template<class F, class... Args>
        void execute(F &&f, Args &&...args)
            requires(!(std::is_integral_v<std::decay_t<F>> && sizeof...(Args) >= 1))
        {
            auto fn = [f = std::forward<F>(f), tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                std::apply(std::move(f), std::move(tup));
            };
            submit_task(callable(std::move(fn)));
        }

        /**
         * @brief Submits a function with arguments for execution at the specified priority.
         * @tparam F Callable type.
         * @tparam Args Argument types.
         * @param priority The task priority (higher = scheduled sooner in priority queues).
         * @param f The function to execute.
         * @param args Arguments to forward to f.
         */
        template<class F, class... Args>
        void execute(const unsigned int priority, F &&f, Args &&...args) {
            auto fn = [f = std::forward<F>(f), tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                std::apply(std::move(f), std::move(tup));
            };
            submit_task(callable(std::move(fn), priority));
        }

        /**
         * @brief Initiates a graceful shutdown.
         *
         * No new tasks are accepted. Already-queued tasks will be executed to completion.
         */
        void shutdown() {
            pool_fsm_state expected = pool_fsm_state::running;
            if (pool_state.compare_exchange_strong(expected, pool_fsm_state::shutdown)) {
                work_queue->wake_all();
            }
            // Wake watcher thread in case there are no non-core threads to trigger its exit
            join_cv.notify_one();
        }

        /**
         * @brief Initiates an immediate shutdown.
         *
         * No new tasks are accepted. Queued tasks are drained and returned.
         * In-progress tasks are allowed to finish but no new tasks are started.
         *
         * @return A vector of tasks that were still queued and not yet executed.
         */
        std::vector<callable> shutdown_now() {
            std::vector<callable> remaining;

            pool_fsm_state expected = pool_fsm_state::running;
            if (pool_state.compare_exchange_strong(expected, pool_fsm_state::stop)) {
                callable task{};
                while (work_queue->try_pop(task)) {
                    remaining.push_back(std::move(task));
                }
            }
            // Wake watcher thread in case there are no non-core threads to trigger its exit
            join_cv.notify_one();
            work_queue->wake_all();
            return remaining;
        }

        /**
         * @brief Blocks until all workers have terminated or the timeout expires.
         *
         * If timeout is zero, waits indefinitely.
         *
         * @param timeout Maximum duration to wait. Zero means wait forever.
         * @return true if all workers terminated within the timeout, false otherwise.
         */
        bool await_termination(const std::chrono::seconds timeout) {
            bool terminated = false;

            {
                std::unique_lock lock(non_core_thread_mutex);
                // Wait for all non-core threads to exit
                auto all_non_core_thread_stopped = [this] { return non_core_threads.empty(); };

                if (0 == timeout.count()) {
                    termination_cv.wait(lock, all_non_core_thread_stopped);
                    terminated = true;
                }
                else {
                    terminated = termination_cv.wait_for(lock, timeout, all_non_core_thread_stopped);
                }
            }
            // If not terminated, skip join — tasks may be stuck; destructor will force cleanup
            if (terminated) {
                join_all_threads();
            }

            return terminated;
        }

    private:
        /**
         * @brief Background watcher thread that joins zombie non-core threads.
         *
         * Waits for non-core threads to signal completion, joins them, and removes
         * them from the active map. Exits when the pool is no longer running and
         * all non-core threads have been reclaimed.
         */
        void watcher_thread_loop() {
            while (true) {
                std::vector<std::thread::id> local_zombie_tids;
                {
                    std::unique_lock join_lock(join_mutex);
                    join_cv.wait(join_lock, [this] {
                        return !zombie_non_core_threads.empty() || pool_fsm_state::running != pool_state.load();
                    });
                    local_zombie_tids.swap(zombie_non_core_threads);
                }

                {
                    std::unique_lock map_lock(non_core_thread_mutex);
                    for (auto &tid: local_zombie_tids) {
                        auto it = non_core_threads.find(tid);
                        if (it != non_core_threads.end()) {
                            it->second.join();
                            non_core_threads.erase(it);
                        }
                    }
                    if (non_core_threads.empty() && pool_fsm_state::running != pool_state.load()) {
                        // Notify await_termination
                        termination_cv.notify_one();
                        break;
                    }
                }
            }
        }

        /// @brief Eagerly creates all core worker threads at construction time.
        void start_core_threads() {
            core_threads.reserve(core_pool_size);
            for (unsigned int i = 0; i < core_pool_size; ++i) {
                core_threads.emplace_back([this] { core_worker_loop(); });
            }
        }

        /**
         * @brief Attempts to start a non-core worker with the given initial task.
         * @param initial_task The task to execute immediately (moved out on success).
         * @return true if a new non-core worker was started, false if max_pool_size reached.
         */
        bool start_non_core_worker(callable &initial_task) {
            std::scoped_lock lock(non_core_thread_mutex);
            if (core_pool_size + non_core_threads.size() >= max_pool_size) {
                return false;
            }
            std::jthread work_thread(
                [this, task = std::move(initial_task)]() mutable { non_core_worker_loop(std::move(task)); });
            const std::thread::id tid = work_thread.get_id();
            non_core_threads[tid] = std::move(work_thread);
            return true;
        }

        /**
         * @brief Internal task submission logic.
         *
         * Attempts to enqueue the task, spawn a non-core worker, or apply the
         * rejection policy in that order.
         *
         * @param task The task to submit (moved).
         * @throws rejected_execution_exception if the pool is not running (always)
         *         or if the abort policy is active and the pool is saturated.
         */
        void submit_task(callable &&task) {
            if (pool_fsm_state::running != pool_state.load()) {
                throw rejected_execution_exception(THREAD_POOL_NOT_RUN);
            }

            // Step 1: try to enqueue
            if (work_queue->try_push(std::move(task))) {
                return;
            }

            // Step 2: queue full — try to start a non-core worker
            if (start_non_core_worker(task)) {
                return;
            }

            // Step 3: apply rejection policy
            switch (rej_policy) {
                case reject_policy::abort:
                    throw rejected_execution_exception(THREAD_POOL_FULL);
                case reject_policy::caller_runs:
                    if (pool_fsm_state::running == pool_state.load()) {
                        task();
                    }
                    break;
                case reject_policy::discard_oldest:
                    if (pool_fsm_state::running == pool_state.load()) {
                        callable oldest_task{};
                        if (work_queue->try_pop(oldest_task)) {
                            // Retry enqueue once after discarding oldest
                            work_queue->try_push(std::move(task));
                        }
                    }
                    break;
                case reject_policy::discard:
                    break;
            }
        }

        /// @brief Joins all core and remaining non-core threads, then notifies termination.
        void join_all_threads() {
            while (!core_threads.empty()) {
                auto &tail_th = core_threads.back();
                if (tail_th.joinable()) {
                    tail_th.join();
                }
                core_threads.pop_back();
            }
            {
                std::unique_lock lock(non_core_thread_mutex);
                auto it = non_core_threads.begin();
                while (it != non_core_threads.end()) {
                    if (it->second.joinable()) {
                        it->second.join();
                        it = non_core_threads.erase(it);
                    }
                    else {
                        it = non_core_threads.erase(it);
                    }
                }
            }
            // Notify await_termination()
            termination_cv.notify_one();
        }

        /**
         * @brief Core worker loop. Polls the queue and executes tasks.
         *
         * Exits when shutdown_now (stop) is called unconditionally, or when
         * shutdown is called and the queue is empty.
         */
        void core_worker_loop() const {
            while (true) {
                // shutdown_now: exit unconditionally
                if (pool_fsm_state::stop == pool_state.load()) {
                    break;
                }
                callable task{};
                if (!work_queue->timed_pop(task, std::chrono::milliseconds(1000))) {
                    // Already in shutdown state and queue is empty, core thread exits
                    if (pool_fsm_state::shutdown == pool_state.load() && work_queue->empty()) {
                        break;
                    }
                    continue;
                }
                // If state changed to stop after popping a task, discard it and exit
                if (pool_fsm_state::stop == pool_state.load()) {
                    break;
                }

                try {
                    task();
                }
                // NOLINTNEXTLINE
                catch (...) {
                    // Swallow user exceptions to prevent std::terminate.
                }
            }
        }

        /**
         * @brief Non-core worker loop. Executes an initial task, then polls with keep-alive timeout.
         *
         * After the initial task completes, the worker polls the queue with the configured
         * keep-alive timeout. If no task arrives within that period, the worker exits.
         * On pool shutdown/stop, the worker exits after completing its current task.
         *
         * @param initial_task The first task to execute (passed from submit_task).
         */
        void non_core_worker_loop(const callable &initial_task) {
            // Execute the initial task first
            try {
                initial_task();
            }
            // NOLINTNEXTLINE
            catch (...) {
                // Swallow user exceptions to prevent std::terminate.
            }
            // After initial task, enter keep-alive polling phase
            while (pool_fsm_state::running == pool_state.load()) {
                // Timed wait — exit if idle too long
                callable task = {};
                const auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(keep_alive_time);
                if (!work_queue->timed_pop(task, timeout)) {
                    break; // idle timeout, non-core thread exits
                }
                // If state changed to stop after popping a task, discard it and exit
                if (pool_fsm_state::stop == pool_state.load()) {
                    break;
                }

                try {
                    task();
                }
                // NOLINTNEXTLINE
                catch (...) {
                    // Swallow user exceptions to prevent std::terminate.
                }
            }
            // Notify watcher thread to reclaim this non-core thread before exiting
            {
                const std::thread::id tid = std::this_thread::get_id();
                std::scoped_lock join_lock(join_mutex);
                zombie_non_core_threads.push_back(tid);
            }
            join_cv.notify_one();
        }

        unsigned int core_pool_size;          ///< Number of core worker threads.
        unsigned int max_pool_size;           ///< Maximum total worker threads.
        std::chrono::seconds keep_alive_time; ///< Idle timeout for non-core workers.

        std::vector<std::jthread> core_threads;                             ///< Core worker threads (created eagerly).
        std::unordered_map<std::thread::id, std::jthread> non_core_threads; ///< Active non-core worker threads.
        std::jthread watcher_thread; ///< Background thread that joins zombie non-core threads.

        std::unique_ptr<blocking_queue<callable>> work_queue; ///< The task queue backing this pool.
        reject_policy rej_policy;                             ///< The active rejection policy.

        std::atomic<pool_fsm_state> pool_state{pool_fsm_state::running}; ///< Current pool lifecycle state.
        mutable std::mutex non_core_thread_mutex;                        ///< Guards non_core_threads map.
        mutable std::mutex join_mutex;                                   ///< Guards zombie_non_core_threads list.
        std::condition_variable termination_cv;               ///< Signaled when all non-core threads are reclaimed.
        std::condition_variable join_cv;                      ///< Signaled when a non-core thread becomes a zombie.
        std::vector<std::thread::id> zombie_non_core_threads; ///< IDs of non-core threads awaiting join.
    };
} // namespace tp
