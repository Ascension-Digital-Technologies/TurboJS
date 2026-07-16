#include "jit.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int test_frontiers(void) {
    TurboJSIRFunction ir;
    TurboJSSSAGraph graph;
    TurboJSIRDiagnostic diagnostic = {0};
    size_t i;

    TurboJS_IRFunctionInit(&ir, 1);
    TurboJS_SSAGraphInit(&graph);
    for (i = 0; i < 3; ++i) CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_ARGUMENT, .destination=0, .immediate=0 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_BRANCH_FALSE, .left=0, .target=4 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_CONSTANT_I64, .destination=1, .immediate=10 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_JUMP, .target=5 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_CONSTANT_I64, .destination=1, .immediate=20 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_RETURN_I64, .left=1 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSABuildFromIR(&ir, &graph, &diagnostic) == TURBOJS_IR_OK);
    CHECK(graph.block_count == 4);
    CHECK((graph.blocks[1].dominance_frontier_mask & (1ull << 3)) != 0);
    CHECK((graph.blocks[2].dominance_frontier_mask & (1ull << 3)) != 0);
    TurboJS_SSAGraphDestroy(&graph);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}

static int test_specialization(void) {
    TurboJSIRFunction ir;
    TurboJSSSAGraph graph;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSFeedbackVector feedback;
    TurboJSSSAOptimizationStats stats = {0};
    int64_t args[2] = {12, 30};
    size_t i;
    int guards = 0;

    TurboJS_IRFunctionInit(&ir, 2);
    TurboJS_SSAGraphInit(&graph);
    for (i = 0; i < 3; ++i) CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_ARGUMENT, .destination=0, .immediate=0 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_ARGUMENT, .destination=1, .immediate=1 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_ADD_I64, .destination=2, .left=0, .right=1 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_RETURN_I64, .left=2 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSABuildFromIR(&ir, &graph, &diagnostic) == TURBOJS_IR_OK);

    TurboJS_FeedbackVectorInit(&feedback, 2);
    for (i = 0; i < 1000; ++i) TurboJS_FeedbackObserveCall(&feedback, args, 2);
    CHECK(TurboJS_SSASpecializeFromFeedback(&graph, &feedback, &stats) == TURBOJS_IR_OK);
    CHECK(stats.guards_inserted == 2);
    CHECK(graph.deopt_exit_count == 2);
    for (i = 0; i < graph.value_count; ++i) {
        const TurboJSSSAValue *v = &graph.values[i];
        if (v->opcode == TURBOJS_SSA_ARGUMENT) CHECK(v->type == TURBOJS_SSA_TYPE_INT32);
        if (v->opcode == TURBOJS_SSA_GUARD_INT32) {
            CHECK(v->has_deopt_edge);
            CHECK(v->deopt_id < graph.deopt_exit_count);
            guards++;
        }
    }
    CHECK(guards == 2);
    CHECK(TurboJS_SSAVerify(&graph));
    TurboJS_SSAGraphDestroy(&graph);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}

int main(void) {
    CHECK(test_frontiers() == 0);
    CHECK(test_specialization() == 0);
    puts("SSA dominance frontiers and feedback specialization passed");
    return 0;
}
