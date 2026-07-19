#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "jit.h"

typedef enum TurboJSEngineOpcode {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) OP_ ## id,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
} TurboJSEngineOpcode;


typedef struct MockObject {
    uintptr_t shape;
    TurboJSRegionValue slots[8];
} MockObject;

static int mock_guard_shape(TurboJSRegionValue value, uintptr_t expected, void *opaque)
{
    MockObject *object = (MockObject *)(uintptr_t)value;
    (void)opaque;
    return object && object->shape == expected;
}

static int mock_load_slot(TurboJSRegionValue value, uint32_t index,
                          TurboJSRegionValue *out, void *opaque)
{
    MockObject *object = (MockObject *)(uintptr_t)value;
    (void)opaque;
    if (!object || index >= 8 || !out)
        return 0;
    *out = object->slots[index];
    return 1;
}

static int mock_store_slot(TurboJSRegionValue value, uint32_t index,
                           TurboJSRegionValue stored, void *opaque)
{
    MockObject *object = (MockObject *)(uintptr_t)value;
    (void)opaque;
    if (!object || index >= 8)
        return 0;
    object->slots[index] = stored;
    return 1;
}

static int mock_guard_dependency(uint16_t generation, uint16_t flags, void *opaque)
{
    const uint16_t *minimum = (const uint16_t *)opaque;
    (void)flags;
    return !minimum || generation >= *minimum;
}

static int mock_to_i64(TurboJSRegionValue value, int64_t *out, void *opaque)
{
    (void)opaque;
    if (!out)
        return 0;
    *out = (int64_t)value;
    return 1;
}

static TurboJSRegionValue mock_from_i64(int64_t value, void *opaque)
{
    (void)opaque;
    return (TurboJSRegionValue)value;
}

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int has_opcode(const TurboJSSSAGraph *g, TurboJSSSAOpcode opcode, uint32_t atom,
                      uint32_t *left, uint32_t *right)
{
    size_t i;
    for (i = 0; i < g->value_count; ++i) {
        const TurboJSSSAValue *v = &g->values[i];
        if (v->opcode == opcode && v->metadata == atom) {
            if (left) *left = v->left;
            if (right) *right = v->right;
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    const uint32_t atom = 0x12345678u;
    const uint8_t load_code[] = {
        OP_get_arg0,
        OP_get_field, 0x78, 0x56, 0x34, 0x12,
        OP_return
    };
    const uint8_t store_code[] = {
        OP_get_arg0,
        OP_get_arg1,
        OP_put_field, 0x78, 0x56, 0x34, 0x12,
        OP_push_0,
        OP_return
    };
    TurboJSEngineBytecodeInfo input;
    TurboJSSSAGraph graph;
    TurboJSIRDiagnostic diagnostic = {0};
    TurboJSRegionNativeFunction *native = NULL;
    TurboJSRegionNativeStats native_stats = {0};
    uint32_t left, right, applied;
    TurboJSPropertyFeedback feedback;

    TurboJS_SSAGraphInit(&graph);
    input.bytecode = load_code; input.bytecode_length = sizeof(load_code);
    input.argument_count = 1; input.local_count = 0; input.stack_size = 1;
    input.numeric_mode = TURBOJS_ENGINE_NUMERIC_INT32;
    CHECK(TurboJS_EngineBytecodeRegionBuildSSA(&input, &graph, &diagnostic) == TURBOJS_IR_OK);
    CHECK(has_opcode(&graph, TURBOJS_SSA_PROPERTY_LOAD, atom, &left, &right));
    CHECK(left != TURBOJS_SSA_NO_VALUE);
    CHECK(right == TURBOJS_SSA_NO_VALUE);
    CHECK(graph.values[left].opcode == TURBOJS_SSA_ARGUMENT);
    feedback.source_instruction = 1;
    feedback.atom = atom;
    feedback.shape_identity = (uintptr_t)0x1000;
    feedback.property_index = 7;
    feedback.flags = TURBOJS_PROPERTY_FEEDBACK_LOAD | TURBOJS_PROPERTY_FEEDBACK_OWN_DATA;
    feedback.generation = 3;
    applied = 0;
    CHECK(TurboJS_SSAApplyPropertyFeedback(&graph, &feedback, 1, &applied) == TURBOJS_IR_OK);
    CHECK(applied == 1);
    {
        size_t i; int found = 0;
        for (i = 0; i < graph.value_count; ++i) {
            TurboJSSSAValue *v = &graph.values[i];
            if (v->opcode == TURBOJS_SSA_PROPERTY_LOAD) {
                CHECK(v->guard_shape == (uintptr_t)0x1000);
                CHECK(v->property_index == 7);
                CHECK(v->property_feedback_generation == 3);
                found = 1;
            }
        }
        CHECK(found);
    }
    {
        TurboJSRegionObjectLayout layout = {0};
        layout.object_pointer_mask = UINT64_MAX;
        layout.shape_offset = (uint32_t)offsetof(MockObject, shape);
        layout.property_storage_offset = (uint32_t)offsetof(MockObject, slots);
        layout.property_stride = (uint32_t)sizeof(TurboJSRegionValue);
        layout.property_value_offset = 0;
        layout.property_storage_indirect = 0;
        CHECK(TurboJS_RegionNativeCompileWithObjectLayout(&graph, &layout, &native,
                                                          &native_stats, &diagnostic) == TURBOJS_IR_OK);
    }
    CHECK(native != NULL);
#if defined(__x86_64__) && !defined(_WIN32)
    CHECK(native_stats.inline_property_pic_cases == 1);
    CHECK(native_stats.inline_property_loads == 1);
    CHECK(native_stats.inline_property_stores == 0);
    CHECK(TurboJS_RegionNativeCodeSize(native) > 0);
    CHECK(native_stats.native_code_bytes == TurboJS_RegionNativeCodeSize(native));
    CHECK(native_stats.frame_bytes == 64);
#endif
    {
        MockObject object = {0};
        TurboJSRegionValue argument, result_value = 0;
        TurboJSRegionValueOps ops = {
            mock_guard_shape, mock_load_slot, mock_store_slot,
            mock_guard_dependency, mock_to_i64, mock_from_i64
        };
        object.shape = (uintptr_t)0x1000;
        object.slots[7] = 1234;
        argument = (TurboJSRegionValue)(uintptr_t)&object;
        CHECK(TurboJS_RegionNativeInvokeValues(native, &argument, 1, &ops,
                                                NULL, &result_value) == TURBOJS_IR_OK);
        CHECK(result_value == 1234);
        object.shape = (uintptr_t)0x9999;
        CHECK(TurboJS_RegionNativeInvokeValues(native, &argument, 1, &ops,
                                                NULL, &result_value) == TURBOJS_IR_BAILOUT);
    }
    TurboJS_RegionNativeFunctionDestroy(native);
    native = NULL;
    TurboJS_SSAGraphDestroy(&graph);

    TurboJS_SSAGraphInit(&graph);
    input.bytecode = store_code; input.bytecode_length = sizeof(store_code);
    input.argument_count = 2; input.local_count = 0; input.stack_size = 2;
    CHECK(TurboJS_EngineBytecodeRegionBuildSSA(&input, &graph, &diagnostic) == TURBOJS_IR_OK);
    CHECK(has_opcode(&graph, TURBOJS_SSA_PROPERTY_STORE, atom, &left, &right));
    CHECK(left != TURBOJS_SSA_NO_VALUE && right != TURBOJS_SSA_NO_VALUE);
    CHECK(graph.values[left].opcode == TURBOJS_SSA_ARGUMENT);
    CHECK(graph.values[right].opcode == TURBOJS_SSA_ARGUMENT);
    feedback.source_instruction = 2;
    feedback.atom = atom;
    feedback.shape_identity = (uintptr_t)0x2000;
    feedback.property_index = 4;
    feedback.flags = TURBOJS_PROPERTY_FEEDBACK_STORE | TURBOJS_PROPERTY_FEEDBACK_OWN_DATA;
    feedback.generation = 4;
    applied = 99;
    CHECK(TurboJS_SSAApplyPropertyFeedback(&graph, &feedback, 1, &applied) == TURBOJS_IR_OK);
    CHECK(applied == 0); /* store feedback must prove writability */
    feedback.flags |= TURBOJS_PROPERTY_FEEDBACK_WRITABLE;
    CHECK(TurboJS_SSAApplyPropertyFeedback(&graph, &feedback, 1, &applied) == TURBOJS_IR_OK);
    CHECK(applied == 1);
    {
        size_t i; int found = 0;
        for (i = 0; i < graph.value_count; ++i) {
            TurboJSSSAValue *v = &graph.values[i];
            if (v->opcode == TURBOJS_SSA_PROPERTY_STORE) {
                CHECK(v->guard_shape == (uintptr_t)0x2000);
                CHECK(v->property_index == 4);
                CHECK((v->property_flags & TURBOJS_PROPERTY_FEEDBACK_WRITABLE) != 0);
                found = 1;
            }
        }
        CHECK(found);
    }
    {
        TurboJSRegionObjectLayout layout = {0};
        layout.object_pointer_mask = UINT64_MAX;
        layout.shape_offset = (uint32_t)offsetof(MockObject, shape);
        layout.property_storage_offset = (uint32_t)offsetof(MockObject, slots);
        layout.property_stride = (uint32_t)sizeof(TurboJSRegionValue);
        CHECK(TurboJS_RegionNativeCompileWithObjectLayout(&graph, &layout, &native,
                                                          &native_stats, &diagnostic) == TURBOJS_IR_OK);
    }
    CHECK(native != NULL);
    CHECK(native_stats.inline_property_pic_cases == 1);
    CHECK(native_stats.inline_property_stores == 1);
    {
        MockObject object = {0};
        TurboJSRegionValue arguments[2], result_value = 0;
        uint16_t minimum_generation = 4;
        TurboJSRegionValueOps ops = {
            mock_guard_shape, mock_load_slot, mock_store_slot,
            mock_guard_dependency, mock_to_i64, mock_from_i64
        };
        object.shape = (uintptr_t)0x2000;
        arguments[0] = (TurboJSRegionValue)(uintptr_t)&object;
        arguments[1] = 777;
        CHECK(TurboJS_RegionNativeInvokeValues(native, arguments, 2, &ops,
                                                &minimum_generation,
                                                &result_value) == TURBOJS_IR_OK);
        CHECK(object.slots[4] == 777);
        object.shape = (uintptr_t)0x9999;
        arguments[1] = 888;
        CHECK(TurboJS_RegionNativeInvokeValues(native, arguments, 2, &ops,
                                                &minimum_generation,
                                                &result_value) == TURBOJS_IR_BAILOUT);
        CHECK(object.slots[4] == 777);
    }
    TurboJS_RegionNativeFunctionDestroy(native);
    native = NULL;
    TurboJS_SSAGraphDestroy(&graph);

    /* Four-shape polymorphic load. */
    TurboJS_SSAGraphInit(&graph);
    input.bytecode = load_code; input.bytecode_length = sizeof(load_code);
    input.argument_count = 1; input.local_count = 0; input.stack_size = 1;
    CHECK(TurboJS_EngineBytecodeRegionBuildSSA(&input, &graph, &diagnostic) == TURBOJS_IR_OK);
    {
        TurboJSPropertyFeedback cases[4];
        size_t i;
        for (i = 0; i < 4; ++i) {
            cases[i].source_instruction = 1;
            cases[i].atom = atom;
            cases[i].shape_identity = (uintptr_t)(0x3000 + i);
            cases[i].property_index = (uint32_t)i;
            cases[i].flags = TURBOJS_PROPERTY_FEEDBACK_LOAD |
                             TURBOJS_PROPERTY_FEEDBACK_OWN_DATA;
            cases[i].generation = (uint16_t)(10 + i);
        }
        CHECK(TurboJS_SSAApplyPropertyFeedback(&graph, cases, 4, &applied) == TURBOJS_IR_OK);
        CHECK(applied == 1);
        {
            TurboJSRegionObjectLayout layout = {0};
            layout.object_pointer_mask = UINT64_MAX;
            layout.shape_offset = (uint32_t)offsetof(MockObject, shape);
            layout.property_storage_offset = (uint32_t)offsetof(MockObject, slots);
            layout.property_stride = (uint32_t)sizeof(TurboJSRegionValue);
            CHECK(TurboJS_RegionNativeCompileWithObjectLayout(&graph, &layout, &native,
                                                              &native_stats, &diagnostic) == TURBOJS_IR_OK);
        }
        CHECK(native_stats.frame_bytes == 64);
        CHECK(native_stats.inline_property_pic_cases == 4);
        CHECK(native_stats.inline_property_loads == 1);
        CHECK(native_stats.inline_property_dependency_guards == 4);
        for (i = 0; i < 4; ++i) {
            MockObject object = {0};
            TurboJSRegionValue argument, result_value = 0;
            uint16_t minimum_generation = 10;
            TurboJSRegionValueOps ops = {
                mock_guard_shape, mock_load_slot, mock_store_slot,
                mock_guard_dependency, mock_to_i64, mock_from_i64
            };
            object.shape = cases[i].shape_identity;
            object.slots[i] = (TurboJSRegionValue)(100 + i);
            argument = (TurboJSRegionValue)(uintptr_t)&object;
            CHECK(TurboJS_RegionNativeInvokeValues(native, &argument, 1, &ops,
                                                    &minimum_generation,
                                                    &result_value) == TURBOJS_IR_OK);
            CHECK(result_value == (TurboJSRegionValue)(100 + i));
        }
        {
            MockObject object = {0};
            TurboJSRegionValue argument, result_value = 0;
            uint16_t minimum_generation = 99;
            TurboJSRegionValueOps ops = {
                mock_guard_shape, mock_load_slot, mock_store_slot,
                mock_guard_dependency, mock_to_i64, mock_from_i64
            };
            object.shape = cases[0].shape_identity;
            object.slots[0] = 100;
            argument = (TurboJSRegionValue)(uintptr_t)&object;
            CHECK(TurboJS_RegionNativeInvokeValues(native, &argument, 1, &ops,
                                                    &minimum_generation,
                                                    &result_value) == TURBOJS_IR_BAILOUT);
        }
        {
            MockObject object = {0};
            TurboJSRegionValue argument, result_value = 0;
            TurboJSRegionValueOps ops = {
                mock_guard_shape, mock_load_slot, mock_store_slot,
                mock_guard_dependency, mock_to_i64, mock_from_i64
            };
            object.shape = (uintptr_t)0x9999;
            argument = (TurboJSRegionValue)(uintptr_t)&object;
            CHECK(TurboJS_RegionNativeInvokeValues(native, &argument, 1, &ops,
                                                    NULL, &result_value) == TURBOJS_IR_BAILOUT);
        }
    }
    TurboJS_RegionNativeFunctionDestroy(native);
    TurboJS_SSAGraphDestroy(&graph);

    puts("property SSA supports monomorphic stores and four-shape load PICs");
    return 0;
}
