# Thread Pool

<!-- TOC -->
- [1. Features](#1.-features)
- [2. Directory Structure](#2.-directory-structure)
- [3. Build](#3.-build)
  - [3.1. CMake](#3.1.-cmake)
  - [3.2. Meson](#3.2.-meson)
- [4. Usage Example](#4.-usage-example)
  - [4.1. FIFO queue (default)](#4.1.-fifo-queue-%28default%29)
  - [4.2. Priority queue](#4.2.-priority-queue)
- [5. Interface Reference](#5.-interface-reference)
  - [5.1. task_queue<T>](#5.1.-task_queue%3Ct%3E)
  - [5.2. callable](#5.2.-callable)
  - [5.3. thread_pool](#5.3.-thread_pool)
- [6. Testing](#6.-testing)
- [7. Dependencies](#7.-dependencies)
- [8. License](#8.-license)
<!-- /TOC -->

A C++ thread pool implementation modeled after Java's `ThreadPoolExecutor`, supporting core threads, maximum threads, idle timeout, task queues, and rejection policies.

All public APIs are placed under the `tp` namespace to avoid name collisions.

## 1. Features

- **Java-like ThreadPoolExecutor**: supports `corePoolSize`, `maximumPoolSize`, `keepAliveTime`, work queue, and rejection policies
- **Callable interface**: lightweight wrapper around `std::function<void()>`, with optional priority support
- **Automatic lambda wrapping**: `execute()` accepts any callable plus arguments, automatically bound into a task
- **Exception safety**: exceptions thrown by user tasks are caught inside worker threads and will **not** cause `std::terminate`
- **Priority tasks**: `execute()` submits tasks with explicit priority for priority-queue ordering
- **Pluggable task queues**:
  - `tp::fifo_task_queue<T>` — FIFO blocking queue
  - `tp::priority_task_queue<T, Compare>` — priority blocking queue
- **Rejection policies**: `abort`, `caller_runs`, `discard`, `discard_oldest`
- **Dual build systems**: CMake (≥3.11) and Meson (≥1.1)
- **Catch2 unit tests**: BDD-style tests, optional build

## 2. Directory Structure

```shell
.
├── include/
│   ├── callable.hpp          # Callable task wrapper + priority support
│   ├── task_queue.hpp        # Task queue interface + FIFO / priority implementations
│   └── thread_pool.hpp       # ThreadPool implementation
├── tests/
│   ├── test_fifo_task_queue.cpp
│   ├── test_priority_task_queue.cpp
│   ├── test_thread_pool_basic.cpp
│   ├── test_thread_pool_priority.cpp
│   ├── test_thread_pool_reject.cpp
│   └── test_thread_pool_shutdown.cpp
├── CMakeLists.txt
├── meson.build
├── meson.options
└── subprojects/catch2.wrap
```

## 3. Build

### 3.1. CMake

```shell
# Default: tests are not built
cmake -B cmake-build-debug
cmake --build cmake-build-debug -j$(nproc)
```

### 3.2. Meson

```shell
# Default: tests are not built
meson setup meson-build-debug
meson compile -C meson-build-debug -j$(nproc)
```

## 4. Usage Example

### 4.1. FIFO queue (default)

```cpp
#include <iostream>
#include <memory>
#include "threadpool/task_queue.hpp"
#include "threadpool/thread_pool.hpp"

void task_func(int id) {
    std::cout << "Processing " << id << "\n";
}

struct task_obj {
    void operator()(int a, int b) const {
        std::cout << a << " + " << b << " = " << (a + b) << "\n";
    }
};

int main() {
    auto work_queue = std::make_unique<tp::fifo_task_queue<tp::callable>>();
    tp::thread_pool pool(4, 8, std::chrono::seconds(60), std::move(work_queue));

    // 1. Stateless lambda
    pool.execute([] {
        std::cout << "Hello from thread pool\n";
    });

    // 2. Lambda with captures and arguments
    int factor = 10;
    pool.execute([factor](int x) {
        std::cout << "Result: " << x * factor << "\n";
    }, 5);

    // 3. Ordinary function with arguments
    pool.execute(task_func, 42);

    // 4. Function object
    pool.execute(task_obj{}, 3, 4);

    pool.shutdown();
    pool.await_termination(std::chrono::seconds(5));
    return 0;
}
```

### 4.2. Priority queue

```cpp
#include <iostream>
#include <memory>

#include "threadpool/task_queue.hpp"
#include "threadpool/thread_pool.hpp"

int main() {
    auto work_queue = std::make_unique<tp::priority_task_queue<tp::callable, tp::callable_priority_compare>>();
    tp::thread_pool pool(4, 8, std::chrono::seconds(60), std::move(work_queue));

    // Higher priority value = earlier execution
    pool.execute(1, [] { std::cout << "low\n"; });
    pool.execute(5, [] { std::cout << "high\n"; });
    pool.execute(3, [] { std::cout << "medium\n"; });

    pool.shutdown();
    pool.await_termination(std::chrono::seconds(5));
    // Output order: high, medium, low
    return 0;
}
```

## 5. Interface Reference

### 5.1. task_queue<T>

| Method | Description |
|--------|-------------|
| `try_push(T&&)` | Non-blocking enqueue, returns `false` if full |
| `pop()` | Blocking dequeue |
| `try_pop(T&)` | Non-blocking dequeue, returns `false` if empty |
| `pop_with_timeout(T&, timeout)` | Blocking dequeue with timeout |
| `size()` | Queue size snapshot |
| `wake_all()` | Wake up all threads blocked on `pop()` / `pop_with_timeout()` |

> All types in this section reside in the `tp` namespace.

### 5.2. callable

`callable` is a lightweight task wrapper holding a `std::function<void()>` and an optional `unsigned int` priority.

```cpp
tp::callable task([] { /* ... */ });               // default priority (LOWEST)
tp::callable task([] { /* ... */ }, 10);           // explicit priority
```

- `LOWEST_PRIORITY` = `0`
- `HIGHEST_PRIORITY` = `std::numeric_limits<unsigned int>::max()`

### 5.3. thread_pool

`thread_pool` is **non-copyable and non-movable**.

| Method | Description |
|--------|-------------|
| `execute(callable)` | Submit a pre-built `callable` task |
| `execute(F&&, Args&&...)` | Submit any callable with arguments (auto-wrapped) |
| `execute(priority, F&&, Args&&...)` | Submit with explicit priority |
| `shutdown()` | Graceful shutdown: no new tasks accepted, queued tasks are executed |
| `shutdown_now()` | Immediate shutdown: returns a list of unexecuted tasks |
| `await_termination(timeout)` | Wait for all threads to exit (`std::chrono::seconds`, negative = infinite) |

**Destruction behavior**: the destructor calls `shutdown()` and waits up to **30 seconds** for all workers to finish. If workers are still alive after the timeout (e.g. tasks are deadlocked), they are forcefully detached to prevent `std::terminate`.

## 6. Testing

The project uses [Catch2 v3](https://github.com/catchorg/Catch2) as the testing framework. Test cases are written in BDD style (`SCENARIO/GIVEN/WHEN/THEN`).

```shell
# CMake
cmake -B cmake-build-debug -DTHREAD_POOL_BUILD_TESTS=ON
cmake --build cmake-build-debug -j$(nproc)
ctest --test-dir cmake-build-debug -j$(nproc)

# Meson
meson setup meson-build-debug -Dbuild_tests=true
meson compile -C meson-build-debug -j$(nproc)
meson test -C meson-build-debug -j$(nproc)
```

## 7. Dependencies

- **Build**: C++17 compiler
- **Testing**: [Catch2 v3](https://github.com/catchorg/Catch2) (automatically fetched via CMake FetchContent or Meson wrap)

## 8. License

[Apache License 2.0](LICENSE)
