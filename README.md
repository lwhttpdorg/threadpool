# Thread Pool

<!-- TOC -->
- [1. Features](#1-features)
- [2. Quick Start](#2-quick-start)
  - [2.1. CMake](#21-cmake)
  - [2.2. Meson](#22-meson)
  - [2.3. Code](#23-code)
- [3. Build](#3-build)
- [4. Testing](#4-testing)
- [5. Dependencies](#5-dependencies)
- [6. License](#6-license)
<!-- /TOC -->

A C++20 thread pool implementation modeled after Java's `ThreadPoolExecutor`, supporting core threads, maximum threads, idle timeout, task queues, and rejection policies.

All public APIs are placed under the `tp` namespace. See [DESIGN.md](DESIGN.md) for architecture details.

## 1. Features

- Eager core threads + on-demand non-core threads with idle timeout
- Pluggable task queues (FIFO / priority)
- Priority-based task scheduling
- Rejection policies: `abort`, `caller_runs`, `discard`, `discard_oldest`
- Exception safety (user exceptions never crash the process)
- No thread detachment (all threads unconditionally joined via `std::jthread`)
- Background watcher thread for non-core thread reclamation
- Dual build systems: CMake (≥3.11) and Meson (≥1.1)

## 2. Quick Start

### 2.1. CMake

```cmake
cmake_minimum_required(VERSION 3.11)
project(myapp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(ThreadPool REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE tp::thread_pool)
```

### 2.2. Meson

```meson
project('myapp', 'cpp', version: '1.0.0', default_options: ['cpp_std=c++20'])

threadpool_dep = dependency('ThreadPool')

executable('myapp', 'main.cpp', dependencies: threadpool_dep)
```

### 2.3. Code

```cpp
#include <chrono>
#include <cstdio>
#include <memory>

#include <threadpool/blocking_queue.hpp>
#include <threadpool/thread_pool.hpp>

void func_no_arg() {
    printf("no-arg function\n");
}

void func_with_args(int x, int y) {
    printf("%d * %d = %d\n", x, y, x * y);
}

int main() {
    auto queue = std::make_unique<tp::fifo_task_queue>(16);
    tp::thread_pool pool(2, 4, std::chrono::seconds(30), std::move(queue));

    pool.execute([] { printf("hello from pool\n"); });
    pool.execute([](int n) { printf("n = %d\n", n); }, 7);
    pool.execute(func_no_arg);
    pool.execute(func_with_args, 3, 4);
    pool.execute(10, [] { printf("high priority task\n"); });

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
cmake -B cmake-build -DCMAKE_BUILD_TYPE=Debug -DTP_BUILD_TESTS=ON -DTP_ENABLE_ASAN=ON -DTP_ENABLE_CODECOVER=ON
cmake --build cmake-build -j$(nproc)
ctest --test-dir cmake-build -j$(nproc)

# Meson
meson setup meson-build --buildtype=debug -Dbuild_tests=true -Db_sanitize=address,undefined -Denable_codecover=true
meson compile -C meson-build -j$(nproc)
meson test -C meson-build -j$(nproc)
```

## 5. Dependencies

- **Build**: C++20 compiler (GCC ≥ 10, Clang ≥ 11, MSVC ≥ 19.28)
- **Testing**: [Catch2 v3](https://github.com/catchorg/Catch2) (auto-fetched)

## 6. License

[Apache License 2.0](LICENSE)
