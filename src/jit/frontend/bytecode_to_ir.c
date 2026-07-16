#include <string.h>
#include "jit.h"

static TurboJSIRStatus bc_fail(TurboJSIRDiagnostic *d, TurboJSIRStatus s,
                               size_t i, const char *m)
{
    if (d) { d->status = s; d->instruction_index = i; d->message = m; }
    return s;
}

TurboJSIRStatus TurboJS_BytecodeToIR(const TurboJSBytecodeFunction *bc,
                                     TurboJSIRFunction *out,
                                     TurboJSIRDiagnostic *diag)
{
    size_t i;
    if (!bc || !out || (!bc->instructions && bc->instruction_count))
        return bc_fail(diag, TURBOJS_IR_INVALID_ARGUMENT, 0, "invalid bytecode function");
    if (bc->register_count > TURBOJS_IR_MAX_REGISTERS)
        return bc_fail(diag, TURBOJS_IR_INVALID_REGISTER, 0, "bytecode register limit exceeded");
    TurboJS_IRFunctionInit(out, bc->argument_count);
    out->register_count = bc->register_count;
    for (i = 0; i < bc->instruction_count; ++i) {
        const TurboJSBytecodeInstruction *src = &bc->instructions[i];
        TurboJSIRInstruction dst;
        memset(&dst, 0, sizeof(dst));
        dst.destination = src->destination;
        dst.left = src->left;
        dst.right = src->right;
        dst.immediate = src->immediate;
        dst.target = src->target;
        switch (src->opcode) {
        case TURBOJS_BC_ARGUMENT: dst.opcode = TURBOJS_IR_ARGUMENT; break;
        case TURBOJS_BC_CONSTANT_I64: dst.opcode = TURBOJS_IR_CONSTANT_I64; break;
        case TURBOJS_BC_ADD_I64: dst.opcode = TURBOJS_IR_ADD_I64; break;
        case TURBOJS_BC_SUB_I64: dst.opcode = TURBOJS_IR_SUB_I64; break;
        case TURBOJS_BC_MUL_I64: dst.opcode = TURBOJS_IR_MUL_I64; break;
        case TURBOJS_BC_LESS_THAN_I64: dst.opcode = TURBOJS_IR_LESS_THAN_I64; break;
        case TURBOJS_BC_JUMP: dst.opcode = TURBOJS_IR_JUMP; break;
        case TURBOJS_BC_BRANCH_TRUE: dst.opcode = TURBOJS_IR_BRANCH_TRUE; break;
        case TURBOJS_BC_RETURN_I64: dst.opcode = TURBOJS_IR_RETURN_I64; break;
        default:
            TurboJS_IRFunctionDestroy(out);
            return bc_fail(diag, TURBOJS_IR_INVALID_OPCODE, i, "unknown bytecode opcode");
        }
        if (TurboJS_IREmit(out, dst) != TURBOJS_IR_OK) {
            TurboJS_IRFunctionDestroy(out);
            return bc_fail(diag, TURBOJS_IR_OUT_OF_MEMORY, i, "unable to append IR instruction");
        }
    }
    {
        TurboJSIRStatus status = TurboJS_IRVerify(out, diag);
        if (status != TURBOJS_IR_OK)
            TurboJS_IRFunctionDestroy(out);
        return status;
    }
}
