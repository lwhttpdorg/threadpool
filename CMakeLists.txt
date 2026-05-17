cmake_minimum_required(VERSION 3.11)

project(ThreadPool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Interface library for the header-only thread pool components
add_library(thread_pool INTERFACE)
target_include_directories(thread_pool INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(THREAD_POOL_BUILD_TESTS "Build thread_pool tests" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(THREAD_POOL_BUILD_TESTS)
    find_package(Catch2 3 CONFIG QUIET)
    if(NOT Catch2_FOUND)
        message(STATUS "Catch2 not found, fetching from GitHub...")
        Include(FetchContent)
        FetchContent_Declare(
          Catch2
          GIT_REPOSITORY https://github.com/catchorg/Catch2.git
          GIT_TAG        v3.4.0
        )
        FetchContent_MakeAvailable(Catch2)
    else()
        message(STATUS "Found system-installed Catch2")
    endif()
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation & packaging
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS thread_pool
    EXPORT ThreadPool-targets
)

install(EXPORT ThreadPool-targets
    FILE ThreadPoolTargets.cmake
    NAMESPACE tp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThreadPoolConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool
)
