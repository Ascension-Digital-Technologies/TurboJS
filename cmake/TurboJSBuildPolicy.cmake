# Compiler, platform, feature-option, and sanitizer policy.
include_guard(GLOBAL)

include(CheckCCompilerFlag)
include(CheckIPOSupported)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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
xoption(TURBOJS_BUILD_CLI_STATIC "Build statically linked TurboJS command-line executables" OFF)
xoption(TURBOJS_BUILD_CLI_WITH_MIMALLOC "Build the TurboJS CLI with mimalloc" OFF)
xoption(TURBOJS_BUILD_CLI_WITH_STATIC_MIMALLOC "Build the TurboJS CLI with statically linked mimalloc" OFF)
xoption(TURBOJS_DISABLE_PARSER "Disable JS source code parser" OFF)
xoption(TURBOJS_ENABLE_ASAN "Enable AddressSanitizer (ASan)" OFF)
xoption(TURBOJS_ENABLE_MSAN "Enable MemorySanitizer (MSan)" OFF)
xoption(TURBOJS_ENABLE_TSAN "Enable ThreadSanitizer (TSan)" OFF)
xoption(TURBOJS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (UBSan)" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" OR EMSCRIPTEN)
    set(TURBOJS_OPTIMIZING_JIT_DEFAULT OFF)
else()
    set(TURBOJS_OPTIMIZING_JIT_DEFAULT ON)
endif()
xoption(TURBOJS_ENABLE_OPTIMIZING_JIT "Enable Redline optimizing JIT, Slipstream OSR, and Telemetry" ${TURBOJS_OPTIMIZING_JIT_DEFAULT})
xoption(TURBOJS_BUILD_BENCHMARKS "Build TurboJS native benchmark executables" ON)
xoption(TURBOJS_ENABLE_IPO "Enable interprocedural optimization for Release-family builds" OFF)
xoption(TURBOJS_ENABLE_NATIVE_ARCH "Optimize native Release builds for the current CPU" OFF)

set(TURBOJS_IPO_AVAILABLE OFF)
if(TURBOJS_ENABLE_IPO AND CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo|MinSizeRel")
    check_ipo_supported(RESULT TURBOJS_IPO_AVAILABLE OUTPUT TURBOJS_IPO_ERROR LANGUAGES C)
    if(TURBOJS_IPO_AVAILABLE)
        message(STATUS "TurboJS interprocedural optimization enabled")
    else()
        message(WARNING "TurboJS IPO requested but unavailable: ${TURBOJS_IPO_ERROR}")
    endif()
endif()

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


if(TURBOJS_ENABLE_NATIVE_ARCH AND CMAKE_BUILD_TYPE MATCHES "Release|RelWithDebInfo")
    if(MSVC)
        add_compile_options(/arch:AVX2)
    elseif(NOT CMAKE_CROSSCOMPILING AND NOT CMAKE_SYSTEM_NAME STREQUAL "WASI" AND NOT EMSCRIPTEN)
        xcheck_add_c_compiler_flag(-march=native)
        xcheck_add_c_compiler_flag(-mtune=native)
    endif()
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

