find_package(Catch2 3 CONFIG QUIET)
if(NOT Catch2_FOUND)
  message(STATUS "Catch2 not found, fetching from GitHub...")
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0)
  FetchContent_MakeAvailable(Catch2)
else()
  message(STATUS "Found system-installed Catch2")
endif()

include(CTest)
include(Catch)

# Helper: apply ASAN to a target (GNU/Clang only, silently skipped on MSVC)
function(apply_asan_to_target tgt)
  if(MSVC)
    return()
  endif()
  target_compile_options(
    ${tgt}
    PRIVATE -fsanitize=address
            -fsanitize=undefined
            -fno-sanitize-recover=all
            -fsanitize=float-divide-by-zero
            -fsanitize=float-cast-overflow
            -fno-sanitize=null
            -fno-sanitize=alignment)
  if(CMAKE_BUILD_TYPE MATCHES "Debug")
    target_compile_options(${tgt} PRIVATE -fno-omit-frame-pointer)
  endif()
  target_link_options(${tgt} PRIVATE -fsanitize=address -fsanitize=undefined)
endfunction()

if(TP_ENABLE_CODECOVER)
  # Code coverage target for GNU builds
  add_library(test_code_cover INTERFACE)
  target_compile_options(test_code_cover INTERFACE --coverage -O0 -g)
  target_link_options(test_code_cover INTERFACE --coverage)
endif()

set(TEST_SRCS
    test_blocking_queue.cpp
    test_priority_blocking_queue.cpp
    test_thread_pool_basic.cpp
    test_thread_pool_shutdown.cpp
    test_thread_pool_reject.cpp
    test_thread_pool_priority.cpp
    test_thread_pool_edge.cpp)

foreach(src ${TEST_SRCS})
  get_filename_component(test_name ${src} NAME_WE)
  add_executable(${test_name} ${src})
  target_link_libraries(${test_name} PRIVATE thread_pool Catch2::Catch2WithMain)
  if(TP_ENABLE_ASAN)
    apply_asan_to_target(${test_name})
  endif()
  if(TP_ENABLE_CODECOVER)
    target_link_libraries(${test_name} PRIVATE test_code_cover)
  endif()
  catch_discover_tests(${test_name})
endforeach()
