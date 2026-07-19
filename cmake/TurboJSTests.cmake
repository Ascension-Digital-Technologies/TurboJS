# Focused JIT, embedding, engine, and Test262 validation targets.
include_guard(GLOBAL)

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

    add_executable(turbojs-tagged-spool-test tests/jit/tagged_spool_test.c)
    target_link_libraries(turbojs-tagged-spool-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.tagged-spool COMMAND turbojs-tagged-spool-test)

    add_executable(turbojs-spool-frame-state-test tests/jit/spool_frame_state_test.c)
    target_link_libraries(turbojs-spool-frame-state-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.spool-frame-state COMMAND turbojs-spool-frame-state-test)

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

    add_executable(turbojs-property-ssa-test tests/jit/property_ssa_test.c)
    target_include_directories(turbojs-property-ssa-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-property-ssa-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.property-ssa COMMAND turbojs-property-ssa-test)

    add_executable(turbojs-property-cse-test tests/jit/property_cse_test.c)
    target_link_libraries(turbojs-property-cse-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.property-cse COMMAND turbojs-property-cse-test)

    add_executable(turbojs-element-ssa-test tests/jit/element_ssa_test.c)
    target_link_libraries(turbojs-element-ssa-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.element-ssa COMMAND turbojs-element-ssa-test)

    add_executable(turbojs-element-loop-range-test tests/jit/element_loop_range_test.c)
    target_link_libraries(turbojs-element-loop-range-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.element-loop-range COMMAND turbojs-element-loop-range-test)

    add_executable(turbojs-element-native-loop-test tests/jit/element_native_loop_test.c)
    target_link_libraries(turbojs-element-native-loop-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.element-native-loop COMMAND turbojs-element-native-loop-test)

    add_executable(turbojs-element-native-f64-loop-test tests/jit/element_native_f64_loop_test.c)
    target_link_libraries(turbojs-element-native-f64-loop-test PRIVATE turbojs_jit ${TURBOJS_MATH_LIBRARIES})
    add_test(NAME turbojs.jit.element-native-f64-loop COMMAND turbojs-element-native-f64-loop-test)

    add_executable(turbojs-element-native-f64-transform-test tests/jit/element_native_f64_transform_test.c)
    target_link_libraries(turbojs-element-native-f64-transform-test PRIVATE turbojs_jit ${TURBOJS_MATH_LIBRARIES})
    add_test(NAME turbojs.jit.element-native-f64-transform COMMAND turbojs-element-native-f64-transform-test)
    add_executable(turbojs-element-native-f64-fusion-variants-test tests/jit/element_native_f64_fusion_variants_test.c)
    target_link_libraries(turbojs-element-native-f64-fusion-variants-test PRIVATE turbojs_jit ${TURBOJS_MATH_LIBRARIES})
    add_test(NAME turbojs.jit.element-native-f64-fusion-variants COMMAND turbojs-element-native-f64-fusion-variants-test)
    add_executable(turbojs-element-native-f64-dual-source-test tests/jit/element_native_f64_dual_source_test.c)
    target_link_libraries(turbojs-element-native-f64-dual-source-test PRIVATE turbojs_jit ${TURBOJS_MATH_LIBRARIES})
    add_test(NAME turbojs.jit.element-native-f64-dual-source COMMAND turbojs-element-native-f64-dual-source-test)

    add_executable(turbojs-element-native-f64-bounds-test tests/jit/element_native_f64_bounds_test.c)
    target_link_libraries(turbojs-element-native-f64-bounds-test PRIVATE turbojs_jit ${TURBOJS_MATH_LIBRARIES})
    add_test(NAME turbojs.jit.element-native-f64-bounds COMMAND turbojs-element-native-f64-bounds-test)

    add_executable(turbojs-engine-locals-test tests/jit/engine_locals_test.c)
    target_include_directories(turbojs-engine-locals-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-engine-locals-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.engine-locals COMMAND turbojs-engine-locals-test)

    add_executable(turbojs-engine-control-flow-test tests/jit/engine_control_flow_test.c)
    target_include_directories(turbojs-engine-control-flow-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-engine-control-flow-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.engine-control-flow COMMAND turbojs-engine-control-flow-test)

    add_executable(turbojs-engine-nonempty-stack-merge-test tests/jit/engine_nonempty_stack_merge_test.c)
    target_include_directories(turbojs-engine-nonempty-stack-merge-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-engine-nonempty-stack-merge-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.engine-nonempty-stack-merge COMMAND turbojs-engine-nonempty-stack-merge-test)

    add_executable(turbojs-engine-runtime-helper-exit-test tests/jit/engine_runtime_helper_exit_test.c)
    target_include_directories(turbojs-engine-runtime-helper-exit-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-engine-runtime-helper-exit-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.engine-runtime-helper-exit COMMAND turbojs-engine-runtime-helper-exit-test)

    add_executable(turbojs-engine-bytecode-analysis-test tests/jit/engine_bytecode_analysis_test.c)
    target_include_directories(turbojs-engine-bytecode-analysis-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-engine-bytecode-analysis-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.engine-bytecode-analysis COMMAND turbojs-engine-bytecode-analysis-test)

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

    add_executable(turbojs-runtime-helper-continuation-test tests/jit/runtime_helper_continuation_test.c)
    target_link_libraries(turbojs-runtime-helper-continuation-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.runtime-helper-continuation COMMAND turbojs-runtime-helper-continuation-test)

    add_executable(turbojs-runtime-dispatch-test tests/jit/runtime_dispatch_test.c)
    target_link_libraries(turbojs-runtime-dispatch-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.runtime-dispatch COMMAND turbojs-runtime-dispatch-test)

    add_executable(turbojs-type-feedback-test tests/jit/type_feedback_test.c)
    target_link_libraries(turbojs-type-feedback-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.type-feedback COMMAND turbojs-type-feedback-test)

    add_executable(turbojs-call-feedback-test tests/jit/call_feedback_test.c)
    target_link_libraries(turbojs-call-feedback-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.call-feedback COMMAND turbojs-call-feedback-test)

    add_executable(turbojs-native-entry-handle-test tests/jit/native_entry_handle_test.c)
    target_link_libraries(turbojs-native-entry-handle-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.native-entry-handle COMMAND turbojs-native-entry-handle-test)

    add_executable(turbojs-js-frame-abi-test tests/jit/js_frame_abi_test.c)
    target_link_libraries(turbojs-js-frame-abi-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.js-frame-abi COMMAND turbojs-js-frame-abi-test)

    add_executable(turbojs-pipeline-identity-test tests/jit/pipeline_identity_test.c)
    target_link_libraries(turbojs-pipeline-identity-test PRIVATE turbojs)
    add_test(NAME turbojs.jit.pipeline-identity COMMAND turbojs-pipeline-identity-test)

    add_executable(turbojs-runtime-optimization-config-test tests/jit/runtime_optimization_config_test.c)
    target_link_libraries(turbojs-runtime-optimization-config-test PRIVATE turbojs)
    add_test(NAME turbojs.jit.runtime-optimization-config COMMAND turbojs-runtime-optimization-config-test)

    add_executable(turbojs-clutch-call-frame-test tests/jit/clutch_call_frame_test.c)
    target_link_libraries(turbojs-clutch-call-frame-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.clutch-call-frame COMMAND turbojs-clutch-call-frame-test)

    add_executable(turbojs-clutch-compiled-call-test tests/jit/clutch_compiled_call_test.c)
    add_executable(turbojs-clutch-dependency-invalidation-test tests/jit/clutch_dependency_invalidation_test.c)
    target_link_libraries(turbojs-clutch-compiled-call-test PRIVATE turbojs_jit)
    target_link_libraries(turbojs-clutch-dependency-invalidation-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.clutch-compiled-call COMMAND turbojs-clutch-compiled-call-test)
    add_test(NAME turbojs.jit.clutch-dependency-invalidation COMMAND turbojs-clutch-dependency-invalidation-test)

    add_executable(turbojs-reverse-dependency-index-test tests/jit/reverse_dependency_index_test.c)
    target_link_libraries(turbojs-reverse-dependency-index-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.reverse-dependency-index COMMAND turbojs-reverse-dependency-index-test)

    add_executable(turbojs-self-recursive-clutch-test tests/jit/self_recursive_clutch_test.c)
    target_link_libraries(turbojs-self-recursive-clutch-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.self-recursive-clutch COMMAND turbojs-self-recursive-clutch-test)

    add_executable(turbojs-clutch-receiver-call-test tests/jit/clutch_receiver_call_test.c)
    target_link_libraries(turbojs-clutch-receiver-call-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.clutch-receiver-call COMMAND turbojs-clutch-receiver-call-test)

    add_executable(turbojs-clutch-selective-repatch-test tests/jit/clutch_selective_repatch_test.c)
    target_link_libraries(turbojs-clutch-selective-repatch-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.clutch-selective-repatch COMMAND turbojs-clutch-selective-repatch-test)

    add_executable(turbojs-callable-reference-test tests/jit/callable_reference_test.c)
    target_link_libraries(turbojs-callable-reference-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.callable-reference COMMAND turbojs-callable-reference-test)

    add_executable(turbojs-tagged-callable-reference-test tests/jit/tagged_callable_reference_test.c)
    target_link_libraries(turbojs-tagged-callable-reference-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.tagged-callable-reference COMMAND turbojs-tagged-callable-reference-test)

    add_executable(turbojs-rooted-closure-environment-test tests/jit/rooted_closure_environment_test.c)
    add_executable(turbojs-engine-callable-load-test tests/jit/engine_callable_load_test.c)
    add_executable(turbojs-engine-method-call-test tests/jit/engine_method_call_test.c)
    target_link_libraries(turbojs-engine-method-call-test PRIVATE turbojs_jit)
    target_include_directories(turbojs-engine-method-call-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME turbojs-engine-method-call-test COMMAND turbojs-engine-method-call-test)
    target_link_libraries(turbojs-engine-callable-load-test PRIVATE turbojs_jit)
    target_include_directories(turbojs-engine-callable-load-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME turbojs-engine-callable-load COMMAND turbojs-engine-callable-load-test)
    target_link_libraries(turbojs-rooted-closure-environment-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.rooted-closure-environment COMMAND turbojs-rooted-closure-environment-test)

    add_executable(turbojs-clutch-wide-call-test tests/jit/clutch_wide_call_test.c)
    target_link_libraries(turbojs-clutch-wide-call-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.clutch-wide-call COMMAND turbojs-clutch-wide-call-test)

    add_executable(turbojs-clutch-float64-call-test tests/jit/clutch_float64_call_test.c)
    target_link_libraries(turbojs-clutch-float64-call-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.clutch-float64-call COMMAND turbojs-clutch-float64-call-test)

    add_executable(turbojs-engine-bytecode-clutch-lowering-test tests/jit/engine_bytecode_clutch_lowering_test.c)
    target_include_directories(turbojs-engine-bytecode-clutch-lowering-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(turbojs-engine-bytecode-clutch-lowering-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.engine-bytecode-clutch-lowering COMMAND turbojs-engine-bytecode-clutch-lowering-test)

    add_executable(turbojs-arm64-encoder-test tests/jit/arm64_encoder_test.c)
    target_include_directories(turbojs-arm64-encoder-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(turbojs-arm64-encoder-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.arm64-encoder COMMAND turbojs-arm64-encoder-test)

    add_executable(turbojs-arm64-lowering-test tests/jit/arm64_lowering_test.c)
    target_include_directories(turbojs-arm64-lowering-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(turbojs-arm64-lowering-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.arm64-lowering COMMAND turbojs-arm64-lowering-test)

    add_executable(turbojs-downstream-api-test tests/embedding/downstream_api_test.c)
    target_link_libraries(turbojs-downstream-api-test PRIVATE turbojs)
    add_test(NAME turbojs.embedding.downstream-api COMMAND turbojs-downstream-api-test)

    add_executable(turbojs-runtime-stress-test tests/embedding/runtime_stress_test.c)
    target_link_libraries(turbojs-runtime-stress-test PRIVATE turbojs)
    add_test(NAME turbojs.runtime.lifecycle-stress COMMAND turbojs-runtime-stress-test)
    set_tests_properties(turbojs.runtime.lifecycle-stress PROPERTIES TIMEOUT 120)

    add_executable(turbojs-ssa-optimizing-test tests/jit/ssa_optimizing_test.c)
    target_link_libraries(turbojs-ssa-optimizing-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.ssa-optimizing COMMAND turbojs-ssa-optimizing-test)
    add_executable(turbojs-ssa-randomized-differential-test tests/jit/ssa_randomized_differential_test.c)
    target_link_libraries(turbojs-ssa-randomized-differential-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.ssa-randomized-differential COMMAND turbojs-ssa-randomized-differential-test)

    add_executable(turbojs-ssa-cfg-test tests/jit/ssa_cfg_test.c)
    target_link_libraries(turbojs-ssa-cfg-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.ssa-cfg COMMAND turbojs-ssa-cfg-test)

    add_executable(turbojs-ssa-specialization-test tests/jit/ssa_specialization_test.c)
    target_link_libraries(turbojs-ssa-specialization-test PRIVATE turbojs_jit)

    add_executable(turbojs-completion-pipeline-test tests/jit/completion_pipeline_test.c)
    target_link_libraries(turbojs-completion-pipeline-test PRIVATE turbojs_jit)

    add_executable(turbojs-automatic-optimizing-tier-test tests/jit/automatic_optimizing_tier_test.c)
    target_link_libraries(turbojs-automatic-optimizing-tier-test PRIVATE turbojs_jit)

    add_executable(turbojs-aot-limits-test tests/jit/aot_limits_test.c)
    target_link_libraries(turbojs-aot-limits-test PRIVATE turbojs_jit)

    add_executable(turbojs-float64-shapes-test tests/jit/float64_shapes_test.c)
    target_link_libraries(turbojs-float64-shapes-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.float64-shapes COMMAND turbojs-float64-shapes-test)

    add_executable(turbojs-float64-mixed-numeric-test tests/jit/float64_mixed_numeric_test.c)
    target_link_libraries(turbojs-float64-mixed-numeric-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.float64-mixed-numeric COMMAND turbojs-float64-mixed-numeric-test)

    add_executable(turbojs-osr-linear-scan-test tests/jit/osr_linear_scan_test.c)
    target_link_libraries(turbojs-osr-linear-scan-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.osr-linear-scan COMMAND turbojs-osr-linear-scan-test)

    add_executable(turbojs-osr-frame-moves-test tests/jit/osr_frame_moves_test.c)
    target_link_libraries(turbojs-osr-frame-moves-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.osr-frame-moves COMMAND turbojs-osr-frame-moves-test)

    add_executable(turbojs-osr-entry-test tests/jit/osr_entry_test.c)
    target_link_libraries(turbojs-osr-entry-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.osr-entry COMMAND turbojs-osr-entry-test)

    add_executable(turbojs-osr-native-loop-test tests/jit/osr_native_loop_test.c)
    target_link_libraries(turbojs-osr-native-loop-test PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.osr-native-loop COMMAND turbojs-osr-native-loop-test)

    add_executable(turbojs-tier-throughput-benchmark tests/benchmarks/tier_throughput_benchmark.c)
    target_link_libraries(turbojs-tier-throughput-benchmark PRIVATE turbojs_jit)

    add_executable(turbojs-osr-native-loop-benchmark tests/benchmarks/osr_native_loop_benchmark.c)
    target_link_libraries(turbojs-osr-native-loop-benchmark PRIVATE turbojs_jit)

    add_executable(turbojs-clutch-compiled-call-benchmark tests/benchmarks/clutch_compiled_call_benchmark.c)
    target_link_libraries(turbojs-clutch-compiled-call-benchmark PRIVATE turbojs_jit)
    add_test(NAME turbojs.jit.ssa-specialization COMMAND turbojs-ssa-specialization-test)
    add_test(NAME turbojs.jit.completion-pipeline COMMAND turbojs-completion-pipeline-test)
    add_test(NAME turbojs.jit.automatic-optimizing-tier COMMAND turbojs-automatic-optimizing-tier-test)
    add_test(NAME turbojs.jit.aot-limits COMMAND turbojs-aot-limits-test)

    add_custom_target(turbojs-jit-tests
        DEPENDS
            turbojs-ir-test
            turbojs-register-pressure-test
            turbojs-bytecode-branch-test
            turbojs-jit-differential-test
            turbojs-engine-bytecode-test
            turbojs-engine-locals-test
            turbojs-engine-control-flow-test
            turbojs-engine-bytecode-analysis-test
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
            turbojs-call-feedback-test
            turbojs-native-entry-handle-test
            turbojs-js-frame-abi-test
            turbojs-ssa-optimizing-test
            turbojs-ssa-cfg-test
            turbojs-ssa-specialization-test
            turbojs-completion-pipeline-test
            turbojs-automatic-optimizing-tier-test
            turbojs-aot-limits-test
            turbojs-float64-shapes-test
            turbojs-float64-mixed-numeric-test
            turbojs-osr-linear-scan-test
            turbojs-osr-frame-moves-test
            turbojs-osr-entry-test
            turbojs-osr-native-loop-test
            turbojs-tier-throughput-benchmark
            turbojs-osr-native-loop-benchmark
            turbojs-clutch-compiled-call-benchmark
    )

    if(TURBOJS_BUILD_ENGINE_TESTS)
        add_executable(turbojs-optimization-test tests/unit/optimization_test.c)
        target_link_libraries(turbojs-optimization-test PRIVATE turbojs)
        add_test(NAME turbojs.optimization COMMAND turbojs-optimization-test)

        add_executable(turbojs-vm-jit-test tests/jit/vm_integration_test.c)
        target_link_libraries(turbojs-vm-jit-test PRIVATE turbojs)
        add_test(NAME turbojs.jit.vm-integration COMMAND turbojs-vm-jit-test)

        if(TURBOJS_ENABLE_OPTIMIZING_JIT)
            add_executable(turbojs-vm-region-test tests/jit/vm_region_integration_test.c)
            target_link_libraries(turbojs-vm-region-test PRIVATE turbojs)
            add_test(NAME turbojs.jit.vm-region COMMAND turbojs-vm-region-test)

            add_executable(turbojs-region-observability-benchmark tests/benchmarks/region_observability_benchmark.c)
            target_link_libraries(turbojs-region-observability-benchmark PRIVATE turbojs)
        endif()

        add_executable(turbojs-spool-relay-benchmark tests/benchmarks/spool_relay_benchmark.c)
        target_link_libraries(turbojs-spool-relay-benchmark PRIVATE turbojs)

        add_executable(turbojs-vm-property-ic-test tests/jit/vm_property_ic_test.c)
        target_link_libraries(turbojs-vm-property-ic-test PRIVATE turbojs)
        add_test(NAME turbojs.vm.property-ic COMMAND turbojs-vm-property-ic-test)

        add_executable(turbojs-vm-relay-call-ic-test tests/jit/vm_relay_call_ic_test.c)
        target_link_libraries(turbojs-vm-relay-call-ic-test PRIVATE turbojs)
        add_test(NAME turbojs.vm.relay-call-ic COMMAND turbojs-vm-relay-call-ic-test)

        add_executable(turbojs-vm-spool-relay-test tests/jit/vm_spool_relay_test.c)
        target_link_libraries(turbojs-vm-spool-relay-test PRIVATE turbojs)
        add_test(NAME turbojs.vm.spool-relay COMMAND turbojs-vm-spool-relay-test)

        add_executable(turbojs-vm-clutch-float64-test tests/jit/vm_clutch_float64_test.c)
        target_link_libraries(turbojs-vm-clutch-float64-test PRIVATE turbojs)
        add_test(NAME turbojs.vm.clutch-float64 COMMAND turbojs-vm-clutch-float64-test)

        if(TURBOJS_ENABLE_OPTIMIZING_JIT)
            add_executable(turbojs-vm-call-feedback-test tests/jit/vm_call_feedback_test.c)
            target_link_libraries(turbojs-vm-call-feedback-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.call-feedback COMMAND turbojs-vm-call-feedback-test)
            add_executable(turbojs-vm-telemetry-clutch-test tests/jit/vm_telemetry_clutch_test.c)
            target_link_libraries(turbojs-vm-telemetry-clutch-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.telemetry-clutch COMMAND turbojs-vm-telemetry-clutch-test)
        endif()
        if(TURBOJS_ENABLE_OPTIMIZING_JIT)
            add_executable(turbojs-vm-osr-dense-array-test tests/jit/vm_osr_dense_array_test.c)
            target_link_libraries(turbojs-vm-osr-dense-array-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.osr-dense-array COMMAND turbojs-vm-osr-dense-array-test)
            add_executable(turbojs-vm-automatic-osr-loop-test tests/jit/vm_automatic_osr_loop_test.c)
            target_link_libraries(turbojs-vm-automatic-osr-loop-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.automatic-osr-loop COMMAND turbojs-vm-automatic-osr-loop-test)
            add_executable(turbojs-vm-osr-reentry-test tests/jit/vm_osr_reentry_test.c)
            target_link_libraries(turbojs-vm-osr-reentry-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.osr-reentry COMMAND turbojs-vm-osr-reentry-test)
            add_executable(turbojs-vm-loop-call-specialization-test tests/jit/vm_loop_and_call_specialization_test.c)
            target_link_libraries(turbojs-vm-loop-call-specialization-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.loop-call-specialization COMMAND turbojs-vm-loop-call-specialization-test)
            add_executable(turbojs-vm-collection-specialization-test tests/jit/vm_collection_specialization_test.c)
            target_link_libraries(turbojs-vm-collection-specialization-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.collection-specialization COMMAND turbojs-vm-collection-specialization-test)
            add_executable(turbojs-vm-advanced-call-specialization-test tests/jit/vm_advanced_call_specialization_test.c)
            target_link_libraries(turbojs-vm-advanced-call-specialization-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.advanced-call-specialization COMMAND turbojs-vm-advanced-call-specialization-test)
            add_executable(turbojs-vm-coupled-float-test tests/jit/vm_coupled_float_test.c)
            target_link_libraries(turbojs-vm-coupled-float-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.coupled-float COMMAND turbojs-vm-coupled-float-test)
            add_executable(turbojs-vm-grouped-accumulator-test tests/jit/vm_grouped_accumulator_test.c)
            target_link_libraries(turbojs-vm-grouped-accumulator-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.grouped-accumulator COMMAND turbojs-vm-grouped-accumulator-test)
            add_executable(turbojs-vm-callback-router-test tests/jit/vm_callback_router_test.c)
            target_link_libraries(turbojs-vm-callback-router-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.callback-router COMMAND turbojs-vm-callback-router-test)
            add_executable(turbojs-vm-application-region-test tests/jit/vm_application_region_test.c)
            target_link_libraries(turbojs-vm-application-region-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.application-region COMMAND turbojs-vm-application-region-test)
            add_executable(turbojs-vm-ast-visitor-region-test tests/jit/vm_ast_visitor_region_test.c)
            target_link_libraries(turbojs-vm-ast-visitor-region-test PRIVATE turbojs)
            add_test(NAME turbojs.vm.ast-visitor-region COMMAND turbojs-vm-ast-visitor-region-test)
        endif()

        add_executable(turbojs-vm-float64-test tests/jit/vm_float64_integration_test.c)
        target_link_libraries(turbojs-vm-float64-test PRIVATE turbojs)
        add_test(NAME turbojs.jit.vm-float64 COMMAND turbojs-vm-float64-test)

        add_dependencies(turbojs-jit-tests
            turbojs-optimization-test
            turbojs-vm-jit-test
            turbojs-spool-relay-benchmark
            turbojs-vm-property-ic-test
            turbojs-vm-relay-call-ic-test
            turbojs-vm-spool-relay-test
            turbojs-vm-clutch-float64-test
            turbojs-vm-float64-test
        )
        if(TURBOJS_ENABLE_OPTIMIZING_JIT)
            add_dependencies(turbojs-jit-tests
                turbojs-vm-call-feedback-test
                turbojs-vm-region-test
                turbojs-region-observability-benchmark
                turbojs-vm-osr-dense-array-test
                turbojs-vm-automatic-osr-loop-test
                turbojs-vm-osr-reentry-test
                turbojs-vm-loop-call-specialization-test
                turbojs-vm-collection-specialization-test
                turbojs-vm-advanced-call-specialization-test
                turbojs-vm-coupled-float-test
            )
        endif()
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
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/test262/test")
        add_custom_target(run-test262
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test262.py
                    --engine $<TARGET_FILE:turbojs_cli> --suite ${CMAKE_CURRENT_SOURCE_DIR}/third_party/test262
                    --single-variant --profile full --resume --allow-failures
                    --report ${CMAKE_BINARY_DIR}/test262-full-report.json
            DEPENDS turbojs_cli
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            USES_TERMINAL
        )
        add_custom_target(run-test262-core
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test262.py
                    --engine $<TARGET_FILE:turbojs_cli> --suite ${CMAKE_CURRENT_SOURCE_DIR}/third_party/test262
                    --single-variant --profile core --resume --allow-failures
                    --report ${CMAKE_BINARY_DIR}/test262-core-report.json
            DEPENDS turbojs_cli
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            USES_TERMINAL
        )
    else()
        message(FATAL_ERROR
            "TURBOJS_BUILD_TEST262_RUNNER=ON but third_party/test262 is missing. "
            "Run: python scripts/fetch_test262.py")
    endif()
endif()

if(TURBOJS_BUILD_TESTS)
    add_executable(turbojs-runtime-continuation-cache-test
        tests/jit/runtime_continuation_cache_test.c)
    target_link_libraries(turbojs-runtime-continuation-cache-test PRIVATE turbojs_jit)
    add_test(NAME turbojs-runtime-continuation-cache COMMAND turbojs-runtime-continuation-cache-test)

    add_executable(turbojs-continuation-dependency-owner-test
        tests/jit/continuation_dependency_owner_test.c)
    target_link_libraries(turbojs-continuation-dependency-owner-test PRIVATE turbojs_jit)
    add_test(NAME turbojs-continuation-dependency-owner COMMAND turbojs-continuation-dependency-owner-test)

    add_executable(turbojs-vault-weighted-aging-test
        tests/jit/vault_weighted_aging_test.c)
    target_link_libraries(turbojs-vault-weighted-aging-test PRIVATE turbojs_jit)
    add_test(NAME turbojs-vault-weighted-aging COMMAND turbojs-vault-weighted-aging-test)
endif()
