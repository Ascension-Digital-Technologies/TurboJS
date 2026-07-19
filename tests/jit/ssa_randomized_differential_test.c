#include "jit.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

typedef struct RNG { uint64_t state; } RNG;
static uint32_t rng_next(RNG *r) { r->state ^= r->state << 13; r->state ^= r->state >> 7; r->state ^= r->state << 17; return (uint32_t)r->state; }

static int64_t eval_graph(const TurboJSSSAGraph *g, int64_t arg, int *ok)
{
    int64_t *values = calloc(g->value_count, sizeof(*values));
    size_t i;
    int64_t result = 0;
    *ok = values != NULL;
    if (!values) return 0;
    for (i = 0; i < g->value_count && *ok; ++i) {
        const TurboJSSSAValue *v = &g->values[i];
        if (v->removed) continue;
        switch (v->opcode) {
        case TURBOJS_SSA_ARGUMENT: values[i] = arg; break;
        case TURBOJS_SSA_CONSTANT_I64: values[i] = v->immediate; break;
        case TURBOJS_SSA_ADD_I64: values[i] = (int64_t)((uint64_t)values[v->left] + (uint64_t)values[v->right]); break;
        case TURBOJS_SSA_SUB_I64: values[i] = (int64_t)((uint64_t)values[v->left] - (uint64_t)values[v->right]); break;
        case TURBOJS_SSA_MUL_I64: values[i] = (int64_t)((uint64_t)values[v->left] * (uint64_t)values[v->right]); break;
        case TURBOJS_SSA_LESS_THAN_I64: values[i] = values[v->left] < values[v->right]; break;
        case TURBOJS_SSA_RETURN: result = values[v->left]; break;
        default: break;
        }
    }
    free(values);
    return result;
}

static int run_case(RNG *rng, unsigned case_index)
{
    TurboJSIRFunction ir;
    TurboJSSSAGraph before, after;
    TurboJSIRDiagnostic diagnostic = {0};
    int64_t constants[24];
    unsigned i, op_count = 6 + (rng_next(rng) % 18);
    int ok1, ok2;
    int64_t expected, actual;
    TurboJS_IRFunctionInit(&ir, 0);
    TurboJS_SSAGraphInit(&before);
    TurboJS_SSAGraphInit(&after);
    for (i = 0; i < op_count + 2; ++i) CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_ARGUMENT,.destination=0,.immediate=0}) == TURBOJS_IR_OK);
    constants[0] = (int64_t)((int32_t)(rng_next(rng) % 2001) - 1000);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_CONSTANT_I64,.destination=1,.immediate=constants[0]}) == TURBOJS_IR_OK);
    for (i = 0; i < op_count; ++i) {
        uint32_t left = rng_next(rng) % (i + 2);
        uint32_t right = rng_next(rng) % (i + 2);
        TurboJSIROpcode op;
        switch (rng_next(rng) % 3) {
        case 0: op = TURBOJS_IR_ADD_I64; break;
        case 1: op = TURBOJS_IR_SUB_I64; break;
        default: op = TURBOJS_IR_MUL_I64; break;
        }
        CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=op,.destination=i+2,.left=left,.right=right}) == TURBOJS_IR_OK);
    }
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_RETURN_I64,.left=op_count+1}) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSABuildFromIR(&ir, &before, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSABuildFromIR(&ir, &after, &diagnostic) == TURBOJS_IR_OK);
    expected = eval_graph(&before, (int64_t)(int32_t)rng_next(rng), &ok1);
    (void)TurboJS_SSAOptimize(&after);
    CHECK(TurboJS_SSAVerify(&after));
    actual = eval_graph(&after, (int64_t)(int32_t)rng_next(rng), &ok2);
    /* Re-evaluate with a deterministic shared argument. */
    {
        int64_t arg = (int64_t)((int32_t)(case_index * 7919u));
        expected = eval_graph(&before, arg, &ok1);
        actual = eval_graph(&after, arg, &ok2);
    }
    CHECK(ok1 && ok2);
    if (expected != actual) {
        fprintf(stderr, "differential mismatch case=%u expected=%" PRId64 " actual=%" PRId64 "\n", case_index, expected, actual);
        return 1;
    }
    TurboJS_SSAGraphDestroy(&before);
    TurboJS_SSAGraphDestroy(&after);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}

static int test_redundant_guards(void)
{
    TurboJSIRFunction ir;
    TurboJSSSAGraph graph;
    TurboJSFeedbackVector feedback;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSSSAOptimizationStats stats = {0};
    TurboJS_IRFunctionInit(&ir, 1);
    TurboJS_SSAGraphInit(&graph);
    TurboJS_FeedbackVectorInit(&feedback, 1);
    CHECK(TurboJS_IRAllocateRegister(&ir) != TURBOJS_IR_NO_REGISTER);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_ARGUMENT,.destination=0,.immediate=0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_IREmit(&ir, (TurboJSIRInstruction){.opcode=TURBOJS_IR_RETURN_I64,.left=0}) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSABuildFromIR(&ir, &graph, &diagnostic) == TURBOJS_IR_OK);
    { int64_t argument = 7; TurboJS_FeedbackObserveCall(&feedback, &argument, 1); TurboJS_FeedbackObserveCall(&feedback, &argument, 1); }
    CHECK(TurboJS_SSASpecializeFromFeedback(&graph, &feedback, &stats) == TURBOJS_IR_OK);
    CHECK(TurboJS_SSASpecializeFromFeedback(&graph, &feedback, &stats) == TURBOJS_IR_OK);
    stats = TurboJS_SSAOptimize(&graph);
    CHECK(stats.guards_eliminated == 1);
    CHECK(TurboJS_SSAVerify(&graph));
    TurboJS_SSAGraphDestroy(&graph);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}

int main(void)
{
    RNG rng = { UINT64_C(0x9e3779b97f4a7c15) };
    unsigned i;
    CHECK(test_redundant_guards() == 0);
    for (i = 0; i < 1000; ++i) CHECK(run_case(&rng, i) == 0);
    puts("1000 randomized SSA optimizer differential cases passed");
    return 0;
}
