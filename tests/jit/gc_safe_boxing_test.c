#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

typedef struct RootState {
    unsigned retains;
    unsigned releases;
    void *expected;
} RootState;

static void *retain_ref(void *opaque, void *reference)
{
    RootState *state = (RootState *)opaque;
    if (reference != state->expected)
        return NULL;
    state->retains++;
    return reference;
}

static void release_ref(void *opaque, void *reference)
{
    RootState *state = (RootState *)opaque;
    if (reference == state->expected)
        state->releases++;
}

int main(void)
{
    int object;
    double number = 3.141592653589793;
    int64_t number_bits;
    int64_t registers[3];
    TurboJSValueKind kinds[3] = {
        TURBOJS_VALUE_F64,
        TURBOJS_VALUE_HEAP_REFERENCE,
        TURBOJS_VALUE_BOOLEAN
    };
    TurboJSDeoptFrame native_frame;
    TurboJSBoxedDeoptFrame boxed;
    TurboJSRootingHooks hooks;
    RootState roots;

    memcpy(&number_bits, &number, sizeof(number_bits));
    registers[0] = number_bits;
    registers[1] = (int64_t)(uintptr_t)&object;
    registers[2] = 1;
    memset(&native_frame, 0, sizeof(native_frame));
    native_frame.register_count = 3;
    native_frame.materialized_register_mask = 7;
    native_frame.live_register_mask = 7;
    native_frame.register_values = registers;
    native_frame.register_kinds = kinds;

    memset(&roots, 0, sizeof(roots));
    roots.expected = &object;
    hooks.opaque = &roots;
    hooks.retain = retain_ref;
    hooks.release = release_ref;

    CHECK(TurboJS_BoxDeoptFrameRooted(&native_frame, &hooks, &boxed) == TURBOJS_IR_OK);
    CHECK(boxed.registers[0].tag == TURBOJS_BOXED_FLOAT64);
    CHECK(boxed.registers[0].as.number == number);
    CHECK(boxed.registers[1].tag == TURBOJS_BOXED_HEAP_REFERENCE);
    CHECK(boxed.registers[1].as.reference == &object);
    CHECK(boxed.registers[2].tag == TURBOJS_BOXED_BOOLEAN);
    CHECK(boxed.registers[2].as.integer == 1);
    CHECK(boxed.reference_register_mask == 2);
    CHECK(roots.retains == 1 && roots.releases == 0);
    TurboJS_BoxedDeoptFrameDestroy(&boxed);
    CHECK(roots.releases == 1);

    CHECK(TurboJS_BoxDeoptFrame(&native_frame, &boxed) == TURBOJS_IR_OK);
    CHECK(boxed.registers[1].tag == TURBOJS_BOXED_UNDEFINED);
    CHECK(boxed.reference_register_mask == 0);
    TurboJS_BoxedDeoptFrameDestroy(&boxed);

    puts("GC-safe boxed deoptimization values test passed");
    return 0;
}
