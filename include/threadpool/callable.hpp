#pragma once

#include <functional>
#include <limits>
#include <type_traits>

namespace tp {
    class callable {
    public:
        static constexpr unsigned int LOWEST_PRIORITY = std::numeric_limits<unsigned int>::min();
        static constexpr unsigned int HIGHEST_PRIORITY = std::numeric_limits<unsigned int>::max();

        callable() : func(nullptr), priority(HIGHEST_PRIORITY) {
        }

        explicit callable(std::function<void(void)> _func) : func(std::move(_func)), priority(LOWEST_PRIORITY) {
        }

        callable(std::function<void(void)> _func, unsigned int _priority) :
            func(std::move(_func)), priority(_priority) {
        }

        void operator()() const {
            if (func) {
                func();
            }
        }

        int compare(const callable &other) const {
            return static_cast<int>(this->priority) - static_cast<int>(other.priority);
        }

        explicit operator bool() const {
            return func != nullptr;
        }

    protected:
        std::function<void(void)> func;
        unsigned int priority = 0;
    };

    struct callable_priority_compare {
        bool operator()(const callable &lhs, const callable &rhs) const {
            return lhs.compare(rhs) < 0;
        }
    };
} // namespace tp
