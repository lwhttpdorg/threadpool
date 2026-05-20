#pragma once

#include <functional> // for std::function
#include <limits>     // for std::numeric_limits

namespace tp {

    /**
     * @class callable
     * @brief A callable wrapper that associates a void() function with a priority value.
     *
     * Used as the task unit within the thread pool. Higher priority values indicate
     * higher scheduling priority when used with a priority_task_queue.
     */
    class callable final {
    public:
        /// @brief Default priority (lowest possible value).
        static constexpr unsigned int DEFAULT_PRIORITY = std::numeric_limits<unsigned int>::min();

        /// @brief Constructs an empty callable with default priority.
        callable() noexcept : func(nullptr), priority(DEFAULT_PRIORITY) {
        }

        /// @brief Constructs an empty callable with the given priority.
        /// @param _priority The priority value.
        explicit callable(unsigned int _priority) noexcept : func(nullptr), priority(_priority) {
        }

        /// @brief Constructs a callable with the given function and default priority.
        /// @param _func The function to wrap.
        explicit callable(std::function<void(void)> _func) : func(std::move(_func)), priority(DEFAULT_PRIORITY) {
        }

        /// @brief Constructs a callable with the given function and priority.
        /// @param _func The function to wrap.
        /// @param _priority The priority value.
        callable(std::function<void(void)> _func, unsigned int _priority) :
            func(std::move(_func)), priority(_priority) {
        }

        callable(const callable &) = default;
        callable &operator=(const callable &) = default;
        callable(callable &&) noexcept = default;
        callable &operator=(callable &&) noexcept = default;

        /// @brief Invokes the wrapped function. No-op if empty.
        void operator()() const {
            if (func) {
                func();
            }
        }

        /**
         * @brief Three-way comparison by priority.
         * @param other The callable to compare against.
         * @return -1 if this < other, 1 if this > other, 0 if equal.
         */
        int compare(const callable &other) const noexcept {
            if (priority < other.priority) {
                return -1;
            }
            if (priority > other.priority) {
                return 1;
            }
            return 0;
        }

    protected:
        std::function<void(void)> func; ///< The wrapped function.
        unsigned int priority = 0;      ///< The scheduling priority.
    };

    /**
     * @struct callable_priority_less
     * @brief Comparator for callable objects, ordering by priority (less-than).
     */
    struct callable_priority_less {
        bool operator()(const callable &lhs, const callable &rhs) const noexcept {
            return lhs.compare(rhs) < 0;
        }
    };
} // namespace tp
