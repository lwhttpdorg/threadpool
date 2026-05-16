# Thread Pool

<!-- TOC -->
- [1. Features](#1.-features)
- [2. Directory Structure](#2.-directory-structure)
- [3. Build](#3.-build)
  - [3.1. CMake](#3.1.-cmake)
  - [3.2. Meson](#3.2.-meson)
- [4. Usage Example](#4.-usage-example)
- [5. Interface Reference](#5.-interface-reference)
  - [5.1. task_queue<T>](#5.1.-task_queue%3Ct%3E)
  - [5.2. thread_pool](#5.2.-thread_pool)
- [6. Testing](#6.-testing)
- [7. Dependencies](#7.-dependencies)
- [8. License](#8.-license)
<!-- /TOC -->

A C++ thread pool implementation modeled after Java's `ThreadPoolExecutor`, supporting core threads, maximum threads, idle timeout, task queues, and rejection policies.

All public APIs are placed under the `tp` namespace to avoid name collisions.

## 1. Features

- **Java-like ThreadPoolExecutor**: supports `corePoolSize`, `maximumPoolSize`, `keepAliveTime`, work queue, and rejection policies
- **Runnable interface**: similar to Java's `Runnable`, with automatic lambda wrapping
- **Double-buffered task queues**:
  - `tp::fifo_task_queue<T>` — FIFO blocking queue
  - `tp::priority_task_queue<T, Compare>` — priority blocking queue
- **Rejection policies**: `abort`, `caller_runs`, `discard`, `discard_oldest`
- **Dual build systems**: CMake (≥3.11) and Meson (≥1.1)
- **Catch2 unit tests**: BDD-style tests, optional build

## 2. Directory Structure

```shell
.
├── include/
│   ├── runnable.hpp          # Runnable interface + lambda wrapper
│   ├── task_queue.hpp        # Task queue interface + FIFO / priority implementations
│   └── thread_pool.hpp       # ThreadPool implementation
├── tests/
│   ├── test_fifo_task_queue.cpp
│   ├── test_priority_task_queue.cpp
│   ├── test_thread_pool_basic.cpp
│   ├── test_thread_pool_shutdown.cpp
│   └── test_thread_pool_reject.cpp
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

### FIFO queue (default)

```cpp
#include <iostream>
#include <memory>
#include "task_queue.hpp"
#include "thread_pool.hpp"

void task_func(int id) {
    std::cout << "Processing " << id << "\n";
}

struct task_obj {
    void operator()(int a, int b) const {
        std::cout << a << " + " << b << " = " << (a + b) << "\n";
    }
};

int main() {
    auto work_queue = std::make_unique<tp::fifo_task_queue<tp::task>>();
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

### Priority queue

```cpp
#include <iostream>
#include <memory>
#include "task_queue.hpp"
#include "thread_pool.hpp"

int main() {
    auto work_queue = std::make_unique<tp::priority_task_queue<tp::task, tp::task_priority_compare>>();
    tp::thread_pool pool(4, 8, std::chrono::seconds(60), std::move(work_queue));

    // Higher priority value = earlier execution
    pool.execute_with_priority(1, [] { std::cout << "low\n"; });
    pool.execute_with_priority(5, [] { std::cout << "high\n"; });
    pool.execute_with_priority(3, [] { std::cout << "medium\n"; });

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
| `push(T&&)` | Blocking enqueue |
| `try_push(T&&)` | Non-blocking enqueue, returns `false` on failure |
| `pop()` | Blocking dequeue |
| `try_pop(T&)` | Non-blocking dequeue, returns `false` on failure |
| `pop_with_timeout(T&, ms)` | Dequeue with timeout |
| `empty()` / `size()` | Queue state snapshot |

> All types in this section reside in the `tp` namespace.

### 5.2. thread_pool

| Method | Description |
|--------|-------------|
| `execute(F&&, Args&&...)` | Submit a task (lambdas, function objects, etc. are auto-wrapped) |
| `shutdown()` | Graceful shutdown: no new tasks accepted, queued tasks are executed |
| `shutdown_now()` | Immediate shutdown: returns a list of unexecuted tasks |
| `await_termination(ms)` | Wait for all threads to exit |
| `is_shutdown()` / `is_terminated()` | State queries |
| `active_count()` | Current number of active threads |
| `queue_size()` | Number of pending tasks in the queue |

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
