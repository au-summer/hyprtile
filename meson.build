project('hyprtile', 'cpp',
  version: '0.1',
  default_options: ['buildtype=debug'],
  meson_version: '>= 1.1'
)

cpp_compiler = meson.get_compiler('cpp')
if cpp_compiler.has_argument('-std=c++23')
  add_global_arguments('-std=c++23', language: 'cpp')
elif cpp_compiler.has_argument('-std=c++2b')
  add_global_arguments('-std=c++2b', language: 'cpp')
else
  error('Could not configure current C++ compiler (' + cpp_compiler.get_id() + ' ' + cpp_compiler.version() + ') with required C++ standard (C++23)')
endif

project_args = ['-Wno-narrowing', '-shared', '-fPIC']
if cpp_compiler.get_id() == 'gcc'
  project_args += ['--no-gnu-unique']
endif

add_project_arguments(project_args, language: 'cpp')

globber = run_command('find', 'src', '-name', '*.cpp', check: true)
src = globber.stdout().strip().split('\n')

dependencies = [
  dependency('pixman-1'),
  dependency('libdrm'),
  dependency('hyprland'),
  dependency('pangocairo'),
  dependency('libinput'),
  dependency('libudev'),
  dependency('wayland-server'),
  dependency('xkbcommon'),
]

shared_module(
  meson.project_name(),
  src,
  dependencies: dependencies,
  install: true,
  name_prefix: ''
)
