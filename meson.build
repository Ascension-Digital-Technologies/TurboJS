project(
  'turbojs',
  'c',
  version: '0.16.0',
  default_options: ['c_std=gnu11', 'warning_level=2', 'buildtype=release', 'default_library=static'],
  license: 'MIT',
  license_files: 'LICENSE',
  meson_version: '>=1.3.0',
)

cc = meson.get_compiler('c')
host_system = host_machine.system()
python = import('python').find_installation('python3')
pkgconfig = import('pkgconfig')

public_inc = include_directories('include/turbojs')
private_inc = include_directories('.', 'src', 'src/internal')
all_includes = [public_inc, private_inc]

project_args = ['-DTURBOJS_BUILD', '-D_GNU_SOURCE']
if not get_option('parser')
  project_args += '-DTURBOJS_DISABLE_PARSER'
endif
if get_option('optimizing_jit')
  project_args += '-DTURBOJS_ENABLE_OPTIMIZING_JIT'
endif
if host_system == 'windows'
  project_args += ['-DWIN32_LEAN_AND_MEAN', '-D_WIN32_WINNT=0x0601']
endif
add_project_arguments(project_args, language: 'c')

system_deps = [
  cc.find_library('m', required: false),
  dependency('threads', required: false),
  dependency('dl', required: false),
]

jit_sources = files(
  'src/jit/frontend/bytecode_to_ir.c',
  'src/jit/frontend/engine_bytecode_to_ir.c',
  'src/jit/frontend/engine_bytecode_analysis.c',
  'src/jit/frontend/engine_bytecode_cfg.c',
  'src/jit/frontend/engine_bytecode_region.c',
  'src/jit/frontend/engine_bytecode_state.c',
  'src/jit/frontend/spool_frame_state.c',
  'src/jit/frontend/engine_bytecode_ssa.c',
  'src/jit/frontend/property_feedback.c',
  'src/jit/aot/portable_ir.c',
  'src/jit/aot/module.c',
  'src/jit/ir/ir.c',
  'src/jit/ir/ir_verify.c',
  'src/jit/ir/ir_interpreter.c',
  'src/jit/ir/tagged_ir.c',
  'src/jit/ir/callable_specialize.c',
  'src/jit/ir/deopt_boxing.c',
  'src/jit/runtime/executable_memory.c',
  'src/jit/runtime/code_cache.c',
  'src/jit/runtime/tiered_execution.c',
  'src/jit/runtime/type_feedback.c',
  'src/jit/runtime/call_feedback.c',
  'src/jit/runtime/callable_reference.c',
  'src/jit/runtime/js_frame_abi.c',
  'src/jit/runtime/helpers/table.c',
  'src/jit/runtime/helpers/invoke.c',
  'src/jit/runtime/helpers/native_continuation.c',
  'src/jit/runtime/helpers/continuation.c',
  'src/jit/runtime/helpers/continuation_cache.c',
  'src/jit/runtime/osr.c',
  'src/jit/runtime/osr_frame.c',
  'src/jit/runtime/osr_entry.c',
  'src/jit/runtime/osr_loop.c',
  'src/jit/optimizing/ssa.c',
  'src/jit/optimizing/pipeline.c',
  'src/jit/optimizing/linear_scan.c',
  'src/jit/optimizing/parallel_moves.c',
  'src/jit/backend/x64/baseline_x64.c',
  'src/jit/backend/x64/region_x64.c',
  'src/jit/backend/x64/simd_kernels.c',
  'src/jit/backend/x64/region_x64_stack.c',
  'src/jit/backend/arm64/arm64_encoder.c',
  'src/jit/backend/arm64/arm64_lowering.c',
  'src/objects/shapes.c',
)

jit_library = static_library(
  'turbojs-jit',
  jit_sources,
  include_directories: all_includes,
  dependencies: system_deps,
  install: true,
)

engine_sources = files(
  'src/numeric/dtoa.c',
  'src/regexp/regexp.c',
  'src/unicode/unicode.c',
  'generated/turbojs_engine_unit.c',
  'src/core/version_api.c',
  'src/core/runtime_config_api.c',
  'src/jit/optimization_api.c',
  'src/jit/runtime/pipeline_identity.c',
  'src/core/context_access_api.c',
  'src/vm/job_api.c',
  'src/core/lifecycle_api.c',
  'src/vm/job_enqueue_api.c',
  'src/objects/class_proto_api.c',
  'src/gc/exception_state_api.c',
  'src/builtins/promise_hooks_api.c',
  'src/gc/memory_diagnostics_api.c',
  'src/modules/module_api.c',
  'src/api/turbojs_embed.c',
)

libc_enabled = get_option('libc')
libc_sources = files('src/api/turbojs-libc.c')
if libc_enabled
  add_project_arguments('-DTURBOJS_BUILD_LIBC', language: 'c')
endif

shared_definition = get_option('default_library') == 'shared' ? ['-DBUILDING_TURBOJS_SHARED'] : []
consumer_definition = get_option('default_library') == 'shared' ? ['-DUSING_TURBOJS_SHARED'] : []

engine_library = library(
  'turbojs',
  engine_sources,
  libc_enabled ? libc_sources : [],
  include_directories: all_includes,
  dependencies: system_deps,
  link_with: jit_library,
  c_shared_args: shared_definition,
  gnu_symbol_visibility: 'hidden',
  version: meson.project_version(),
  install: true,
)

engine_dependency = declare_dependency(
  compile_args: consumer_definition,
  include_directories: public_inc,
  link_with: engine_library,
  dependencies: system_deps,
)

if libc_enabled
  runtime_dependency = engine_dependency
else
  runtime_library = static_library(
    'turbojs-runtime',
    libc_sources,
    include_directories: all_includes,
    dependencies: system_deps,
    install: true,
  )
  runtime_dependency = declare_dependency(
    include_directories: public_inc,
    link_with: [runtime_library, engine_library],
    dependencies: system_deps,
  )
endif

compiler_exe = executable(
  'turbojsc',
  'apps/turbojsc/main.c',
  include_directories: all_includes,
  dependencies: runtime_dependency,
  install: true,
)

cli_deps = [runtime_dependency]
mimalloc = dependency('mimalloc', required: get_option('cli_mimalloc'))
if mimalloc.found()
  cli_deps += declare_dependency(dependencies: mimalloc, compile_args: '-DTURBOJS_USE_MIMALLOC')
endif

cli_exe = executable(
  'turbojs',
  'generated/runtime/repl.c',
  'generated/runtime/standalone.c',
  'apps/turbojs/main.c',
  include_directories: all_includes,
  dependencies: cli_deps,
  export_dynamic: host_system != 'windows',
  install: true,
)

executable(
  'turbojs-aot-inspect',
  'tools/aot/tjm_inspect.c',
  include_directories: all_includes,
  link_with: jit_library,
  dependencies: system_deps,
)

install_headers(
  'include/turbojs/turbojs.h',
  'include/turbojs/turbojs-libc.h',
  'include/turbojs/turbojs_embed.h',
  'include/turbojs/optimization.h',
  'include/turbojs/jit.h',
  subdir: 'turbojs',
)
install_data('LICENSE', 'NOTICE.md', install_dir: get_option('datadir') / 'doc' / 'turbojs')

pkgconfig.generate(
  engine_library,
  subdirs: 'turbojs',
  name: 'TurboJS',
  description: 'Compact embeddable JavaScript engine',
  version: meson.project_version(),
  extra_cflags: consumer_definition,
)

meson.override_dependency('turbojs', engine_dependency)
meson.override_find_program('turbojs', cli_exe)
meson.override_find_program('turbojsc', compiler_exe)

run_target(
  'architecture-check',
  command: [python, 'tools/validation/check_architecture.py'],
)
run_target(
  'regenerate-engine',
  command: [python, 'tools/generators/generate_engine_unit.py'],
)
run_target(
  'regenerate-runtime',
  command: [python, 'tools/generators/generate_runtime.py', '--compiler', compiler_exe],
)

if get_option('tests').allowed()
  test(
    'architecture',
    python,
    args: ['tools/validation/check_architecture.py'],
    workdir: meson.project_source_root(),
  )
  test(
    'embedding-downstream-api',
    executable(
      'turbojs-downstream-api-test',
      'tests/embedding/downstream_api_test.c',
      include_directories: all_includes,
      dependencies: engine_dependency,
    ),
  )
  test(
    'runtime-lifecycle-stress',
    executable(
      'turbojs-runtime-stress-test',
      'tests/embedding/runtime_stress_test.c',
      include_directories: all_includes,
      dependencies: engine_dependency,
    ),
    timeout: 120,
  )
  if get_option('optimizing_jit')
    test(
      'collection-specialization',
      executable(
        'turbojs-vm-collection-specialization-test',
        'tests/jit/vm_collection_specialization_test.c',
        include_directories: all_includes,
        dependencies: engine_dependency,
      ),
    )
    test(
      'advanced-call-specialization',
      executable(
        'turbojs-vm-advanced-call-specialization-test',
        'tests/jit/vm_advanced_call_specialization_test.c',
        include_directories: all_includes,
        dependencies: engine_dependency,
      ),
    )
    test(
      'coupled-float',
      executable(
        'turbojs-vm-coupled-float-test',
        'tests/jit/vm_coupled_float_test.c',
        include_directories: all_includes,
        dependencies: engine_dependency,
      ),
    )
    test(
      'grouped-accumulator',
      executable(
        'turbojs-vm-grouped-accumulator-test',
        'tests/jit/vm_grouped_accumulator_test.c',
        include_directories: all_includes,
        dependencies: engine_dependency,
      ),
    )
  endif
endif

if get_option('examples').allowed()
  executable(
    'turbojs-stable-consumer',
    'examples/embed-stable/main.c',
    include_directories: all_includes,
    dependencies: engine_dependency,
  )
endif

if get_option('benchmarks')
  executable(
    'turbojs-runtime-continuation-cache-benchmark',
    'benchmarks/jit/runtime_continuation_cache_benchmark.c',
    include_directories: all_includes,
    link_with: jit_library,
    dependencies: system_deps,
  )
endif
