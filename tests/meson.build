test_srcs = [
    'test_fifo_task_queue.cpp',
    'test_priority_task_queue.cpp',
    'test_thread_pool_basic.cpp',
    'test_thread_pool_shutdown.cpp',
    'test_thread_pool_reject.cpp',
    'test_thread_pool_priority.cpp',
]

foreach src : test_srcs
    name = src.substring(0, -4)
    exe = executable(name, src,
        include_directories : thread_pool_inc,
        dependencies : [catch2_dep, dependency('threads')],
    )
    test(name, exe)
endforeach
