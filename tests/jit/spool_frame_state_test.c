#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void)
{
    TurboJSSpoolFrameState state, clone;
    TurboJSBoxedValue value, out;
    memset(&value, 0, sizeof(value));
    value.tag = TURBOJS_BOXED_INT32;
    value.as.integer = 42;

    CHECK(TurboJS_SpoolFrameStateInit(&state, 96, 2) == TURBOJS_IR_OK);
    CHECK(TurboJS_SpoolFrameStateSetLocal(&state, 95, value) == TURBOJS_IR_OK);
    for (int i = 0; i < 130; ++i) {
        value.as.integer = i;
        CHECK(TurboJS_SpoolFrameStatePush(&state, value) == TURBOJS_IR_OK);
    }
    CHECK(state.stack_count == 130 && state.stack_capacity >= 130);
    CHECK(TurboJS_SpoolFrameStateClone(&state, &clone) == TURBOJS_IR_OK);
    CHECK(TurboJS_SpoolFrameStateShapeCompatible(&state, &clone));
    CHECK(TurboJS_SpoolFrameStateGetLocal(&clone, 95, &out) == TURBOJS_IR_OK);
    CHECK(out.tag == TURBOJS_BOXED_INT32 && out.as.integer == 42);
    CHECK(TurboJS_SpoolFrameStatePop(&clone, &out) == TURBOJS_IR_OK);
    CHECK(out.as.integer == 129 && clone.stack_count == 129);
    CHECK(!TurboJS_SpoolFrameStateShapeCompatible(&state, &clone));

    TurboJS_SpoolFrameStateDestroy(&clone);
    TurboJS_SpoolFrameStateDestroy(&state);
    return 0;
}
