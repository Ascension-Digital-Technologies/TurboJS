#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jit.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static void init_graph(TurboJSSSAGraph *g, size_t count)
{
    size_t i;
    TurboJS_SSAGraphInit(g);
    g->values = (TurboJSSSAValue *)calloc(count, sizeof(*g->values));
    g->blocks = (TurboJSSSABlock *)calloc(1, sizeof(*g->blocks));
    g->value_count = g->value_capacity = count;
    g->block_count = g->block_capacity = 1;
    g->entry_block = 0;
    g->blocks[0].id = 0;
    g->blocks[0].reachable = 1;
    g->blocks[0].first_value = 0;
    g->blocks[0].value_count = (uint32_t)count;
    for (i = 0; i < count; ++i) {
        g->values[i].id = (uint32_t)i;
        g->values[i].block = 0;
        g->values[i].left = TURBOJS_SSA_NO_VALUE;
        g->values[i].right = TURBOJS_SSA_NO_VALUE;
        g->values[i].type = TURBOJS_SSA_TYPE_REFERENCE;
    }
}


static void init_two_block_graph(TurboJSSSAGraph *g, size_t count)
{
    size_t i;
    TurboJS_SSAGraphInit(g);
    g->values = (TurboJSSSAValue *)calloc(count, sizeof(*g->values));
    g->blocks = (TurboJSSSABlock *)calloc(2, sizeof(*g->blocks));
    g->value_count = g->value_capacity = count;
    g->block_count = g->block_capacity = 2;
    g->entry_block = 0;
    g->blocks[0].id = 0;
    g->blocks[0].reachable = 1;
    g->blocks[0].first_value = 0;
    g->blocks[0].value_count = 2;
    g->blocks[0].successor_count = 1;
    g->blocks[0].successors[0] = 1;
    g->blocks[0].immediate_dominator = TURBOJS_SSA_NO_BLOCK;
    g->blocks[1].id = 1;
    g->blocks[1].reachable = 1;
    g->blocks[1].first_value = 2;
    g->blocks[1].value_count = (uint32_t)(count - 2);
    g->blocks[1].predecessor_count = 1;
    g->blocks[1].predecessors[0] = 0;
    g->blocks[1].immediate_dominator = 0;
    for (i = 0; i < count; ++i) {
        g->values[i].id = (uint32_t)i;
        g->values[i].block = i < 2 ? 0 : 1;
        g->values[i].left = TURBOJS_SSA_NO_VALUE;
        g->values[i].right = TURBOJS_SSA_NO_VALUE;
        g->values[i].type = TURBOJS_SSA_TYPE_REFERENCE;
    }
}


static void init_diamond_graph(TurboJSSSAGraph *g)
{
    size_t i;
    TurboJS_SSAGraphInit(g);
    g->values = (TurboJSSSAValue *)calloc(5, sizeof(*g->values));
    g->blocks = (TurboJSSSABlock *)calloc(4, sizeof(*g->blocks));
    g->value_count = g->value_capacity = 5;
    g->block_count = g->block_capacity = 4;
    g->entry_block = 0;
    for (i = 0; i < 4; ++i) {
        g->blocks[i].id = (uint32_t)i;
        g->blocks[i].reachable = 1;
        g->blocks[i].loop_header = TURBOJS_SSA_NO_BLOCK;
    }
    g->blocks[0].first_value = 0;
    g->blocks[0].value_count = 2;
    g->blocks[0].successor_count = 2;
    g->blocks[0].successors[0] = 1;
    g->blocks[0].successors[1] = 2;
    g->blocks[0].immediate_dominator = TURBOJS_SSA_NO_BLOCK;
    g->blocks[1].first_value = 2;
    g->blocks[1].value_count = 0;
    g->blocks[1].predecessor_count = 1;
    g->blocks[1].predecessors[0] = 0;
    g->blocks[1].successor_count = 1;
    g->blocks[1].successors[0] = 3;
    g->blocks[1].immediate_dominator = 0;
    g->blocks[2].first_value = 2;
    g->blocks[2].value_count = 0;
    g->blocks[2].predecessor_count = 1;
    g->blocks[2].predecessors[0] = 0;
    g->blocks[2].successor_count = 1;
    g->blocks[2].successors[0] = 3;
    g->blocks[2].immediate_dominator = 0;
    g->blocks[3].first_value = 2;
    g->blocks[3].value_count = 3;
    g->blocks[3].predecessor_count = 2;
    g->blocks[3].predecessors[0] = 1;
    g->blocks[3].predecessors[1] = 2;
    g->blocks[3].immediate_dominator = 0;
    for (i = 0; i < 5; ++i) {
        g->values[i].id = (uint32_t)i;
        g->values[i].block = i < 2 ? 0 : 3;
        g->values[i].left = TURBOJS_SSA_NO_VALUE;
        g->values[i].right = TURBOJS_SSA_NO_VALUE;
        g->values[i].type = TURBOJS_SSA_TYPE_REFERENCE;
    }
}

static void init_store_diamond_graph(TurboJSSSAGraph *g)
{
    size_t i;
    TurboJS_SSAGraphInit(g);
    g->values = (TurboJSSSAValue *)calloc(7, sizeof(*g->values));
    g->blocks = (TurboJSSSABlock *)calloc(4, sizeof(*g->blocks));
    g->value_count = g->value_capacity = 7;
    g->block_count = g->block_capacity = 4;
    g->entry_block = 0;
    for (i = 0; i < 4; ++i) {
        g->blocks[i].id = (uint32_t)i;
        g->blocks[i].reachable = 1;
        g->blocks[i].loop_header = TURBOJS_SSA_NO_BLOCK;
    }
    g->blocks[0].first_value = 0;
    g->blocks[0].value_count = 3;
    g->blocks[0].successor_count = 2;
    g->blocks[0].successors[0] = 1;
    g->blocks[0].successors[1] = 2;
    g->blocks[0].immediate_dominator = TURBOJS_SSA_NO_BLOCK;
    g->blocks[1].first_value = 3;
    g->blocks[1].value_count = 1;
    g->blocks[1].predecessor_count = 1;
    g->blocks[1].predecessors[0] = 0;
    g->blocks[1].successor_count = 1;
    g->blocks[1].successors[0] = 3;
    g->blocks[1].immediate_dominator = 0;
    g->blocks[2].first_value = 4;
    g->blocks[2].value_count = 1;
    g->blocks[2].predecessor_count = 1;
    g->blocks[2].predecessors[0] = 0;
    g->blocks[2].successor_count = 1;
    g->blocks[2].successors[0] = 3;
    g->blocks[2].immediate_dominator = 0;
    g->blocks[3].first_value = 5;
    g->blocks[3].value_count = 2;
    g->blocks[3].predecessor_count = 2;
    g->blocks[3].predecessors[0] = 1;
    g->blocks[3].predecessors[1] = 2;
    g->blocks[3].immediate_dominator = 0;
    for (i = 0; i < 7; ++i) {
        g->values[i].id = (uint32_t)i;
        g->values[i].block = i < 3 ? 0 : (i == 3 ? 1 : (i == 4 ? 2 : 3));
        g->values[i].left = TURBOJS_SSA_NO_VALUE;
        g->values[i].right = TURBOJS_SSA_NO_VALUE;
        g->values[i].type = TURBOJS_SSA_TYPE_REFERENCE;
    }
}

static void init_loop_graph(TurboJSSSAGraph *g, int with_store)
{
    size_t i;
    size_t count = with_store ? 6 : 5;
    TurboJS_SSAGraphInit(g);
    g->values = (TurboJSSSAValue *)calloc(count, sizeof(*g->values));
    g->blocks = (TurboJSSSABlock *)calloc(3, sizeof(*g->blocks));
    g->value_count = g->value_capacity = count;
    g->block_count = g->block_capacity = 3;
    g->entry_block = 0;
    for (i = 0; i < 3; ++i) {
        g->blocks[i].id = (uint32_t)i;
        g->blocks[i].reachable = 1;
    }
    g->blocks[0].first_value = 0;
    g->blocks[0].value_count = 2;
    g->blocks[0].successor_count = 1;
    g->blocks[0].successors[0] = 1;
    g->blocks[0].immediate_dominator = TURBOJS_SSA_NO_BLOCK;
    g->blocks[0].loop_header = TURBOJS_SSA_NO_BLOCK;
    g->blocks[1].first_value = 2;
    g->blocks[1].value_count = 2;
    g->blocks[1].predecessor_count = 2;
    g->blocks[1].predecessors[0] = 0;
    g->blocks[1].predecessors[1] = 2;
    g->blocks[1].successor_count = 1;
    g->blocks[1].successors[0] = 2;
    g->blocks[1].immediate_dominator = 0;
    g->blocks[1].loop_header = 1;
    g->blocks[1].loop_depth = 1;
    g->blocks[2].first_value = 4;
    g->blocks[2].value_count = with_store ? 2 : 1;
    g->blocks[2].predecessor_count = 1;
    g->blocks[2].predecessors[0] = 1;
    g->blocks[2].successor_count = 1;
    g->blocks[2].successors[0] = 1;
    g->blocks[2].immediate_dominator = 1;
    g->blocks[2].loop_header = 1;
    g->blocks[2].loop_depth = 1;
    for (i = 0; i < count; ++i) {
        g->values[i].id = (uint32_t)i;
        g->values[i].block = i < 2 ? 0 : (i < 4 ? 1 : 2);
        g->values[i].left = TURBOJS_SSA_NO_VALUE;
        g->values[i].right = TURBOJS_SSA_NO_VALUE;
        g->values[i].type = TURBOJS_SSA_TYPE_REFERENCE;
    }
}
static void set_property_atom(TurboJSSSAValue *v, TurboJSSSAOpcode opcode,
                              uint32_t object, uint32_t value, uint32_t atom)
{
    v->opcode = opcode;
    v->left = object;
    v->right = value;
    v->metadata = atom;
    v->property_case_count = 1;
    v->property_shapes[0] = (uintptr_t)0x5000;
    v->property_indices[0] = 3;
    v->property_generations[0] = 9;
    v->property_case_flags[0] = TURBOJS_PROPERTY_FEEDBACK_OWN_DATA |
        (opcode == TURBOJS_SSA_PROPERTY_LOAD ? TURBOJS_PROPERTY_FEEDBACK_LOAD :
         TURBOJS_PROPERTY_FEEDBACK_STORE | TURBOJS_PROPERTY_FEEDBACK_WRITABLE);
    v->property_feedback_generation = 9;
}

static void set_property(TurboJSSSAValue *v, TurboJSSSAOpcode opcode,
                         uint32_t object, uint32_t value)
{
    set_property_atom(v, opcode, object, value, 0x1234);
}

static void init_alias_diamond_graph(TurboJSSSAGraph *g)
{
    size_t i;
    TurboJS_SSAGraphInit(g);
    g->values = (TurboJSSSAValue *)calloc(6, sizeof(*g->values));
    g->blocks = (TurboJSSSABlock *)calloc(4, sizeof(*g->blocks));
    g->value_count = g->value_capacity = 6;
    g->block_count = g->block_capacity = 4;
    g->entry_block = 0;
    for (i = 0; i < 4; ++i) {
        g->blocks[i].id = (uint32_t)i;
        g->blocks[i].reachable = 1;
        g->blocks[i].loop_header = TURBOJS_SSA_NO_BLOCK;
    }
    g->blocks[0].first_value = 0;
    g->blocks[0].value_count = 3;
    g->blocks[0].successor_count = 2;
    g->blocks[0].successors[0] = 1;
    g->blocks[0].successors[1] = 2;
    g->blocks[0].immediate_dominator = TURBOJS_SSA_NO_BLOCK;
    g->blocks[1].first_value = 3;
    g->blocks[1].value_count = 1;
    g->blocks[1].predecessor_count = 1;
    g->blocks[1].predecessors[0] = 0;
    g->blocks[1].successor_count = 1;
    g->blocks[1].successors[0] = 3;
    g->blocks[1].immediate_dominator = 0;
    g->blocks[2].first_value = 4;
    g->blocks[2].value_count = 0;
    g->blocks[2].predecessor_count = 1;
    g->blocks[2].predecessors[0] = 0;
    g->blocks[2].successor_count = 1;
    g->blocks[2].successors[0] = 3;
    g->blocks[2].immediate_dominator = 0;
    g->blocks[3].first_value = 4;
    g->blocks[3].value_count = 2;
    g->blocks[3].predecessor_count = 2;
    g->blocks[3].predecessors[0] = 1;
    g->blocks[3].predecessors[1] = 2;
    g->blocks[3].immediate_dominator = 0;
    for (i = 0; i < 6; ++i) {
        g->values[i].id = (uint32_t)i;
        g->values[i].block = i < 3 ? 0 : (i == 3 ? 1 : 3);
        g->values[i].left = TURBOJS_SSA_NO_VALUE;
        g->values[i].right = TURBOJS_SSA_NO_VALUE;
        g->values[i].type = TURBOJS_SSA_TYPE_REFERENCE;
    }
}


static void init_nested_store_join_graph(TurboJSSSAGraph *g)
{
    size_t i;
    static const uint32_t firsts[7] = {0, 4, 4, 5, 6, 7, 8};
    static const uint32_t counts[7] = {4, 0, 1, 1, 1, 1, 2};
    TurboJS_SSAGraphInit(g);
    g->values = (TurboJSSSAValue *)calloc(10, sizeof(*g->values));
    g->blocks = (TurboJSSSABlock *)calloc(7, sizeof(*g->blocks));
    g->value_count = g->value_capacity = 10;
    g->block_count = g->block_capacity = 7;
    g->entry_block = 0;
    for (i = 0; i < 7; ++i) {
        g->blocks[i].id = (uint32_t)i;
        g->blocks[i].reachable = 1;
        g->blocks[i].loop_header = TURBOJS_SSA_NO_BLOCK;
        g->blocks[i].first_value = firsts[i];
        g->blocks[i].value_count = counts[i];
    }
    /* Outer split: inner diamond (1) or direct store (5). */
    g->blocks[0].successor_count = 2;
    g->blocks[0].successors[0] = 1;
    g->blocks[0].successors[1] = 5;
    g->blocks[0].immediate_dominator = TURBOJS_SSA_NO_BLOCK;
    g->blocks[1].predecessor_count = 1;
    g->blocks[1].predecessors[0] = 0;
    g->blocks[1].successor_count = 2;
    g->blocks[1].successors[0] = 2;
    g->blocks[1].successors[1] = 3;
    g->blocks[1].immediate_dominator = 0;
    g->blocks[2].predecessor_count = 1;
    g->blocks[2].predecessors[0] = 1;
    g->blocks[2].successor_count = 1;
    g->blocks[2].successors[0] = 4;
    g->blocks[2].immediate_dominator = 1;
    g->blocks[3].predecessor_count = 1;
    g->blocks[3].predecessors[0] = 1;
    g->blocks[3].successor_count = 1;
    g->blocks[3].successors[0] = 4;
    g->blocks[3].immediate_dominator = 1;
    g->blocks[4].predecessor_count = 2;
    g->blocks[4].predecessors[0] = 2;
    g->blocks[4].predecessors[1] = 3;
    g->blocks[4].successor_count = 1;
    g->blocks[4].successors[0] = 6;
    g->blocks[4].immediate_dominator = 1;
    g->blocks[5].predecessor_count = 1;
    g->blocks[5].predecessors[0] = 0;
    g->blocks[5].successor_count = 1;
    g->blocks[5].successors[0] = 6;
    g->blocks[5].immediate_dominator = 0;
    g->blocks[6].predecessor_count = 2;
    g->blocks[6].predecessors[0] = 4;
    g->blocks[6].predecessors[1] = 5;
    g->blocks[6].immediate_dominator = 0;
    for (i = 0; i < 10; ++i) {
        g->values[i].id = (uint32_t)i;
        g->values[i].left = TURBOJS_SSA_NO_VALUE;
        g->values[i].right = TURBOJS_SSA_NO_VALUE;
        g->values[i].type = TURBOJS_SSA_TYPE_REFERENCE;
    }
    for (i = 0; i < 4; ++i) g->values[i].block = 0;
    g->values[4].block = 2;
    g->values[5].block = 3;
    g->values[6].block = 4;
    g->values[7].block = 5;
    g->values[8].block = 6;
    g->values[9].block = 6;
}

int main(void)
{
    TurboJSSSAGraph g;
    TurboJSSSAOptimizationStats stats;

    /* A guarded store forwards its SSA value into a later matching load. */
    init_graph(&g, 5);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    g.values[1].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[1].immediate = 1;
    set_property(&g.values[2], TURBOJS_SSA_PROPERTY_STORE, 0, 1);
    set_property(&g.values[3], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[4].opcode = TURBOJS_SSA_RETURN;
    g.values[4].left = 3;
    g.values[0].use_count = 2;
    g.values[1].use_count = 1;
    g.values[3].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_store_forwardings == 1);
    CHECK(g.values[3].removed);
    CHECK(g.values[4].left == 1);
    CHECK(!g.values[2].removed);
    TurboJS_SSAGraphDestroy(&g);

    /* An intervening aliasing store prevents store-to-load forwarding. */
    init_graph(&g, 7);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    g.values[1].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[1].immediate = 1;
    g.values[2].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[2].immediate = 2;
    set_property(&g.values[3], TURBOJS_SSA_PROPERTY_STORE, 0, 1);
    set_property(&g.values[4], TURBOJS_SSA_PROPERTY_STORE, 0, 2);
    set_property(&g.values[5], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[6].opcode = TURBOJS_SSA_RETURN;
    g.values[6].left = 5;
    g.values[0].use_count = 3;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    g.values[5].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_store_forwardings == 1);
    CHECK(g.values[5].removed);
    CHECK(g.values[6].left == 2);
    TurboJS_SSAGraphDestroy(&g);

    /* Two identical guarded loads with no intervening write reuse one value. */
    init_graph(&g, 5);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    set_property(&g.values[1], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    set_property(&g.values[2], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[3].opcode = TURBOJS_SSA_ADD_I64;
    g.values[3].type = TURBOJS_SSA_TYPE_INT64;
    g.values[3].left = 1;
    g.values[3].right = 2;
    g.values[4].opcode = TURBOJS_SSA_RETURN;
    g.values[4].left = 3;
    g.values[0].use_count = 2;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    g.values[3].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_loads_eliminated == 1);
    CHECK(stats.property_dependency_reuses == 1);
    CHECK(g.values[2].removed);
    CHECK(g.values[3].left == 1 && g.values[3].right == 1);
    TurboJS_SSAGraphDestroy(&g);

    /* A matching guarded store becomes the value definition for a later load. */
    init_graph(&g, 6);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    g.values[1].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[1].immediate = 1;
    set_property(&g.values[2], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    set_property(&g.values[3], TURBOJS_SSA_PROPERTY_STORE, 0, 1);
    set_property(&g.values[4], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[5].opcode = TURBOJS_SSA_RETURN;
    g.values[5].left = 4;
    g.values[0].use_count = 3;
    g.values[1].use_count = 1;
    g.values[4].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_loads_eliminated == 0);
    CHECK(stats.property_store_forwardings == 1);
    CHECK(!g.values[3].removed);
    CHECK(g.values[4].removed);
    CHECK(g.values[5].left == 1);
    TurboJS_SSAGraphDestroy(&g);

    /* A dominating load is reusable in a unique-successor block. */
    init_two_block_graph(&g, 5);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    set_property(&g.values[1], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    set_property(&g.values[2], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[3].opcode = TURBOJS_SSA_ADD_I64;
    g.values[3].type = TURBOJS_SSA_TYPE_INT64;
    g.values[3].left = 1;
    g.values[3].right = 2;
    g.values[4].opcode = TURBOJS_SSA_RETURN;
    g.values[4].left = 3;
    g.values[0].use_count = 2;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    g.values[3].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_loads_eliminated == 1);
    CHECK(stats.property_cross_block_loads_eliminated == 1);
    CHECK(stats.property_unique_path_reuses == 1);
    CHECK(g.values[2].removed);
    CHECK(g.values[3].left == 1 && g.values[3].right == 1);
    TurboJS_SSAGraphDestroy(&g);

    /* An aliasing store in the successor blocks cross-block reuse. */
    init_two_block_graph(&g, 6);
    g.blocks[1].value_count = 4;
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    set_property(&g.values[1], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[2].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[2].immediate = 1;
    set_property(&g.values[3], TURBOJS_SSA_PROPERTY_STORE, 0, 2);
    set_property(&g.values[4], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[5].opcode = TURBOJS_SSA_RETURN;
    g.values[5].left = 4;
    g.values[0].use_count = 3;
    g.values[2].use_count = 1;
    g.values[4].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_cross_block_loads_eliminated == 0);
    CHECK(stats.property_store_forwardings == 1);
    CHECK(!g.values[3].removed);
    CHECK(g.values[4].removed);
    CHECK(g.values[5].left == 2);
    TurboJS_SSAGraphDestroy(&g);

    /* A dominating load is reusable after a store-free diamond join. */
    init_diamond_graph(&g);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    set_property(&g.values[1], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    set_property(&g.values[2], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[3].opcode = TURBOJS_SSA_ADD_I64;
    g.values[3].type = TURBOJS_SSA_TYPE_INT64;
    g.values[3].left = 1;
    g.values[3].right = 2;
    g.values[4].opcode = TURBOJS_SSA_RETURN;
    g.values[4].left = 3;
    g.values[0].use_count = 2;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    g.values[3].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_join_reuses == 1);
    CHECK(stats.property_memory_versions_proven >= 1);
    CHECK(g.values[2].removed);
    CHECK(g.values[3].left == 1 && g.values[3].right == 1);
    TurboJS_SSAGraphDestroy(&g);

    /* A preheader property value is loop invariant when the natural loop has no aliasing store. */
    init_loop_graph(&g, 0);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    set_property(&g.values[1], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    set_property(&g.values[2], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[3].opcode = TURBOJS_SSA_RETURN;
    g.values[3].left = 2;
    g.values[4].opcode = TURBOJS_SSA_JUMP;
    g.values[4].immediate = 1;
    g.values[0].use_count = 2;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_loop_invariant_reuses == 1);
    CHECK(g.values[2].removed);
    CHECK(g.values[3].left == 1);
    TurboJS_SSAGraphDestroy(&g);

    /* An aliasing store on the backedge blocks loop-invariant reuse. */
    init_loop_graph(&g, 1);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    g.values[1].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[1].immediate = 1;
    set_property(&g.values[2], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[3].opcode = TURBOJS_SSA_RETURN;
    g.values[3].left = 2;
    set_property(&g.values[4], TURBOJS_SSA_PROPERTY_STORE, 0, 1);
    g.values[5].opcode = TURBOJS_SSA_JUMP;
    g.values[5].immediate = 1;
    g.values[0].use_count = 2;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_loop_invariant_reuses == 0);
    CHECK(!g.values[2].removed);
    CHECK(!g.values[4].removed);
    TurboJS_SSAGraphDestroy(&g);

    /* A write to another known property class does not kill x across a join. */
    init_alias_diamond_graph(&g);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    g.values[1].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[1].immediate = 1;
    set_property_atom(&g.values[2], TURBOJS_SSA_PROPERTY_LOAD, 0,
                      TURBOJS_SSA_NO_VALUE, 0x1111);
    set_property_atom(&g.values[3], TURBOJS_SSA_PROPERTY_STORE, 0, 1, 0x2222);
    set_property_atom(&g.values[4], TURBOJS_SSA_PROPERTY_LOAD, 0,
                      TURBOJS_SSA_NO_VALUE, 0x1111);
    g.values[5].opcode = TURBOJS_SSA_RETURN;
    g.values[5].left = 4;
    g.values[0].use_count = 3;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    g.values[4].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_join_reuses == 1);
    CHECK(stats.property_memory_phis == 1);
    CHECK(stats.property_alias_classes_proven >= 1);
    CHECK(stats.property_non_aliasing_stores_ignored >= 1);
    CHECK(g.values[4].removed);
    CHECK(g.values[5].left == 2);
    CHECK(!g.values[3].removed);
    TurboJS_SSAGraphDestroy(&g);

    puts("property memory phis preserve per-alias-class versions across joins and loops");
    /* Stores on both sides of a diamond become a value phi at the join. */
    init_store_diamond_graph(&g);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    g.values[1].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[1].immediate = 1;
    g.values[1].type = TURBOJS_SSA_TYPE_INT64;
    g.values[2].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[2].immediate = 2;
    g.values[2].type = TURBOJS_SSA_TYPE_INT64;
    set_property(&g.values[3], TURBOJS_SSA_PROPERTY_STORE, 0, 1);
    set_property(&g.values[4], TURBOJS_SSA_PROPERTY_STORE, 0, 2);
    set_property(&g.values[5], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[6].opcode = TURBOJS_SSA_RETURN;
    g.values[6].left = 5;
    g.values[0].use_count = 3;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    g.values[5].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_store_phis == 1);
    CHECK(g.values[5].opcode == TURBOJS_SSA_PHI);
    CHECK(g.values[5].left == 1 && g.values[5].right == 2);
    CHECK(g.values[6].left == 5);
    CHECK(!g.values[3].removed && !g.values[4].removed);
    TurboJS_SSAGraphDestroy(&g);


    /* Nested joins reuse a property-state phi as an outer reaching value. */
    init_nested_store_join_graph(&g);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    g.values[1].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[1].immediate = 1;
    g.values[1].type = TURBOJS_SSA_TYPE_INT64;
    g.values[2].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[2].immediate = 2;
    g.values[2].type = TURBOJS_SSA_TYPE_INT64;
    g.values[3].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[3].immediate = 3;
    g.values[3].type = TURBOJS_SSA_TYPE_INT64;
    set_property(&g.values[4], TURBOJS_SSA_PROPERTY_STORE, 0, 1);
    set_property(&g.values[5], TURBOJS_SSA_PROPERTY_STORE, 0, 2);
    set_property(&g.values[6], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    set_property(&g.values[7], TURBOJS_SSA_PROPERTY_STORE, 0, 3);
    set_property(&g.values[8], TURBOJS_SSA_PROPERTY_LOAD, 0, TURBOJS_SSA_NO_VALUE);
    g.values[9].opcode = TURBOJS_SSA_RETURN;
    g.values[9].left = 8;
    g.values[0].use_count = 5;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    g.values[3].use_count = 1;
    g.values[6].use_count = 1;
    g.values[8].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_store_phis == 2);
    CHECK(g.values[6].opcode == TURBOJS_SSA_PHI);
    CHECK(g.values[8].opcode == TURBOJS_SSA_PHI);
    CHECK(g.values[8].left == 6 && g.values[8].right == 3);
    CHECK(g.values[9].left == 8);
    TurboJS_SSAGraphDestroy(&g);

    /* A later plain own-data write makes an earlier unread write dead. */
    init_graph(&g, 5);
    g.values[0].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[0].immediate = 0;
    g.values[1].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[1].immediate = 1;
    g.values[2].opcode = TURBOJS_SSA_ARGUMENT;
    g.values[2].immediate = 2;
    set_property(&g.values[3], TURBOJS_SSA_PROPERTY_STORE, 0, 1);
    set_property(&g.values[4], TURBOJS_SSA_PROPERTY_STORE, 0, 2);
    g.values[0].use_count = 2;
    g.values[1].use_count = 1;
    g.values[2].use_count = 1;
    stats = TurboJS_SSAOptimize(&g);
    CHECK(stats.property_dead_stores_eliminated == 1);
    CHECK(g.values[3].removed);
    CHECK(!g.values[4].removed);
    TurboJS_SSAGraphDestroy(&g);

    return 0;
}
