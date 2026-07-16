#include <limits.h>
#include <stdio.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int build_add(TurboJSIRFunction *ir)
{
    TurboJSIRInstruction ins = {0};
    uint16_t a, b, sum;
    TurboJS_IRFunctionInit(ir, 2);
    a = TurboJS_IRAllocateRegister(ir);
    b = TurboJS_IRAllocateRegister(ir);
    sum = TurboJS_IRAllocateRegister(ir);
    ins.opcode = TURBOJS_IR_ARGUMENT; ins.destination = a; ins.immediate = 0;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ARGUMENT; ins.destination = b; ins.immediate = 1;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_ADD_I64; ins.destination = sum; ins.left = a; ins.right = b;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    ins.opcode = TURBOJS_IR_RETURN_I64; ins.left = sum;
    CHECK(TurboJS_IREmit(ir, ins) == TURBOJS_IR_OK);
    return 0;
}

int main(void)
{
    TurboJSFeedbackVector feedback;
    TurboJSOptimizationPolicy policy = TurboJS_OptimizationPolicyDefault();
    TurboJSOptimizationReport report;
    TurboJSIRFunction ir;
    TurboJSCodeCache *cache;
    TurboJSTieredFunction tiered;
    TurboJSTieredResult route;
    TurboJSIRDiagnostic diagnostic = {0};
    int key;
    int64_t args[2] = {20, 22};
    int64_t wide[2] = {(int64_t)INT_MAX + 100, 1};
    int64_t result;
    int i;

    TurboJS_FeedbackVectorInit(&feedback, 2);
    for (i = 0; i < 8; i++) {
        TurboJS_FeedbackObserveCall(&feedback, args, 2);
        TurboJS_FeedbackObserveResult(&feedback, 42);
    }
    policy.minimum_executions = 8;
    report = TurboJS_EvaluateOptimization(&feedback, &policy);
    CHECK(report.decision == TURBOJS_OPTIMIZATION_ELIGIBLE);
    CHECK(TurboJS_FeedbackSlotIsStable(&feedback.arguments[0]));
    CHECK(feedback.arguments[0].observed_types == TURBOJS_FEEDBACK_INT32);

    TurboJS_FeedbackObserveCall(&feedback, wide, 2);
    TurboJS_FeedbackObserveResult(&feedback, wide[0] + 1);
    report = TurboJS_EvaluateOptimization(&feedback, &policy);
    CHECK(report.decision == TURBOJS_OPTIMIZATION_UNSTABLE_ARGUMENTS);
    CHECK(report.unstable_argument == 0);
    CHECK(feedback.arguments[0].transitions == 1);

    TurboJS_FeedbackVectorInit(&feedback, 1);
    for (i = 0; i < 8; i++) {
        TurboJS_FeedbackObserveCall(&feedback, args, 1);
        TurboJS_FeedbackObserveResult(&feedback, args[0]);
    }
    for (i = 0; i < 4; i++) TurboJS_FeedbackObserveBailout(&feedback);
    report = TurboJS_EvaluateOptimization(&feedback, &policy);
    CHECK(report.decision == TURBOJS_OPTIMIZATION_TOO_MANY_BAILOUTS);

    CHECK(build_add(&ir) == 0);
    cache = TurboJS_CodeCacheCreate(2, 4096);
    CHECK(cache != NULL);
    TurboJS_TieredFunctionInit(&tiered, &key, 2);
    CHECK(TurboJS_TieredInvoke(&tiered, cache, &ir, args, 2, &result, &route, &diagnostic) == TURBOJS_IR_OK);
    CHECK(TurboJS_TieredInvoke(&tiered, cache, &ir, args, 2, &result, &route, &diagnostic) == TURBOJS_IR_OK);
    CHECK(tiered.feedback.execution_count == 2);
    CHECK(tiered.feedback.result.observations == 2);
    CHECK(tiered.feedback.arguments[0].observed_types == TURBOJS_FEEDBACK_INT32);

    printf("type feedback and optimization eligibility passed: %s\n",
           TurboJS_OptimizationDecisionName(TURBOJS_OPTIMIZATION_ELIGIBLE));
    TurboJS_CodeCacheDestroy(cache);
    TurboJS_IRFunctionDestroy(&ir);
    return 0;
}
