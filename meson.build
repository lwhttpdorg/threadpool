project(
    'thread_pool',
    'cpp',
    version: '1.0.0',
    meson_version: '>=1.1',
    default_options: ['cpp_std=c++17', 'buildtype=release', 'warning_level=0'],
)

install_headers(
    'include/threadpool/callable.hpp',
    'include/threadpool/task_queue.hpp',
    'include/threadpool/thread_pool.hpp',
    subdir: 'threadpool',
)

thread_pool_inc = include_directories('include')
threads_dep = dependency('threads')
thread_pool_dep = declare_dependency(
    include_directories: thread_pool_inc,
    dependencies: [threads_dep],
)

pkg = import('pkgconfig')
pkg.generate(
    thread_pool_dep,
    name: 'threadpool',
    filebase: 'ThreadPool',
    description: 'C++ header-only thread pool library',
)

if get_option('build_tests')
    subdir('tests')
endif
