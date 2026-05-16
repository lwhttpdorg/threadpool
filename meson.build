project('thread_pool', 'cpp',
  version : '0.1.0',
  meson_version : '>=1.1',
  default_options : ['cpp_std=c++17']
)

thread_pool_inc = include_directories('include')
thread_pool_dep = declare_dependency(
  include_directories : thread_pool_inc,
)

if get_option('build_tests')
  catch2_proj = subproject('catch2')
  catch2_dep = catch2_proj.get_variable('catch2_with_main_dep')
  subdir('tests')
endif
