cmake_minimum_required(VERSION 3.11)

project(
  ThreadPool
  VERSION 1.0.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Interface library for the header-only thread pool components
add_library(thread_pool INTERFACE)
target_include_directories(
  thread_pool INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                        $<INSTALL_INTERFACE:include>)
find_package(Threads REQUIRED)
target_link_libraries(thread_pool INTERFACE Threads::Threads)

option(TP_BUILD_TESTS "Build thread_pool tests" OFF)
option(TP_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(TP_ENABLE_CODECOVER "Enable code coverage (GNU only)" OFF)

if(TP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Installation & packaging
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS thread_pool EXPORT ThreadPool-targets)

install(
  EXPORT ThreadPool-targets
  FILE ThreadPoolTargets.cmake
  NAMESPACE tp::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThreadPoolConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/ThreadPoolConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ThreadPool)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/ThreadPool.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/ThreadPool.pc
  @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ThreadPool.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
