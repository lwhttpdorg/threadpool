#pragma once

#include <functional>
#include <limits>
#include <type_traits>

namespace tp {
    class callable {
    public:
        static constexpr unsigned int LOWEST_PRIORITY = std::numeric_limits<unsigned int>::min();
        static constexpr unsigned int HIGHEST_PRIORITY = std::numeric_limits<unsigned int>::max();

        // Constructs a poison pill (empty callable) with the given priority.
        // HIGHEST_PRIORITY for shutdown_now (immediate exit),
        // LOWEST_PRIORITY for graceful shutdown (real tasks consumed first).
        explicit callable(unsigned int _priority) noexcept : func(nullptr), priority(_priority) {
        }

        explicit callable(std::function<void(void)> _func) : func(std::move(_func)), priority(LOWEST_PRIORITY) {
        }

        callable(std::function<void(void)> _func, unsigned int _priority) :
            func(std::move(_func)), priority(_priority) {
        }

        callable(const callable &) = default;
        callable &operator=(const callable &) = default;
        callable(callable &&) noexcept = default;
        callable &operator=(callable &&) noexcept = default;

        void operator()() const {
            if (func) {
                func();
            }
        }

        bool is_poison_pill() const noexcept {
            return func == nullptr;
        }

        int compare(const callable &other) const noexcept {
            if (priority < other.priority) {
                return -1;
            }
            if (priority > other.priority) {
                return 1;
            }
            return 0;
        }

        explicit operator bool() const noexcept {
            return !is_poison_pill();
        }

    protected:
        std::function<void(void)> func;
        unsigned int priority = 0;
    };

    struct callable_priority_compare {
        bool operator()(const callable &lhs, const callable &rhs) const noexcept {
            return lhs.compare(rhs) < 0;
        }
    };
} // namespace tp
