#include <stdint.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    return 1; \
} } while (0)

int main(void)
{
    TurboJSJSFrameLayout layout;
    CHECK(TurboJS_JSFrameLayoutInit(&layout, 3, 5, 7) == TURBOJS_IR_OK);
    CHECK(layout.abi_version == TURBOJS_JS_FRAME_ABI_VERSION);
    CHECK(layout.slot_size == 8);
    CHECK(layout.fixed_slot_count == TURBOJS_JS_FRAME_FIXED_SLOT_COUNT);
    CHECK(layout.total_slots == TURBOJS_JS_FRAME_FIXED_SLOT_COUNT + 15);
    CHECK(layout.frame_size_bytes == layout.total_slots * 8);

    CHECK(TurboJS_JSFrameFixedSlotOffset(TURBOJS_JS_FRAME_CONTEXT) == 16);
    CHECK(TurboJS_JSFrameArgumentOffset(&layout, 0) ==
          TURBOJS_JS_FRAME_FIXED_SLOT_COUNT * 8);
    CHECK(TurboJS_JSFrameArgumentOffset(&layout, 2) ==
          (TURBOJS_JS_FRAME_FIXED_SLOT_COUNT + 2) * 8);
    CHECK(TurboJS_JSFrameLocalOffset(&layout, 0) ==
          (TURBOJS_JS_FRAME_FIXED_SLOT_COUNT + 3) * 8);
    CHECK(TurboJS_JSFrameStackOffset(&layout, 0) ==
          (TURBOJS_JS_FRAME_FIXED_SLOT_COUNT + 3 + 5) * 8);
    CHECK(TurboJS_JSFrameStackOffset(&layout, 7) == SIZE_MAX);

    puts("TurboJS Spool/Redline frame ABI passed");
    return 0;
}
