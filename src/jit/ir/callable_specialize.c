#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "jit.h"

typedef struct KnownValue {
    const TurboJSCallableReference *callable;
    uint8_t kind;
} KnownValue;

enum { KNOWN_NONE = 0, KNOWN_NUMERIC = 1, KNOWN_CALLABLE = 2 };

static TurboJSIRStatus emit_copy(TurboJSIRFunction *out, TurboJSIRInstruction in)
{
    return TurboJS_IREmit(out, in);
}

TurboJSIRStatus TurboJS_IRSpecializeCallableReferences(
    const TurboJSIRFunction *input, TurboJSIRFunction *output,
    TurboJSIRDiagnostic *diagnostic)
{
    KnownValue regs[TURBOJS_IR_MAX_REGISTERS];
    KnownValue locals[TURBOJS_IR_MAX_REGISTERS];
    size_t i = 0;
    TurboJSIRStatus status = TURBOJS_IR_OK;
    if (!input || !output)
        return TURBOJS_IR_INVALID_ARGUMENT;
    memset(regs, 0, sizeof(regs));
    memset(locals, 0, sizeof(locals));
    TurboJS_IRFunctionInit(output, input->argument_count);
    TurboJS_IRFunctionSetLocalCount(output, input->local_count);
    output->source_local_count = input->source_local_count;
    while (output->register_count < input->register_count)
        if (TurboJS_IRAllocateRegister(output) == TURBOJS_IR_NO_REGISTER)
            goto unsupported;
    for (i = 0; i < input->instruction_count; ++i) {
        TurboJSIRInstruction in = input->instructions[i];
        TurboJSIRInstruction out = in;
        switch (in.opcode) {
        case TURBOJS_IR_VALUE_CALLABLE_CONSTANT:
            regs[in.destination].kind = KNOWN_CALLABLE;
            regs[in.destination].callable =
                (const TurboJSCallableReference *)(uintptr_t)in.immediate;
            break;
        case TURBOJS_IR_VALUE_CONSTANT_I32:
            out.opcode = TURBOJS_IR_CONSTANT_I64;
            regs[in.destination].kind = KNOWN_NUMERIC;
            status = emit_copy(output, out); if (status != TURBOJS_IR_OK) goto failed;
            break;
        case TURBOJS_IR_VALUE_ARGUMENT:
            out.opcode = TURBOJS_IR_ARGUMENT;
            regs[in.destination].kind = KNOWN_NUMERIC;
            status = emit_copy(output, out); if (status != TURBOJS_IR_OK) goto failed;
            break;
        case TURBOJS_IR_VALUE_MOVE:
            regs[in.destination] = regs[in.left];
            if (regs[in.left].kind == KNOWN_NUMERIC) {
                out.opcode = TURBOJS_IR_ADD_I64;
                out.right = in.left;
                out.immediate = 0;
                /* A move is represented as local roundtrip only when needed;
                 * static callable moves need no machine instruction. */
                goto unsupported;
            }
            break;
        case TURBOJS_IR_VALUE_LOCAL_SET:
            if ((uint64_t)in.immediate >= input->local_count) goto unsupported;
            locals[in.immediate] = regs[in.left];
            if (regs[in.left].kind == KNOWN_NUMERIC) {
                out.opcode = TURBOJS_IR_LOCAL_SET;
                status = emit_copy(output, out); if (status != TURBOJS_IR_OK) goto failed;
            }
            break;
        case TURBOJS_IR_VALUE_LOCAL_GET:
            if ((uint64_t)in.immediate >= input->local_count) goto unsupported;
            regs[in.destination] = locals[in.immediate];
            if (regs[in.destination].kind == KNOWN_NUMERIC) {
                out.opcode = TURBOJS_IR_LOCAL_GET;
                status = emit_copy(output, out); if (status != TURBOJS_IR_OK) goto failed;
            }
            break;
        case TURBOJS_IR_VALUE_CALL_I64:
        case TURBOJS_IR_VALUE_CALL_F64: {
            const TurboJSCallableReference *ref;
            TurboJSClutchCallSite *site;
            size_t argc = (size_t)in.immediate, ai;
            if (regs[in.left].kind != KNOWN_CALLABLE ||
                !(ref = regs[in.left].callable) ||
                argc > TURBOJS_CLUTCH_MAX_ARGUMENTS ||
                ref->argument_count != argc ||
                !TurboJS_CallableReferenceIsLive(ref))
                goto unsupported;
            site = TurboJS_IRAllocateClutchCallSite(output);
            if (!site) goto oom;
            TurboJS_ClutchCallSiteInit(site, ref->target,
                ref->expected_generation, (TurboJSNativeEntryKind)ref->expected_kind,
                (uint16_t)argc);
            TurboJS_ClutchCallSiteSetTargetIdentity(site, ref->target_identity);
            if (ref->closure_environment) {
                TurboJSIRStatus env_status =
                    TurboJS_ClutchCallSiteSetClosureEnvironment(
                        site, (void *)ref->closure_environment,
                        ref->owns_environment ? &ref->environment_rooting : NULL);
                if (env_status != TURBOJS_IR_OK) {
                    status = env_status;
                    goto failed;
                }
            }
            for (ai = 0; ai < argc; ++ai) {
                uint16_t source = (uint16_t)(in.right + ai);
                if (source >= input->register_count || regs[source].kind != KNOWN_NUMERIC)
                    goto unsupported;
                if (TurboJS_ClutchCallSiteSetArgument(site, (uint16_t)ai, source) != TURBOJS_IR_OK)
                    goto unsupported;
            }
            out.opcode = in.opcode == TURBOJS_IR_VALUE_CALL_I64 ?
                TURBOJS_IR_CALL_NATIVE_I64 : TURBOJS_IR_CALL_NATIVE_F64;
            out.immediate = (int64_t)(uintptr_t)site;
            out.left = out.right = 0;
            regs[in.destination].kind = KNOWN_NUMERIC;
            status = emit_copy(output, out); if (status != TURBOJS_IR_OK) goto failed;
            break;
        }
        case TURBOJS_IR_VALUE_RETURN:
            if (regs[in.left].kind != KNOWN_NUMERIC) goto unsupported;
            out.opcode = TURBOJS_IR_RETURN_I64;
            status = emit_copy(output, out); if (status != TURBOJS_IR_OK) goto failed;
            break;
        case TURBOJS_IR_RETURN_I64:
        case TURBOJS_IR_RETURN_F64:
        case TURBOJS_IR_ARGUMENT:
        case TURBOJS_IR_CONSTANT_I64:
        case TURBOJS_IR_CONSTANT_F64:
        case TURBOJS_IR_ADD_I64:
        case TURBOJS_IR_SUB_I64:
        case TURBOJS_IR_MUL_I64:
        case TURBOJS_IR_CALL_NATIVE_I64:
        case TURBOJS_IR_CALL_NATIVE_F64:
            status = emit_copy(output, out); if (status != TURBOJS_IR_OK) goto failed;
            if (in.destination < input->register_count)
                regs[in.destination].kind = KNOWN_NUMERIC;
            break;
        default:
            goto unsupported;
        }
    }
    if (TurboJS_IRVerify(output, diagnostic) != TURBOJS_IR_OK)
        goto unsupported;
    return TURBOJS_IR_OK;
unsupported:
    TurboJS_IRFunctionDestroy(output);
    if (diagnostic) { diagnostic->status = TURBOJS_IR_UNSUPPORTED; diagnostic->instruction_index = i; diagnostic->message = "tagged callable wrapper remains dynamic"; }
    return TURBOJS_IR_UNSUPPORTED;
oom:
    TurboJS_IRFunctionDestroy(output);
    return TURBOJS_IR_OUT_OF_MEMORY;
failed:
    TurboJS_IRFunctionDestroy(output);
    return status;
}
