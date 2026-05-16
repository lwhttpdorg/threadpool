include(CTest)
include(Catch)

set(TEST_SRCS
    test_fifo_task_queue.cpp
    test_priority_task_queue.cpp
    test_thread_pool_basic.cpp
    test_thread_pool_shutdown.cpp
    test_thread_pool_reject.cpp
    test_thread_pool_priority.cpp
)

foreach(src ${TEST_SRCS})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE thread_pool Catch2::Catch2WithMain)
    catch_discover_tests(${name})
endforeach()
