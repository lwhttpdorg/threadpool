# Thread Pool

<!-- TOC -->
- [1. Features](#1.-features)
- [2. Quick Start](#2.-quick-start)
- [3. Build](#3.-build)
- [4. Testing](#4.-testing)
- [5. Dependencies](#5.-dependencies)
- [6. License](#6.-license)
<!-- /TOC -->

A C++ thread pool implementation modeled after Java's `ThreadPoolExecutor`, supporting core threads, maximum threads, idle timeout, task queues, and rejection policies.

All public APIs are placed under the `tp` namespace. See [DESIGN.md](DESIGN.md) for architecture details.

## 1. Features

- Eager core threads + on-demand non-core threads with idle timeout
- Pluggable task queues (FIFO / priority)
- Priority-based task scheduling
- Rejection policies: `abort`, `caller_runs`, `discard`, `discard_oldest`
- Exception safety (user exceptions never crash the process)
- No thread detachment (all threads unconditionally joined)
- Dual build systems: CMake (≥3.11) and Meson (≥1.1)

## 2. Quick Start

```cpp
#include "threadpool/task_queue.hpp"
#include "threadpool/thread_pool.hpp"

int main() {
    auto queue = std::make_unique<tp::fifo_task_queue<tp::callable>>();
    tp::thread_pool pool(4, 8, std::chrono::seconds(60), std::move(queue));

    pool.execute([] { /* task */ });
    pool.execute([](int x) { /* task with arg */ }, 42);
    pool.execute(5, [] { /* priority task */ });

    pool.shutdown();
    pool.await_termination(std::chrono::seconds(5));
}
```

## 3. Build

```shell
# CMake
cmake -B cmake-build
cmake --build cmake-build -j$(nproc)

# Meson
meson setup meson-build
meson compile -C meson-build -j$(nproc)
```

## 4. Testing

```shell
# CMake
cmake -B cmake-build -DTP_BUILD_TESTS=ON -DTP_ENABLE_ASAN=ON -DTP_ENABLE_CODECOVER=ON
cmake --build cmake-build -j$(nproc)
ctest --test-dir cmake-build -j$(nproc)

# Meson
meson setup meson-build -Dbuild_tests=true -Db_sanitize=address,undefined -Denable_codecover=true
meson compile -C meson-build -j$(nproc)
meson test -C meson-build -j$(nproc)
```

## 5. Dependencies

- **Build**: C++17 compiler
- **Testing**: [Catch2 v3](https://github.com/catchorg/Catch2) (auto-fetched)

## 6. License

[Apache License 2.0](LICENSE)
