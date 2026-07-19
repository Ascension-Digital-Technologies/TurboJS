# Engine libraries, command-line programs, WASM/WASI products, and developer tools.
include_guard(GLOBAL)

# TurboJS library
#

xoption(TURBOJS_BUILD_LIBC "Build standard library modules as part of the library" OFF)
macro(add_turbojs_libc_if_needed target)
    if(NOT TURBOJS_BUILD_LIBC)
        target_link_libraries(${target} PRIVATE turbojs_runtime)
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

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(turbojs-architecture-check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validation/check_architecture.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking TurboJS engine subsystem boundaries"
    VERBATIM
)
add_custom_target(turbojs-legacy-identity-check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validation/check_legacy_identity.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking first-party source for stale upstream-engine identity"
    VERBATIM
)

add_custom_target(turbojs-regenerate-engine
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generators/generate_engine_unit.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Regenerating checked-in TurboJS engine unit"
    VERBATIM
)


# Deliberately avoid CONFIGURE_DEPENDS here. On Windows/Ninja, the generated
# VerifyGlobs rule can enter an endless CMake regeneration loop when the build
# directory lives inside the source tree. Developer scripts run configure before
# builds, so newly added source files are still discovered deterministically.
file(GLOB_RECURSE TURBOJS_ENGINE_DOMAIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.c
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
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/generators/generate_engine_unit.py
            --output ${TURBOJS_ENGINE_GENERATED}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TurboJSSubsystems.json
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
    src/core/runtime_config_api.c
    src/jit/optimization_api.c
    src/jit/runtime/pipeline_identity.c
    src/core/context_access_api.c
    src/vm/job_api.c
    src/core/lifecycle_api.c
    src/vm/job_enqueue_api.c
    src/objects/class_proto_api.c
    src/gc/exception_state_api.c
    src/builtins/promise_hooks_api.c
    src/gc/memory_diagnostics_api.c
    src/modules/module_api.c
    src/api/turbojs_embed.c
)

if(TURBOJS_BUILD_LIBC)
    list(APPEND turbojs_sources src/api/turbojs-libc.c)
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

# Link libm only on platforms that provide it as a separate library.
# The Microsoft C runtime already includes these functions and has no m.lib.
set(TURBOJS_MATH_LIBRARIES)
find_library(M_LIBRARIES m)
if(M_LIBRARIES)
    set(TURBOJS_MATH_LIBRARIES ${M_LIBRARIES})
elseif(CMAKE_C_COMPILER_ID STREQUAL "TinyCC")
    set(TURBOJS_MATH_LIBRARIES m)
endif()
list(APPEND turbojs_system_libs ${TURBOJS_MATH_LIBRARIES})

add_library(turbojs ${turbojs_sources})
add_library(TurboJS::Engine ALIAS turbojs)
target_compile_definitions(turbojs PRIVATE ${turbojs_defines})
target_include_directories(turbojs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(turbojs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/turbojs>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/turbojs>
)
target_link_libraries(turbojs PUBLIC ${turbojs_system_libs})

# Tiered execution infrastructure. Kept as a focused library so IR/JIT tests
# can build quickly without recompiling the large engine unity translation unit.
include(TurboJSJITSources)
add_library(turbojs_jit STATIC ${TURBOJS_JIT_SOURCES})
add_library(TurboJS::JIT ALIAS turbojs_jit)
target_include_directories(turbojs_jit PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/turbojs>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/turbojs>
)
target_include_directories(turbojs_jit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(turbojs_jit PRIVATE ${turbojs_defines})
target_link_libraries(turbojs PRIVATE turbojs_jit)

if(TURBOJS_IPO_AVAILABLE)
    set_property(TARGET turbojs turbojs_jit PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

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
    TURBOJS_MODULE_BUILD
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
    add_library(turbojs_runtime STATIC src/api/turbojs-libc.c)
    add_library(TurboJS::Runtime ALIAS turbojs_runtime)
    target_compile_definitions(turbojs_runtime PRIVATE ${turbojs_defines})
    target_include_directories(turbojs_runtime PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/turbojs
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
    )
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
        -sEXPORT_NAME=getTurboJS # give a name
        -sTEXTDECODER=1 # it will be 2 if we use -Oz, and that will cause js -> c string convertion fail
        -sNO_DEFAULT_TO_CXX # this project is pure c project, no need for c plus plus handle
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap
    )
    target_compile_definitions(turbojs_wasm PRIVATE ${turbojs_defines})
    target_link_libraries(turbojs_wasm PRIVATE ${TURBOJS_MATH_LIBRARIES})
endif()


# TurboJS bytecode compiler
#

add_executable(turbojsc
    apps/turbojsc/main.c
)
add_turbojs_libc_if_needed(turbojsc)
add_static_if_needed(turbojsc)
target_compile_definitions(turbojsc PRIVATE ${turbojs_defines})
target_include_directories(turbojsc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/internal
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(turbojsc PRIVATE turbojs)


# TurboJS CLI
#

add_executable(turbojs_cli
    generated/runtime/repl.c
    generated/runtime/standalone.c
    apps/turbojs/main.c
)
target_include_directories(turbojs_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_turbojs_libc_if_needed(turbojs_cli)
add_static_if_needed(turbojs_cli)
set_target_properties(turbojs_cli PROPERTIES
    OUTPUT_NAME "turbojs"
    PDB_NAME "turbojs_cli"
    COMPILE_PDB_NAME "turbojs_cli"
)
target_compile_definitions(turbojs_cli PRIVATE ${turbojs_defines})
target_link_libraries(turbojs_cli PRIVATE turbojs)
if(TURBOJS_IPO_AVAILABLE)
    set_property(TARGET turbojs_cli PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
if (NOT WIN32)
    set_target_properties(turbojs_cli PROPERTIES ENABLE_EXPORTS TRUE)
endif()

# WASI Reactor
#

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    option(TURBOJS_WASI_REACTOR "Build WASI reactor (exports library functions, no _start)" OFF)
    if(TURBOJS_WASI_REACTOR)
        add_executable(turbojs_wasi
            src/api/turbojs-libc.c
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
    target_compile_definitions(turbojs_cli PRIVATE TURBOJS_USE_MIMALLOC)
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

