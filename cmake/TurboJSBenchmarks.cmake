# Standalone native benchmark targets.
include_guard(GLOBAL)

if(NOT TURBOJS_BUILD_BENCHMARKS)
    return()
endif()

function(turbojs_add_jit_benchmark target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE turbojs_jit)
endfunction()

turbojs_add_jit_benchmark(
    turbojs-runtime-continuation-cache-benchmark
    benchmarks/jit/runtime_continuation_cache_benchmark.c)
turbojs_add_jit_benchmark(
    turbojs-element-native-benchmark
    benchmarks/jit/element_native_benchmark.c)
turbojs_add_jit_benchmark(
    turbojs-element-native-loop-benchmark
    benchmarks/jit/element_native_loop_benchmark.c)
turbojs_add_jit_benchmark(
    turbojs-element-native-unrolled-loop-benchmark
    benchmarks/jit/element_native_unrolled_loop_benchmark.c)
turbojs_add_jit_benchmark(
    turbojs-element-simd-benchmark
    benchmarks/jit/element_simd_benchmark.c)

add_executable(
    turbojs-element-simd-bounds-benchmark
    benchmarks/jit/element_simd_bounds_benchmark.c)
target_include_directories(turbojs-element-simd-bounds-benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/jit/backend/x64)
target_link_libraries(turbojs-element-simd-bounds-benchmark PRIVATE turbojs_jit ${TURBOJS_MATH_LIBRARIES})
