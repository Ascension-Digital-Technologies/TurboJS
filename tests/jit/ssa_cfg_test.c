#include "jit.h"
#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int build_diamond(void) {
    TurboJSIRFunction ir;
    TurboJSSSAGraph graph;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSSSAOptimizationStats stats;
    size_t i;
    int found_phi = 0;

    TurboJS_IRFunctionInit(&ir, 0);
    TurboJS_SSAGraphInit(&graph);
    for (i = 0; i < 2; ++i) CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);

    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_CONSTANT_I64, .destination=0, .immediate=1 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_BRANCH_FALSE, .left=0, .target=4 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_CONSTANT_I64, .destination=1, .immediate=10 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_JUMP, .target=5 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_CONSTANT_I64, .destination=1, .immediate=20 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_RETURN_I64, .left=1 }) == TURBOJS_IR_OK);

    CHECK(TurboJS_SSABuildFromIR(&ir, &graph, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSAVerify(&graph));
    CHECK(graph.block_count == 4);
    CHECK(graph.blocks[0].successor_count == 2);
    CHECK(graph.blocks[3].predecessor_count == 2);
    CHECK(graph.blocks[1].immediate_dominator == 0);
    CHECK(graph.blocks[2].immediate_dominator == 0);
    CHECK(graph.blocks[3].immediate_dominator == 0);

    for (i = 0; i < graph.value_count; ++i) {
        if (graph.values[i].opcode == TURBOJS_SSA_PHI && graph.values[i].block == 3) found_phi = 1;
    }
    CHECK(found_phi);

    stats = TurboJS_SSAOptimize(&graph);
    CHECK(stats.branches_folded == 1);
    CHECK(stats.blocks_removed >= 1);

    TurboJS_SSAGraphDestroy(&graph);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}

static int build_loop(void) {
    TurboJSIRFunction ir;
    TurboJSSSAGraph graph;
    TurboJSIRDiagnostic diagnostic = {0};
    size_t i;
    int found_loop = 0;

    TurboJS_IRFunctionInit(&ir, 1);
    TurboJS_SSAGraphInit(&graph);
    for (i = 0; i < 4; ++i) CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);

    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_ARGUMENT, .destination=0, .immediate=0 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_CONSTANT_I64, .destination=1, .immediate=0 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_LESS_THAN_I64, .destination=2, .left=1, .right=0 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_BRANCH_FALSE, .left=2, .target=7 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_CONSTANT_I64, .destination=3, .immediate=1 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_ADD_I64, .destination=1, .left=1, .right=3 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_JUMP, .target=2 }) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){ .opcode=TURBOJS_IR_RETURN_I64, .left=1 }) == TURBOJS_IR_OK);

    CHECK(TurboJS_SSABuildFromIR(&ir, &graph, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSAVerify(&graph));
    for (i = 0; i < graph.block_count; ++i) {
        if (graph.blocks[i].loop_depth > 0) found_loop = 1;
    }
    CHECK(found_loop);

    TurboJS_SSAGraphDestroy(&graph);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}

int main(void) {
    CHECK(build_diamond() == 0);
    CHECK(build_loop() == 0);
    puts("SSA CFG, dominators, phi insertion, and loop discovery passed");
    return 0;
}
