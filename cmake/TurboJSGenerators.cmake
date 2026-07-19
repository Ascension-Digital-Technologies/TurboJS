# Checked-in source generators and generator validation executables.
include_guard(GLOBAL)

# Unicode generator
#

add_executable(unicode_gen EXCLUDE_FROM_ALL
    src/unicode/unicode.c
    tools/generators/unicode/unicode_gen.c
)
target_compile_definitions(unicode_gen PRIVATE ${turbojs_defines})

add_executable(function_source
    generated/runtime/function_source.c
)
add_turbojs_libc_if_needed(function_source)
target_include_directories(function_source PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/turbojs ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(function_source PRIVATE ${turbojs_defines})
target_link_libraries(function_source PRIVATE turbojs)

# Examples
#

