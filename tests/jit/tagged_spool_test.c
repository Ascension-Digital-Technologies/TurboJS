#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static TurboJSIRInstruction ins(TurboJSIROpcode opcode, uint16_t dst,
                                uint16_t left, int64_t immediate)
{
    TurboJSIRInstruction value;
    memset(&value, 0, sizeof(value));
    value.opcode = opcode;
    value.destination = dst;
    value.left = left;
    value.right = TURBOJS_IR_NO_REGISTER;
    value.immediate = immediate;
    return value;
}

int main(void)
{
    TurboJSIRFunction function;
    TurboJSBoxedValue argument, result;
    uint16_t arg, truthy, local;

    TurboJS_IRFunctionInit(&function, 1);
    TurboJS_IRFunctionSetLocalCount(&function, 1);
    arg = TurboJS_IRAllocateRegister(&function);
    truthy = TurboJS_IRAllocateRegister(&function);
    local = TurboJS_IRAllocateRegister(&function);
    CHECK(arg != TURBOJS_IR_NO_REGISTER && truthy != TURBOJS_IR_NO_REGISTER && local != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_VALUE_ARGUMENT, arg, TURBOJS_IR_NO_REGISTER, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_VALUE_LOCAL_SET, TURBOJS_IR_NO_REGISTER, arg, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_VALUE_LOCAL_GET, local, TURBOJS_IR_NO_REGISTER, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_VALUE_TO_BOOLEAN, truthy, local, 0)) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&function, ins(TURBOJS_IR_VALUE_RETURN, TURBOJS_IR_NO_REGISTER, truthy, 0)) == TURBOJS_IR_OK);

    memset(&argument, 0, sizeof(argument));
    argument.tag = TURBOJS_BOXED_HEAP_REFERENCE;
    argument.as.reference = &function;
    CHECK(TurboJS_IRExecuteTagged(&function, &argument, 1, &result) == TURBOJS_IR_OK);
    CHECK(result.tag == TURBOJS_BOXED_BOOLEAN && result.as.integer == 1);

    argument.tag = TURBOJS_BOXED_UNDEFINED;
    argument.as.bits = 0;
    CHECK(TurboJS_IRExecuteTagged(&function, &argument, 1, &result) == TURBOJS_IR_OK);
    CHECK(result.tag == TURBOJS_BOXED_BOOLEAN && result.as.integer == 0);

    TurboJS_IRFunctionDestroy(&function);
    return 0;
}
