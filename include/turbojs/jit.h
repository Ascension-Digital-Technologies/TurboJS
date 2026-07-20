#ifndef TURBOJS_JIT_H
#define TURBOJS_JIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TURBOJS_IR_MAX_REGISTERS 64u
#define TURBOJS_IR_NO_REGISTER UINT16_MAX

typedef struct TurboJSClutchCallSite TurboJSClutchCallSite;
typedef struct TurboJSCallableReference TurboJSCallableReference;
typedef struct TurboJSIRDiagnostic TurboJSIRDiagnostic;

typedef enum TurboJSIROpcode {
    TURBOJS_IR_NOP = 0,
    TURBOJS_IR_ARGUMENT,
    TURBOJS_IR_CONSTANT_I64,
    TURBOJS_IR_CONSTANT_F64,
    TURBOJS_IR_ADD_I64,
    TURBOJS_IR_SUB_I64,
    TURBOJS_IR_MUL_I64,
    TURBOJS_IR_ADD_F64,
    TURBOJS_IR_SUB_F64,
    TURBOJS_IR_MUL_F64,
    TURBOJS_IR_DIV_F64,
    TURBOJS_IR_LESS_THAN_F64,
    TURBOJS_IR_LESS_EQUAL_F64,
    TURBOJS_IR_EQUAL_F64,
    TURBOJS_IR_I64_TO_F64,
    TURBOJS_IR_F64_TO_I64_TRUNC,
    TURBOJS_IR_ADD_I32_CHECKED,
    TURBOJS_IR_SUB_I32_CHECKED,
    TURBOJS_IR_MUL_I32_CHECKED,
    TURBOJS_IR_DIV_I32_CHECKED,
    TURBOJS_IR_REM_I32_CHECKED,
    TURBOJS_IR_RUNTIME_HELPER,
    /* Spool tagged baseline operations. These preserve full JavaScript values
     * instead of forcing the frontend to specialize every value numerically. */
    TURBOJS_IR_VALUE_ARGUMENT,
    TURBOJS_IR_VALUE_UNDEFINED,
    TURBOJS_IR_VALUE_CONSTANT_I32,
    TURBOJS_IR_VALUE_MOVE,
    TURBOJS_IR_VALUE_LOCAL_GET,
    TURBOJS_IR_VALUE_LOCAL_SET,
    TURBOJS_IR_VALUE_TO_BOOLEAN,
    TURBOJS_IR_VALUE_CALLABLE_CONSTANT,
    TURBOJS_IR_VALUE_CALL_I64,
    TURBOJS_IR_VALUE_CALL_F64,
    TURBOJS_IR_VALUE_RETURN,
    TURBOJS_IR_LESS_THAN_I64,
    TURBOJS_IR_LOCAL_GET,
    TURBOJS_IR_LOCAL_SET,
    TURBOJS_IR_JUMP,
    TURBOJS_IR_BRANCH_TRUE,
    TURBOJS_IR_BRANCH_FALSE,
    TURBOJS_IR_CALL_NATIVE_I64,
    TURBOJS_IR_CALL_NATIVE_F64,
    TURBOJS_IR_RETURN_I64,
    TURBOJS_IR_RETURN_F64
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
    /* Source-visible locals before hidden Spool frame slots are appended. */
    uint16_t source_local_count;
    /* Optional value-kind hints used by native backends for precise GC maps. */
    uint8_t register_kind_hints[TURBOJS_IR_MAX_REGISTERS];
    uint8_t local_kind_hints[TURBOJS_IR_MAX_REGISTERS];
    /* IR-owned Clutch call sites emitted by automatic bytecode lowering. */
    TurboJSClutchCallSite **owned_clutch_sites;
    size_t owned_clutch_site_count;
    size_t owned_clutch_site_capacity;
    /* IR-owned rooted callable references emitted by engine bytecode lowering. */
    TurboJSCallableReference **owned_callable_references;
    size_t owned_callable_reference_count;
    size_t owned_callable_reference_capacity;
    /* Stable identity and mutation generation for runtime continuation caches. */
    uint64_t instance_id;
    uint64_t revision;
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
    TURBOJS_VALUE_HEAP_REFERENCE,
    TURBOJS_VALUE_CALLABLE_REFERENCE
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
    TURBOJS_SAFEPOINT_RUNTIME_CALL = 4,
    TURBOJS_SAFEPOINT_CLUTCH_CALL = 5
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
    TURBOJS_BOXED_HEAP_REFERENCE,
    TURBOJS_BOXED_CALLABLE_REFERENCE
} TurboJSBoxedValueTag;

typedef struct TurboJSBoxedValue {
    TurboJSBoxedValueTag tag;
    union {
        int64_t integer;
        double number;
        void *reference;
        const struct TurboJSCallableReference *callable;
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


/* Dynamically sized Spool frame state used while lowering general bytecode.
 * Values use TurboJSBoxedValue until the VM bridge supplies its exact tagged
 * JSValue adapter. The state mirrors Pulse locals and operand-stack slots. */
typedef struct TurboJSSpoolFrameState {
    TurboJSBoxedValue *locals;
    TurboJSBoxedValue *stack;
    uint32_t local_count;
    uint32_t stack_count;
    uint32_t stack_capacity;
    uint32_t bytecode_offset;
} TurboJSSpoolFrameState;

TurboJSIRStatus TurboJS_SpoolFrameStateInit(TurboJSSpoolFrameState *state,
                                             uint32_t local_count,
                                             uint32_t stack_capacity);
void TurboJS_SpoolFrameStateDestroy(TurboJSSpoolFrameState *state);
TurboJSIRStatus TurboJS_SpoolFrameStateClone(const TurboJSSpoolFrameState *source,
                                              TurboJSSpoolFrameState *destination);
TurboJSIRStatus TurboJS_SpoolFrameStatePush(TurboJSSpoolFrameState *state,
                                             TurboJSBoxedValue value);
TurboJSIRStatus TurboJS_SpoolFrameStatePop(TurboJSSpoolFrameState *state,
                                            TurboJSBoxedValue *value);
TurboJSIRStatus TurboJS_SpoolFrameStateSetLocal(TurboJSSpoolFrameState *state,
                                                 uint32_t index,
                                                 TurboJSBoxedValue value);
TurboJSIRStatus TurboJS_SpoolFrameStateGetLocal(const TurboJSSpoolFrameState *state,
                                                 uint32_t index,
                                                 TurboJSBoxedValue *value);
int TurboJS_SpoolFrameStateShapeCompatible(const TurboJSSpoolFrameState *left,
                                            const TurboJSSpoolFrameState *right);

/* Execute the tagged subset of Spool IR. This portable interpreter is the
 * semantic oracle for future x64/ARM64 tagged lowering. */
TurboJSIRStatus TurboJS_IRExecuteTagged(const TurboJSIRFunction *function,
                                         const TurboJSBoxedValue *arguments,
                                         size_t argument_count,
                                         TurboJSBoxedValue *result);
/* Specialize a straight-line tagged CallableRef wrapper into ordinary
 * Clutch/native IR. Returns TURBOJS_IR_UNSUPPORTED when dynamic tagged
 * semantics remain. The output owns its generated Clutch metadata. */
TurboJSIRStatus TurboJS_IRSpecializeCallableReferences(
    const TurboJSIRFunction *input, TurboJSIRFunction *output,
    TurboJSIRDiagnostic *diagnostic);

typedef struct TurboJSNativeFunction TurboJSNativeFunction;

/*
 * Clutch native-entry contract.
 *
 * Vault owns the native allocation. The handle is published only while the
 * corresponding cache entry is live and is invalidated before code memory is
 * released. Relay caches the generation, never the raw code pointer.
 */
typedef enum TurboJSNativeEntryKind {
    TURBOJS_NATIVE_ENTRY_NONE = 0,
    TURBOJS_NATIVE_ENTRY_INT32 = 1,
    TURBOJS_NATIVE_ENTRY_FLOAT64 = 2
} TurboJSNativeEntryKind;

typedef struct TurboJSNativeEntryHandle {
    const TurboJSNativeFunction *function;
    uint64_t generation;
    uint16_t argument_count;
    uint8_t kind;
    uint8_t result_kind;
} TurboJSNativeEntryHandle;

void TurboJS_NativeEntryHandleInit(TurboJSNativeEntryHandle *handle);
void TurboJS_NativeEntryHandleInvalidate(TurboJSNativeEntryHandle *handle);
int TurboJS_NativeEntryHandleIsLive(const TurboJSNativeEntryHandle *handle,
                                    uint64_t expected_generation,
                                    TurboJSNativeEntryKind expected_kind);
TurboJSIRStatus TurboJS_NativeEntryInvokeI64(
    const TurboJSNativeEntryHandle *handle,
    uint64_t expected_generation,
    const int64_t *arguments,
    size_t argument_count,
    int64_t *result);
size_t TurboJS_NativeInvalidateClutchTarget(
    TurboJSNativeFunction *function,
    const TurboJSNativeEntryHandle *target);
size_t TurboJS_NativeRepatchClutchIdentity(
    TurboJSNativeFunction *function, uint64_t target_identity,
    const TurboJSNativeEntryHandle *target, uint64_t generation,
    TurboJSNativeEntryKind kind, uint16_t argument_count,
    size_t *incompatible);
size_t TurboJS_NativeClutchSiteCount(const TurboJSNativeFunction *function);
const TurboJSNativeEntryHandle *TurboJS_NativeClutchSiteTargetAt(
    const TurboJSNativeFunction *function, size_t index);
uint64_t TurboJS_NativeClutchSiteIdentityAt(
    const TurboJSNativeFunction *function, size_t index);
TurboJSIRStatus TurboJS_NativeEntryInvokeF64(
    const TurboJSNativeEntryHandle *handle,
    uint64_t expected_generation,
    const double *arguments,
    size_t argument_count,
    double *result);

/*
 * Clutch compiled-call frame. This is the stable hand-off record used by
 * Spool callers before a direct native edge is emitted by Gearbox. The frame
 * deliberately owns no code pointer: generation validation remains in Vault.
 */
typedef enum TurboJSClutchCallFlags {
    TURBOJS_CLUTCH_CALL_NONE = 0,
    TURBOJS_CLUTCH_CALL_TAIL = 1u << 0,
    TURBOJS_CLUTCH_CALL_RECURSIVE = 1u << 1,
    TURBOJS_CLUTCH_CALL_HAS_ENVIRONMENT = 1u << 2,
    TURBOJS_CLUTCH_CALL_HAS_RECEIVER = 1u << 3
} TurboJSClutchCallFlags;

typedef struct TurboJSClutchCallFrame {
    const TurboJSNativeEntryHandle *target;
    uint64_t expected_generation;
    const void *closure_environment;
    const void *safepoint;
    size_t argument_count;
    uint64_t caller_saved_gpr_mask;
    uint64_t caller_saved_fpr_mask;
    uint32_t flags;
    uint32_t reserved;
} TurboJSClutchCallFrame;

void TurboJS_ClutchCallFrameInit(TurboJSClutchCallFrame *frame);
TurboJSIRStatus TurboJS_ClutchCallI64(
    const TurboJSClutchCallFrame *frame, const int64_t *arguments,
    int64_t *result);

#define TURBOJS_CLUTCH_MAX_ARGUMENTS 4u
#define TURBOJS_CLUTCH_CALL_HAS_RECEIVER_SHAPE_GUARD 0x10u

struct TurboJSClutchCallSite {
    const TurboJSNativeEntryHandle *target;
    uint64_t expected_generation;
    uint16_t argument_count;
    uint8_t expected_kind;
    uint8_t flags;
    uint32_t continuation_bytecode_offset;
    const void *closure_environment;
    TurboJSRootingHooks environment_rooting;
    uint8_t owns_environment;
    uint8_t environment_reserved[7];
    uint64_t target_identity;
    uint64_t receiver_shape_identity;
    uint32_t receiver_shape_offset;
    uint32_t receiver_guard_reserved;
    uint16_t receiver_register;
    uint16_t argument_registers[TURBOJS_CLUTCH_MAX_ARGUMENTS];
};

void TurboJS_ClutchCallSiteInit(TurboJSClutchCallSite *site,
                                const TurboJSNativeEntryHandle *target,
                                uint64_t expected_generation,
                                TurboJSNativeEntryKind expected_kind,
                                uint16_t argument_count);
void TurboJS_ClutchCallSiteSetTargetIdentity(
    TurboJSClutchCallSite *site, uint64_t target_identity);
TurboJSIRStatus TurboJS_ClutchCallSiteSetClosureEnvironment(
    TurboJSClutchCallSite *site, void *closure_environment,
    const TurboJSRootingHooks *rooting);
TurboJSIRStatus TurboJS_ClutchCallSiteClone(
    TurboJSClutchCallSite *destination, const TurboJSClutchCallSite *source);
void TurboJS_ClutchCallSiteDestroy(TurboJSClutchCallSite *site);
TurboJSIRStatus TurboJS_ClutchCallSiteSetArgument(
    TurboJSClutchCallSite *site, uint16_t argument_index,
    uint16_t source_register);
TurboJSIRStatus TurboJS_ClutchCallSiteSetReceiver(
    TurboJSClutchCallSite *site, uint16_t source_register);
TurboJSIRStatus TurboJS_ClutchCallSiteSetReceiverShapeGuard(
    TurboJSClutchCallSite *site, uint32_t shape_offset,
    uint64_t expected_shape_identity);
TurboJSIRStatus TurboJS_ClutchCallSiteInvokeI64(
    const TurboJSClutchCallSite *site, const int64_t *arguments,
    int64_t *result);
TurboJSIRStatus TurboJS_ClutchCallSiteInvokeF64(
    const TurboJSClutchCallSite *site, const double *arguments,
    double *result);
TurboJSIRStatus TurboJS_ClutchCallF64(
    const TurboJSClutchCallFrame *frame, const double *arguments,
    double *result);


typedef struct TurboJSCallableReference {
    uint64_t target_identity;
    const TurboJSNativeEntryHandle *target;
    uint64_t expected_generation;
    const void *closure_environment;
    TurboJSRootingHooks environment_rooting;
    uint16_t argument_count;
    uint8_t expected_kind;
    uint8_t flags;
    uint8_t owns_environment;
} TurboJSCallableReference;

void TurboJS_CallableReferenceInit(
    TurboJSCallableReference *reference, uint64_t target_identity,
    const TurboJSNativeEntryHandle *target, uint64_t expected_generation,
    TurboJSNativeEntryKind expected_kind, uint16_t argument_count,
    const void *closure_environment);
TurboJSIRStatus TurboJS_CallableReferenceInitRooted(
    TurboJSCallableReference *reference, uint64_t target_identity,
    const TurboJSNativeEntryHandle *target, uint64_t expected_generation,
    TurboJSNativeEntryKind expected_kind, uint16_t argument_count,
    void *closure_environment, const TurboJSRootingHooks *rooting);
void TurboJS_CallableReferenceDestroy(TurboJSCallableReference *reference);
int TurboJS_CallableReferenceIsLive(const TurboJSCallableReference *reference);
TurboJSIRStatus TurboJS_CallableReferenceInvokeI64(
    const TurboJSCallableReference *reference, const int64_t *arguments,
    size_t argument_count, int64_t *result);
TurboJSIRStatus TurboJS_CallableReferenceInvokeF64(
    const TurboJSCallableReference *reference, const double *arguments,
    size_t argument_count, double *result);

typedef TurboJSIRStatus (*TurboJSSlowPathCallback)(
    void *opaque,
    const TurboJSBoxedDeoptFrame *frame,
    const TurboJSIRInstruction *failed_instruction,
    TurboJSBoxedValue *result);

typedef struct TurboJSCodeCache TurboJSCodeCache;

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
    uint64_t native_continuation_compiles;
    uint64_t native_continuation_entries;
    uint64_t native_continuation_fallbacks;
    uint64_t native_continuation_cache_hits;
    uint64_t native_continuation_cache_misses;
    uint64_t native_continuation_cache_evictions;
    void *native_continuation_cache; /* legacy private fallback */
    TurboJSCodeCache *continuation_vault;
} TurboJSRuntimeHelperTable;

void TurboJS_RuntimeHelperTableInit(TurboJSRuntimeHelperTable *table,
                                    const TurboJSRootingHooks *rooting);
void TurboJS_RuntimeHelperTableDestroy(TurboJSRuntimeHelperTable *table);
void TurboJS_RuntimeHelperContinuationCacheClear(TurboJSRuntimeHelperTable *table);
void TurboJS_RuntimeHelperAttachContinuationVault(TurboJSRuntimeHelperTable *table,
                                                  TurboJSCodeCache *vault);
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
TurboJSIRStatus TurboJS_IRResumeWithRuntimeHelpers(
    const TurboJSIRFunction *ir_function,
    const TurboJSDeoptFrame *native_frame,
    TurboJSRuntimeHelperTable *helpers,
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
void TurboJS_IRFunctionSetRegisterKind(TurboJSIRFunction *function, uint16_t reg, TurboJSValueKind kind);
void TurboJS_IRFunctionSetLocalKind(TurboJSIRFunction *function, uint16_t local, TurboJSValueKind kind);
TurboJSValueKind TurboJS_IRFunctionRegisterKind(const TurboJSIRFunction *function, uint16_t reg);
TurboJSValueKind TurboJS_IRFunctionLocalKind(const TurboJSIRFunction *function, uint16_t local);
uint16_t TurboJS_IRAllocateRegister(TurboJSIRFunction *function);
TurboJSIRStatus TurboJS_IREmit(TurboJSIRFunction *function,
                               TurboJSIRInstruction instruction);
TurboJSClutchCallSite *TurboJS_IRAllocateClutchCallSite(
    TurboJSIRFunction *function);
TurboJSCallableReference *TurboJS_IRAllocateCallableReference(
    TurboJSIRFunction *function);
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
TurboJSIRStatus TurboJS_IRExecuteF64(const TurboJSIRFunction *function,
                                    const double *arguments,
                                    size_t argument_count,
                                    double *result);
TurboJSIRStatus TurboJS_NativeInvokeF64(const TurboJSNativeFunction *function,
                                        const double *arguments,
                                        size_t argument_count,
                                        double *result);
void TurboJS_NativeFunctionDestroy(TurboJSNativeFunction *function);
size_t TurboJS_NativeCodeSize(const TurboJSNativeFunction *function);
TurboJSValueKind TurboJS_NativeResultKind(const TurboJSNativeFunction *function);
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


/* Compact object-shape and monomorphic inline-cache API. Shapes are immutable
 * after creation; transitions produce canonical child shapes. */
#define TURBOJS_SHAPE_MAX_PROPERTIES 64u
typedef struct TurboJSShapeTable TurboJSShapeTable;
typedef struct TurboJSShape TurboJSShape;
typedef struct TurboJSPropertyInlineCache {
    uint32_t shape_id;
    uint16_t property_offset;
    uint16_t hits;
    uint32_t misses;
} TurboJSPropertyInlineCache;

TurboJSShapeTable *TurboJS_ShapeTableCreate(void);
void TurboJS_ShapeTableDestroy(TurboJSShapeTable *table);
const TurboJSShape *TurboJS_ShapeRoot(const TurboJSShapeTable *table);
const TurboJSShape *TurboJS_ShapeTransition(TurboJSShapeTable *table,
                                            const TurboJSShape *shape,
                                            const char *property_name);
uint32_t TurboJS_ShapeId(const TurboJSShape *shape);
uint16_t TurboJS_ShapePropertyCount(const TurboJSShape *shape);
int TurboJS_ShapeLookup(const TurboJSShape *shape,
                        const char *property_name,
                        uint16_t *out_offset);
void TurboJS_PropertyInlineCacheInit(TurboJSPropertyInlineCache *cache);
int TurboJS_PropertyInlineCacheLookup(TurboJSPropertyInlineCache *cache,
                                      const TurboJSShape *shape,
                                      const char *property_name,
                                      uint16_t *out_offset);

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
typedef enum TurboJSEngineNumericMode {
    TURBOJS_ENGINE_NUMERIC_INT32 = 0,
    TURBOJS_ENGINE_NUMERIC_FLOAT64 = 1
} TurboJSEngineNumericMode;

typedef TurboJSIRStatus (*TurboJSEngineCallResolver)(
    void *opaque, uint32_t bytecode_offset, uint16_t argument_count,
    TurboJSEngineNumericMode numeric_mode, TurboJSClutchCallSite *out_site);

typedef enum TurboJSEngineCallableLoadKind {
    TURBOJS_ENGINE_CALLABLE_GLOBAL = 1,
    TURBOJS_ENGINE_CALLABLE_CLOSURE = 2
} TurboJSEngineCallableLoadKind;

typedef TurboJSIRStatus (*TurboJSEngineCallableResolver)(
    void *opaque, uint32_t bytecode_offset, TurboJSEngineCallableLoadKind kind,
    uint32_t index_or_atom, TurboJSCallableReference *out_reference);


typedef TurboJSIRStatus (*TurboJSEngineMethodPropertyResolver)(
    void *opaque, uint32_t bytecode_offset, uint32_t atom,
    TurboJSCallableReference *out_reference, uint32_t *out_shape_offset,
    uint64_t *out_shape_identity);

typedef enum TurboJSSpoolRejectionReason {
    TURBOJS_SPOOL_REJECT_NONE = 0,
    TURBOJS_SPOOL_REJECT_INVALID_INPUT,
    TURBOJS_SPOOL_REJECT_FRAME_LIMIT,
    TURBOJS_SPOOL_REJECT_TRUNCATED_BYTECODE,
    TURBOJS_SPOOL_REJECT_STACK_UNDERFLOW,
    TURBOJS_SPOOL_REJECT_STACK_OVERFLOW,
    TURBOJS_SPOOL_REJECT_REGISTER_LIMIT,
    TURBOJS_SPOOL_REJECT_LOCAL_INDEX,
    TURBOJS_SPOOL_REJECT_ARGUMENT_INDEX,
    TURBOJS_SPOOL_REJECT_BRANCH_TARGET,
    TURBOJS_SPOOL_REJECT_STACK_MERGE,
    TURBOJS_SPOOL_REJECT_CALL_FEEDBACK,
    TURBOJS_SPOOL_REJECT_NUMERIC_MODE,
    TURBOJS_SPOOL_REJECT_DYNAMIC_OPCODE,
    TURBOJS_SPOOL_REJECT_OUT_OF_MEMORY
} TurboJSSpoolRejectionReason;

typedef struct TurboJSSpoolLoweringStats {
    uint32_t maximum_stack_depth;
    uint32_t branch_target_count;
    uint32_t nonempty_stack_merge_count;
    uint32_t stack_spill_store_count;
    uint32_t stack_reload_count;
    uint32_t runtime_helper_exit_count;
    uint32_t partial_function_count;
    uint32_t callable_global_load_count;
    uint32_t callable_closure_load_count;
    uint32_t callable_load_rejection_count;
    uint32_t receiver_method_call_count;
    uint32_t property_method_load_count;
    uint32_t property_shape_guard_count;
    uint32_t property_method_rejection_count;
    uint32_t rejection_reason;
    uint32_t rejection_bytecode_offset;
} TurboJSSpoolLoweringStats;

typedef struct TurboJSEngineBytecodeInfo {
    const uint8_t *bytecode;
    size_t bytecode_length;
    uint16_t argument_count;
    uint16_t local_count;
    uint16_t stack_size;
    TurboJSEngineNumericMode numeric_mode;
    /* Optional Telemetry-backed resolver used to lower stable call0-call3/call
     * bytecodes into IR-owned Clutch sites. Returning UNSUPPORTED preserves
     * the canonical dynamic JavaScript fallback. */
    TurboJSEngineCallResolver call_resolver;
    void *call_resolver_opaque;
    /* Optional resolver for stable global and closure callable loads. The
     * frontend clones the rooted reference into IR-owned storage. */
    TurboJSEngineCallableResolver callable_resolver;
    void *callable_resolver_opaque;
    /* Optional monomorphic own-property method resolver used by get_field2. */
    TurboJSEngineMethodPropertyResolver method_property_resolver;
    void *method_property_resolver_opaque;
    /* Optional exact frontend coverage and rejection telemetry. */
    TurboJSSpoolLoweringStats *lowering_stats;
} TurboJSEngineBytecodeInfo;

TurboJSIRStatus TurboJS_EngineBytecodeToIR(const TurboJSEngineBytecodeInfo *bytecode,
                                           TurboJSIRFunction *out_ir,
                                           TurboJSIRDiagnostic *diagnostic);

/* Whole-function bytecode capability and control-flow inventory. This is the
 * front door for the general region compiler: it validates instruction
 * boundaries, discovers basic blocks/backedges, and separates operations that
 * can be lowered directly from operations that require runtime helper exits. */
typedef struct TurboJSBytecodeAnalysis {
    uint32_t instruction_count;
    uint32_t basic_block_count;
    uint32_t branch_count;
    uint32_t backedge_count;
    uint32_t direct_instruction_count;
    uint32_t helper_instruction_count;
    uint32_t invalid_instruction_count;
    uint32_t maximum_stack_depth;
    uint8_t has_exceptional_operations;
    uint8_t has_calls;
    uint8_t has_property_operations;
    uint8_t has_indexed_operations;
} TurboJSBytecodeAnalysis;

TurboJSIRStatus TurboJS_EngineBytecodeAnalyze(
    const TurboJSEngineBytecodeInfo *bytecode,
    TurboJSBytecodeAnalysis *analysis,
    TurboJSIRDiagnostic *diagnostic);


#define TURBOJS_BYTECODE_NO_BLOCK UINT32_MAX
#define TURBOJS_BYTECODE_MAX_BLOCK_EDGES 8u

typedef enum TurboJSBytecodeBlockFlags {
    TURBOJS_BYTECODE_BLOCK_REACHABLE = 1u << 0,
    TURBOJS_BYTECODE_BLOCK_LOOP_HEADER = 1u << 1,
    TURBOJS_BYTECODE_BLOCK_HAS_BACKEDGE = 1u << 2,
    TURBOJS_BYTECODE_BLOCK_HELPER_EXIT = 1u << 3
} TurboJSBytecodeBlockFlags;

typedef struct TurboJSBytecodeBlock {
    uint32_t start_offset;
    uint32_t end_offset;
    uint32_t first_instruction;
    uint32_t instruction_count;
    uint32_t entry_stack_depth;
    uint32_t exit_stack_depth;
    uint32_t successors[2];
    uint32_t predecessors[TURBOJS_BYTECODE_MAX_BLOCK_EDGES];
    uint8_t successor_count;
    uint8_t predecessor_count;
    uint8_t flags;
    uint8_t reserved;
} TurboJSBytecodeBlock;

typedef struct TurboJSBytecodeCFG {
    TurboJSBytecodeBlock *blocks;
    uint32_t *instruction_offsets;
    uint32_t *offset_to_block;
    size_t block_count;
    size_t instruction_count;
    uint32_t maximum_stack_depth;
} TurboJSBytecodeCFG;

TurboJSIRStatus TurboJS_EngineBytecodeBuildCFG(
    const TurboJSEngineBytecodeInfo *bytecode,
    TurboJSBytecodeCFG *cfg,
    TurboJSIRDiagnostic *diagnostic);
void TurboJS_EngineBytecodeCFGDestroy(TurboJSBytecodeCFG *cfg);


typedef enum TurboJSBytecodeRegionBlockFlags {
    TURBOJS_BYTECODE_REGION_REACHABLE = 1u << 0,
    TURBOJS_BYTECODE_REGION_LOOP_HEADER = 1u << 1,
    TURBOJS_BYTECODE_REGION_HELPER_EXIT = 1u << 2,
    TURBOJS_BYTECODE_REGION_MERGE = 1u << 3
} TurboJSBytecodeRegionBlockFlags;

typedef struct TurboJSBytecodeRegionBlock {
    uint32_t cfg_block;
    uint32_t entry_stack_depth;
    uint32_t local_phi_count;
    uint32_t stack_phi_count;
    uint8_t predecessor_count;
    uint8_t successor_count;
    uint8_t flags;
    uint8_t reserved;
} TurboJSBytecodeRegionBlock;

typedef struct TurboJSBytecodeRegionPlan {
    TurboJSBytecodeCFG cfg;
    TurboJSBytecodeRegionBlock *blocks;
    size_t block_count;
    uint32_t reachable_block_count;
    uint32_t helper_exit_block_count;
    uint32_t merge_block_count;
    uint32_t loop_header_count;
    uint32_t estimated_local_phis;
    uint32_t estimated_stack_phis;
} TurboJSBytecodeRegionPlan;

TurboJSIRStatus TurboJS_EngineBytecodeBuildRegionPlan(
    const TurboJSEngineBytecodeInfo *bytecode,
    TurboJSBytecodeRegionPlan *plan,
    TurboJSIRDiagnostic *diagnostic);
void TurboJS_EngineBytecodeRegionPlanDestroy(TurboJSBytecodeRegionPlan *plan);


#define TURBOJS_REGION_NO_VALUE UINT32_MAX

typedef enum TurboJSBytecodeRegionValueKind {
    TURBOJS_REGION_VALUE_INITIAL_LOCAL = 0,
    TURBOJS_REGION_VALUE_ARGUMENT = 1,
    TURBOJS_REGION_VALUE_CONSTANT = 2,
    TURBOJS_REGION_VALUE_OPERATION = 3,
    TURBOJS_REGION_VALUE_PHI = 4,
    TURBOJS_REGION_VALUE_HELPER = 5
} TurboJSBytecodeRegionValueKind;

typedef struct TurboJSBytecodeRegionPhiInput {
    uint32_t predecessor_block;
    uint32_t value;
} TurboJSBytecodeRegionPhiInput;

typedef struct TurboJSBytecodeRegionValue {
    uint32_t id;
    uint32_t block;
    uint32_t source_offset;
    uint32_t left;
    uint32_t right;
    uint32_t phi_input_start;
    uint16_t phi_input_count;
    uint16_t local_or_stack_index;
    uint8_t kind;
    uint8_t opcode;
    uint8_t is_stack_value;
    uint8_t reserved;
    uint32_t metadata;
} TurboJSBytecodeRegionValue;

typedef struct TurboJSBytecodeRegionStateBlock {
    uint32_t entry_local_offset;
    uint32_t exit_local_offset;
    uint32_t entry_stack_offset;
    uint32_t exit_stack_offset;
    uint32_t active_local_phis;
    uint32_t active_stack_phis;
} TurboJSBytecodeRegionStateBlock;

typedef struct TurboJSBytecodeRegionStateGraph {
    TurboJSBytecodeRegionPlan plan;
    TurboJSBytecodeRegionStateBlock *blocks;
    TurboJSBytecodeRegionValue *values;
    TurboJSBytecodeRegionPhiInput *phi_inputs;
    uint32_t *entry_locals;
    uint32_t *exit_locals;
    uint32_t *entry_stack;
    uint32_t *exit_stack;
    size_t value_count;
    size_t value_capacity;
    size_t phi_input_count;
    size_t phi_input_capacity;
    uint32_t local_count;
    uint32_t stack_stride;
    uint32_t active_phi_count;
} TurboJSBytecodeRegionStateGraph;

TurboJSIRStatus TurboJS_EngineBytecodeBuildRegionStateGraph(
    const TurboJSEngineBytecodeInfo *bytecode,
    TurboJSBytecodeRegionStateGraph *graph,
    TurboJSIRDiagnostic *diagnostic);
void TurboJS_EngineBytecodeRegionStateGraphDestroy(
    TurboJSBytecodeRegionStateGraph *graph);

/* Owning cache for compiled baseline functions. Cache keys are supplied by the
 * embedding runtime and normally identify a JSFunctionBytecode allocation. */
typedef struct TurboJSCodeCacheStats {
    size_t entry_count;
    size_t code_bytes;
    uint64_t hits;
    uint64_t misses;
    uint64_t compilations;
    uint64_t evictions;
    uint64_t dependent_call_sites_invalidated;
    uint64_t continuation_dependency_invalidations;
    uint64_t reverse_dependency_lookups;
    uint64_t reverse_dependency_nodes_visited;
    uint64_t reverse_dependency_registrations;
    uint64_t reverse_dependency_unregistrations;
    uint64_t clutch_repatch_attempts;
    uint64_t clutch_repatch_successes;
    uint64_t clutch_repatch_incompatible;
    uint64_t clutch_call_sites_repatched;
    uint64_t repatch_identity_lookups;
    uint64_t repatch_identity_nodes_visited;
    uint64_t repatch_identity_registrations;
    uint64_t repatch_identity_unregistrations;
    uint64_t weighted_function_evictions;
    uint64_t weighted_continuation_evictions;
    uint64_t continuation_hits;
    uint64_t continuation_misses;
    uint64_t continuation_compilations;
    uint64_t continuation_evictions;
    size_t continuation_entry_count;
    size_t continuation_code_bytes;
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
TurboJSIRStatus TurboJS_CodeCacheAttachEntryHandle(
    TurboJSCodeCache *cache,
    const void *key,
    TurboJSNativeEntryHandle *handle,
    TurboJSNativeEntryKind kind,
    uint16_t argument_count);
TurboJSIRStatus TurboJS_CodeCacheAttachEntryHandleIdentity(
    TurboJSCodeCache *cache,
    const void *key,
    TurboJSNativeEntryHandle *handle,
    TurboJSNativeEntryKind kind,
    uint16_t argument_count,
    uint64_t target_identity);
void TurboJS_CodeCacheInvalidate(TurboJSCodeCache *cache, const void *key);
void TurboJS_CodeCacheClear(TurboJSCodeCache *cache);
TurboJSCodeCacheStats TurboJS_CodeCacheGetStats(const TurboJSCodeCache *cache);

const TurboJSNativeFunction *TurboJS_CodeCacheLookupContinuation(
    TurboJSCodeCache *cache, uint64_t function_id, uint64_t function_revision,
    size_t start_instruction, size_t *out_prologue_count);
TurboJSIRStatus TurboJS_CodeCacheStoreContinuation(
    TurboJSCodeCache *cache, uint64_t function_id, uint64_t function_revision,
    size_t start_instruction, size_t prologue_count,
    TurboJSNativeFunction *native, const TurboJSNativeFunction **out_function);


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

/* Telemetry: bounded per-call-site target feedback used by Relay call ICs. */
#define TURBOJS_CALL_FEEDBACK_MAX_TARGETS 4u

typedef enum TurboJSCallFeedbackState {
    TURBOJS_CALL_FEEDBACK_UNINITIALIZED = 0,
    TURBOJS_CALL_FEEDBACK_MONOMORPHIC,
    TURBOJS_CALL_FEEDBACK_POLYMORPHIC,
    TURBOJS_CALL_FEEDBACK_MEGAMORPHIC
} TurboJSCallFeedbackState;

typedef struct TurboJSCallFeedbackTarget {
    uint64_t target_identity;
    uint32_t hits;
    uint32_t reserved;
} TurboJSCallFeedbackTarget;

typedef struct TurboJSCallFeedbackSlot {
    TurboJSCallFeedbackTarget targets[TURBOJS_CALL_FEEDBACK_MAX_TARGETS];
    uint64_t observations;
    uint32_t misses;
    uint32_t generation;
    uint8_t state;
    uint8_t target_count;
    uint16_t reserved;
} TurboJSCallFeedbackSlot;

void TurboJS_CallFeedbackInit(TurboJSCallFeedbackSlot *slot);
TurboJSCallFeedbackState TurboJS_CallFeedbackObserve(
    TurboJSCallFeedbackSlot *slot, uint64_t target_identity);
TurboJSCallFeedbackState TurboJS_CallFeedbackGetState(
    const TurboJSCallFeedbackSlot *slot);
const char *TurboJS_CallFeedbackStateName(TurboJSCallFeedbackState state);
int TurboJS_CallFeedbackMonomorphicTarget(
    const TurboJSCallFeedbackSlot *slot,
    uint32_t minimum_hits,
    uint64_t *target_identity);

/*
 * Spool/Redline shared frame contract. Each logical slot is eight bytes on all
 * supported hosts so baseline frames, OSR snapshots, and Rewind deoptimization
 * metadata use one architecture-neutral layout.
 */
#define TURBOJS_JS_FRAME_ABI_VERSION 1u
#define TURBOJS_JS_FRAME_SLOT_SIZE 8u

typedef enum TurboJSJSFrameFixedSlot {
    TURBOJS_JS_FRAME_PREVIOUS_FRAME = 0,
    TURBOJS_JS_FRAME_RETURN_PC,
    TURBOJS_JS_FRAME_CONTEXT,
    TURBOJS_JS_FRAME_FUNCTION,
    TURBOJS_JS_FRAME_ENVIRONMENT,
    TURBOJS_JS_FRAME_BYTECODE_PC,
    TURBOJS_JS_FRAME_ARGUMENT_COUNT,
    TURBOJS_JS_FRAME_ACCUMULATOR,
    TURBOJS_JS_FRAME_FIXED_SLOT_COUNT
} TurboJSJSFrameFixedSlot;

typedef struct TurboJSJSFrameLayout {
    uint32_t abi_version;
    uint32_t slot_size;
    uint32_t fixed_slot_count;
    uint32_t argument_count;
    uint32_t local_count;
    uint32_t stack_capacity;
    uint32_t total_slots;
    uint32_t frame_size_bytes;
} TurboJSJSFrameLayout;

TurboJSIRStatus TurboJS_JSFrameLayoutInit(
    TurboJSJSFrameLayout *layout,
    uint32_t argument_count,
    uint32_t local_count,
    uint32_t stack_capacity);
size_t TurboJS_JSFrameFixedSlotOffset(TurboJSJSFrameFixedSlot slot);
size_t TurboJS_JSFrameArgumentOffset(
    const TurboJSJSFrameLayout *layout, uint32_t index);
size_t TurboJS_JSFrameLocalOffset(
    const TurboJSJSFrameLayout *layout, uint32_t index);
size_t TurboJS_JSFrameStackOffset(
    const TurboJSJSFrameLayout *layout, uint32_t index);

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
    TURBOJS_SSA_AND_I64,
    TURBOJS_SSA_OR_I64,
    TURBOJS_SSA_XOR_I64,
    TURBOJS_SSA_SHL_I64,
    TURBOJS_SSA_SAR_I64,
    TURBOJS_SSA_SHR_I64,
    TURBOJS_SSA_MIN_F64,
    TURBOJS_SSA_MAX_F64,
    TURBOJS_SSA_LESS_THAN_I64,
    TURBOJS_SSA_LESS_EQUAL_I64,
    TURBOJS_SSA_GREATER_THAN_I64,
    TURBOJS_SSA_GREATER_EQUAL_I64,
    TURBOJS_SSA_EQUAL_I64,
    TURBOJS_SSA_PHI,
    TURBOJS_SSA_GUARD_INT32,
    TURBOJS_SSA_PROPERTY_LOAD,
    TURBOJS_SSA_PROPERTY_STORE,
    TURBOJS_SSA_VIRTUAL_OBJECT,
    TURBOJS_SSA_VIRTUAL_FIELD_STORE,
    TURBOJS_SSA_ELEMENT_LOAD,
    TURBOJS_SSA_ELEMENT_STORE,
    TURBOJS_SSA_JUMP,
    TURBOJS_SSA_BRANCH_TRUE,
    TURBOJS_SSA_BRANCH_FALSE,
    TURBOJS_SSA_RETURN
} TurboJSSSAOpcode;

#define TURBOJS_SSA_MAX_BLOCK_EDGES 8u
#define TURBOJS_SSA_NO_BLOCK UINT32_MAX

#ifndef TURBOJS_PROPERTY_PIC_MAX_CASES
#define TURBOJS_PROPERTY_PIC_MAX_CASES 4u
#endif

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
    uint16_t feedback_slot;
    uint32_t metadata;
    uintptr_t guard_shape;
    uint32_t property_index;
    uint16_t property_flags;
    uint16_t property_feedback_generation;
    uint8_t property_case_count;
    uint8_t property_reserved[3];
    uint32_t property_state_receiver;
    uintptr_t property_shapes[TURBOJS_PROPERTY_PIC_MAX_CASES];
    uint32_t property_indices[TURBOJS_PROPERTY_PIC_MAX_CASES];
    uint16_t property_generations[TURBOJS_PROPERTY_PIC_MAX_CASES];
    uint16_t property_case_flags[TURBOJS_PROPERTY_PIC_MAX_CASES];
    uint16_t element_kind;
    uint16_t element_flags;
    uint32_t element_generation;
    uint32_t element_length_value;
    int32_t element_range_min;
    int32_t element_range_max;
    int16_t element_induction_step;
    uint8_t element_bounds_proven;
    uint8_t element_length_hoisted;
    uint8_t element_base_hoisted;
    uint8_t element_range_reserved;
} TurboJSSSAValue;

typedef struct TurboJSSSABlock {
    uint32_t id;
    uint32_t first_value;
    uint32_t value_count;
    uint32_t first_instruction;
    uint32_t instruction_count;
    uint32_t predecessors[TURBOJS_BYTECODE_MAX_BLOCK_EDGES];
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

TurboJSIRStatus TurboJS_EngineBytecodeRegionBuildSSA(
    const TurboJSEngineBytecodeInfo *bytecode,
    TurboJSSSAGraph *ssa,
    TurboJSIRDiagnostic *diagnostic);

#define TURBOJS_PROPERTY_FEEDBACK_LOAD      0x0001u
#define TURBOJS_PROPERTY_FEEDBACK_STORE     0x0002u
#define TURBOJS_PROPERTY_FEEDBACK_WRITABLE  0x0004u
#define TURBOJS_PROPERTY_FEEDBACK_OWN_DATA  0x0008u
#define TURBOJS_PROPERTY_FEEDBACK_PROTOTYPE 0x0010u

typedef struct TurboJSPropertyFeedback {
    uint32_t source_instruction;
    uint32_t atom;
    uintptr_t shape_identity;
    uint32_t property_index;
    uint16_t flags;
    uint16_t generation;
} TurboJSPropertyFeedback;

TurboJSIRStatus TurboJS_SSAApplyPropertyFeedback(
    TurboJSSSAGraph *graph, const TurboJSPropertyFeedback *feedback,
    size_t feedback_count, uint32_t *applied_count);

/* Phase 58: executable multi-block SSA regions. */
typedef struct TurboJSRegionNativeFunction TurboJSRegionNativeFunction;
typedef struct TurboJSRegionNativeStats {
    uint32_t block_count;
    uint32_t phi_count;
    uint32_t edge_move_count;
    uint32_t cycle_break_count;
    uint32_t allocated_intervals;
    uint32_t spilled_intervals;
    uint16_t spill_slots;
    uint16_t reserved;
    uint32_t register_values;
    uint32_t fragment_count;
    uint32_t split_count;
    uint32_t phi_coalesce_candidates;
    uint32_t phi_coalesce_successes;
    uint32_t phi_coalesce_rejected;
    size_t frame_bytes;
    size_t native_code_bytes;
    uint32_t inline_property_pic_cases;
    uint32_t inline_property_loads;
    uint32_t inline_property_stores;
    uint32_t inline_property_dependency_guards;
    uint32_t inline_element_loads;
    uint32_t inline_element_stores;
    uint32_t inline_element_kind_guards;
    uint32_t inline_element_generation_guards;
    uint32_t inline_element_bounds_guards;
    uint32_t inline_element_dynamic_indexes;
    uint32_t inline_element_dynamic_stores;
    uint32_t inline_element_loops;
    uint32_t inline_element_loop_length_hoists;
    uint32_t inline_element_loop_base_hoists;
    uint32_t inline_element_loop_bounds_eliminated;
    uint32_t inline_element_loop_unroll_factor;
    uint32_t inline_element_loop_accumulators;
    uint32_t inline_element_float64_loops;
    uint32_t inline_element_transform_loops;
    uint32_t inline_element_scale_only_loops;
    uint32_t inline_element_bias_only_loops;
    uint32_t inline_element_subtract_only_loops;
    uint32_t inline_element_dual_source_loops;
    uint32_t inline_element_dual_source_subtract_loops;
    uint32_t inline_element_min_loops;
    uint32_t inline_element_max_loops;
    uint32_t inline_element_clamp_loops;
    uint32_t inline_element_float64_operations;
    uint32_t inline_element_simd_loops;
    uint32_t inline_element_simd_width;
    uint32_t inline_element_simd_level;
    uint32_t inline_element_simd_fma;
    uint32_t inline_element_simd_fast_math;
    uint64_t compile_time_ns;
} TurboJSRegionNativeStats;
TurboJSIRStatus TurboJS_RegionNativeCompile(
    const TurboJSSSAGraph *graph, TurboJSRegionNativeFunction **out_function,
    TurboJSRegionNativeStats *stats, TurboJSIRDiagnostic *diagnostic);
TurboJSIRStatus TurboJS_RegionNativeInvoke(
    const TurboJSRegionNativeFunction *function, const int64_t *arguments,
    size_t argument_count, int64_t *result);

/* Phase 82: JSValue-capable guarded property-region invocation. On SysV x64,
   property-load regions enter through generated executable code that roots the
   public arguments before dispatching the retained guarded region plan. The region
   backend treats values as opaque 64-bit payloads; the embedding owns shape
   validation, rooted slot loads, and numeric conversion. */
typedef uint64_t TurboJSRegionValue;
typedef struct TurboJSRegionObjectLayout {
    uint64_t object_tag_mask;
    uint64_t object_tag_value;
    uint64_t object_pointer_mask;
    uint32_t shape_offset;
    uint32_t property_storage_offset;
    uint32_t property_stride;
    uint32_t property_value_offset;
    uint8_t property_storage_indirect;
    uint8_t reserved[7];
} TurboJSRegionObjectLayout;


typedef struct TurboJSRegionElementLayout {
    uint64_t object_tag_mask;
    uint64_t object_tag_value;
    uint64_t object_pointer_mask;
    uint32_t kind_offset;
    uint32_t generation_offset;
    uint32_t length_offset;
    uint32_t element_storage_offset;
    uint32_t element_stride;
    uint32_t element_value_offset;
    uint8_t element_storage_indirect;
    uint8_t reserved[7];
} TurboJSRegionElementLayout;

#define TURBOJS_ELEMENT_KIND_PACKED_I64  1u
#define TURBOJS_ELEMENT_KIND_HOLEY_I64   2u
#define TURBOJS_ELEMENT_KIND_TYPED_F64   3u
#define TURBOJS_ELEMENT_FLAG_WRITABLE    0x0001u
#define TURBOJS_ELEMENT_FLAG_HOLES       0x0002u
#define TURBOJS_ELEMENT_FLAG_DETACHABLE  0x0004u
#define TURBOJS_ELEMENT_FLAG_FAST_MATH   0x0008u

typedef struct TurboJSRegionValueOps {
    int (*guard_shape)(TurboJSRegionValue value, uintptr_t expected_shape, void *opaque);
    int (*load_own_slot)(TurboJSRegionValue object, uint32_t property_index,
                         TurboJSRegionValue *out_value, void *opaque);
    int (*store_own_slot)(TurboJSRegionValue object, uint32_t property_index,
                          TurboJSRegionValue value, void *opaque);
    int (*guard_property_dependency)(uint16_t generation, uint16_t flags,
                                     void *opaque);
    int (*to_i64)(TurboJSRegionValue value, int64_t *out_value, void *opaque);
    TurboJSRegionValue (*from_i64)(int64_t value, void *opaque);
    int (*guard_elements)(TurboJSRegionValue value, uint16_t kind,
                          uint32_t generation, uint16_t flags, void *opaque);
    int (*load_element)(TurboJSRegionValue value, uint32_t index,
                        TurboJSRegionValue *out_value, void *opaque);
    int (*store_element)(TurboJSRegionValue value, uint32_t index,
                         TurboJSRegionValue stored, void *opaque);
    int (*array_length)(TurboJSRegionValue value, uint32_t *out_length,
                        void *opaque);
} TurboJSRegionValueOps;

/* Compile a property region against a private, embedding-owned object layout
   contract. Exact own-data loads with one to four shape cases and exact writable
   stores may be lowered to inline x64 tag/shape/dependency/slot instructions;
   all other graphs retain the guarded evaluator. */
TurboJSIRStatus TurboJS_RegionNativeCompileWithObjectLayout(
    const TurboJSSSAGraph *graph, const TurboJSRegionObjectLayout *layout,
    TurboJSRegionNativeFunction **out_function,
    TurboJSRegionNativeStats *stats, TurboJSIRDiagnostic *diagnostic);
/* Compile an exact single-block element region against an embedding-owned
   packed/holey/typed storage layout. Constant-index loads and writable stores
   may be emitted directly on SysV x64; unsupported graphs keep the guarded
   value backend. */
TurboJSIRStatus TurboJS_RegionNativeCompileWithElementLayout(
    const TurboJSSSAGraph *graph, const TurboJSRegionElementLayout *layout,
    TurboJSRegionNativeFunction **out_function,
    TurboJSRegionNativeStats *stats, TurboJSIRDiagnostic *diagnostic);
TurboJSIRStatus TurboJS_RegionNativeInvokeValues(
    const TurboJSRegionNativeFunction *function,
    const TurboJSRegionValue *arguments, size_t argument_count,
    const TurboJSRegionValueOps *ops, void *opaque,
    TurboJSRegionValue *result);
void TurboJS_RegionNativeFunctionDestroy(TurboJSRegionNativeFunction *function);
size_t TurboJS_RegionNativeCodeSize(const TurboJSRegionNativeFunction *function);
int TurboJS_RegionNativeWriteCode(const TurboJSRegionNativeFunction *function, const char *path);
int TurboJS_RegionNativeWriteAllocation(const TurboJSRegionNativeFunction *function, const char *path);

typedef struct TurboJSSSAOptimizationStats {
    uint32_t constants_folded;
    uint32_t values_removed;
    uint32_t branches_folded;
    uint32_t blocks_removed;
    uint32_t phis_inserted;
    uint32_t guards_inserted;
    uint32_t types_inferred;
    uint32_t expressions_eliminated;
    uint32_t guards_eliminated;
    uint32_t property_loads_eliminated;
    uint32_t property_store_forwardings;
    uint32_t property_store_phis;
    uint32_t property_dead_stores_eliminated;
    uint32_t virtual_objects_scalarized;
    uint32_t virtual_field_loads_forwarded;
    uint32_t property_dependency_reuses;
    uint32_t property_cross_block_loads_eliminated;
    uint32_t property_unique_path_reuses;
    uint32_t property_loop_body_reuses;
    uint32_t property_join_reuses;
    uint32_t property_memory_versions_proven;
    uint32_t property_loop_invariant_reuses;
    uint32_t property_memory_phis;
    uint32_t property_alias_classes_proven;
    uint32_t property_non_aliasing_stores_ignored;
    uint32_t element_bounds_checks_eliminated;
    uint32_t element_length_reuses;
    uint32_t element_induction_indexes_recognized;
    uint32_t element_loop_range_proofs;
    uint32_t element_canonical_inductions;
    uint32_t element_length_hoists;
    uint32_t element_base_pointer_hoists;
    uint32_t element_loop_bounds_checks_eliminated;
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
uint32_t TurboJS_SSAInferTypes(TurboJSSSAGraph *graph);
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
    uint32_t allocated_intervals;
    uint32_t spilled_intervals;
    uint16_t spill_slots;
    uint16_t reserved;
    uint32_t register_values;
    uint32_t fragment_count;
    uint32_t split_count;
    uint32_t phi_coalesce_candidates;
    uint32_t phi_coalesce_successes;
    uint32_t phi_coalesce_rejected;
    size_t frame_bytes;
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

typedef struct TurboJSAOTLoadLimits {
    size_t max_image_bytes;
    size_t max_function_count;
    size_t max_name_bytes;
    size_t max_total_instructions;
    size_t max_instructions_per_function;
} TurboJSAOTLoadLimits;

TurboJSAOTLoadLimits TurboJS_AOTLoadLimitsDefault(void);

TurboJSIRStatus TurboJS_AOTSerializeModule(const TurboJSAOTModuleFunction *functions,
                                            size_t function_count,
                                            TurboJSAOTBuffer *out_buffer,
                                            TurboJSIRDiagnostic *diagnostic);
TurboJSIRStatus TurboJS_AOTDeserializeModule(const uint8_t *data,
                                              size_t size,
                                              TurboJSAOTModule *out_module,
                                              TurboJSIRDiagnostic *diagnostic);
TurboJSIRStatus TurboJS_AOTDeserializeModuleWithLimits(
    const uint8_t *data, size_t size, const TurboJSAOTLoadLimits *limits,
    TurboJSAOTModule *out_module, TurboJSIRDiagnostic *diagnostic);
const TurboJSAOTLoadedFunction *TurboJS_AOTFindFunction(const TurboJSAOTModule *module,
                                                        const char *name);
void TurboJS_AOTModuleDestroy(TurboJSAOTModule *module);
TurboJSIRStatus TurboJS_AOTInspectModule(const uint8_t *data,
                                         size_t size,
                                         TurboJSAOTModuleInfo *out_info,
                                         TurboJSIRDiagnostic *diagnostic);

/* Phase 36: loop OSR state and compact linear-scan allocation contracts. */
typedef enum TurboJSRegisterClass {
    TURBOJS_REGISTER_CLASS_INTEGER = 0,
    TURBOJS_REGISTER_CLASS_FLOAT64 = 1
} TurboJSRegisterClass;

typedef struct TurboJSLiveInterval {
    uint32_t value_id;
    uint32_t start;
    uint32_t end;
    uint32_t first_use;
    uint32_t use_count;
    uint16_t loop_depth;
    uint8_t is_phi;
    uint8_t crosses_backedge;
    TurboJSRegisterClass register_class;
    int16_t physical_register;
    int16_t spill_slot;
} TurboJSLiveInterval;

typedef struct TurboJSLiveIntervalFragment {
    uint32_t value_id;
    uint32_t start;
    uint32_t end;
    int16_t physical_register;
    int16_t spill_slot;
} TurboJSLiveIntervalFragment;

typedef struct TurboJSLinearScanResult {
    TurboJSLiveInterval *intervals;
    size_t interval_count;
    TurboJSLiveIntervalFragment *fragments;
    size_t fragment_count;
    uint32_t *use_positions;
    size_t use_position_count;
    uint32_t *value_positions;
    size_t value_position_count;
    uint32_t *block_start_positions;
    uint32_t *block_end_positions;
    size_t block_position_count;
    uint16_t integer_register_count;
    uint16_t float_register_count;
    uint16_t spill_slot_count;
    uint16_t split_count;
    uint32_t phi_coalesce_candidates;
    uint32_t phi_coalesce_successes;
    uint32_t phi_coalesce_rejected;
} TurboJSLinearScanResult;

TurboJSIRStatus TurboJS_LinearScanAllocate(const TurboJSSSAGraph *graph,
                                            uint16_t integer_registers,
                                            uint16_t float_registers,
                                            TurboJSLinearScanResult *result);
void TurboJS_LinearScanResultDestroy(TurboJSLinearScanResult *result);
const TurboJSLinearScanResult *TurboJS_OptimizedAllocation(
    const TurboJSOptimizedFunction *function);

typedef enum TurboJSOSRDecision {
    TURBOJS_OSR_CONTINUE_INTERPRETING = 0,
    TURBOJS_OSR_REQUEST_COMPILE,
    TURBOJS_OSR_ENTER_READY_CODE,
    TURBOJS_OSR_DISABLED
} TurboJSOSRDecision;

typedef struct TurboJSOSRState {
    uint32_t backedge_count;
    uint32_t compile_threshold;
    uint32_t entry_count;
    uint32_t bailout_count;
    uint32_t loop_header;
    uint8_t compilation_requested;
    uint8_t code_ready;
    uint8_t disabled;
    uint8_t reserved;
} TurboJSOSRState;

void TurboJS_OSRStateInit(TurboJSOSRState *state,
                          uint32_t loop_header,
                          uint32_t compile_threshold);
TurboJSOSRDecision TurboJS_OSRObserveBackedge(TurboJSOSRState *state);
void TurboJS_OSRMarkCodeReady(TurboJSOSRState *state);
void TurboJS_OSRRecordEntry(TurboJSOSRState *state);
void TurboJS_OSRRecordBailout(TurboJSOSRState *state, uint32_t disable_after);

/* Native counted-loop OSR compiler. The generated x86-64 kernel executes the
 * remaining iterations entirely in registers and writes the final induction
 * and accumulator values back into a typed OSR frame. */
typedef struct TurboJSOSRLoopProgram TurboJSOSRLoopProgram;
typedef struct TurboJSOSREntry TurboJSOSREntry;

typedef enum TurboJSOSRLoopComparison {
    TURBOJS_OSR_LOOP_LT = 0,
    TURBOJS_OSR_LOOP_LTE,
    TURBOJS_OSR_LOOP_GT,
    TURBOJS_OSR_LOOP_GTE
} TurboJSOSRLoopComparison;

typedef struct TurboJSOSRCountedLoopSpec {
    uint16_t induction_local;
    uint16_t limit_local;
    uint16_t accumulator_local;
    int32_t step;
    uint32_t loop_header;
    uint32_t resume_bytecode_offset;
    uint64_t maximum_iterations;
    TurboJSOSRLoopComparison comparison;
} TurboJSOSRCountedLoopSpec;

TurboJSIRStatus TurboJS_OSRCompileCountedI64Loop(
    const TurboJSOSRCountedLoopSpec *spec,
    TurboJSOSRLoopProgram **out_program,
    TurboJSIRDiagnostic *diagnostic);
void TurboJS_OSRLoopProgramDestroy(TurboJSOSRLoopProgram *program);
TurboJSOSREntry TurboJS_OSRLoopProgramEntry(TurboJSOSRLoopProgram *program);
size_t TurboJS_OSRLoopProgramCodeSize(const TurboJSOSRLoopProgram *program);


/* Phase 37: typed OSR frame snapshots and parallel move resolution. */
typedef enum TurboJSOSRValueKind {
    TURBOJS_OSR_VALUE_EMPTY = 0,
    TURBOJS_OSR_VALUE_INT64,
    TURBOJS_OSR_VALUE_FLOAT64,
    TURBOJS_OSR_VALUE_REFERENCE
} TurboJSOSRValueKind;

typedef struct TurboJSOSRValue {
    uint64_t bits;
    TurboJSOSRValueKind kind;
    uint32_t reserved;
} TurboJSOSRValue;

typedef struct TurboJSOSRFrame {
    TurboJSOSRValue *locals;
    TurboJSOSRValue *stack;
    uint32_t local_count;
    uint32_t stack_count;
    uint32_t bytecode_offset;
    uint32_t loop_header;
} TurboJSOSRFrame;

TurboJSIRStatus TurboJS_OSRFrameInit(TurboJSOSRFrame *frame,
                                     uint32_t local_count,
                                     uint32_t stack_count);
void TurboJS_OSRFrameDestroy(TurboJSOSRFrame *frame);
TurboJSIRStatus TurboJS_OSRFrameCapture(TurboJSOSRFrame *frame,
                                        const TurboJSOSRValue *locals,
                                        uint32_t local_count,
                                        const TurboJSOSRValue *stack,
                                        uint32_t stack_count,
                                        uint32_t bytecode_offset,
                                        uint32_t loop_header);
TurboJSIRStatus TurboJS_OSRFrameRestore(const TurboJSOSRFrame *frame,
                                        TurboJSOSRValue *locals,
                                        uint32_t local_capacity,
                                        TurboJSOSRValue *stack,
                                        uint32_t stack_capacity);
TurboJSIRStatus TurboJS_OSRFrameValidate(const TurboJSOSRFrame *frame);

typedef enum TurboJSMoveLocationKind {
    TURBOJS_MOVE_REGISTER = 0,
    TURBOJS_MOVE_SPILL_SLOT = 1,
    TURBOJS_MOVE_SCRATCH = 2
} TurboJSMoveLocationKind;

typedef struct TurboJSMoveLocation {
    TurboJSMoveLocationKind kind;
    TurboJSRegisterClass register_class;
    int16_t index;
} TurboJSMoveLocation;

typedef struct TurboJSParallelMove {
    TurboJSMoveLocation source;
    TurboJSMoveLocation destination;
} TurboJSParallelMove;

typedef struct TurboJSMoveSequence {
    TurboJSParallelMove *moves;
    size_t count;
} TurboJSMoveSequence;

TurboJSIRStatus TurboJS_ResolveParallelMoves(const TurboJSParallelMove *moves,
                                              size_t move_count,
                                              TurboJSMoveLocation integer_scratch,
                                              TurboJSMoveLocation float_scratch,
                                              TurboJSMoveSequence *sequence);
void TurboJS_MoveSequenceDestroy(TurboJSMoveSequence *sequence);

/* Executable OSR entry contract. */
typedef enum TurboJSOSRExitKind {
    TURBOJS_OSR_EXIT_COMPLETED = 0,
    TURBOJS_OSR_EXIT_BAILOUT = 1,
    TURBOJS_OSR_EXIT_ERROR = 2
} TurboJSOSRExitKind;

typedef TurboJSOSRExitKind (*TurboJSOSREntryCallback)(TurboJSOSRFrame *frame,
                                                       void *opaque,
                                                       uint32_t *resume_bytecode_offset);

typedef struct TurboJSOSREntry {
    TurboJSOSREntryCallback callback;
    void *opaque;
    uint32_t loop_header;
    uint32_t bailout_limit;
} TurboJSOSREntry;

typedef struct TurboJSOSRExecutionResult {
    TurboJSOSRExitKind exit_kind;
    uint32_t resume_bytecode_offset;
    uint8_t restored_original_frame;
    uint8_t reserved[3];
} TurboJSOSRExecutionResult;

TurboJSIRStatus TurboJS_OSRExecuteEntry(TurboJSOSRState *state,
                                         const TurboJSOSREntry *entry,
                                         TurboJSOSRFrame *live_frame,
                                         TurboJSOSRExecutionResult *result);

#ifdef __cplusplus
}
#endif

#endif
