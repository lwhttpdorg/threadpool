project(
    'thread_pool',
    'cpp',
    version: '1.0.0',
    meson_version: '>=1.1',
    default_options: ['cpp_std=c++17', 'buildtype=release', 'warning_level=0'],
)

thread_pool_inc = include_directories('include')
threads_dep = dependency('threads')
thread_pool_dep = declare_dependency(
    include_directories: thread_pool_inc,
    dependencies: [threads_dep],
)

if get_option('build_tests')
    subdir('tests')
endif
