#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace tp {
    // Abstract interface for executable tasks, similar to Java's Runnable
    class runnable {
    public:
        static constexpr unsigned int LOWEST_PRIORITY = 0;

        explicit runnable(unsigned int priority = LOWEST_PRIORITY) : _priority(priority) {
        }
        virtual ~runnable() = default;
        virtual void run() = 0;

        unsigned int priority() const {
            return _priority;
        }

    protected:
        unsigned int _priority;
    };

    using work_task = std::unique_ptr<runnable>;

    // Adapter that wraps any callable (lambda, function object, etc.) into a runnable
    template<typename F>
    class lambda_runnable: public runnable {
    public:
        lambda_runnable(F f, unsigned int priority = LOWEST_PRIORITY) : runnable(priority), func_(std::move(f)) {
        }
        void run() override {
            func_();
        }

    private:
        F func_;
    };

    // Factory function: creates a runnable from any callable object
    template<typename F>
    work_task make_runnable(F &&f) {
        return std::make_unique<lambda_runnable<std::decay_t<F>>>(std::forward<F>(f));
    }

    // Factory function: creates a runnable with explicit priority from any callable object
    template<typename F>
    work_task make_runnable(unsigned int priority, F &&f) {
        return std::make_unique<lambda_runnable<std::decay_t<F>>>(std::forward<F>(f), priority);
    }
} // namespace tp
