catch2_dep = dependency(
    'catch2-with-main',
    version: '>=3.0',
    fallback: ['catch2', 'catch2_with_main_dep'],
)

# Code coverage (GNU only)
cover_compile_args = []
cover_link_args = []
if get_option('enable_codecover')
    cover_compile_args = ['--coverage', '-O0', '-g']
    cover_link_args = ['--coverage']
endif

test_srcs = [
    'test_blocking_queue.cpp',
    'test_priority_blocking_queue.cpp',
    'test_thread_pool_basic.cpp',
    'test_thread_pool_shutdown.cpp',
    'test_thread_pool_reject.cpp',
    'test_thread_pool_priority.cpp',
    'test_thread_pool_edge.cpp',
]

foreach src : test_srcs
    test_name = src.substring(0, -4)
    exe = executable(
        test_name,
        src,
        dependencies: [thread_pool_dep, catch2_dep],
        cpp_args: cover_compile_args,
        link_args: cover_link_args,
    )
    test(test_name, exe)
endforeach
