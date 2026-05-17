# thread_pool Design Document

<!-- TOC -->
- [1. Overview](#1.-overview)
- [2. Core Classes](#2.-core-classes)
  - [2.1. `callable` — Task Wrapper](#2.1.-%60callable%60-%E2%80%94-task-wrapper)
  - [2.2. `task_queue<T>` — Queue Interface](#2.2.-%60task_queue%3Ct%3E%60-%E2%80%94-queue-interface)
  - [2.3. `fifo_task_queue<T>` — FIFO Implementation](#2.3.-%60fifo_task_queue%3Ct%3E%60-%E2%80%94-fifo-implementation)
  - [2.4. `priority_task_queue<T, Compare>` — Priority Implementation](#2.4.-%60priority_task_queue%3Ct%2C-compare%3E%60-%E2%80%94-priority-implementation)
  - [2.5. `reject_policy` — Rejection Policy Enum](#2.5.-%60reject_policy%60-%E2%80%94-rejection-policy-enum)
  - [2.6. `state` — Lifecycle State Enum](#2.6.-%60state%60-%E2%80%94-lifecycle-state-enum)
  - [2.7. `thread_pool` — Thread Pool](#2.7.-%60thread_pool%60-%E2%80%94-thread-pool)
- [3. Class Diagram](#3.-class-diagram)
- [4. Task Queue Design](#4.-task-queue-design)
  - [4.1. Poison Pill Shutdown](#4.1.-poison-pill-shutdown)
- [5. Thread Pool Lifecycle](#5.-thread-pool-lifecycle)
- [6. Exception Handling](#6.-exception-handling)
- [7. Rejection Policies](#7.-rejection-policies)
<!-- /TOC -->

## 1. Overview

This project implements a C++ thread pool modeled after Java's `ThreadPoolExecutor`. The design emphasizes:

- **Interface-based architecture**: `task_queue` is an abstract interface, allowing pluggable implementations
- **Move semantics**: tasks (`callable`) are moved through the pipeline, avoiding unnecessary copies of `std::function`
- **Priority support**: `callable` carries an unsigned priority; `execute()` submits tasks with explicit priority for priority-queue ordering
- **Core vs. non-core threads**: core threads persist indefinitely; non-core threads are culled after idle timeout
- **Exception safety**: worker threads catch all exceptions thrown by user tasks, preventing `std::terminate`

## 2. Core Classes

### 2.1. `callable` — Task Wrapper

```cpp
class callable {
public:
    static constexpr unsigned int LOWEST_PRIORITY = std::numeric_limits<unsigned int>::min();
    static constexpr unsigned int HIGHEST_PRIORITY = std::numeric_limits<unsigned int>::max();

    callable();
    explicit callable(std::function<void(void)> _func);
    callable(std::function<void(void)> _func, unsigned int _priority);

    void operator()() const;
    explicit operator bool() const;
    int compare(const callable &other) const;
};
```

- Lightweight wrapper around `std::function<void()>`
- Supports an optional `unsigned int` priority for priority-queue ordering
- Empty `callable` (default-constructed) evaluates to `false` in boolean context

### 2.2. `task_queue<T>` — Queue Interface

```cpp
template<typename T>
class task_queue {
public:
    virtual ~task_queue() = default;
    virtual bool try_push(T&&) = 0;
    virtual T pop() = 0;
    virtual bool try_pop(T&) = 0;
    virtual bool pop_with_timeout(T&, std::chrono::milliseconds) = 0;
    virtual size_t size() const = 0;
    virtual void wake_all() = 0;
};
```

- Abstract blocking queue interface
- `T` is `callable` in the thread pool context

### 2.3. `fifo_task_queue<T>` — FIFO Implementation

- Backed by `std::deque<T>`
- Producers push to the back under lock and notify one consumer
- Consumers pop from the front under lock and notify one producer
- `pop()` blocks on a `condition_variable` with `!task_q.empty()` predicate

### 2.4. `priority_task_queue<T, Compare>` — Priority Implementation

- Backed by `std::vector<T>` with manual heap operations (`push_heap` / `pop_heap`)
- Same locking and notification strategy as FIFO, but with heap ordering via `Compare`

### 2.5. `reject_policy` — Rejection Policy Enum

```cpp
enum class reject_policy {
    abort,         // throw rejected_execution_exception
    caller_runs,   // execute in caller thread
    discard,       // silently drop
    discard_oldest // remove oldest queued task and retry submission once
};
```

Applied when a task cannot be accepted (queue full and max threads reached).

### 2.6. `state` — Lifecycle State Enum

```cpp
enum class state {
    running,   // accepting tasks
    shutdown,  // draining queue, no new tasks
    stop       // immediate exit
};
```

### 2.7. `thread_pool` — Thread Pool

Manages worker threads and task dispatching according to Java `ThreadPoolExecutor` semantics.

`thread_pool` is **non-copyable and non-movable**.

Constructors accept `std::chrono::seconds` or `std::chrono::minutes` for `keep_alive_time`, internally converted to seconds:

```cpp
thread_pool(int core, int max, std::chrono::seconds keep_alive,
            std::unique_ptr<task_queue<callable>> queue, reject_policy policy);

thread_pool(int core, int max, std::chrono::minutes keep_alive,
            std::unique_ptr<task_queue<callable>> queue, reject_policy policy);
```

**Validation**: `core_pool_size` must be **less than or equal to** `max_pool_size`.

Task dispatch flow:

1. If `pool_size < core_pool_size`, create a new worker thread to execute the task directly
2. Otherwise, try to enqueue the task
3. If enqueue fails (queue full) and `pool_size < max_pool_size`, create a non-core worker
4. Otherwise, apply the rejection policy

## 3. Class Diagram

```mermaid
classDiagram
    class callable {
        -std::function~void()~ func
        -unsigned int priority
        +operator()() void
        +operator bool() bool
        +compare(other) int
    }

    class callable_priority_compare {
        +operator()(lhs, rhs) bool
    }

    class task_queue~T~ {
        <<interface>>
        +try_push(T&&) bool
        +pop() T
        +try_pop(T&) bool
        +pop_with_timeout(T&, ms) bool
        +size() size_t
        +wake_all() void
    }

    class fifo_task_queue~T~ {
        -deque~T~ task_q
        -mutex q_mutex
        -condition_variable q_cv
        -size_t capacity
    }

    class priority_task_queue~T, Compare~ {
        -vector~T~ task_q
        -Compare task_compare
        -mutex q_mutex
        -condition_variable q_cv
        -size_t capacity
    }

    class thread_pool {
        +execute(callable) void
        +execute(F&&, Args&&...) void
        +execute(priority, F&&, Args&&...) void
        +shutdown() void
        +shutdown_now() vector~callable~
        +await_termination(timeout) bool
        -core_pool_size int
        -max_pool_size int
        -keep_alive_time seconds
        -worker_threads vector~thread~
        -work_queue unique_ptr~task_queue~
        -rej_policy reject_policy
        -pool_state atomic~state~
        -threads_mutex mutex
        -termination condition_variable
    }

    task_queue~T~ <|-- fifo_task_queue~T~
    task_queue~T~ <|-- priority_task_queue~T, Compare~
    thread_pool --> task_queue~callable~ : owns
    thread_pool ..> callable : executes
```

## 4. Task Queue Design

### 4.1. Poison Pill Shutdown

`thread_pool::shutdown()` pushes one empty `callable` (`callable{}`) per worker into the queue, followed by `wake_all()`. When a worker pops an empty `callable`, it treats it as a poison pill and exits. This avoids needing an interrupt mechanism like Java's `Thread.interrupt()`.

## 5. Thread Pool Lifecycle

```mermaid
stateDiagram-v2
    [*] --> RUNNING: constructor
    RUNNING --> SHUTDOWN: shutdown()
    RUNNING --> STOP: shutdown_now()
    SHUTDOWN --> TERMINATED: all workers exit
    STOP --> TERMINATED: all workers exit
```

| State | Behavior |
|-------|----------|
| `RUNNING` | Accepts new tasks; workers block on queue |
| `SHUTDOWN` | Rejects new tasks; workers drain queue then exit |
| `STOP` | Rejects new tasks; workers exit immediately |
| `TERMINATED` | All workers exited; `await_termination` returns |

**Destruction**: the destructor calls `shutdown()` and waits up to **30 seconds** for all workers to finish. If workers are still alive after the timeout, they are forcefully detached to prevent `std::terminate`.

## 6. Exception Handling

User tasks are executed inside `worker_loop`:

```cpp
if (task) {
    try {
        task();
    } catch (...) {
        // swallow exception
    }
}
```

All exceptions thrown by user code are caught and silently swallowed. This guarantees that a misbehaving task will **not** crash the entire process via `std::terminate`.

## 7. Rejection Policies

| Policy | Behavior |
|--------|----------|
| `abort` | Throws `rejected_execution_exception` |
| `caller_runs` | Runs the task synchronously in the caller thread (only if pool is `running`) |
| `discard` | Silently drops the task |
| `discard_oldest` | Discards the oldest queued task and retries submission **once**; if the retry also fails (or the queue is empty), the task is silently dropped |
