#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "jit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int run_case(TurboJSOSRLoopComparison comparison, int32_t step,
                    int64_t induction, int64_t limit, int64_t accumulator,
                    int64_t expected_i, int64_t expected_sum) {
    TurboJSOSRCountedLoopSpec spec = {0, 1, 2, step, 12, 91, 10000000, comparison};
    TurboJSOSRLoopProgram *program = NULL;
    TurboJSOSRFrame frame;
    TurboJSOSRValue locals[3] = {
        {(uint64_t)induction, TURBOJS_OSR_VALUE_INT64, 0},
        {(uint64_t)limit, TURBOJS_OSR_VALUE_INT64, 0},
        {(uint64_t)accumulator, TURBOJS_OSR_VALUE_INT64, 0}
    };
    TurboJSOSRState state;
    TurboJSOSREntry entry;
    TurboJSOSRExecutionResult result;
    TurboJSIRDiagnostic diagnostic;
    memset(&frame, 0, sizeof(frame));
    memset(&diagnostic, 0, sizeof(diagnostic));
    CHECK(TurboJS_OSRCompileCountedI64Loop(&spec, &program, &diagnostic) == TURBOJS_IR_OK);
    CHECK(program != NULL && TurboJS_OSRLoopProgramCodeSize(program) > 0);
    CHECK(TurboJS_OSRFrameInit(&frame, 3, 0) == TURBOJS_IR_OK);
    CHECK(TurboJS_OSRFrameCapture(&frame, locals, 3, NULL, 0, 40, 12) == TURBOJS_IR_OK);
    TurboJS_OSRStateInit(&state, 12, 1);
    TurboJS_OSRMarkCodeReady(&state);
    entry = TurboJS_OSRLoopProgramEntry(program);
    CHECK(TurboJS_OSRExecuteEntry(&state, &entry, &frame, &result) == TURBOJS_IR_OK);
    CHECK(result.exit_kind == TURBOJS_OSR_EXIT_COMPLETED && result.resume_bytecode_offset == 91);
    CHECK((int64_t)frame.locals[0].bits == expected_i);
    CHECK((int64_t)frame.locals[2].bits == expected_sum);
    CHECK(state.entry_count == 1);
    TurboJS_OSRFrameDestroy(&frame);
    TurboJS_OSRLoopProgramDestroy(program);
    return 0;
}

static int run_f64_case(void) {
    TurboJSOSRCountedLoopSpec spec = {0, 1, 2, 1, 12, 91, 10000000, TURBOJS_OSR_LOOP_LT};
    TurboJSOSRLoopProgram *program = NULL;
    TurboJSOSRFrame frame;
    double initial = 0.5, actual;
    uint64_t initial_bits;
    TurboJSOSRValue locals[3];
    TurboJSOSRState state;
    TurboJSOSREntry entry;
    TurboJSOSRExecutionResult result;
    TurboJSIRDiagnostic diagnostic;
    memcpy(&initial_bits, &initial, sizeof(initial_bits));
    locals[0] = (TurboJSOSRValue){0, TURBOJS_OSR_VALUE_INT64, 0};
    locals[1] = (TurboJSOSRValue){1000000, TURBOJS_OSR_VALUE_INT64, 0};
    locals[2] = (TurboJSOSRValue){initial_bits, TURBOJS_OSR_VALUE_FLOAT64, 0};
    memset(&frame, 0, sizeof(frame));
    memset(&diagnostic, 0, sizeof(diagnostic));
    CHECK(TurboJS_OSRCompileCountedI64Loop(&spec, &program, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_OSRFrameInit(&frame, 3, 0) == TURBOJS_IR_OK);
    CHECK(TurboJS_OSRFrameCapture(&frame, locals, 3, NULL, 0, 40, 12) == TURBOJS_IR_OK);
    TurboJS_OSRStateInit(&state, 12, 1);
    TurboJS_OSRMarkCodeReady(&state);
    entry = TurboJS_OSRLoopProgramEntry(program);
    CHECK(TurboJS_OSRExecuteEntry(&state, &entry, &frame, &result) == TURBOJS_IR_OK);
    CHECK(result.exit_kind == TURBOJS_OSR_EXIT_COMPLETED);
    memcpy(&actual, &frame.locals[2].bits, sizeof(actual));
    CHECK(actual == 499999500000.5);
    CHECK((int64_t)frame.locals[0].bits == 1000000);
    TurboJS_OSRFrameDestroy(&frame);
    TurboJS_OSRLoopProgramDestroy(program);
    return 0;
}

int main(void) {
    CHECK(run_case(TURBOJS_OSR_LOOP_LT, 1, 0, 1000000, 0,
                   1000000, 499999500000LL) == 0);
    CHECK(run_case(TURBOJS_OSR_LOOP_LTE, 1, 5, 8, 10,
                   9, 36) == 0); /* 10 + 5 + 6 + 7 + 8 */
    CHECK(run_case(TURBOJS_OSR_LOOP_GT, -1, 8, 4, 0,
                   4, 26) == 0); /* 8 + 7 + 6 + 5 */
    CHECK(run_case(TURBOJS_OSR_LOOP_GTE, -1, 8, 5, 1,
                   4, 27) == 0); /* 1 + 8 + 7 + 6 + 5 */
    CHECK(run_case(TURBOJS_OSR_LOOP_LT, 2, 3, 12, 7,
                   13, 42) == 0); /* 7 + 3 + 5 + 7 + 9 + 11 */
    CHECK(run_case(TURBOJS_OSR_LOOP_GT, -3, 10, 0, 5,
                   -2, 27) == 0); /* 5 + 10 + 7 + 4 + 1 */
    CHECK(run_f64_case() == 0);
    puts("Native integer and Float64 OSR loops passed");
    return 0;
}
