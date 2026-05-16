test_srcs = [
    'test_fifo_task_queue.cpp',
    'test_priority_task_queue.cpp',
    'test_thread_pool_basic.cpp',
    'test_thread_pool_shutdown.cpp',
    'test_thread_pool_reject.cpp',
    'test_thread_pool_priority.cpp',
]

test_cpp_args = []
test_link_args = []

if get_option('enable_asan')
    test_cpp_args += [
        '-fsanitize=address',
        '-fsanitize=undefined',
        '-fno-sanitize-recover=all',
        '-fsanitize=float-divide-by-zero',
        '-fsanitize=float-cast-overflow',
        '-fno-sanitize=null',
        '-fno-sanitize=alignment',
    ]
    test_link_args += [
        '-fsanitize=address',
        '-fsanitize=undefined',
    ]
endif

foreach src : test_srcs
    test_name = src.substring(0, -4)
    exe = executable(test_name, src,
        include_directories : thread_pool_inc,
        dependencies : [catch2_dep, dependency('threads')],
        cpp_args : test_cpp_args,
        link_args : test_link_args,
    )
    test(test_name, exe)
endforeach
