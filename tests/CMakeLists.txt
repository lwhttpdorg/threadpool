include(CTest)
include(Catch)

# Helper: apply ASAN to a target (GNU/Clang only)
function(apply_asan_to_target tgt)
    if (ENABLE_ASAN)
        if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${tgt} PRIVATE
                -fsanitize=address
                -fsanitize=undefined
                -fno-sanitize-recover=all
                -fsanitize=float-divide-by-zero
                -fsanitize=float-cast-overflow
                -fno-sanitize=null
                -fno-sanitize=alignment
            )
            if (CMAKE_BUILD_TYPE MATCHES "Debug")
                target_compile_options(${tgt} PRIVATE -fno-omit-frame-pointer)
            endif ()
            target_link_options(${tgt} PRIVATE
                -fsanitize=address
                -fsanitize=undefined
            )
        elseif (MSVC)
            message(WARNING "MSVC AddressSanitizer is not supported in this configuration; ENABLE_ASAN is ignored.")
        endif ()
    endif ()
endfunction()

set(TEST_SRCS
    test_fifo_task_queue.cpp
    test_priority_task_queue.cpp
    test_thread_pool_basic.cpp
    test_thread_pool_shutdown.cpp
    test_thread_pool_reject.cpp
    test_thread_pool_priority.cpp
)

foreach(src ${TEST_SRCS})
    get_filename_component(test_name ${src} NAME_WE)
    add_executable(${test_name} ${src})
    target_link_libraries(${test_name} PRIVATE thread_pool Catch2::Catch2WithMain)
    apply_asan_to_target(${test_name})
    catch_discover_tests(${test_name})
endforeach()
