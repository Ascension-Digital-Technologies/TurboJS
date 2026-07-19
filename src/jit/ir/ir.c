#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "jit.h"

static atomic_uint_fast64_t turbojs_ir_next_instance_id = ATOMIC_VAR_INIT(1);

static void touch(TurboJSIRFunction *function)
{
    if (function)
        function->revision++;
}

void TurboJS_IRFunctionInit(TurboJSIRFunction *function, uint16_t argument_count)
{
    if (!function)
        return;
    memset(function, 0, sizeof(*function));
    function->argument_count = argument_count;
    function->instance_id = atomic_fetch_add_explicit(
        &turbojs_ir_next_instance_id, 1, memory_order_relaxed);
    function->revision = 1;
}

void TurboJS_IRFunctionSetLocalCount(TurboJSIRFunction *function, uint16_t local_count)
{
    if (function) {
        function->local_count = local_count;
        touch(function);
    }
}


void TurboJS_IRFunctionSetRegisterKind(TurboJSIRFunction *function, uint16_t reg, TurboJSValueKind kind)
{
    if (function && reg < function->register_count && reg < TURBOJS_IR_MAX_REGISTERS)
        function->register_kind_hints[reg] = (uint8_t)kind, touch(function);
}

void TurboJS_IRFunctionSetLocalKind(TurboJSIRFunction *function, uint16_t local, TurboJSValueKind kind)
{
    if (function && local < function->local_count && local < TURBOJS_IR_MAX_REGISTERS)
        function->local_kind_hints[local] = (uint8_t)kind, touch(function);
}

TurboJSValueKind TurboJS_IRFunctionRegisterKind(const TurboJSIRFunction *function, uint16_t reg)
{
    return (!function || reg >= function->register_count || reg >= TURBOJS_IR_MAX_REGISTERS)
        ? TURBOJS_VALUE_UNKNOWN : (TurboJSValueKind)function->register_kind_hints[reg];
}

TurboJSValueKind TurboJS_IRFunctionLocalKind(const TurboJSIRFunction *function, uint16_t local)
{
    return (!function || local >= function->local_count || local >= TURBOJS_IR_MAX_REGISTERS)
        ? TURBOJS_VALUE_UNKNOWN : (TurboJSValueKind)function->local_kind_hints[local];
}

void TurboJS_IRFunctionDestroy(TurboJSIRFunction *function)
{
    if (!function)
        return;
    free(function->instructions);
    for (size_t i = 0; i < function->owned_clutch_site_count; ++i) {
        TurboJS_ClutchCallSiteDestroy(function->owned_clutch_sites[i]);
        free(function->owned_clutch_sites[i]);
    }
    free(function->owned_clutch_sites);
    for (size_t i = 0; i < function->owned_callable_reference_count; ++i) {
        TurboJS_CallableReferenceDestroy(function->owned_callable_references[i]);
        free(function->owned_callable_references[i]);
    }
    free(function->owned_callable_references);
    memset(function, 0, sizeof(*function));
}


TurboJSClutchCallSite *TurboJS_IRAllocateClutchCallSite(
    TurboJSIRFunction *function)
{
    TurboJSClutchCallSite **sites;
    TurboJSClutchCallSite *site;
    size_t capacity;
    if (!function)
        return NULL;
    if (function->owned_clutch_site_count == function->owned_clutch_site_capacity) {
        capacity = function->owned_clutch_site_capacity ?
            function->owned_clutch_site_capacity * 2u : 4u;
        sites = (TurboJSClutchCallSite **)realloc(
            function->owned_clutch_sites, capacity * sizeof(*sites));
        if (!sites)
            return NULL;
        function->owned_clutch_sites = sites;
        function->owned_clutch_site_capacity = capacity;
    }
    site = (TurboJSClutchCallSite *)calloc(1, sizeof(*site));
    if (!site)
        return NULL;
    function->owned_clutch_sites[function->owned_clutch_site_count++] = site;
    touch(function);
    return site;
}

TurboJSCallableReference *TurboJS_IRAllocateCallableReference(
    TurboJSIRFunction *function)
{
    TurboJSCallableReference **references;
    TurboJSCallableReference *reference;
    size_t capacity;
    if (!function) return NULL;
    if (function->owned_callable_reference_count == function->owned_callable_reference_capacity) {
        capacity = function->owned_callable_reference_capacity ?
            function->owned_callable_reference_capacity * 2u : 4u;
        references = (TurboJSCallableReference **)realloc(
            function->owned_callable_references, capacity * sizeof(*references));
        if (!references) return NULL;
        function->owned_callable_references = references;
        function->owned_callable_reference_capacity = capacity;
    }
    reference = (TurboJSCallableReference *)calloc(1, sizeof(*reference));
    if (!reference) return NULL;
    function->owned_callable_references[function->owned_callable_reference_count++] = reference;
    touch(function);
    return reference;
}

uint16_t TurboJS_IRAllocateRegister(TurboJSIRFunction *function)
{
    if (!function || function->register_count >= TURBOJS_IR_MAX_REGISTERS)
        return TURBOJS_IR_NO_REGISTER;
    touch(function);
    return function->register_count++;
}

TurboJSIRStatus TurboJS_IREmit(TurboJSIRFunction *function,
                               TurboJSIRInstruction instruction)
{
    TurboJSIRInstruction *instructions;
    size_t capacity;

    if (!function)
        return TURBOJS_IR_INVALID_ARGUMENT;
    if (function->instruction_count == function->instruction_capacity) {
        capacity = function->instruction_capacity ? function->instruction_capacity * 2u : 16u;
        instructions = (TurboJSIRInstruction *)realloc(
            function->instructions, capacity * sizeof(*instructions));
        if (!instructions)
            return TURBOJS_IR_OUT_OF_MEMORY;
        function->instructions = instructions;
        function->instruction_capacity = capacity;
    }
    function->instructions[function->instruction_count++] = instruction;
    touch(function);
    return TURBOJS_IR_OK;
}

const char *TurboJS_IRStatusName(TurboJSIRStatus status)
{
    switch (status) {
    case TURBOJS_IR_OK: return "ok";
    case TURBOJS_IR_INVALID_ARGUMENT: return "invalid-argument";
    case TURBOJS_IR_OUT_OF_MEMORY: return "out-of-memory";
    case TURBOJS_IR_INVALID_OPCODE: return "invalid-opcode";
    case TURBOJS_IR_INVALID_REGISTER: return "invalid-register";
    case TURBOJS_IR_INVALID_TARGET: return "invalid-target";
    case TURBOJS_IR_MISSING_RETURN: return "missing-return";
    case TURBOJS_IR_EXECUTION_LIMIT: return "execution-limit";
    case TURBOJS_IR_BAILOUT: return "bailout";
    case TURBOJS_IR_EXCEPTION: return "exception";
    case TURBOJS_IR_UNSUPPORTED: return "unsupported";
    default: return "unknown";
    }
}

const char *TurboJS_IROpcodeName(TurboJSIROpcode opcode)
{
    switch (opcode) {
    case TURBOJS_IR_NOP: return "nop";
    case TURBOJS_IR_ARGUMENT: return "argument";
    case TURBOJS_IR_CONSTANT_I64: return "constant.i64";
    case TURBOJS_IR_CONSTANT_F64: return "constant.f64";
    case TURBOJS_IR_ADD_I64: return "add.i64";
    case TURBOJS_IR_SUB_I64: return "sub.i64";
    case TURBOJS_IR_MUL_I64: return "mul.i64";
    case TURBOJS_IR_ADD_F64: return "add.f64";
    case TURBOJS_IR_SUB_F64: return "sub.f64";
    case TURBOJS_IR_MUL_F64: return "mul.f64";
    case TURBOJS_IR_DIV_F64: return "div.f64";
    case TURBOJS_IR_LESS_THAN_F64: return "less-than.f64";
    case TURBOJS_IR_LESS_EQUAL_F64: return "less-equal.f64";
    case TURBOJS_IR_EQUAL_F64: return "equal.f64";
    case TURBOJS_IR_I64_TO_F64: return "i64-to-f64";
    case TURBOJS_IR_F64_TO_I64_TRUNC: return "f64-to-i64-trunc";
    case TURBOJS_IR_ADD_I32_CHECKED: return "add.i32.checked";
    case TURBOJS_IR_SUB_I32_CHECKED: return "sub.i32.checked";
    case TURBOJS_IR_MUL_I32_CHECKED: return "mul.i32.checked";
    case TURBOJS_IR_DIV_I32_CHECKED: return "div.i32.checked";
    case TURBOJS_IR_REM_I32_CHECKED: return "rem.i32.checked";
    case TURBOJS_IR_RUNTIME_HELPER: return "runtime.helper";
    case TURBOJS_IR_LESS_THAN_I64: return "less-than.i64";
    case TURBOJS_IR_LOCAL_GET: return "local.get";
    case TURBOJS_IR_LOCAL_SET: return "local.set";
    case TURBOJS_IR_JUMP: return "jump";
    case TURBOJS_IR_BRANCH_TRUE: return "branch-true";
    case TURBOJS_IR_BRANCH_FALSE: return "branch-false";
    case TURBOJS_IR_RETURN_I64: return "return.i64";
    case TURBOJS_IR_RETURN_F64: return "return.f64";
    default: return "invalid";
    }
}
