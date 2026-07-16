project(
  'turbojs',
  'c',
  version: '0.15.1',
  default_options: [
    'c_std=gnu11,c11',
    'warning_level=3',
    'buildtype=release',
    'b_ndebug=if-release',
  ],
  license: 'MIT',
  license_files: 'LICENSE',
  meson_version: '>=1.3.0',
)

host_system = host_machine.system()
cc = meson.get_compiler('c')

add_project_arguments('-DQUICKJS_NG_BUILD', language: 'c')

turbojs_gcc_warning_args = [
  '-Wno-unsafe-buffer-usage',
  '-Wno-sign-conversion',
  '-Wno-nonportable-system-include-path',
  '-Wno-implicit-int-conversion',
  '-Wno-shorten-64-to-32',
  '-Wno-reserved-macro-identifier',
  '-Wno-reserved-identifier',
  '-Wdeprecated-declarations',

  '-Wno-implicit-fallthrough',
  '-Wno-sign-compare',
  '-Wno-missing-field-initializers',
  '-Wno-unused-parameter',
  '-Wno-unused-but-set-variable',
  '-Wno-array-bounds',
  '-Wno-format-truncation',
]
turbojs_gcc_args = []

if host_system != 'sunos'
  turbojs_gcc_args += '-funsigned-char'
endif

if host_system == 'darwin'
  # https://github.com/turbojs/quickjs/issues/453
  turbojs_gcc_warning_args += '-Wno-maybe-uninitialized'
endif

# https://github.com/microsoft/cpp-docs/tree/main/docs/error-messages/compiler-warnings
turbojs_msvc_warning_args = [
  '/wd4018', # -Wno-sign-conversion
  '/wd4061', # -Wno-implicit-fallthrough
  '/wd4100', # -Wno-unused-parameter
  '/wd4200', # -Wno-zero-length-array
  '/wd4242', # -Wno-shorten-64-to-32
  '/wd4244', # -Wno-shorten-64-to-32
  '/wd4245', # -Wno-sign-compare
  '/wd4267', # -Wno-shorten-64-to-32
  '/wd4388', # -Wno-sign-compare
  '/wd4389', # -Wno-sign-compare
  '/wd4710', # Function not inlined
  '/wd4711', # Function was inlined
  '/wd4820', # Padding added after construct
  '/wd4996', # -Wdeprecated-declarations
  '/wd5045', # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
]
turbojs_msvc_args = [
  '/experimental:c11atomics',
  '/J', # -funsigned-char
]

if cc.get_argument_syntax() == 'msvc'
  add_project_arguments(
    cc.get_supported_arguments(turbojs_msvc_warning_args),
    cc.get_id().contains('clang') ? cc.get_supported_arguments(turbojs_gcc_warning_args) : [],
    turbojs_msvc_args,
    language: 'c',
  )
else
  add_project_arguments(
    cc.get_supported_arguments(turbojs_gcc_warning_args),
    turbojs_gcc_args,
    language: 'c',
  )
endif

if host_system == 'windows'
  # Set a 8MB default stack size on Windows.
  # It defaults to 1MB on MSVC, which is the same as our current JS stack size,
  # so it will overflow and crash otherwise.
  # On MinGW it defaults to 2MB.
  stack_size = 8 * 1024 * 1024
  if cc.get_argument_syntax() == 'msvc'
    add_project_link_arguments(f'/STACK:@stack_size@', language: 'c')
  else
    add_project_link_arguments(f'-Wl,--stack,@stack_size@', language: 'c')
  endif
endif

if meson.is_cross_build()
  native_cc = meson.get_compiler('c', native: true)

  if native_cc.get_argument_syntax() == 'msvc'
    # https://github.com/microsoft/cpp-docs/tree/main/docs/error-messages/compiler-warnings
    add_project_arguments(
      native_cc.get_supported_arguments(turbojs_msvc_warning_args),
      native_cc.get_id().contains('clang') ? native_cc.get_supported_arguments(turbojs_gcc_warning_args) : [],
      turbojs_msvc_args,

      language: 'c',
      native: true,
    )
  else
    add_project_arguments(
      native_cc.get_supported_arguments(turbojs_gcc_warning_args),
      turbojs_gcc_args,

      language: 'c',
      native: true,
    )
  endif
endif
if get_option('debug')
  add_project_arguments(
    cc.get_supported_arguments('-fno-omit-frame-pointer'),
    language: 'c',
  )
endif

turbojs_system_deps = []

m_dep = cc.find_library('m', required: false)
turbojs_system_deps += m_dep
turbojs_system_deps += dependency('threads', required: false)
turbojs_system_deps += dependency('dl', required: false)

turbojs_sources = files(
  'src/numeric/dtoa.c',
  'src/regexp/regexp.c',
  'src/unicode/unicode.c',
  'src/generated/turbojs_engine_unit.c',
  'src/core/version_api.c',
  'src/runtime/runtime_config_api.c',
  'src/runtime/context_access_api.c',
  'src/runtime/job_api.c',
  'src/runtime/lifecycle_api.c',
  'src/runtime/job_enqueue_api.c',
  'src/objects/class_proto_api.c',
  'src/gc/exception_state_api.c',
  'src/builtins/promise_hooks_api.c',
  'src/gc/memory_diagnostics_api.c',
  'src/modules/module_api.c',
)
turbojs_headers = files(
  'TurboJS.h',
)

turbojs_libraryc = get_option('libc')
turbojs_libc_sources = files('runtime/libc/turbojs-libc.c')
turbojs_libc_headers = files('src/api/turbojs-libc.h')

if turbojs_libraryc
  turbojs_headers += turbojs_libc_headers
  add_project_arguments(
    '-DTURBOJS_BUILD_LIBC',
    language: 'c'
  )
endif

turbojs_parser = get_option('parser')

turbojs_c_args = ['-D_GNU_SOURCE']

if host_system == 'windows'
  turbojs_c_args += ['-DWIN32_LEAN_AND_MEAN', '-D_WIN32_WINNT=0x0601']
endif

if not turbojs_parser
  turbojs_c_args += ['-DQJS_DISABLE_PARSER']
endif

turbojs_library = library(
  'turbojs',
  turbojs_sources,
  turbojs_libraryc ? turbojs_libc_sources : [],

  dependencies: turbojs_system_deps,
  c_args: turbojs_c_args,
  c_shared_args: ['-DBUILDING_QJS_SHARED'],
  gnu_symbol_visibility: 'hidden',
  include_directories: include_directories('.', 'src', 'src/api'),

  install: true,
  version: meson.project_version(),
)

turbojs_export_variables = [
  f'have_parser=@turbojs_parser@'
]

# This note is only of interest on Windows.
# Be conservative here: only pass -DUSING_QJS_SHARED iff default_library ==
# 'shared', thus qjs is guaranteed to be a DLL. Not passing -DUSING_QJS_SHARED if
# qjs is a DLL is not harmful, but passing the definition for DLL qjs is desired.
# Conversely, if -DUSING_QJS_SHARED is passed for static TurboJS, the consumers
# will not compile.
turbojs_dependency_args = get_option('default_library') == 'shared' ? ['-DUSING_QJS_SHARED'] : []

turbojs_dependency = declare_dependency(
  compile_args: turbojs_dependency_args,
  link_with: turbojs_library,
  dependencies: turbojs_system_deps,
  include_directories: include_directories('.'),
  variables: turbojs_export_variables,
)

if host_system != 'windows'
  turbojs_module_dependency = declare_dependency(
    compile_args: [turbojs_dependency_args, '-DTURBOJS_MODULE_BUILD'],
    include_directories: include_directories('.'),
  )
else
  turbojs_module_dependency = declare_dependency(
    compile_args: [turbojs_dependency_args, '-DTURBOJS_MODULE_BUILD'],
    include_directories: include_directories('.'),
    # Windows can't resolve symbols at runtime thus we explicitly link to qjs.
    dependencies: turbojs_dependency,
  )
endif

if turbojs_libraryc
  turbojs_libraryc_dep = declare_dependency(
    include_directories: include_directories('.'),
    dependencies: turbojs_dependency,
  )
else
  turbojs_libraryc_lib = static_library(
    'turbojs-libc',
    turbojs_libc_sources,

    c_args: turbojs_c_args,
    dependencies: [turbojs_system_deps, turbojs_dependency],
    gnu_symbol_visibility: 'hidden',
  )
  turbojs_libraryc_dep = declare_dependency(
    link_with: turbojs_libraryc_lib,
    # Pick up src/api/turbojs-libc.h.
    include_directories: include_directories('.'),
  )
endif

if host_system == 'emscripten'
  turbojs_wasm_export_name = 'getQuickJs'
  executable(
    'turbojs_wasm',
    turbojs_sources,
    link_args: cc.get_supported_link_arguments(
      # in emscripten 3.x, this will be set to 16k which is too small for TurboJS.
      '-sSTACK_SIZE=@0@'.format(2 * 1024 * 1024), # let it be 2m = 2 * 1024 * 1024, otherwise, stack overflow may be occured at bootstrap
      '-sNO_INVOKE_RUN',
      '-sNO_EXIT_RUNTIME',
      '-sMODULARIZE', # do not mess the global
      '-sEXPORT_ES6', # export js file to morden es module
      '-sEXPORT_NAME=@0@'.format(turbojs_wasm_export_name), # give a name
      '-sTEXTDECODER=1', # it will be 2 if we use -Oz, and that will cause js -> c string convertion fail
      '-sNO_DEFAULT_TO_CXX', # this project is pure c project, no need for c plus plus handle
      '-sEXPORTED_RUNTIME_METHODS=ccall,cwrap',
    ),
    dependencies: m_dep,
    c_args: turbojs_c_args,
  )
endif

install_headers(turbojs_headers, subdir: 'turbojs')

if not meson.is_subproject()
  docdir = get_option('docdir')
  datadir = get_option('datadir')

  if docdir == ''
    docdir = datadir / 'doc' / meson.project_name()
  endif
  install_data(
    'LICENSE',
    install_dir: docdir,

    install_tag: 'doc',
  )
  install_subdir(
    'examples',
    install_dir: docdir,

    strip_directory: false,
    install_tag: 'doc',
  )
endif

meson.override_dependency('turbojs', turbojs_dependency)

import('pkgconfig').generate(
  turbojs_library,
  subdirs: 'turbojs',
  name: 'turbojs',
  description: 'TurboJS, the Next Generation: a mighty JavaScript engine',
  url: 'https://github.com/turbojs/quickjs',
  version: meson.project_version(),
  variables: turbojs_export_variables,
  # Export -DUSING_QJS_SHARED, if needed, in the pkgconfig file.
  extra_cflags: turbojs_dependency_args,
)

if not turbojs_parser
  subdir_done()
endif

# TurboJS bytecode compiler
turbojsc_sources = files(
  'apps/turbojsc/main.c',
)
turbojsc_executable = executable(
  'turbojsc',
  turbojsc_sources,

  c_args: turbojs_c_args,
  dependencies: [turbojs_dependency, turbojs_libraryc_dep],
  install: true,
)

mimalloc_dep = []
mimalloc_sys_dep = dependency('mimalloc', required: get_option('cli_mimalloc'))
if mimalloc_sys_dep.found()
  mimalloc_dep = declare_dependency(
    dependencies: mimalloc_sys_dep,
    compile_args: '-DQJS_USE_MIMALLOC',
  )
endif

# TurboJS CLI
turbojs_cli_sources = files(
  'src/generated/runtime/repl.c',
  'src/generated/runtime/standalone.c',
  'apps/turbojs/main.c',
)
turbojs_executable = executable(
  'turbojs',
  turbojs_cli_sources,

  c_args: turbojs_c_args,
  dependencies: [turbojs_dependency, turbojs_libraryc_dep, mimalloc_dep],
  export_dynamic: true,

  install: true,
)

if meson.is_cross_build()
  mimalloc_native_dep = []
  mimalloc_sys_native_dep = dependency('mimalloc', required: get_option('cli_mimalloc'), native: true)
  if mimalloc_sys_dep.found()
    mimalloc_native_dep = declare_dependency(
      dependencies: mimalloc_sys_native_dep,
      compile_args: '-DQJS_USE_MIMALLOC',
    )
  endif

  turbojs_native_system_deps = [
    native_cc.find_library('m', required: false),
    dependency('threads', required: false, native: true),
    dependency('dl', required: false, native: true),
  ]
  turbojs_native_library = static_library(
    'turbojs_native',
    turbojs_sources,
    turbojs_libc_sources,

    dependencies: turbojs_native_system_deps,
    c_args: turbojs_c_args,
    gnu_symbol_visibility: 'hidden',

    build_by_default: false,
    native: true,
    install: false,
  )

  meson.override_find_program(
    'turbojsc',
    executable(
      'turbojsc_native',
      turbojsc_sources,

      c_args: turbojs_c_args,
      link_with: turbojs_native_library,
      dependencies: turbojs_native_system_deps,

      build_by_default: false,
      native: true,
      install: false,
    ),
  )
  meson.override_find_program(
    'turbojs',
    executable(
      'turbojs_native',
      turbojs_cli_sources,

      c_args: turbojs_c_args,
      link_with: turbojs_native_library,
      dependencies: [turbojs_native_system_deps, mimalloc_native_dep],
      export_dynamic: true,

      build_by_default: false,
      native: true,
      install: false,
    ),
  )
else
  meson.override_find_program('turbojsc', turbojsc_executable)
  meson.override_find_program('turbojs', turbojs_executable)
endif

tests = get_option('tests').disable_auto_if(meson.is_subproject())
examples = get_option('examples').disable_auto_if(meson.is_subproject())

if tests.allowed()
  if host_system != 'emscripten'
    # Test262 runner
    run262_exe = executable(
      'run-test262',
      'apps/test262-runner/main.c',

      c_args: turbojs_c_args,
      dependencies: [turbojs_dependency, turbojs_libraryc_dep]
    )

    test(
      'test',
      run262_exe,
      args: ['-c', files('tests/config/default.conf')],
      workdir: meson.current_source_dir(),
    )

    foreach bench : [
      'empty_loop',

      'date_now',

      'prop_read',
      'prop_write',
      'prop_create',
      'prop_delete',

      'array_read',
      'array_write',
      'array_prop_create',
      'array_length_decr',
      'array_hole_length_decr',
      'array_push',
      'array_pop',

      'typed_array_read',
      'typed_array_write',

      'global_read',
      'global_write',
      'global_write_strict',

      'local_destruct',
      'global_destruct',
      'global_destruct_strict',

      'func_call',
      'closure_var',

      'int_arith',
      'float_arith',

      'map_set',
      'map_delete',
      'weak_map_set',
      'weak_map_delete',

      'array_for',
      'array_for_in',
      'array_for_of',

      'math_min',

      'object_null',

      'regexp_ascii',
      'regexp_utf16',

      'string_build1',
      'string_build2',

      'string_slice1',
      'string_slice2',
      'string_slice3',

      'sort_bench',

      'int_to_string',
      'int_toString',

      'float_to_string',
      'float_toString',
      'float_toFixed',
      'float_toPrecision',
      'float_toExponential',

      'string_to_int',
      'string_to_float',

      'bigint64_arith',
      'bigint256_arith',
    ]
      benchmark(
        bench,
        turbojs_executable,
        args: [files('tests/microbench.js'), bench],
      )
    endforeach
  endif

  # API test
  test(
    'api',
    executable(
      'api-test',
      'tests/api/api_test.c',

      c_args: turbojs_c_args,
      dependencies: turbojs_dependency,
      build_by_default: false,
    ),
  )

  # LRE test
  test(
    'lre',
    executable(
      'lre-test',
      'tests/unit/regexp_test.c',
      'src/regexp/regexp.c',
      'src/unicode/unicode.c',

      c_args: turbojs_c_args,
      build_by_default: false,
    ),
  )

  # Function.toString() test
  test(
    'function_source',
    executable(
      'function_source',
      'src/generated/runtime/function_source.c',

      c_args: turbojs_c_args,
      dependencies: [turbojs_dependency, turbojs_libraryc_dep],
      build_by_default: false,
    ),
  )
endif

# Unicode generator
unicode_gen = executable(
  'unicode_gen',
  'tools/generators/unicode/unicode_gen.c',

  c_args: turbojs_c_args,
  build_by_default: false,
)

run_target(
  'src/unicode/unicode_tables.h',
  command: [
    unicode_gen,
    meson.current_source_dir() / 'unicode',
    files('src/unicode/unicode_tables.h'),
  ],
)

# bytecode to c source code for builtin and examples
alias_target('codegen',
  run_target(
    'codegen_repl.c',
    command: [
      turbojsc_executable,
      '-ss',
      '-o', files('src/generated/runtime/repl.c'),
      '-m',
      '-n', 'repl.js',
      files('repl.js'),
    ],
  ),
  run_target(
    'codegen_standalone.c',
    command: [
      turbojsc_executable,
      '-ss',
      '-o', files('src/generated/runtime/standalone.c'),
      '-m',
      '-n', 'standalone.js',
      files('standalone.js'),
    ],
  ),
  run_target(
    'codegen_function_source.c',
    command: [
      turbojsc_executable,
      '-e',
      '-o', files('src/generated/runtime/function_source.c'),
      '-n', 'tests/function_source.js',
      files('tests/function_source.js'),
    ],
  ),
  run_target(
    'codegen_hello.c',
    command: [
      turbojsc_executable,
      '-e',
      '-o', files('gen/hello.c'),
      '-n', 'examples/hello.js',
      files('examples/hello.js'),
    ],
  ),
  run_target(
    'codegen_hello_module.c',
    command: [
      turbojsc_executable,
      '-e',
      '-o', files('gen/hello_module.c'),
      '-m',
      '-n', 'examples/hello_module.js',
      files('examples/hello_module.js'),
    ],
  ),
  run_target(
    'codegen_test_fib.c',
    command: [
      turbojsc_executable,
      '-e',
      '-o', files('gen/test_fib.c'),
      '-m',
      '-n', 'examples/test_fib.js',
      files('examples/test_fib.js'),
    ],
  ),
  run_target(
    'codegen_src/generated/builtins/array_fromasync.h',
    command: [
      turbojsc_executable,
      '-C',
      '-ss',
      '-o', files('src/generated/builtins/array_fromasync.h'),
      '-n', 'src/builtins/sources/array_fromasync.js',
      files('src/builtins/sources/array_fromasync.js'),
    ],
  ),
)

if examples.allowed()
  if host_system == 'windows' and get_option('default_library') != 'shared'
    warning('Binary modules used with static TurboJS on Windows.')
    warning(
      'There might be runtime errors when the modules are used due to',
      'two copies of TurboJS in memory.'
    )
    warning('Please use "-Ddefault_library=shared" if possible.')
  endif
  executable(
    'hello',
    'gen/hello.c',

    c_args: turbojs_c_args,
    dependencies: [turbojs_dependency, turbojs_libraryc_dep]
  )

  executable(
    'hello_module',
    'gen/hello_module.c',

    c_args: turbojs_c_args,
    dependencies: [turbojs_dependency, turbojs_libraryc_dep]
  )

  subdir('examples')

  executable(
    'test_fib',
    'gen/test_fib.c',

    c_args: turbojs_c_args,
    dependencies: [turbojs_dependency, turbojs_libraryc_dep],
    export_dynamic: true,
  )
endif
