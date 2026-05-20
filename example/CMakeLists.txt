cmake_minimum_required(VERSION 3.11)
project(myapp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(ThreadPool REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE tp::thread_pool)