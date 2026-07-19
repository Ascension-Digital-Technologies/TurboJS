#include <stdint.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    return 1; \
} } while (0)

int main(void)
{
    TurboJSCallFeedbackSlot slot;
    uint64_t target = 0;
    TurboJS_CallFeedbackInit(&slot);
    CHECK(TurboJS_CallFeedbackGetState(&slot) ==
          TURBOJS_CALL_FEEDBACK_UNINITIALIZED);

    CHECK(TurboJS_CallFeedbackObserve(&slot, 11) ==
          TURBOJS_CALL_FEEDBACK_MONOMORPHIC);
    CHECK(TurboJS_CallFeedbackObserve(&slot, 11) ==
          TURBOJS_CALL_FEEDBACK_MONOMORPHIC);
    CHECK(TurboJS_CallFeedbackMonomorphicTarget(&slot, 2, &target));
    CHECK(target == 11);

    CHECK(TurboJS_CallFeedbackObserve(&slot, 22) ==
          TURBOJS_CALL_FEEDBACK_POLYMORPHIC);
    CHECK(!TurboJS_CallFeedbackMonomorphicTarget(&slot, 1, &target));
    CHECK(TurboJS_CallFeedbackObserve(&slot, 33) ==
          TURBOJS_CALL_FEEDBACK_POLYMORPHIC);
    CHECK(TurboJS_CallFeedbackObserve(&slot, 44) ==
          TURBOJS_CALL_FEEDBACK_POLYMORPHIC);
    CHECK(TurboJS_CallFeedbackObserve(&slot, 55) ==
          TURBOJS_CALL_FEEDBACK_MEGAMORPHIC);
    CHECK(slot.target_count == TURBOJS_CALL_FEEDBACK_MAX_TARGETS);
    CHECK(slot.generation == 3);
    CHECK(slot.observations == 6);
    CHECK(slot.misses == 5);
    CHECK(TurboJS_CallFeedbackObserve(&slot, 11) ==
          TURBOJS_CALL_FEEDBACK_MEGAMORPHIC);

    puts("TurboJS Telemetry call feedback passed");
    return 0;
}
