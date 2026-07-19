# TurboJS execution-pipeline source manifest.
# Keep subsystem ownership visible here instead of growing the root build file.

set(TURBOJS_JIT_FRONTEND_SOURCES
    src/jit/frontend/bytecode_to_ir.c
    src/jit/frontend/engine_bytecode_to_ir.c
    src/jit/frontend/engine_bytecode_analysis.c
    src/jit/frontend/engine_bytecode_cfg.c
    src/jit/frontend/engine_bytecode_region.c
    src/jit/frontend/engine_bytecode_state.c
    src/jit/frontend/spool_frame_state.c
    src/jit/frontend/engine_bytecode_ssa.c
    src/jit/frontend/property_feedback.c
)

set(TURBOJS_JIT_AOT_SOURCES
    src/jit/aot/portable_ir.c
    src/jit/aot/module.c
)

set(TURBOJS_JIT_IR_SOURCES
    src/jit/ir/ir.c
    src/jit/ir/ir_verify.c
    src/jit/ir/ir_interpreter.c
    src/jit/ir/tagged_ir.c
    src/jit/ir/callable_specialize.c
    src/jit/ir/deopt_boxing.c
)

set(TURBOJS_JIT_RUNTIME_SOURCES
    src/jit/runtime/executable_memory.c
    src/jit/runtime/code_cache.c
    src/jit/runtime/tiered_execution.c
    src/jit/runtime/type_feedback.c
    src/jit/runtime/call_feedback.c
    src/jit/runtime/callable_reference.c
    src/jit/runtime/js_frame_abi.c
    src/jit/runtime/helpers/table.c
    src/jit/runtime/helpers/invoke.c
    src/jit/runtime/helpers/native_continuation.c
    src/jit/runtime/helpers/continuation.c
    src/jit/runtime/helpers/continuation_cache.c
    src/jit/runtime/osr.c
    src/jit/runtime/osr_frame.c
    src/jit/runtime/osr_entry.c
    src/jit/runtime/osr_loop.c
)

set(TURBOJS_JIT_OPTIMIZING_SOURCES
    src/jit/optimizing/ssa.c
    src/jit/optimizing/pipeline.c
    src/jit/optimizing/linear_scan.c
    src/jit/optimizing/parallel_moves.c
)

set(TURBOJS_JIT_X64_SOURCES
    src/jit/backend/x64/baseline_x64.c
    src/jit/backend/x64/region_x64.c
    src/jit/backend/x64/simd_kernels.c
    src/jit/backend/x64/region_x64_stack.c
)

set(TURBOJS_JIT_ARM64_SOURCES
    src/jit/backend/arm64/arm64_encoder.c
    src/jit/backend/arm64/arm64_lowering.c
)

set(TURBOJS_JIT_OBJECT_SOURCES
    src/objects/shapes.c
)

set(TURBOJS_JIT_SOURCES
    ${TURBOJS_JIT_FRONTEND_SOURCES}
    ${TURBOJS_JIT_AOT_SOURCES}
    ${TURBOJS_JIT_IR_SOURCES}
    ${TURBOJS_JIT_RUNTIME_SOURCES}
    ${TURBOJS_JIT_OPTIMIZING_SOURCES}
    ${TURBOJS_JIT_X64_SOURCES}
    ${TURBOJS_JIT_ARM64_SOURCES}
    ${TURBOJS_JIT_OBJECT_SOURCES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source" FILES ${TURBOJS_JIT_SOURCES})
