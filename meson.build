project('bptree', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++14'])

thread_dep = dependency('threads')
executable('bplustree',
  'btree.cpp',
  'main.cpp',
  dependencies : thread_dep,
  install : true)
