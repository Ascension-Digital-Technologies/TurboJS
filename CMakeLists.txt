cmake_minimum_required(VERSION 3.10)

project(TurboJS LANGUAGES C)

include(CheckCCompilerFlag)
include(GNUInstallDirs)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)

macro(xoption OPTION_NAME OPTION_TEXT OPTION_DEFAULT)
    option(${OPTION_NAME} ${OPTION_TEXT} ${OPTION_DEFAULT})
    if(DEFINED ENV{${OPTION_NAME}})
        # Allow setting the option through an environment variable.
        set(${OPTION_NAME} $ENV{${OPTION_NAME}})
    endif()
    if(${OPTION_NAME})
        add_definitions(-D${OPTION_NAME})
    endif()
    message(STATUS "  ${OPTION_NAME}: ${${OPTION_NAME}}")
endmacro()

# note: TURBOJS_ENABLE_TSAN is currently incompatible with the other sanitizers but we
# don't explicitly check for that because who knows what the future will bring?
# TURBOJS_ENABLE_MSAN only works with clang at the time of writing; also not checked
# for the same reason
xoption(BUILD_SHARED_LIBS "Build a shared library" OFF)
# if we ever require CMake 3.21, we can automatically detect this setting by looking at PROJECT_IS_TOP_LEVEL
# similar to how glslang does
xoption(TURBOJS_ENABLE_INSTALL "Enable TurboJS installation" ON)
xoption(TURBOJS_BUILD_WERROR "Build with -Werror" OFF)
xoption(TURBOJS_BUILD_EXAMPLES "Build examples" OFF)
xoption(TURBOJS_BUILD_CLI_STATIC "Build a static qjs executable" OFF)
xoption(TURBOJS_BUILD_CLI_WITH_MIMALLOC "Build the qjs executable with mimalloc" OFF)
xoption(TURBOJS_BUILD_CLI_WITH_STATIC_MIMALLOC "Build the qjs executable with mimalloc (statically linked)" OFF)
xoption(TURBOJS_DISABLE_PARSER "Disable JS source code parser" OFF)
xoption(TURBOJS_ENABLE_ASAN "Enable AddressSanitizer (ASan)" OFF)
xoption(TURBOJS_ENABLE_MSAN "Enable MemorySanitizer (MSan)" OFF)
xoption(TURBOJS_ENABLE_TSAN "Enable ThreadSanitizer (TSan)" OFF)
xoption(TURBOJS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (UBSan)" OFF)

# Used to properly define JS_LIBC_EXTERN.
add_compile_definitions(TURBOJS_BUILD)

# MINGW doesn't exist in older cmake versions, newer versions don't know
# about CMAKE_COMPILER_IS_MINGW, and there is no unique CMAKE_C_COMPILER_ID
# for mingw-based compilers...
if(MINGW)
    # do nothing
elseif(CMAKE_C_COMPILER MATCHES "mingw")
    set(MINGW TRUE)
else()
    set(MINGW FALSE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    set(TVOS TRUE)
else()
    set(TVOS FALSE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "watchOS")
    set(WATCHOS TRUE)
else()
    set(WATCHOS FALSE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    set(SUNOS TRUE)
else()
    set(SUNOS FALSE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type selected, default to Release")
    set(CMAKE_BUILD_TYPE "Release")
endif()

message(STATUS "Building in ${CMAKE_BUILD_TYPE} mode")
message(STATUS "Building with ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} on ${CMAKE_SYSTEM}")

macro(xcheck_add_c_compiler_flag FLAG)
    string(REPLACE "-" "" FLAG_NO_HYPHEN ${FLAG})
    check_c_compiler_flag(${FLAG} COMPILER_SUPPORTS_${FLAG_NO_HYPHEN})
    if(COMPILER_SUPPORTS_${FLAG_NO_HYPHEN})
        add_compile_options(${FLAG})
    endif()
endmacro()

xcheck_add_c_compiler_flag(-Wall)
if(NOT MSVC AND NOT IOS AND NOT TVOS AND NOT WATCHOS)
    if(TURBOJS_BUILD_WERROR)
        xcheck_add_c_compiler_flag(-Werror)
    endif()
    xcheck_add_c_compiler_flag(-Wextra)
endif()
xcheck_add_c_compiler_flag(-Wformat=2)
xcheck_add_c_compiler_flag(-Wno-implicit-fallthrough)
xcheck_add_c_compiler_flag(-Wno-sign-compare)
xcheck_add_c_compiler_flag(-Wno-missing-field-initializers)
xcheck_add_c_compiler_flag(-Wno-unused-parameter)
xcheck_add_c_compiler_flag(-Wno-unused-but-set-variable)
xcheck_add_c_compiler_flag(-Wno-unused-result)
xcheck_add_c_compiler_flag(-Wno-stringop-truncation)
xcheck_add_c_compiler_flag(-Wno-array-bounds)
if(NOT SUNOS)
xcheck_add_c_compiler_flag(-funsigned-char)
endif()

# Clang on Windows without MSVC command line fails because the codebase uses
# functions like strcpy over strcpy_s
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND WIN32 AND NOT MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(_CRT_NONSTDC_NO_DEPRECATE)
endif()

# ClangCL is command line compatible with MSVC, so 'MSVC' is set.
if(MSVC)
    xcheck_add_c_compiler_flag(-Wno-unsafe-buffer-usage)
    xcheck_add_c_compiler_flag(-Wno-sign-conversion)
    xcheck_add_c_compiler_flag(-Wno-nonportable-system-include-path)
    xcheck_add_c_compiler_flag(-Wno-implicit-int-conversion)
    xcheck_add_c_compiler_flag(-Wno-shorten-64-to-32)
    xcheck_add_c_compiler_flag(-Wno-reserved-macro-identifier)
    xcheck_add_c_compiler_flag(-Wno-reserved-identifier)
    xcheck_add_c_compiler_flag(-Wdeprecated-declarations)
    xcheck_add_c_compiler_flag(/experimental:c11atomics)
    xcheck_add_c_compiler_flag(/wd4018) # -Wno-sign-conversion
    xcheck_add_c_compiler_flag(/wd4061) # -Wno-implicit-fallthrough
    xcheck_add_c_compiler_flag(/wd4100) # -Wno-unused-parameter
    xcheck_add_c_compiler_flag(/wd4200) # -Wno-zero-length-array
    xcheck_add_c_compiler_flag(/wd4242) # -Wno-shorten-64-to-32
    xcheck_add_c_compiler_flag(/wd4244) # -Wno-shorten-64-to-32
    xcheck_add_c_compiler_flag(/wd4245) # -Wno-sign-compare
    xcheck_add_c_compiler_flag(/wd4267) # -Wno-shorten-64-to-32
    xcheck_add_c_compiler_flag(/wd4388) # -Wno-sign-compare
    xcheck_add_c_compiler_flag(/wd4389) # -Wno-sign-compare
    xcheck_add_c_compiler_flag(/wd4456) # Hides previous local declaration
    xcheck_add_c_compiler_flag(/wd4457) # Hides function parameter
    xcheck_add_c_compiler_flag(/wd4710) # Function not inlined
    xcheck_add_c_compiler_flag(/wd4711) # Function was inlined
    xcheck_add_c_compiler_flag(/wd4820) # Padding added after construct
    xcheck_add_c_compiler_flag(/wd4996) # -Wdeprecated-declarations
    xcheck_add_c_compiler_flag(/wd5045) # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
endif()

# Set a 8MB default stack size on Windows.
# It defaults to 1MB on MSVC, which is the same as our current JS stack size,
# so it will overflow and crash otherwise.
# On MinGW it defaults to 2MB.
if(WIN32)
    if(MSVC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:8388608")
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MINGW)
        # Clang frontend (clang.exe) targeting the MSVC ABI (using lld-link)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Xlinker /STACK:8388608")
    else()
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--stack,8388608")
    endif()
endif()

# MacOS and GCC 11 or later need -Wno-maybe-uninitialized
# https://github.com/turbojs/quickjs/issues/453
if(APPLE AND CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 11)
    xcheck_add_c_compiler_flag(-Wno-maybe-uninitialized)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    add_compile_definitions(
        _WASI_EMULATED_PROCESS_CLOCKS
        _WASI_EMULATED_SIGNAL
    )
    add_link_options(
        -lwasi-emulated-process-clocks
        -lwasi-emulated-signal
    )
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    xcheck_add_c_compiler_flag(/Od)
    xcheck_add_c_compiler_flag(-O0)
    xcheck_add_c_compiler_flag(-ggdb)
    xcheck_add_c_compiler_flag(-fno-omit-frame-pointer)
endif()

if(BUILD_SHARED_LIBS)
    message(STATUS "Building a shared library")
endif()

if(TURBOJS_ENABLE_ASAN)
message(STATUS "Building with ASan")
add_compile_options(
    -fsanitize=address
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
add_link_options(
    -fsanitize=address
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
endif()

if(TURBOJS_ENABLE_MSAN)
message(STATUS "Building with MSan")
add_compile_options(
    -fsanitize=memory
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
add_link_options(
    -fsanitize=memory
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
endif()

if(TURBOJS_ENABLE_TSAN)
message(STATUS "Building with TSan")
add_compile_options(
    -fsanitize=thread
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
add_link_options(
    -fsanitize=thread
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
endif()

if(TURBOJS_ENABLE_UBSAN)
message(STATUS "Building with UBSan")
add_compile_options(
    -fsanitize=undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
add_link_options(
    -fsanitize=undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
endif()


# TurboJS library
#

xoption(TURBOJS_BUILD_LIBC "Build standard library modules as part of the library" OFF)
macro(add_turbojs_libc_if_needed target)
    if(NOT TURBOJS_BUILD_LIBC)
        target_link_libraries(${target} PRIVATE turbojs-libc)
    endif()
endmacro()
macro(add_static_if_needed target)
    if(TURBOJS_BUILD_CLI_STATIC OR MINGW)
        target_link_options(${target} PRIVATE -static)
        if(MINGW)
            target_link_options(${target} PRIVATE -static-libgcc)
        endif()
    endif()
endmacro()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(turbojs-architecture-check
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validation/check_architecture.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking TurboJS engine subsystem boundaries"
        VERBATIM
    )
endif()


# Deliberately avoid CONFIGURE_DEPENDS here. On Windows/Ninja, the generated
# VerifyGlobs rule can enter an endless CMake regeneration loop when the build
# directory lives inside the source tree. Developer scripts run configure before
# builds, so newly added source files are still discovered deterministically.
file(GLOB_RECURSE TURBOJS_ENGINE_DOMAIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/objects/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gc/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vm/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/compiler/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/serialization/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/builtins/*.c
)

set(TURBOJS_ENGINE_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/turbojs_engine_unit.c)
add_custom_command(
    OUTPUT ${TURBOJS_ENGINE_GENERATED}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generators/generate_engine_unit.py
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/src/generated/turbojs_engine_unit.c
            ${TURBOJS_ENGINE_GENERATED}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/subsystems.json
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/generators/generate_engine_unit.py
        ${TURBOJS_ENGINE_DOMAIN_SOURCES}
    COMMENT "Generating TurboJS engine compilation unit"
    VERBATIM
)

set(turbojs_sources
    src/numeric/dtoa.c
    src/regexp/regexp.c
    src/unicode/unicode.c
    ${TURBOJS_ENGINE_GENERATED}
    src/core/version_api.c
    src/runtime/runtime_config_api.c
    src/runtime/optimization_api.c
    src/runtime/context_access_api.c
    src/runtime/job_api.c
    src/runtime/lifecycle_api.c
    src/runtime/job_enqueue_api.c
    src/objects/class_proto_api.c
    src/gc/exception_state_api.c
    src/builtins/promise_hooks_api.c
    src/gc/memory_diagnostics_api.c
    src/modules/module_api.c
)

if(TURBOJS_BUILD_LIBC)
    list(APPEND turbojs_sources runtime/libc/turbojs-libc.c)
    # The definition must be added to the entire project.
    add_compile_definitions(TURBOJS_BUILD_LIBC)
endif()
list(APPEND turbojs_defines _GNU_SOURCE)
if(WIN32)
    # NB: Windows 7 is EOL and we are only supporting in so far as it doesn't interfere with progress.
    list(APPEND turbojs_defines WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
if(TVOS)
    list(APPEND turbojs_defines _TVOS)
endif()
if(WATCHOS)
    list(APPEND turbojs_defines _WATCHOS)
endif()
list(APPEND turbojs_system_libs ${CMAKE_DL_LIBS})
find_package(Threads)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    list(APPEND turbojs_system_libs ${CMAKE_THREAD_LIBS_INIT})
endif()

# try to find libm
find_library(M_LIBRARIES m)
if(M_LIBRARIES OR CMAKE_C_COMPILER_ID STREQUAL "TinyCC")
    list(APPEND turbojs_system_libs m)
endif()

add_library(turbojs ${turbojs_sources})
add_library(TurboJS::Engine ALIAS turbojs)
target_compile_definitions(turbojs PRIVATE ${turbojs_defines})
target_include_directories(turbojs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(turbojs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/api>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/turbojs>
)
target_link_libraries(turbojs PUBLIC ${turbojs_system_libs})

# Tiered execution infrastructure. Kept as a focused library so IR/JIT tests
# can build quickly without recompiling the large engine unity translation unit.
set(TURBOJS_JIT_SOURCES
    src/jit/frontend/bytecode_to_ir.c
    src/jit/frontend/engine_bytecode_to_ir.c
    src/jit/aot/portable_ir.c
    src/jit/aot/module.c
    src/jit/ir/ir.c
    src/jit/ir/ir_verify.c
    src/jit/ir/ir_interpreter.c
    src/jit/ir/deopt_boxing.c
    src/jit/runtime/executable_memory.c
    src/jit/runtime/code_cache.c
    src/jit/runtime/tiered_execution.c
    src/jit/runtime/type_feedback.c
    src/jit/runtime/helper_dispatch.c
    src/jit/optimizing/ssa.c
    src/jit/optimizing/pipeline.c
    src/jit/backend/x64/baseline_x64.c
)
add_library(turbojs_jit STATIC ${TURBOJS_JIT_SOURCES})
add_library(TurboJS::JIT ALIAS turbojs_jit)
target_include_directories(turbojs_jit PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/api>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/turbojs>
)
target_include_directories(turbojs_jit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(turbojs_jit PRIVATE ${turbojs_defines})
target_link_libraries(turbojs PRIVATE turbojs_jit)

# Pass a compiler definition so that Windows gets its declspec's right.
get_target_property(TURBOJS_LIB_TYPE turbojs TYPE)
if(TURBOJS_LIB_TYPE STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(turbojs
        PRIVATE BUILDING_TURBOJS_SHARED
        PUBLIC  USING_TURBOJS_SHARED
    )
endif()

# An interface library for modules.
add_library(turbojs_module INTERFACE)
add_library(TurboJS::Module ALIAS turbojs_module)
target_include_directories(turbojs_module INTERFACE
    $<TARGET_PROPERTY:turbojs,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(turbojs_module INTERFACE
    QUICKJS_NG_MODULE_BUILD
    $<TARGET_PROPERTY:turbojs,INTERFACE_COMPILE_DEFINITIONS>
)
if(WIN32)
    # Since Windows cannot resolve symbols at load time, we need to
    # explicitly link it to TurboJS.
    target_link_libraries(turbojs_module INTERFACE
        turbojs
    )
endif()

if(NOT TURBOJS_BUILD_LIBC)
    add_library(turbojs_runtime STATIC runtime/libc/turbojs-libc.c)
    add_library(TurboJS::Runtime ALIAS turbojs_runtime)
    target_compile_definitions(turbojs_runtime PRIVATE ${turbojs_defines})
    target_link_libraries(turbojs_runtime PRIVATE ${turbojs_system_libs} turbojs)
endif()

if(EMSCRIPTEN)
    add_executable(turbojs_wasm ${turbojs_sources})
    target_link_options(turbojs_wasm PRIVATE
        # in emscripten 3.x, this will be set to 16k which is too small for TurboJS.
        -sSTACK_SIZE=2097152 # let it be 2m = 2 * 1024 * 1024 = 2097152, otherwise, stack overflow may be occured at bootstrap
        -sNO_INVOKE_RUN
        -sNO_EXIT_RUNTIME
        -sMODULARIZE # do not mess the global
        -sEXPORT_ES6 # export js file to morden es module
        -sEXPORT_NAME=getQuickJs # give a name
        -sTEXTDECODER=1 # it will be 2 if we use -Oz, and that will cause js -> c string convertion fail
        -sNO_DEFAULT_TO_CXX # this project is pure c project, no need for c plus plus handle
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap
    )
    target_compile_definitions(turbojs_wasm PRIVATE ${turbojs_defines})
    target_link_libraries(turbojs_wasm PRIVATE m)
endif()


# TurboJS bytecode compiler
#

add_executable(turbojsc
    apps/turbojsc/main.c
)
add_turbojs_libc_if_needed(turbojsc)
add_static_if_needed(turbojsc)
target_compile_definitions(turbojsc PRIVATE ${turbojs_defines})
target_link_libraries(turbojsc PRIVATE turbojs)


# TurboJS CLI
#

add_executable(turbojs_cli
    src/generated/runtime/repl.c
    src/generated/runtime/standalone.c
    apps/turbojs/main.c
)
add_turbojs_libc_if_needed(turbojs_cli)
add_static_if_needed(turbojs_cli)
set_target_properties(turbojs_cli PROPERTIES
    OUTPUT_NAME "turbojs"
    PDB_NAME "turbojs_cli"
    COMPILE_PDB_NAME "turbojs_cli"
)
target_compile_definitions(turbojs_cli PRIVATE ${turbojs_defines})
target_link_libraries(turbojs_cli PRIVATE turbojs)
if (NOT WIN32)
    set_target_properties(turbojs_cli PROPERTIES ENABLE_EXPORTS TRUE)
endif()

# WASI Reactor
#

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    option(QJS_WASI_REACTOR "Build WASI reactor (exports library functions, no _start)" OFF)
    if(QJS_WASI_REACTOR)
        add_executable(turbojs_wasi
            runtime/libc/turbojs-libc.c
            apps/wasi-reactor/main.c
        )
        set_target_properties(turbojs_wasi PROPERTIES
            OUTPUT_NAME "turbojs"
            SUFFIX ".wasm"
        )
        target_compile_definitions(turbojs_wasi PRIVATE ${turbojs_defines})
        target_link_libraries(turbojs_wasi turbojs)
        target_link_options(turbojs_wasi PRIVATE
            -mexec-model=reactor
            # Export all symbols with default visibility (JS_EXTERN functions)
            -Wl,--export-dynamic
            # Memory management (libc symbols need explicit export)
            -Wl,--export=malloc
            -Wl,--export=free
            -Wl,--export=realloc
            -Wl,--export=calloc
        )
    endif()
endif()

if(TURBOJS_BUILD_CLI_WITH_MIMALLOC OR TURBOJS_BUILD_CLI_WITH_STATIC_MIMALLOC)
    find_package(mimalloc REQUIRED)
    target_compile_definitions(turbojs_cli PRIVATE QJS_USE_MIMALLOC)
    # Upstream mimalloc doesn't provide a way to know if both libraries are supported.
    if(TURBOJS_BUILD_CLI_WITH_STATIC_MIMALLOC)
        target_link_libraries(turbojs_cli PRIVATE mimalloc-static)
    else()
        target_link_libraries(turbojs_cli PRIVATE mimalloc)
    endif()
endif()

# Portable AOT inspection utility.
add_executable(turbojs-aot-inspect tools/aot/tjm_inspect.c)
target_link_libraries(turbojs-aot-inspect PRIVATE turbojs_jit)

# Tests
#
include(CTest)
option(TURBOJS_BUILD_TESTS "Build TurboJS focused tests" ON)
option(TURBOJS_BUILD_ENGINE_TESTS "Build tests that require the full engine unity unit" OFF)
option(TURBOJS_BUILD_TEST262_RUNNER "Build the Test262 runner when its source is available" OFF)

if(TURBOJS_BUILD_TESTS)
    add_executable(turbojs-ir-test tests/jit/ir_test.c)
    target_link_libraries(turbojs-ir-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.ir COMMAND turbojs-ir-test)

    add_executable(turbojs-register-pressure-test tests/jit/register_pressure_test.c)
    target_link_libraries(turbojs-register-pressure-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.register-pressure COMMAND turbojs-register-pressure-test)

    add_executable(turbojs-bytecode-branch-test tests/jit/bytecode_branch_test.c)
    target_link_libraries(turbojs-bytecode-branch-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.bytecode-branch COMMAND turbojs-bytecode-branch-test)

    add_executable(turbojs-jit-differential-test tests/jit/differential_test.c)
    target_link_libraries(turbojs-jit-differential-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.differential COMMAND turbojs-jit-differential-test)

    add_executable(turbojs-engine-bytecode-test tests/jit/engine_bytecode_test.c)
    target_include_directories(turbojs-engine-bytecode-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-engine-bytecode-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.engine-bytecode COMMAND turbojs-engine-bytecode-test)

    add_executable(turbojs-engine-locals-test tests/jit/engine_locals_test.c)
    target_include_directories(turbojs-engine-locals-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-engine-locals-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.engine-locals COMMAND turbojs-engine-locals-test)

    add_executable(turbojs-engine-control-flow-test tests/jit/engine_control_flow_test.c)
    target_include_directories(turbojs-engine-control-flow-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-engine-control-flow-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.engine-control-flow COMMAND turbojs-engine-control-flow-test)

    add_executable(turbojs-tiered-cache-test tests/jit/tiered_cache_test.c)
    target_link_libraries(turbojs-tiered-cache-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.tiered-cache COMMAND turbojs-tiered-cache-test)

    add_executable(turbojs-checked-arithmetic-test tests/jit/checked_arithmetic_test.c)
    target_link_libraries(turbojs-checked-arithmetic-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.checked-arithmetic COMMAND turbojs-checked-arithmetic-test)

    add_executable(turbojs-aot-portable-ir-test tests/jit/aot_portable_ir_test.c)
    target_link_libraries(turbojs-aot-portable-ir-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.aot.portable-ir COMMAND turbojs-aot-portable-ir-test)

    add_executable(turbojs-division-deopt-test tests/jit/division_deopt_test.c)
    target_link_libraries(turbojs-division-deopt-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.division-deopt COMMAND turbojs-division-deopt-test)

    add_executable(turbojs-deopt-frame-test tests/jit/deopt_frame_test.c)
    target_link_libraries(turbojs-deopt-frame-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.deopt-frame COMMAND turbojs-deopt-frame-test)

    add_executable(turbojs-deopt-resume-test tests/jit/deopt_resume_test.c)
    target_link_libraries(turbojs-deopt-resume-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.deopt-resume COMMAND turbojs-deopt-resume-test)

    add_executable(turbojs-gc-safe-boxing-test tests/jit/gc_safe_boxing_test.c)
    target_link_libraries(turbojs-gc-safe-boxing-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.gc-safe-boxing COMMAND turbojs-gc-safe-boxing-test)

    add_executable(turbojs-boxed-deopt-test tests/jit/boxed_deopt_test.c)
    target_link_libraries(turbojs-boxed-deopt-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.boxed-deopt COMMAND turbojs-boxed-deopt-test)

    add_executable(turbojs-stack-map-test tests/jit/stack_map_test.c)
    target_link_libraries(turbojs-stack-map-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.stack-maps COMMAND turbojs-stack-map-test)

    add_executable(turbojs-runtime-safepoint-test tests/jit/runtime_safepoint_test.c)
    target_link_libraries(turbojs-runtime-safepoint-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.runtime-safepoint COMMAND turbojs-runtime-safepoint-test)

    add_executable(turbojs-runtime-helper-test tests/jit/runtime_helper_test.c)
    target_link_libraries(turbojs-runtime-helper-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.runtime-helper COMMAND turbojs-runtime-helper-test)

    add_executable(turbojs-runtime-dispatch-test tests/jit/runtime_dispatch_test.c)
    target_link_libraries(turbojs-runtime-dispatch-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.runtime-dispatch COMMAND turbojs-runtime-dispatch-test)

    add_executable(turbojs-type-feedback-test tests/jit/type_feedback_test.c)
    target_link_libraries(turbojs-type-feedback-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.type-feedback COMMAND turbojs-type-feedback-test)

    add_executable(turbojs-ssa-optimizing-test tests/jit/ssa_optimizing_test.c)
    target_link_libraries(turbojs-ssa-optimizing-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.ssa-optimizing COMMAND turbojs-ssa-optimizing-test)

    add_executable(turbojs-ssa-cfg-test tests/jit/ssa_cfg_test.c)
    target_link_libraries(turbojs-ssa-cfg-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.ssa-cfg COMMAND turbojs-ssa-cfg-test)

    add_executable(turbojs-ssa-specialization-test tests/jit/ssa_specialization_test.c)
    target_link_libraries(turbojs-ssa-specialization-test PRIVATE turbojs_jit)

    add_executable(turbojs-completion-pipeline-test tests/jit/completion_pipeline_test.c)
    target_link_libraries(turbojs-completion-pipeline-test PRIVATE turbojs_jit)

    add_executable(turbojs-automatic-optimizing-tier-test tests/jit/automatic_optimizing_tier_test.c)
    target_link_libraries(turbojs-automatic-optimizing-tier-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.ssa-specialization COMMAND turbojs-ssa-specialization-test)
    add_test(NAME turbojs.jit.completion-pipeline COMMAND turbojs-completion-pipeline-test)
    add_test(NAME turbojs.jit.automatic-optimizing-tier COMMAND turbojs-automatic-optimizing-tier-test)

    add_custom_target(turbojs-jit-tests
        DEPENDS
            turbojs-ir-test
            turbojs-register-pressure-test
            turbojs-bytecode-branch-test
            turbojs-jit-differential-test
            turbojs-engine-bytecode-test
            turbojs-engine-locals-test
            turbojs-engine-control-flow-test
            turbojs-tiered-cache-test
            turbojs-checked-arithmetic-test
            turbojs-aot-portable-ir-test
            turbojs-division-deopt-test
            turbojs-deopt-frame-test
            turbojs-deopt-resume-test
            turbojs-gc-safe-boxing-test
            turbojs-boxed-deopt-test
            turbojs-stack-map-test
            turbojs-runtime-safepoint-test
            turbojs-runtime-helper-test
            turbojs-runtime-dispatch-test
            turbojs-type-feedback-test
            turbojs-ssa-optimizing-test
            turbojs-ssa-cfg-test
            turbojs-ssa-specialization-test
            turbojs-completion-pipeline-test
            turbojs-automatic-optimizing-tier-test
    )

    if(TURBOJS_BUILD_ENGINE_TESTS)
        add_executable(turbojs-optimization-test tests/unit/optimization_test.c)
        target_link_libraries(turbojs-optimization-test PRIVATE turbojs)
        add_test(NAME turbojs.optimization COMMAND turbojs-optimization-test)

        add_executable(turbojs-vm-jit-test tests/jit/vm_integration_test.c)
        target_link_libraries(turbojs-vm-jit-test PRIVATE turbojs)
        add_test(NAME turbojs.jit.vm-integration COMMAND turbojs-vm-jit-test)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/api_test.c")
        add_executable(api-test tests/api/api_test.c)
        target_compile_definitions(api-test PRIVATE ${turbojs_defines})
        target_link_libraries(api-test PRIVATE turbojs)
        add_test(NAME turbojs.api COMMAND api-test)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/regexp_test.c")
        add_executable(lre-test tests/unit/regexp_test.c src/regexp/regexp.c src/unicode/unicode.c)
        target_compile_definitions(lre-test PRIVATE ${turbojs_defines})
        add_test(NAME turbojs.regexp COMMAND lre-test)
    endif()
endif()

if(TURBOJS_BUILD_TEST262_RUNNER AND NOT EMSCRIPTEN)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/test262-runner/main.c")
        add_executable(run-test262 apps/test262-runner/main.c)
        add_turbojs_libc_if_needed(run-test262)
        target_compile_definitions(run-test262 PRIVATE ${turbojs_defines})
        target_link_libraries(run-test262 PRIVATE turbojs)
    else()
        message(FATAL_ERROR "TURBOJS_BUILD_TEST262_RUNNER=ON but apps/test262-runner/main.c is missing")
    endif()
endif()

# Unicode generator
#

add_executable(unicode_gen EXCLUDE_FROM_ALL
    src/unicode/unicode.c
    tools/generators/unicode/unicode_gen.c
)
target_compile_definitions(unicode_gen PRIVATE ${turbojs_defines})

add_executable(function_source
    src/generated/runtime/function_source.c
)
add_turbojs_libc_if_needed(function_source)
target_include_directories(function_source PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/api ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(function_source PRIVATE ${turbojs_defines})
target_link_libraries(function_source PRIVATE turbojs)

# Examples
#
if(TURBOJS_BUILD_EXAMPLES)
    add_executable(turbojs-optimization-profile examples/optimization_profile.c)
    target_link_libraries(turbojs-optimization-profile PRIVATE turbojs)

    add_executable(turbojs-arithmetic-jit examples/jit/arithmetic_jit.c)
    target_link_libraries(turbojs-arithmetic-jit PRIVATE turbojs_jit)

    add_executable(turbojs-vm-tiering-benchmark examples/jit/vm_tiering_benchmark.c)
    target_link_libraries(turbojs-vm-tiering-benchmark PRIVATE turbojs)
endif()

# Install target
#
if(TURBOJS_ENABLE_INSTALL)
    install(DIRECTORY src/api/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/turbojs FILES_MATCHING PATTERN "*.h")
    install(TARGETS turbojs turbojs_jit turbojs_runtime turbojsc turbojs_cli
        EXPORT TurboJSConfig
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT TurboJSConfig
        NAMESPACE TurboJS::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TurboJS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
        install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
    endif()
    install(DIRECTORY docs examples DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
