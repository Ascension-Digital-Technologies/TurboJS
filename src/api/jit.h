#ifndef TURBOJS_JIT_H
#define TURBOJS_JIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TURBOJS_IR_MAX_REGISTERS 64u
#define TURBOJS_IR_NO_REGISTER UINT16_MAX

typedef enum TurboJSIROpcode {
    TURBOJS_IR_NOP = 0,
    TURBOJS_IR_ARGUMENT,
    TURBOJS_IR_CONSTANT_I64,
    TURBOJS_IR_ADD_I64,
    TURBOJS_IR_SUB_I64,
    TURBOJS_IR_MUL_I64,
    TURBOJS_IR_ADD_I32_CHECKED,
    TURBOJS_IR_SUB_I32_CHECKED,
    TURBOJS_IR_MUL_I32_CHECKED,
    TURBOJS_IR_DIV_I32_CHECKED,
    TURBOJS_IR_REM_I32_CHECKED,
    TURBOJS_IR_RUNTIME_HELPER,
    TURBOJS_IR_LESS_THAN_I64,
    TURBOJS_IR_LOCAL_GET,
    TURBOJS_IR_LOCAL_SET,
    TURBOJS_IR_JUMP,
    TURBOJS_IR_BRANCH_TRUE,
    TURBOJS_IR_BRANCH_FALSE,
    TURBOJS_IR_RETURN_I64
} TurboJSIROpcode;

typedef struct TurboJSIRInstruction {
    TurboJSIROpcode opcode;
    uint16_t destination;
    uint16_t left;
    uint16_t right;
    int64_t immediate;
    uint32_t target;
    uint32_t bytecode_offset;
} TurboJSIRInstruction;

typedef struct TurboJSIRFunction {
    TurboJSIRInstruction *instructions;
    size_t instruction_count;
    size_t instruction_capacity;
    uint16_t register_count;
    uint16_t argument_count;
    uint16_t local_count;
} TurboJSIRFunction;

typedef enum TurboJSIRStatus {
    TURBOJS_IR_OK = 0,
    TURBOJS_IR_INVALID_ARGUMENT,
    TURBOJS_IR_OUT_OF_MEMORY,
    TURBOJS_IR_INVALID_OPCODE,
    TURBOJS_IR_INVALID_REGISTER,
    TURBOJS_IR_INVALID_TARGET,
    TURBOJS_IR_MISSING_RETURN,
    TURBOJS_IR_EXECUTION_LIMIT,
    TURBOJS_IR_BAILOUT,
    TURBOJS_IR_EXCEPTION,
    TURBOJS_IR_UNSUPPORTED
} TurboJSIRStatus;

typedef enum TurboJSBailoutReason {
    TURBOJS_BAILOUT_NONE = 0,
    TURBOJS_BAILOUT_INTEGER_OVERFLOW,
    TURBOJS_BAILOUT_DIVISION_BY_ZERO,
    TURBOJS_BAILOUT_DIVISION_OVERFLOW,
    TURBOJS_BAILOUT_SAFEPOINT_REQUESTED,
    TURBOJS_BAILOUT_RUNTIME_HELPER
} TurboJSBailoutReason;

typedef enum TurboJSValueKind {
    TURBOJS_VALUE_UNKNOWN = 0,
    TURBOJS_VALUE_I64,
    TURBOJS_VALUE_I32,
    TURBOJS_VALUE_BOOLEAN,
    TURBOJS_VALUE_F64,
    TURBOJS_VALUE_HEAP_REFERENCE
} TurboJSValueKind;

typedef struct TurboJSBailoutInfo {
    TurboJSBailoutReason reason;
    size_t instruction_index;
    uint32_t bytecode_offset;
} TurboJSBailoutInfo;


typedef enum TurboJSSafepointKind {
    TURBOJS_SAFEPOINT_BAILOUT = 1,
    TURBOJS_SAFEPOINT_LOOP_BACKEDGE = 2,
    TURBOJS_SAFEPOINT_RETURN = 3,
    TURBOJS_SAFEPOINT_RUNTIME_CALL = 4
} TurboJSSafepointKind;

typedef struct TurboJSStackMap {
    TurboJSSafepointKind kind;
    size_t instruction_index;
    size_t native_offset;
    uint32_t bytecode_offset;
    uint64_t live_register_mask;
    uint64_t live_local_mask;
    uint64_t reference_register_mask;
    uint64_t reference_local_mask;
} TurboJSStackMap;

typedef void (*TurboJSGCTraceCallback)(void *opaque, void *reference);
typedef void *(*TurboJSGCRelocateCallback)(void *opaque, void *reference);

typedef struct TurboJSSafepointController {
    volatile uint32_t requested;
    volatile uint32_t epoch;
} TurboJSSafepointController;

/* Conservative snapshot of machine-materialized values at a native bailout.
 * Pointers remain valid until the next invocation or function destruction. */
typedef struct TurboJSDeoptFrame {
    TurboJSBailoutInfo bailout;
    uint16_t register_count;
    uint16_t local_count;
    uint16_t stack_count;
    uint64_t materialized_register_mask;
    uint64_t materialized_local_mask;
    uint64_t live_register_mask;
    uint64_t live_local_mask;
    const int64_t *register_values;
    const int64_t *local_values;
    const TurboJSValueKind *register_kinds;
    const TurboJSValueKind *local_kinds;
} TurboJSDeoptFrame;


typedef enum TurboJSBoxedValueTag {
    TURBOJS_BOXED_UNDEFINED = 0,
    TURBOJS_BOXED_INT32,
    TURBOJS_BOXED_INT64,
    TURBOJS_BOXED_BOOLEAN,
    TURBOJS_BOXED_FLOAT64,
    TURBOJS_BOXED_HEAP_REFERENCE
} TurboJSBoxedValueTag;

typedef struct TurboJSBoxedValue {
    TurboJSBoxedValueTag tag;
    union {
        int64_t integer;
        double number;
        void *reference;
        uint64_t bits;
    } as;
} TurboJSBoxedValue;

typedef void *(*TurboJSRootRetainCallback)(void *opaque, void *reference);
typedef void (*TurboJSRootReleaseCallback)(void *opaque, void *rooted_reference);

typedef struct TurboJSRootingHooks {
    void *opaque;
    TurboJSRootRetainCallback retain;
    TurboJSRootReleaseCallback release;
} TurboJSRootingHooks;

typedef struct TurboJSBoxedDeoptFrame {
    TurboJSBailoutInfo bailout;
    uint16_t register_count;
    uint16_t local_count;
    uint16_t stack_count;
    uint64_t live_register_mask;
    uint64_t live_local_mask;
    TurboJSBoxedValue *registers;
    TurboJSBoxedValue *locals;
    TurboJSBoxedValue *stack;
    uint64_t reference_register_mask;
    uint64_t reference_local_mask;
    TurboJSRootingHooks rooting;
} TurboJSBoxedDeoptFrame;

typedef struct TurboJSNativeFunction TurboJSNativeFunction;

typedef TurboJSIRStatus (*TurboJSSlowPathCallback)(
    void *opaque,
    const TurboJSBoxedDeoptFrame *frame,
    const TurboJSIRInstruction *failed_instruction,
    TurboJSBoxedValue *result);

#define TURBOJS_RUNTIME_HELPER_LIMIT 256u

typedef TurboJSIRStatus (*TurboJSRuntimeHelperCallback)(
    void *opaque,
    const TurboJSBoxedDeoptFrame *frame,
    const TurboJSIRInstruction *call_instruction,
    TurboJSBoxedValue *result);

typedef struct TurboJSRuntimeHelperEntry {
    TurboJSRuntimeHelperCallback callback;
    void *opaque;
} TurboJSRuntimeHelperEntry;

typedef struct TurboJSRuntimeHelperTable {
    TurboJSRuntimeHelperEntry entries[TURBOJS_RUNTIME_HELPER_LIMIT];
    TurboJSRootingHooks rooting;
    uint64_t calls;
    uint64_t exceptions;
    uint64_t missing_helpers;
} TurboJSRuntimeHelperTable;

void TurboJS_RuntimeHelperTableInit(TurboJSRuntimeHelperTable *table,
                                    const TurboJSRootingHooks *rooting);
TurboJSIRStatus TurboJS_RuntimeHelperRegister(TurboJSRuntimeHelperTable *table,
                                              uint16_t helper_id,
                                              TurboJSRuntimeHelperCallback callback,
                                              void *opaque);
void TurboJS_RuntimeHelperUnregister(TurboJSRuntimeHelperTable *table,
                                     uint16_t helper_id);
TurboJSIRStatus TurboJS_NativeInvokeWithRuntime(
    const TurboJSNativeFunction *native_function,
    const TurboJSIRFunction *ir_function,
    TurboJSRuntimeHelperTable *helpers,
    const int64_t *arguments,
    size_t argument_count,
    int64_t *result);

TurboJSIRStatus TurboJS_BoxDeoptFrame(const TurboJSDeoptFrame *native_frame,
                                      TurboJSBoxedDeoptFrame *boxed_frame);
TurboJSIRStatus TurboJS_BoxDeoptFrameRooted(const TurboJSDeoptFrame *native_frame,
                                            const TurboJSRootingHooks *rooting,
                                            TurboJSBoxedDeoptFrame *boxed_frame);
void TurboJS_BoxedDeoptFrameDestroy(TurboJSBoxedDeoptFrame *frame);
TurboJSIRStatus TurboJS_IRResumeWithSlowPath(const TurboJSIRFunction *function,
                                             const TurboJSDeoptFrame *native_frame,
                                             TurboJSSlowPathCallback slow_path,
                                             void *opaque,
                                             int64_t *result);

typedef struct TurboJSIRDiagnostic {
    TurboJSIRStatus status;
    size_t instruction_index;
    const char *message;
} TurboJSIRDiagnostic;

void TurboJS_IRFunctionInit(TurboJSIRFunction *function, uint16_t argument_count);
void TurboJS_IRFunctionDestroy(TurboJSIRFunction *function);
void TurboJS_IRFunctionSetLocalCount(TurboJSIRFunction *function, uint16_t local_count);
uint16_t TurboJS_IRAllocateRegister(TurboJSIRFunction *function);
TurboJSIRStatus TurboJS_IREmit(TurboJSIRFunction *function,
                               TurboJSIRInstruction instruction);
TurboJSIRStatus TurboJS_IRVerify(const TurboJSIRFunction *function,
                                 TurboJSIRDiagnostic *diagnostic);
const char *TurboJS_IRStatusName(TurboJSIRStatus status);
const char *TurboJS_IROpcodeName(TurboJSIROpcode opcode);

TurboJSIRStatus TurboJS_IRExecute(const TurboJSIRFunction *function,
                                  const int64_t *arguments,
                                  size_t argument_count,
                                  int64_t *result);
TurboJSIRStatus TurboJS_IRResumeAfterBailout(const TurboJSIRFunction *function,
                                             const TurboJSDeoptFrame *frame,
                                             int64_t slow_path_result,
                                             int64_t *result);

TurboJSIRStatus TurboJS_BaselineCompile(const TurboJSIRFunction *function,
                                        TurboJSNativeFunction **out_function,
                                        TurboJSIRDiagnostic *diagnostic);
TurboJSIRStatus TurboJS_NativeInvoke(const TurboJSNativeFunction *function,
                                     const int64_t *arguments,
                                     size_t argument_count,
                                     int64_t *result);
void TurboJS_NativeFunctionDestroy(TurboJSNativeFunction *function);
size_t TurboJS_NativeCodeSize(const TurboJSNativeFunction *function);
TurboJSBailoutInfo TurboJS_NativeLastBailout(const TurboJSNativeFunction *function);
TurboJSDeoptFrame TurboJS_NativeLastDeoptFrame(const TurboJSNativeFunction *function);
size_t TurboJS_NativeStackMapCount(const TurboJSNativeFunction *function);
const TurboJSStackMap *TurboJS_NativeStackMapAt(const TurboJSNativeFunction *function, size_t index);
void TurboJS_TraceDeoptFrame(const TurboJSDeoptFrame *frame, TurboJSGCTraceCallback trace, void *opaque);
void TurboJS_RelocateDeoptFrame(TurboJSDeoptFrame *frame, TurboJSGCRelocateCallback relocate, void *opaque);
void TurboJS_SafepointControllerInit(TurboJSSafepointController *controller);
void TurboJS_SafepointRequest(TurboJSSafepointController *controller);
void TurboJS_SafepointClear(TurboJSSafepointController *controller);
void TurboJS_NativeSetSafepointController(TurboJSNativeFunction *function, TurboJSSafepointController *controller);


#define TURBOJS_AOT_FORMAT_VERSION 2u

typedef struct TurboJSAOTBuffer {
    uint8_t *data;
    size_t size;
} TurboJSAOTBuffer;

TurboJSIRStatus TurboJS_AOTSerializeIR(const TurboJSIRFunction *function,
                                        TurboJSAOTBuffer *out_buffer,
                                        TurboJSIRDiagnostic *diagnostic);
TurboJSIRStatus TurboJS_AOTDeserializeIR(const uint8_t *data,
                                          size_t size,
                                          TurboJSIRFunction *out_function,
                                          TurboJSIRDiagnostic *diagnostic);
void TurboJS_AOTBufferDestroy(TurboJSAOTBuffer *buffer);

/* Compact portable bytecode used by the JIT frontend and AOT pipeline. */
typedef enum TurboJSBytecodeOpcode {
    TURBOJS_BC_ARGUMENT = 0,
    TURBOJS_BC_CONSTANT_I64,
    TURBOJS_BC_ADD_I64,
    TURBOJS_BC_SUB_I64,
    TURBOJS_BC_MUL_I64,
    TURBOJS_BC_LESS_THAN_I64,
    TURBOJS_BC_JUMP,
    TURBOJS_BC_BRANCH_TRUE,
    TURBOJS_BC_RETURN_I64
} TurboJSBytecodeOpcode;

typedef struct TurboJSBytecodeInstruction {
    TurboJSBytecodeOpcode opcode;
    uint16_t destination;
    uint16_t left;
    uint16_t right;
    int64_t immediate;
    uint32_t target;
} TurboJSBytecodeInstruction;

typedef struct TurboJSBytecodeFunction {
    const TurboJSBytecodeInstruction *instructions;
    size_t instruction_count;
    uint16_t register_count;
    uint16_t argument_count;
} TurboJSBytecodeFunction;

TurboJSIRStatus TurboJS_BytecodeToIR(const TurboJSBytecodeFunction *bytecode,
                                     TurboJSIRFunction *out_ir,
                                     TurboJSIRDiagnostic *diagnostic);

/* Existing TurboJS engine bytecode bridge. This deliberately accepts the raw
 * function bytecode buffer so the public JIT library does not expose private
 * JSFunctionBytecode layout details. */
typedef struct TurboJSEngineBytecodeInfo {
    const uint8_t *bytecode;
    size_t bytecode_length;
    uint16_t argument_count;
    uint16_t local_count;
    uint16_t stack_size;
} TurboJSEngineBytecodeInfo;

TurboJSIRStatus TurboJS_EngineBytecodeToIR(const TurboJSEngineBytecodeInfo *bytecode,
                                           TurboJSIRFunction *out_ir,
                                           TurboJSIRDiagnostic *diagnostic);

/* Owning cache for compiled baseline functions. Cache keys are supplied by the
 * embedding runtime and normally identify a JSFunctionBytecode allocation. */
typedef struct TurboJSCodeCache TurboJSCodeCache;

typedef struct TurboJSCodeCacheStats {
    size_t entry_count;
    size_t code_bytes;
    uint64_t hits;
    uint64_t misses;
    uint64_t compilations;
    uint64_t evictions;
} TurboJSCodeCacheStats;

TurboJSCodeCache *TurboJS_CodeCacheCreate(size_t maximum_entries,
                                          size_t maximum_code_bytes);
void TurboJS_CodeCacheDestroy(TurboJSCodeCache *cache);
const TurboJSNativeFunction *TurboJS_CodeCacheLookup(TurboJSCodeCache *cache,
                                                     const void *key);
TurboJSIRStatus TurboJS_CodeCacheCompile(TurboJSCodeCache *cache,
                                         const void *key,
                                         const TurboJSIRFunction *ir,
                                         const TurboJSNativeFunction **out_function,
                                         TurboJSIRDiagnostic *diagnostic);
void TurboJS_CodeCacheInvalidate(TurboJSCodeCache *cache, const void *key);
void TurboJS_CodeCacheClear(TurboJSCodeCache *cache);
TurboJSCodeCacheStats TurboJS_CodeCacheGetStats(const TurboJSCodeCache *cache);


#define TURBOJS_FEEDBACK_MAX_ARGUMENTS 16u

typedef enum TurboJSFeedbackType {
    TURBOJS_FEEDBACK_NONE = 0,
    TURBOJS_FEEDBACK_INT32 = 1u << 0,
    TURBOJS_FEEDBACK_INT64 = 1u << 1,
    TURBOJS_FEEDBACK_BOOLEAN = 1u << 2,
    TURBOJS_FEEDBACK_FLOAT64 = 1u << 3,
    TURBOJS_FEEDBACK_HEAP_REFERENCE = 1u << 4,
    TURBOJS_FEEDBACK_UNDEFINED = 1u << 5
} TurboJSFeedbackType;

typedef struct TurboJSFeedbackSlot {
    uint32_t observed_types;
    uint32_t observations;
    uint32_t transitions;
} TurboJSFeedbackSlot;

typedef struct TurboJSFeedbackVector {
    TurboJSFeedbackSlot arguments[TURBOJS_FEEDBACK_MAX_ARGUMENTS];
    TurboJSFeedbackSlot result;
    uint16_t argument_count;
    uint32_t execution_count;
    uint32_t bailout_count;
    uint32_t exception_count;
} TurboJSFeedbackVector;

typedef enum TurboJSOptimizationDecision {
    TURBOJS_OPTIMIZATION_ELIGIBLE = 0,
    TURBOJS_OPTIMIZATION_TOO_COLD,
    TURBOJS_OPTIMIZATION_UNSTABLE_ARGUMENTS,
    TURBOJS_OPTIMIZATION_UNSTABLE_RESULT,
    TURBOJS_OPTIMIZATION_TOO_MANY_BAILOUTS,
    TURBOJS_OPTIMIZATION_TOO_MANY_EXCEPTIONS,
    TURBOJS_OPTIMIZATION_ARGUMENT_LIMIT
} TurboJSOptimizationDecision;

typedef struct TurboJSOptimizationPolicy {
    uint32_t minimum_executions;
    uint32_t maximum_bailouts;
    uint32_t maximum_exceptions;
    uint8_t require_stable_arguments;
    uint8_t require_stable_result;
} TurboJSOptimizationPolicy;

typedef struct TurboJSOptimizationReport {
    TurboJSOptimizationDecision decision;
    uint16_t unstable_argument;
    uint32_t executions;
    uint32_t bailouts;
    uint32_t exceptions;
} TurboJSOptimizationReport;

void TurboJS_FeedbackVectorInit(TurboJSFeedbackVector *vector, uint16_t argument_count);
void TurboJS_FeedbackObserveCall(TurboJSFeedbackVector *vector,
                                 const int64_t *arguments,
                                 size_t argument_count);
void TurboJS_FeedbackObserveResult(TurboJSFeedbackVector *vector, int64_t result);
void TurboJS_FeedbackObserveBailout(TurboJSFeedbackVector *vector);
void TurboJS_FeedbackObserveException(TurboJSFeedbackVector *vector);
uint32_t TurboJS_FeedbackClassifyInteger(int64_t value);
int TurboJS_FeedbackSlotIsStable(const TurboJSFeedbackSlot *slot);
TurboJSOptimizationPolicy TurboJS_OptimizationPolicyDefault(void);
TurboJSOptimizationReport TurboJS_EvaluateOptimization(
    const TurboJSFeedbackVector *vector,
    const TurboJSOptimizationPolicy *policy);
const char *TurboJS_OptimizationDecisionName(TurboJSOptimizationDecision decision);

typedef struct TurboJSOptimizedFunction TurboJSOptimizedFunction;

typedef enum TurboJSTieredResult {
    TURBOJS_TIERED_INTERPRETED = 0,
    TURBOJS_TIERED_BASELINE_COMPILED,
    TURBOJS_TIERED_BASELINE_NATIVE,
    TURBOJS_TIERED_OPTIMIZED_COMPILED,
    TURBOJS_TIERED_OPTIMIZED_NATIVE
} TurboJSTieredResult;

#define TURBOJS_TIERED_COMPILED TURBOJS_TIERED_BASELINE_COMPILED
#define TURBOJS_TIERED_NATIVE TURBOJS_TIERED_BASELINE_NATIVE

typedef struct TurboJSTieredStats {
    uint64_t interpreted_calls;
    uint64_t baseline_calls;
    uint64_t optimized_calls;
    uint64_t baseline_compilations;
    uint64_t optimized_compilations;
    uint64_t optimized_invalidations;
} TurboJSTieredStats;

typedef struct TurboJSTieredFunction {
    const void *cache_key;
    uint32_t call_count;
    uint32_t compile_threshold;
    uint32_t optimization_threshold;
    uint8_t compilation_attempted;
    uint8_t optimization_attempted;
    TurboJSFeedbackVector feedback;
    TurboJSOptimizationPolicy optimization_policy;
    TurboJSOptimizedFunction *optimized;
    TurboJSTieredStats stats;
} TurboJSTieredFunction;

void TurboJS_TieredFunctionInit(TurboJSTieredFunction *function,
                                const void *cache_key,
                                uint32_t compile_threshold);
void TurboJS_TieredFunctionInitAdvanced(TurboJSTieredFunction *function,
                                        const void *cache_key,
                                        uint32_t baseline_threshold,
                                        uint32_t optimization_threshold,
                                        const TurboJSOptimizationPolicy *policy);
void TurboJS_TieredFunctionInvalidateOptimized(TurboJSTieredFunction *function);
void TurboJS_TieredFunctionDestroy(TurboJSTieredFunction *function);
TurboJSTieredStats TurboJS_TieredFunctionGetStats(const TurboJSTieredFunction *function);
TurboJSIRStatus TurboJS_TieredInvoke(TurboJSTieredFunction *function,
                                     TurboJSCodeCache *cache,
                                     const TurboJSIRFunction *ir,
                                     const int64_t *arguments,
                                     size_t argument_count,
                                     int64_t *result,
                                     TurboJSTieredResult *execution_result,
                                     TurboJSIRDiagnostic *diagnostic);

#define TURBOJS_SSA_NO_VALUE UINT32_MAX

typedef enum TurboJSSSAType {
    TURBOJS_SSA_TYPE_UNKNOWN = 0,
    TURBOJS_SSA_TYPE_INT32,
    TURBOJS_SSA_TYPE_INT64,
    TURBOJS_SSA_TYPE_BOOLEAN,
    TURBOJS_SSA_TYPE_FLOAT64,
    TURBOJS_SSA_TYPE_REFERENCE
} TurboJSSSAType;

typedef enum TurboJSSSAOpcode {
    TURBOJS_SSA_NOP = 0,
    TURBOJS_SSA_ARGUMENT,
    TURBOJS_SSA_CONSTANT_I64,
    TURBOJS_SSA_ADD_I64,
    TURBOJS_SSA_SUB_I64,
    TURBOJS_SSA_MUL_I64,
    TURBOJS_SSA_LESS_THAN_I64,
    TURBOJS_SSA_PHI,
    TURBOJS_SSA_GUARD_INT32,
    TURBOJS_SSA_JUMP,
    TURBOJS_SSA_BRANCH_TRUE,
    TURBOJS_SSA_BRANCH_FALSE,
    TURBOJS_SSA_RETURN
} TurboJSSSAOpcode;

#define TURBOJS_SSA_MAX_BLOCK_EDGES 8u
#define TURBOJS_SSA_NO_BLOCK UINT32_MAX

typedef struct TurboJSSSAValue {
    TurboJSSSAOpcode opcode;
    TurboJSSSAType type;
    uint32_t id;
    uint32_t block;
    uint32_t left;
    uint32_t right;
    int64_t immediate;
    uint32_t use_count;
    uint32_t source_instruction;
    uint32_t deopt_id;
    uint8_t removed;
    uint8_t has_deopt_edge;
} TurboJSSSAValue;

typedef struct TurboJSSSABlock {
    uint32_t id;
    uint32_t first_value;
    uint32_t value_count;
    uint32_t first_instruction;
    uint32_t instruction_count;
    uint32_t predecessors[TURBOJS_SSA_MAX_BLOCK_EDGES];
    uint32_t successors[TURBOJS_SSA_MAX_BLOCK_EDGES];
    uint32_t predecessor_count;
    uint32_t successor_count;
    uint32_t immediate_dominator;
    uint32_t loop_header;
    uint64_t dominance_frontier_mask;
    uint16_t loop_depth;
    uint8_t reachable;
    uint8_t removed;
} TurboJSSSABlock;

typedef struct TurboJSSSAGraph {
    TurboJSSSAValue *values;
    size_t value_count;
    size_t value_capacity;
    TurboJSSSABlock *blocks;
    size_t block_count;
    size_t block_capacity;
    uint32_t entry_block;
    uint32_t deopt_exit_count;
} TurboJSSSAGraph;

typedef struct TurboJSSSAOptimizationStats {
    uint32_t constants_folded;
    uint32_t values_removed;
    uint32_t branches_folded;
    uint32_t blocks_removed;
    uint32_t phis_inserted;
    uint32_t guards_inserted;
} TurboJSSSAOptimizationStats;

void TurboJS_SSAGraphInit(TurboJSSSAGraph *graph);
void TurboJS_SSAGraphDestroy(TurboJSSSAGraph *graph);
TurboJSIRStatus TurboJS_SSABuildFromIR(const TurboJSIRFunction *function,
                                       TurboJSSSAGraph *graph,
                                       TurboJSIRDiagnostic *diagnostic);
TurboJSIRStatus TurboJS_SSAAddPhi(TurboJSSSAGraph *graph,
                                  uint32_t block,
                                  uint32_t left,
                                  uint32_t right,
                                  TurboJSSSAType type,
                                  uint32_t *out_value);
TurboJSIRStatus TurboJS_SSAComputeDominators(TurboJSSSAGraph *graph);
TurboJSIRStatus TurboJS_SSAComputeDominanceFrontiers(TurboJSSSAGraph *graph);
TurboJSIRStatus TurboJS_SSADetectLoops(TurboJSSSAGraph *graph);
TurboJSIRStatus TurboJS_SSASpecializeFromFeedback(
    TurboJSSSAGraph *graph,
    const TurboJSFeedbackVector *feedback,
    TurboJSSSAOptimizationStats *stats);
TurboJSSSAOptimizationStats TurboJS_SSAOptimize(TurboJSSSAGraph *graph);
int TurboJS_SSAVerify(const TurboJSSSAGraph *graph);



/* Completed v1 optimizing pipeline. The optimizing tier uses typed SSA for
 * analysis, then lowers supported graphs to the proven baseline native backend.
 * Unsupported graphs fail closed and remain on the baseline/interpreter tier. */
typedef struct TurboJSOptimizingStats {
    TurboJSSSAOptimizationStats ssa;
    uint32_t input_values;
    uint32_t output_values;
    uint32_t deopt_exits;
    size_t native_code_bytes;
} TurboJSOptimizingStats;

TurboJSIRStatus TurboJS_OptimizingCompile(const TurboJSIRFunction *function,
                                          const TurboJSFeedbackVector *feedback,
                                          TurboJSOptimizedFunction **out_function,
                                          TurboJSOptimizingStats *stats,
                                          TurboJSIRDiagnostic *diagnostic);
TurboJSIRStatus TurboJS_OptimizedInvoke(const TurboJSOptimizedFunction *function,
                                        const int64_t *arguments,
                                        size_t argument_count,
                                        int64_t *result);
void TurboJS_OptimizedFunctionDestroy(TurboJSOptimizedFunction *function);
size_t TurboJS_OptimizedCodeSize(const TurboJSOptimizedFunction *function);

#define TURBOJS_AOT_MODULE_VERSION 1u
#define TURBOJS_AOT_MAX_FUNCTIONS 1024u

typedef struct TurboJSAOTModuleFunction {
    const char *name;
    const TurboJSIRFunction *ir;
} TurboJSAOTModuleFunction;

typedef struct TurboJSAOTLoadedFunction {
    char *name;
    TurboJSIRFunction ir;
} TurboJSAOTLoadedFunction;

typedef struct TurboJSAOTModule {
    TurboJSAOTLoadedFunction *functions;
    size_t function_count;
} TurboJSAOTModule;

typedef struct TurboJSAOTModuleInfo {
    uint16_t version;
    size_t function_count;
    size_t image_size;
    uint32_t checksum;
} TurboJSAOTModuleInfo;

TurboJSIRStatus TurboJS_AOTSerializeModule(const TurboJSAOTModuleFunction *functions,
                                            size_t function_count,
                                            TurboJSAOTBuffer *out_buffer,
                                            TurboJSIRDiagnostic *diagnostic);
TurboJSIRStatus TurboJS_AOTDeserializeModule(const uint8_t *data,
                                              size_t size,
                                              TurboJSAOTModule *out_module,
                                              TurboJSIRDiagnostic *diagnostic);
const TurboJSAOTLoadedFunction *TurboJS_AOTFindFunction(const TurboJSAOTModule *module,
                                                        const char *name);
void TurboJS_AOTModuleDestroy(TurboJSAOTModule *module);
TurboJSIRStatus TurboJS_AOTInspectModule(const uint8_t *data,
                                         size_t size,
                                         TurboJSAOTModuleInfo *out_info,
                                         TurboJSIRDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif
