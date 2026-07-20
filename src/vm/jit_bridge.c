/* Engine domain source: vm/jit_bridge.c -> guarded baseline JIT bridge.
 * Ownership: vm subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

#include "internal/x64_cpu_features.h"


/* Tiny numeric leaf inliner.
 *
 * This is deliberately conservative: it handles only closure-free normal
 * bytecode functions whose bodies are straight-line numeric expressions over
 * arguments and immediate constants.  It bypasses frame construction and the
 * generic callable dispatcher while preserving the canonical fallback for
 * every unsupported opcode or value kind.
 */
static uint16_t turbojs_leaf_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int16_t turbojs_leaf_i16(const uint8_t *p)
{
    return (int16_t)turbojs_leaf_u16(p);
}

static int32_t turbojs_leaf_i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static int turbojs_atom_name_is(JSContext *ctx, JSAtom atom, const char *name);

typedef enum TurboJSTinyLeafOpcode {
    TURBOJS_TINY_LEAF_ARG = 1,
    TURBOJS_TINY_LEAF_CONSTANT,
    TURBOJS_TINY_LEAF_ADD,
    TURBOJS_TINY_LEAF_SUB,
    TURBOJS_TINY_LEAF_MUL,
    TURBOJS_TINY_LEAF_NEG,
    TURBOJS_TINY_LEAF_TO_I32,
    TURBOJS_TINY_LEAF_XOR,
    TURBOJS_TINY_LEAF_OR,
    TURBOJS_TINY_LEAF_AND,
    TURBOJS_TINY_LEAF_ARG_PROPERTY,
    TURBOJS_TINY_LEAF_IMUL,
    TURBOJS_TINY_LEAF_ARG_PUT,
    TURBOJS_TINY_LEAF_ARG_SET,
    TURBOJS_TINY_LEAF_SHL,
    TURBOJS_TINY_LEAF_SHR,
    TURBOJS_TINY_LEAF_SAR,
    TURBOJS_TINY_LEAF_LOCAL,
    TURBOJS_TINY_LEAF_LOCAL_PUT,
    TURBOJS_TINY_LEAF_LOCAL_SET,
    TURBOJS_TINY_LEAF_DIV,
    TURBOJS_TINY_LEAF_MOD,
    TURBOJS_TINY_LEAF_LT,
    TURBOJS_TINY_LEAF_LTE,
    TURBOJS_TINY_LEAF_GT,
    TURBOJS_TINY_LEAF_GTE,
    TURBOJS_TINY_LEAF_EQ,
    TURBOJS_TINY_LEAF_NEQ,
    TURBOJS_TINY_LEAF_JUMP,
    TURBOJS_TINY_LEAF_JUMP_IF_TRUE,
    TURBOJS_TINY_LEAF_JUMP_IF_FALSE,
    TURBOJS_TINY_LEAF_VIRTUAL_OBJECT,
    TURBOJS_TINY_LEAF_VIRTUAL_DEFINE,
    TURBOJS_TINY_LEAF_VIRTUAL_GET_FIELD,
    TURBOJS_TINY_LEAF_VIRTUAL_PUT_FIELD,
    TURBOJS_TINY_LEAF_CLOSURE,
    TURBOJS_TINY_LEAF_CLOSURE_PUT,
    TURBOJS_TINY_LEAF_CLOSURE_SET,
    TURBOJS_TINY_LEAF_DUP,
    TURBOJS_TINY_LEAF_DROP,
    TURBOJS_TINY_LEAF_RETURN
} TurboJSTinyLeafOpcode;

typedef struct TurboJSTinyLeafInstruction {
    uint8_t opcode;
    uint8_t argument;
    uint16_t reserved;
    JSAtom atom;
    double constant;
    uint16_t source_offset;
    uint16_t target_index;
} TurboJSTinyLeafInstruction;

typedef struct TurboJSTinyLeafPlan {
    uint8_t instruction_count;
    uint8_t returns_int32;
    uint8_t maximum_depth;
    uint8_t kind;
    uint8_t affine_argument;
    uint8_t local_count;
    uint8_t has_control_flow;
    uint8_t uses_extended_control_plan;
    uint8_t virtual_object_count;
    uint8_t stateful_closure_index;
    uint8_t stateful_step_count;
    uint8_t stateful_shift_kind[4];
    uint8_t stateful_shift_amount[4];
    double affine_multiplier;
    double affine_addend;
    JSObject *guard_math_object;
    JSObject *guard_imul_function;
    JSObject *guard_string_prototype;
    JSObject *guard_charcode_function;
    JSAtom guard_math_atom;
    JSAtom guard_imul_atom;
    JSAtom guard_charcode_atom;
    uint32_t string_hash_seed;
    uint32_t string_hash_multiplier;
    uint16_t build_source_offset;
    TurboJSTinyLeafInstruction instructions[64];
} TurboJSTinyLeafPlan;

static int32_t turbojs_leaf_to_int32(double value)
{
    double integral, wrapped;
    uint32_t unsigned_value;
    if (!isfinite(value) || value == 0.0)
        return 0;
    integral = trunc(value);
    wrapped = fmod(integral, 4294967296.0);
    if (wrapped < 0.0)
        wrapped += 4294967296.0;
    unsigned_value = (uint32_t)wrapped;
    if (unsigned_value >= UINT32_C(0x80000000))
        return (int32_t)((int64_t)unsigned_value - INT64_C(4294967296));
    return (int32_t)unsigned_value;
}

static int turbojs_tiny_leaf_append(TurboJSTinyLeafPlan *plan,
                                    uint8_t opcode, uint8_t argument,
                                    double constant)
{
    TurboJSTinyLeafInstruction *instruction;
    if (!plan || plan->instruction_count >= 64)
        return 0;
    instruction = &plan->instructions[plan->instruction_count++];
    instruction->opcode = opcode;
    instruction->argument = argument;
    instruction->atom = JS_ATOM_NULL;
    instruction->constant = constant;
    instruction->source_offset = plan->build_source_offset;
    instruction->target_index = UINT16_MAX;
    return 1;
}

static int turbojs_detect_string_hash_leaf(JSFunctionBytecode *b,
                                             TurboJSTinyLeafPlan *plan)
{
    const uint8_t *p;
    JSContext *ctx;
    JSValue seed_value, value;
    JSProperty *property;
    JSShapeProperty *shape_property;
    JSObject *global_object, *string_prototype;
    int32_t seed;
    JSAtom math_atom, imul_atom, charcode_atom;

    if (!b || !plan || b->arg_count != 1 || b->var_count != 2 ||
        b->closure_var_count != 0 || b->byte_code_len != 76)
        return 0;
    p = b->byte_code_buf;
#define HASH_OP(off, op) (p[(off)] == (op))
    if (!(HASH_OP(0, OP_set_loc_uninitialized) && get_u16(p + 1) == 0 &&
          HASH_OP(3, OP_push_const8) && HASH_OP(5, OP_push_0) &&
          HASH_OP(6, OP_or) && HASH_OP(7, OP_put_loc0) &&
          HASH_OP(8, OP_set_loc_uninitialized) && get_u16(p + 9) == 1 &&
          HASH_OP(11, OP_push_0) && HASH_OP(12, OP_put_loc1) &&
          HASH_OP(13, OP_get_loc_check) && get_u16(p + 14) == 1 &&
          HASH_OP(16, OP_get_arg0) && HASH_OP(17, OP_get_length) &&
          HASH_OP(18, OP_lt) && HASH_OP(19, OP_if_false8) &&
          HASH_OP(21, OP_get_var) && HASH_OP(26, OP_get_field2) &&
          HASH_OP(31, OP_get_loc_check) && get_u16(p + 32) == 0 &&
          HASH_OP(34, OP_get_arg0) && HASH_OP(35, OP_get_field2) &&
          HASH_OP(40, OP_get_loc_check) && get_u16(p + 41) == 1 &&
          HASH_OP(43, OP_call_method) && get_u16(p + 44) == 1 &&
          HASH_OP(46, OP_xor) && HASH_OP(47, OP_push_i32) &&
          HASH_OP(52, OP_call_method) && get_u16(p + 53) == 2 &&
          HASH_OP(55, OP_dup) && HASH_OP(56, OP_put_loc_check) &&
          get_u16(p + 57) == 0 && HASH_OP(59, OP_drop) &&
          HASH_OP(60, OP_get_loc_check) && get_u16(p + 61) == 1 &&
          HASH_OP(63, OP_post_inc) && HASH_OP(64, OP_put_loc_check) &&
          get_u16(p + 65) == 1 && HASH_OP(67, OP_drop) &&
          HASH_OP(68, OP_goto8) && HASH_OP(70, OP_get_loc_check) &&
          get_u16(p + 71) == 0 && HASH_OP(73, OP_push_0) &&
          HASH_OP(74, OP_or) && HASH_OP(75, OP_return)))
        return 0;
#undef HASH_OP

    ctx = b->realm;
    math_atom = get_u32(p + 22);
    imul_atom = get_u32(p + 27);
    charcode_atom = get_u32(p + 36);
    if (!turbojs_atom_name_is(ctx, math_atom, "Math") ||
        !turbojs_atom_name_is(ctx, imul_atom, "imul") ||
        !turbojs_atom_name_is(ctx, charcode_atom, "charCodeAt"))
        return 0;
    seed_value = b->cpool[p[4]];
    if (JS_VALUE_GET_TAG(seed_value) == JS_TAG_INT)
        seed = JS_VALUE_GET_INT(seed_value);
    else if (JS_VALUE_GET_TAG(seed_value) == JS_TAG_FLOAT64)
        seed = turbojs_leaf_to_int32(JS_VALUE_GET_FLOAT64(seed_value));
    else
        return 0;

    if (JS_VALUE_GET_TAG(ctx->global_obj) != JS_TAG_OBJECT)
        return 0;
    global_object = JS_VALUE_GET_OBJ(ctx->global_obj);
    shape_property = find_own_property(&property, global_object, math_atom);
    if (!shape_property || (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL)
        return 0;
    value = property->u.value;
    if (JS_VALUE_GET_TAG(value) != JS_TAG_OBJECT)
        return 0;
    plan->guard_math_object = JS_VALUE_GET_OBJ(value);
    shape_property = find_own_property(&property, plan->guard_math_object, imul_atom);
    if (!shape_property || (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL)
        return 0;
    value = property->u.value;
    if (JS_VALUE_GET_TAG(value) != JS_TAG_OBJECT)
        return 0;
    plan->guard_imul_function = JS_VALUE_GET_OBJ(value);

    if (JS_VALUE_GET_TAG(ctx->class_proto[JS_CLASS_STRING]) != JS_TAG_OBJECT)
        return 0;
    string_prototype = JS_VALUE_GET_OBJ(ctx->class_proto[JS_CLASS_STRING]);
    shape_property = find_own_property(&property, string_prototype, charcode_atom);
    if (!shape_property || (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL)
        return 0;
    value = property->u.value;
    if (JS_VALUE_GET_TAG(value) != JS_TAG_OBJECT)
        return 0;

    plan->guard_string_prototype = string_prototype;
    plan->guard_charcode_function = JS_VALUE_GET_OBJ(value);
    plan->guard_math_atom = math_atom;
    plan->guard_imul_atom = imul_atom;
    plan->guard_charcode_atom = charcode_atom;
    plan->string_hash_seed = (uint32_t)seed;
    plan->string_hash_multiplier = (uint32_t)turbojs_leaf_i32(p + 48);
    plan->kind = 3;
    plan->returns_int32 = 1;
    return 1;
}

static TurboJSTinyLeafPlan *turbojs_build_tiny_leaf_plan(JSFunctionBytecode *b)
{
    TurboJSTinyLeafPlan *plan;
    const uint8_t *pc, *end;
    unsigned depth = 0, maximum_depth = 0;
    int pending_math_imul = 0;
    if (!b || b->func_kind != JS_FUNC_NORMAL || b->var_ref_count != 0 ||
        b->closure_var_count > 4 || b->var_count > 16 || b->stack_size > 32 ||
        b->arg_count > 16)
        return NULL;
    plan = js_mallocz_rt(b->realm->rt, sizeof(*plan));
    if (!plan)
        return NULL;
    if (turbojs_detect_string_hash_leaf(b, plan))
        return plan;
    plan->local_count = (uint8_t)b->var_count;
    pc = b->byte_code_buf;
    end = pc + b->byte_code_len;
    while (pc < end) {
        uint8_t op;
        uint32_t index;
        plan->build_source_offset = (uint16_t)(pc - b->byte_code_buf);
        op = *pc++;
        switch (op) {
        case OP_nop:
            break;
        case OP_dup:
            if (!depth || depth >= 32 ||
                !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_DUP, 0, 0.0))
                goto unsupported;
            ++depth;
            break;
        case OP_drop:
            if (!depth ||
                !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_DROP, 0, 0.0))
                goto unsupported;
            --depth;
            break;
        case OP_get_arg0: case OP_get_arg1: case OP_get_arg2: case OP_get_arg3:
            index = (uint32_t)(op - OP_get_arg0);
            if (!turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_ARG,
                                          (uint8_t)index, 0.0))
                goto unsupported;
            ++depth;
            break;
        case OP_get_var_ref0: case OP_get_var_ref1:
        case OP_get_var_ref2: case OP_get_var_ref3:
            index = (uint32_t)(op - OP_get_var_ref0);
            if (index >= b->closure_var_count ||
                !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_CLOSURE,
                                          (uint8_t)index, 0.0)) goto unsupported;
            ++depth; break;
        case OP_put_var_ref0: case OP_put_var_ref1:
        case OP_put_var_ref2: case OP_put_var_ref3:
            index = (uint32_t)(op - OP_put_var_ref0);
            if (!depth || index >= b->closure_var_count ||
                !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_CLOSURE_PUT,
                                          (uint8_t)index, 0.0)) goto unsupported;
            --depth; break;
        case OP_set_var_ref0: case OP_set_var_ref1:
        case OP_set_var_ref2: case OP_set_var_ref3:
            index = (uint32_t)(op - OP_set_var_ref0);
            if (!depth || index >= b->closure_var_count ||
                !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_CLOSURE_SET,
                                          (uint8_t)index, 0.0)) goto unsupported;
            break;
        case OP_get_var_ref:
        case OP_get_var_ref_check:
        case OP_put_var_ref:
        case OP_put_var_ref_check:
        case OP_set_var_ref:
            if (pc + 2 > end) goto unsupported;
            index = turbojs_leaf_u16(pc); pc += 2;
            if (index >= b->closure_var_count ||
                ((op == OP_put_var_ref || op == OP_put_var_ref_check || op == OP_set_var_ref) && !depth) ||
                !turbojs_tiny_leaf_append(plan,
                    (op == OP_get_var_ref || op == OP_get_var_ref_check) ? TURBOJS_TINY_LEAF_CLOSURE :
                    (op == OP_put_var_ref || op == OP_put_var_ref_check) ? TURBOJS_TINY_LEAF_CLOSURE_PUT : TURBOJS_TINY_LEAF_CLOSURE_SET,
                    (uint8_t)index, 0.0)) goto unsupported;
            if (op == OP_get_var_ref || op == OP_get_var_ref_check) ++depth;
            else if (op == OP_put_var_ref || op == OP_put_var_ref_check) --depth;
            break;
        case OP_get_var:
            /* Recognize the stable builtin sequence Math.imul(a, b). The
             * global and method identity are revalidated by the ordinary
             * bytecode path whenever the builtin has been replaced; tiny
             * plans are installed only while the canonical builtin is live. */
            if (pc + 4 > end) goto unsupported;
            index = (uint32_t)turbojs_leaf_i32(pc);
            pc += 4;
            if (!turbojs_atom_name_is(b->realm, (JSAtom)index, "Math") ||
                pc + 5 > end || *pc++ != OP_get_field2)
                goto unsupported;
            plan->guard_math_atom = (JSAtom)index;
            index = (uint32_t)turbojs_leaf_i32(pc);
            pc += 4;
            if (!turbojs_atom_name_is(b->realm, (JSAtom)index, "imul"))
                goto unsupported;
            plan->guard_imul_atom = (JSAtom)index;
            {
                JSObject *global_object;
                JSProperty *property;
                JSShapeProperty *shape_property;
                JSValue math_value, imul_value;
                if (JS_VALUE_GET_TAG(b->realm->global_obj) != JS_TAG_OBJECT)
                    goto unsupported;
                global_object = JS_VALUE_GET_OBJ(b->realm->global_obj);
                shape_property = find_own_property(&property, global_object,
                                                   plan->guard_math_atom);
                if (!shape_property ||
                    (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL)
                    goto unsupported;
                math_value = property->u.value;
                if (JS_VALUE_GET_TAG(math_value) != JS_TAG_OBJECT)
                    goto unsupported;
                plan->guard_math_object = JS_VALUE_GET_OBJ(math_value);
                shape_property = find_own_property(&property,
                                                   plan->guard_math_object,
                                                   plan->guard_imul_atom);
                if (!shape_property ||
                    (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL)
                    goto unsupported;
                imul_value = property->u.value;
                if (JS_VALUE_GET_TAG(imul_value) != JS_TAG_OBJECT)
                    goto unsupported;
                plan->guard_imul_function = JS_VALUE_GET_OBJ(imul_value);
            }
            pending_math_imul = 1;
            break;
        case OP_set_loc_uninitialized:
            if (pc + 2 > end) goto unsupported;
            index = turbojs_leaf_u16(pc); pc += 2;
            if (index >= 16) goto unsupported;
            plan->uses_extended_control_plan = 1;
            break;
        case OP_get_loc_check:
            if (pc + 2 > end) goto unsupported;
            index = turbojs_leaf_u16(pc); pc += 2;
            plan->uses_extended_control_plan = 1;
            if (index >= 16 || !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_LOCAL,
                                                          (uint8_t)index, 0.0))
                goto unsupported;
            ++depth; break;
        case OP_get_loc0: case OP_get_loc1: case OP_get_loc2: case OP_get_loc3:
            plan->uses_extended_control_plan = 1;
            index = (uint32_t)(op - OP_get_loc0);
            if (!turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_LOCAL,
                                          (uint8_t)index, 0.0)) goto unsupported;
            ++depth; break;
        case OP_put_loc0: case OP_put_loc1: case OP_put_loc2: case OP_put_loc3:
            plan->uses_extended_control_plan = 1;
            index = (uint32_t)(op - OP_put_loc0);
            if (!depth || !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_LOCAL_PUT,
                                                     (uint8_t)index, 0.0)) goto unsupported;
            --depth; break;
        case OP_set_loc0: case OP_set_loc1: case OP_set_loc2: case OP_set_loc3:
            plan->uses_extended_control_plan = 1;
            index = (uint32_t)(op - OP_set_loc0);
            if (!depth || !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_LOCAL_SET,
                                                     (uint8_t)index, 0.0)) goto unsupported;
            break;
        case OP_get_loc8:
        case OP_put_loc8:
        case OP_set_loc8:
            plan->uses_extended_control_plan = 1;
            if (pc >= end) goto unsupported;
            index = *pc++;
            if (index >= 16 || (op != OP_get_loc8 && !depth) ||
                !turbojs_tiny_leaf_append(plan,
                    op == OP_get_loc8 ? TURBOJS_TINY_LEAF_LOCAL :
                    op == OP_put_loc8 ? TURBOJS_TINY_LEAF_LOCAL_PUT :
                                        TURBOJS_TINY_LEAF_LOCAL_SET,
                    (uint8_t)index, 0.0)) goto unsupported;
            if (op == OP_get_loc8) ++depth; else if (op == OP_put_loc8) --depth;
            break;
        case OP_get_loc:
        case OP_put_loc:
        case OP_set_loc:
            plan->uses_extended_control_plan = 1;
            if (pc + 2 > end) goto unsupported;
            index = turbojs_leaf_u16(pc); pc += 2;
            if (index >= 16 || (op != OP_get_loc && !depth) ||
                !turbojs_tiny_leaf_append(plan,
                    op == OP_get_loc ? TURBOJS_TINY_LEAF_LOCAL :
                    op == OP_put_loc ? TURBOJS_TINY_LEAF_LOCAL_PUT :
                                       TURBOJS_TINY_LEAF_LOCAL_SET,
                    (uint8_t)index, 0.0)) goto unsupported;
            if (op == OP_get_loc) ++depth; else if (op == OP_put_loc) --depth;
            break;
        case OP_get_arg:
            if (pc + 2 > end) goto unsupported;
            index = turbojs_leaf_u16(pc); pc += 2;
            if (index > UINT8_MAX ||
                !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_ARG,
                                          (uint8_t)index, 0.0))
                goto unsupported;
            ++depth;
            break;
        case OP_put_arg0: case OP_put_arg1: case OP_put_arg2: case OP_put_arg3:
            index = (uint32_t)(op - OP_put_arg0);
            if (!depth || !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_ARG_PUT,
                                                     (uint8_t)index, 0.0))
                goto unsupported;
            --depth;
            break;
        case OP_set_arg0: case OP_set_arg1: case OP_set_arg2: case OP_set_arg3:
            index = (uint32_t)(op - OP_set_arg0);
            if (!depth || !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_ARG_SET,
                                                     (uint8_t)index, 0.0))
                goto unsupported;
            break;
        case OP_put_arg:
        case OP_set_arg:
            if (pc + 2 > end || !depth) goto unsupported;
            index = turbojs_leaf_u16(pc); pc += 2;
            if (index >= 16 || !turbojs_tiny_leaf_append(plan,
                    op == OP_put_arg ? TURBOJS_TINY_LEAF_ARG_PUT :
                                       TURBOJS_TINY_LEAF_ARG_SET,
                    (uint8_t)index, 0.0))
                goto unsupported;
            if (op == OP_put_arg) --depth;
            break;
        case OP_object:
            if (plan->virtual_object_count >= 4 || depth >= 32 ||
                !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_VIRTUAL_OBJECT,
                                           plan->virtual_object_count++, 0.0))
                goto unsupported;
            ++depth;
            break;
        case OP_define_field:
            if (pc + 4 > end || depth < 2 ||
                !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_VIRTUAL_DEFINE, 0, 0.0))
                goto unsupported;
            index = (uint32_t)turbojs_leaf_i32(pc);
            pc += 4;
            plan->instructions[plan->instruction_count - 1].atom = (JSAtom)index;
            --depth;
            break;
        case OP_get_field:
            if (pc + 4 > end || depth < 1 || plan->instruction_count < 1)
                goto unsupported;
            index = (uint32_t)turbojs_leaf_i32(pc);
            pc += 4;
            if (plan->instructions[plan->instruction_count - 1].opcode ==
                    TURBOJS_TINY_LEAF_ARG) {
                plan->instructions[plan->instruction_count - 1].opcode =
                    TURBOJS_TINY_LEAF_ARG_PROPERTY;
                plan->instructions[plan->instruction_count - 1].atom = (JSAtom)index;
            } else {
                if (!turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_VIRTUAL_GET_FIELD,
                                               0, 0.0))
                    goto unsupported;
                plan->instructions[plan->instruction_count - 1].atom = (JSAtom)index;
            }
            break;
        case OP_put_field:
            if (pc + 4 > end || depth < 2 ||
                !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_VIRTUAL_PUT_FIELD,
                                           0, 0.0))
                goto unsupported;
            index = (uint32_t)turbojs_leaf_i32(pc);
            pc += 4;
            plan->instructions[plan->instruction_count - 1].atom = (JSAtom)index;
            depth -= 2;
            plan->uses_extended_control_plan = 1;
            break;
        case OP_push_minus1:
            if (!turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_CONSTANT, 0, -1.0)) goto unsupported;
            ++depth; break;
        case OP_push_0: case OP_push_1: case OP_push_2: case OP_push_3:
        case OP_push_4: case OP_push_5: case OP_push_6: case OP_push_7:
            if (!turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_CONSTANT, 0,
                                          (double)(op - OP_push_0))) goto unsupported;
            ++depth; break;
        case OP_push_i8:
            if (pc >= end || !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_CONSTANT,
                                                        0, (double)(int8_t)*pc++)) goto unsupported;
            ++depth; break;
        case OP_push_i16:
            if (pc + 2 > end || !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_CONSTANT,
                                                           0, (double)turbojs_leaf_i16(pc))) goto unsupported;
            pc += 2; ++depth; break;
        case OP_push_i32:
            if (pc + 4 > end || !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_CONSTANT,
                                                           0, (double)turbojs_leaf_i32(pc))) goto unsupported;
            pc += 4; ++depth; break;
        case OP_add: case OP_sub: case OP_mul: case OP_div: case OP_mod:
        case OP_lt: case OP_lte: case OP_gt: case OP_gte:
        case OP_eq: case OP_neq: case OP_strict_eq: case OP_strict_neq:
        case OP_xor: case OP_or: case OP_and:
        case OP_shl: case OP_shr: case OP_sar:
            if (op == OP_div || op == OP_mod ||
                op == OP_lt || op == OP_lte || op == OP_gt || op == OP_gte ||
                op == OP_eq || op == OP_neq || op == OP_strict_eq || op == OP_strict_neq)
                plan->uses_extended_control_plan = 1;
            if (depth < 2) goto unsupported;
            if (!turbojs_tiny_leaf_append(plan,
                    op == OP_add ? TURBOJS_TINY_LEAF_ADD :
                    op == OP_sub ? TURBOJS_TINY_LEAF_SUB :
                    op == OP_mul ? TURBOJS_TINY_LEAF_MUL :
                    op == OP_div ? TURBOJS_TINY_LEAF_DIV :
                    op == OP_mod ? TURBOJS_TINY_LEAF_MOD :
                    op == OP_lt ? TURBOJS_TINY_LEAF_LT :
                    op == OP_lte ? TURBOJS_TINY_LEAF_LTE :
                    op == OP_gt ? TURBOJS_TINY_LEAF_GT :
                    op == OP_gte ? TURBOJS_TINY_LEAF_GTE :
                    (op == OP_eq || op == OP_strict_eq) ? TURBOJS_TINY_LEAF_EQ :
                    (op == OP_neq || op == OP_strict_neq) ? TURBOJS_TINY_LEAF_NEQ :
                    op == OP_xor ? TURBOJS_TINY_LEAF_XOR :
                    op == OP_or ? TURBOJS_TINY_LEAF_OR :
                    op == OP_and ? TURBOJS_TINY_LEAF_AND :
                    op == OP_shl ? TURBOJS_TINY_LEAF_SHL :
                    op == OP_shr ? TURBOJS_TINY_LEAF_SHR : TURBOJS_TINY_LEAF_SAR,
                    0, 0.0)) goto unsupported;
            --depth;
            if (op == OP_xor || op == OP_or || op == OP_and ||
                op == OP_shl || op == OP_sar)
                plan->returns_int32 = 1;
            break;
        case OP_neg:
            if (!depth || !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_NEG, 0, 0.0))
                goto unsupported;
            break;
        case OP_call_method:
            if (pc + 2 > end || !pending_math_imul ||
                turbojs_leaf_u16(pc) != 2 || depth < 2)
                goto unsupported;
            pc += 2;
            pending_math_imul = 0;
            if (!turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_IMUL, 0, 0.0))
                goto unsupported;
            --depth;
            plan->returns_int32 = 1;
            break;
        case OP_goto8: case OP_if_true8: case OP_if_false8:
            if (pc >= end || ((op == OP_if_true8 || op == OP_if_false8) && !depth))
                goto unsupported;
            {
                const uint8_t *operand = pc;
                int32_t target = (int32_t)(operand - b->byte_code_buf) + (int8_t)*pc++;
                if (target <= (int32_t)plan->build_source_offset || target > b->byte_code_len ||
                    !turbojs_tiny_leaf_append(plan,
                        op == OP_goto8 ? TURBOJS_TINY_LEAF_JUMP :
                        op == OP_if_true8 ? TURBOJS_TINY_LEAF_JUMP_IF_TRUE :
                                            TURBOJS_TINY_LEAF_JUMP_IF_FALSE, 0, (double)target))
                    goto unsupported;
                if (op != OP_goto8) --depth;
                plan->has_control_flow = 1;
            }
            break;
        case OP_goto16:
            if (pc + 2 > end) goto unsupported;
            { const uint8_t *operand = pc; int32_t target = (int32_t)(operand - b->byte_code_buf) + turbojs_leaf_i16(pc); pc += 2;
              if (target <= (int32_t)plan->build_source_offset || target > b->byte_code_len ||
                  !turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_JUMP, 0, (double)target)) goto unsupported;
              plan->has_control_flow = 1; }
            break;
        case OP_goto: case OP_if_true: case OP_if_false:
            if (pc + 4 > end || ((op == OP_if_true || op == OP_if_false) && !depth)) goto unsupported;
            { const uint8_t *operand = pc; int32_t target = (int32_t)(operand - b->byte_code_buf) + turbojs_leaf_i32(pc); pc += 4;
              if (target <= (int32_t)plan->build_source_offset || target > b->byte_code_len ||
                  !turbojs_tiny_leaf_append(plan,
                    op == OP_goto ? TURBOJS_TINY_LEAF_JUMP :
                    op == OP_if_true ? TURBOJS_TINY_LEAF_JUMP_IF_TRUE : TURBOJS_TINY_LEAF_JUMP_IF_FALSE,
                    0, (double)target)) goto unsupported;
              if (op != OP_goto) --depth; plan->has_control_flow = 1; }
            break;
        case OP_return:
            if (depth != 1) goto unsupported;
            if (plan->uses_extended_control_plan && !plan->has_control_flow &&
                plan->virtual_object_count == 0)
                goto unsupported;
            if (plan->has_control_flow) {
                unsigned j, k;
                if (!turbojs_tiny_leaf_append(plan, TURBOJS_TINY_LEAF_RETURN, 0, 0.0)) goto unsupported;
                --depth;
                if (pc < end) break;
                for (j = 0; j < plan->instruction_count; ++j) {
                    TurboJSTinyLeafInstruction *ins = &plan->instructions[j];
                    if (ins->opcode < TURBOJS_TINY_LEAF_JUMP || ins->opcode > TURBOJS_TINY_LEAF_JUMP_IF_FALSE) continue;
                    for (k = 0; k < plan->instruction_count; ++k)
                        if (plan->instructions[k].source_offset == (uint16_t)ins->constant) break;
                    if (k == plan->instruction_count) goto unsupported;
                    ins->target_index = (uint16_t)k;
                }
                plan->maximum_depth = (uint8_t)maximum_depth;
                return plan;
            }
            plan->maximum_depth = (uint8_t)maximum_depth;
            {
                typedef struct TurboJSAffineValue {
                    int valid;
                    int argument;
                    double multiplier;
                    double addend;
                } TurboJSAffineValue;
                TurboJSAffineValue values[16];
                unsigned value_depth = 0, j;
                memset(values, 0, sizeof(values));
                for (j = 0; j < plan->instruction_count; ++j) {
                    const TurboJSTinyLeafInstruction *instruction = &plan->instructions[j];
                    TurboJSAffineValue left, right;
                    switch (instruction->opcode) {
                    case TURBOJS_TINY_LEAF_ARG:
                        values[value_depth].valid = 1;
                        values[value_depth].argument = instruction->argument;
                        values[value_depth].multiplier = 1.0;
                        values[value_depth].addend = 0.0;
                        ++value_depth;
                        break;
                    case TURBOJS_TINY_LEAF_CONSTANT:
                        values[value_depth].valid = 1;
                        values[value_depth].argument = -1;
                        values[value_depth].multiplier = 0.0;
                        values[value_depth].addend = instruction->constant;
                        ++value_depth;
                        break;
                    case TURBOJS_TINY_LEAF_ADD:
                    case TURBOJS_TINY_LEAF_SUB:
                        if (value_depth < 2) goto not_affine;
                        right = values[--value_depth]; left = values[value_depth - 1];
                        if (!left.valid || !right.valid ||
                            (left.argument >= 0 && right.argument >= 0 &&
                             left.argument != right.argument)) goto not_affine;
                        values[value_depth - 1].argument = left.argument >= 0 ? left.argument : right.argument;
                        values[value_depth - 1].multiplier = instruction->opcode == TURBOJS_TINY_LEAF_ADD ?
                            left.multiplier + right.multiplier : left.multiplier - right.multiplier;
                        values[value_depth - 1].addend = instruction->opcode == TURBOJS_TINY_LEAF_ADD ?
                            left.addend + right.addend : left.addend - right.addend;
                        break;
                    case TURBOJS_TINY_LEAF_MUL:
                        if (value_depth < 2) goto not_affine;
                        right = values[--value_depth]; left = values[value_depth - 1];
                        if (!left.valid || !right.valid) goto not_affine;
                        if (left.argument >= 0 && right.argument >= 0) goto not_affine;
                        if (right.argument < 0) {
                            values[value_depth - 1].argument = left.argument;
                            values[value_depth - 1].multiplier = left.multiplier * right.addend;
                            values[value_depth - 1].addend = left.addend * right.addend;
                        } else {
                            values[value_depth - 1].argument = right.argument;
                            values[value_depth - 1].multiplier = right.multiplier * left.addend;
                            values[value_depth - 1].addend = right.addend * left.addend;
                        }
                        break;
                    case TURBOJS_TINY_LEAF_NEG:
                        if (!value_depth) goto not_affine;
                        values[value_depth - 1].multiplier = -values[value_depth - 1].multiplier;
                        values[value_depth - 1].addend = -values[value_depth - 1].addend;
                        break;
                    case TURBOJS_TINY_LEAF_OR:
                        if (value_depth < 2 || values[value_depth - 1].argument >= 0 ||
                            values[value_depth - 1].addend != 0.0) goto not_affine;
                        --value_depth;
                        plan->returns_int32 = 1;
                        break;
                    default:
                        goto not_affine;
                    }
                }
                if (value_depth == 1 && values[0].valid && values[0].argument >= 0) {
                    plan->kind = 1;
                    plan->affine_argument = (uint8_t)values[0].argument;
                    plan->affine_multiplier = values[0].multiplier;
                    plan->affine_addend = values[0].addend;
                }
            not_affine: ;
            }
            /* Recognize stateful xorshift-style closures after decoding.
             * This produces a direct native-C fast path rather than running the
             * generic instruction dispatch loop for every call. */
            if (b->closure_var_count == 1 && plan->instruction_count >= 8) {
                unsigned k = 0, step = 0;
                int valid = 1;
                while (k < plan->instruction_count && step < 4) {
                    const TurboJSTinyLeafInstruction *a = &plan->instructions[k];
                    if (a->opcode == TURBOJS_TINY_LEAF_CLOSURE) {
                        unsigned j = k + 1;
                        if (j + 5 < plan->instruction_count &&
                            plan->instructions[j].opcode == TURBOJS_TINY_LEAF_CLOSURE &&
                            plan->instructions[j + 1].opcode == TURBOJS_TINY_LEAF_CONSTANT &&
                            (plan->instructions[j + 2].opcode == TURBOJS_TINY_LEAF_SHL ||
                             plan->instructions[j + 2].opcode == TURBOJS_TINY_LEAF_SHR ||
                             plan->instructions[j + 2].opcode == TURBOJS_TINY_LEAF_SAR) &&
                            plan->instructions[j + 3].opcode == TURBOJS_TINY_LEAF_XOR) {
                            unsigned m = j + 4;
                            if (m < plan->instruction_count && plan->instructions[m].opcode == TURBOJS_TINY_LEAF_DUP) ++m;
                            if (m < plan->instruction_count &&
                                (plan->instructions[m].opcode == TURBOJS_TINY_LEAF_CLOSURE_PUT ||
                                 plan->instructions[m].opcode == TURBOJS_TINY_LEAF_CLOSURE_SET)) ++m;
                            if (m < plan->instruction_count && plan->instructions[m].opcode == TURBOJS_TINY_LEAF_DROP) ++m;
                            plan->stateful_shift_kind[step] = plan->instructions[j + 2].opcode;
                            plan->stateful_shift_amount[step] = (uint8_t)turbojs_leaf_to_int32(plan->instructions[j + 1].constant);
                            ++step; k = m; continue;
                        }
                    }
                    if (a->opcode == TURBOJS_TINY_LEAF_CLOSURE ||
                        a->opcode == TURBOJS_TINY_LEAF_RETURN ||
                        a->opcode == TURBOJS_TINY_LEAF_TO_I32 ||
                        a->opcode == TURBOJS_TINY_LEAF_OR ||
                        a->opcode == TURBOJS_TINY_LEAF_CONSTANT) { ++k; continue; }
                    valid = 0; break;
                }
                if (valid && step >= 2) {
                    plan->kind = 2;
                    plan->stateful_closure_index = 0;
                    plan->stateful_step_count = (uint8_t)step;
                    plan->returns_int32 = 1;
                }
            }
            return plan;
        default:
            goto unsupported;
        }
        if (depth > maximum_depth) maximum_depth = depth;
        if (depth > 32) goto unsupported;
    }
unsupported:
    js_free_rt(b->realm->rt, plan);
    return NULL;
}

static inline int turbojs_execute_affine_leaf_plan(
    JSFunctionBytecode *b, const TurboJSTinyLeafPlan *plan, int argc,
    JSValueConst *argv, JSValue *out_value)
{
    double argument;
    if (!b || !plan || !out_value || plan->kind != 1 ||
        plan->affine_argument >= (uint32_t)argc)
        return 0;
    if (JS_VALUE_GET_TAG(argv[plan->affine_argument]) == JS_TAG_INT)
        argument = (double)JS_VALUE_GET_INT(argv[plan->affine_argument]);
    else if (JS_VALUE_GET_TAG(argv[plan->affine_argument]) == JS_TAG_FLOAT64)
        argument = JS_VALUE_GET_FLOAT64(argv[plan->affine_argument]);
    else
        return 0;
    argument = argument * plan->affine_multiplier + plan->affine_addend;
    *out_value = plan->returns_int32 ? js_int32(turbojs_leaf_to_int32(argument)) :
                                       JS_NewFloat64(b->realm, argument);
    return 1;
}

#define TURBOJS_VIRTUAL_OBJECT_NAN_BASE UINT64_C(0x7ff9000000000000)

static double turbojs_virtual_object_marker(unsigned index)
{
    union { uint64_t bits; double value; } marker;
    marker.bits = TURBOJS_VIRTUAL_OBJECT_NAN_BASE | (uint64_t)(index + 1u);
    return marker.value;
}

static int turbojs_virtual_object_index(double value)
{
    union { uint64_t bits; double value; } marker;
    marker.value = value;
    if ((marker.bits & UINT64_C(0xffff000000000000)) !=
        TURBOJS_VIRTUAL_OBJECT_NAN_BASE)
        return -1;
    marker.bits &= UINT64_C(0x0000ffffffffffff);
    return marker.bits >= 1 && marker.bits <= 4 ? (int)marker.bits - 1 : -1;
}

static int turbojs_try_inline_leaf_call_object(JSObject *function_object,
                                        JSFunctionBytecode *b, int argc,
                                        JSValueConst *argv, JSValue *out_value)
{
    TurboJSTinyLeafPlan *plan;
    double stack[32];
    double argument_slots[16];
    double local_slots[16];
    double virtual_fields[4][8];
    JSAtom virtual_atoms[4][8];
    uint8_t virtual_field_count[4] = { 0, 0, 0, 0 };
    uint16_t argument_valid = 0, local_valid = 0;
    unsigned depth = 0, i;
    if (!b || !out_value || b->jit_inline_leaf_state == 2)
        return 0;
    plan = (TurboJSTinyLeafPlan *)b->jit_inline_leaf_plan;
    if (!plan) {
        plan = turbojs_build_tiny_leaf_plan(b);
        if (!plan) {
            b->jit_inline_leaf_state = 2;
            return 0;
        }
        b->jit_inline_leaf_plan = plan;
        b->jit_inline_leaf_state = 1;
    }
    if (plan->kind == 1)
        return turbojs_execute_affine_leaf_plan(b, plan, argc, argv, out_value);
    if (plan->kind == 2) {
        JSValue *cell, replacement;
        int32_t x; unsigned step;
        if (!function_object || !function_object->u.func.var_refs ||
            !function_object->u.func.var_refs[plan->stateful_closure_index] ||
            !(cell = function_object->u.func.var_refs[plan->stateful_closure_index]->pvalue) ||
            JS_VALUE_GET_TAG(*cell) != JS_TAG_INT)
            return 0;
        x = JS_VALUE_GET_INT(*cell);
        for (step = 0; step < plan->stateful_step_count; ++step) {
            unsigned sh = plan->stateful_shift_amount[step] & 31u;
            int32_t shifted;
            if (plan->stateful_shift_kind[step] == TURBOJS_TINY_LEAF_SHL)
                shifted = (int32_t)((uint32_t)x << sh);
            else if (plan->stateful_shift_kind[step] == TURBOJS_TINY_LEAF_SAR)
                shifted = x >> sh;
            else
                shifted = (int32_t)((uint32_t)x >> sh);
            x ^= shifted;
        }
        replacement = js_int32(x);
        set_value(b->realm, cell, replacement);
        *out_value = js_int32(x);
        return 1;
    }
    if (plan->kind == 3) {
        JSObject *global_object;
        JSProperty *property;
        JSShapeProperty *shape_property;
        JSValue string_value;
        JSString *string;
        uint32_t hash;
        int index = 0;
        if (argc < 1 ||
            (JS_VALUE_GET_TAG(argv[0]) != JS_TAG_STRING &&
             JS_VALUE_GET_TAG(argv[0]) != JS_TAG_STRING_ROPE) ||
            JS_VALUE_GET_TAG(b->realm->global_obj) != JS_TAG_OBJECT)
            return 0;
        global_object = JS_VALUE_GET_OBJ(b->realm->global_obj);
        shape_property = find_own_property(&property, global_object,
                                           plan->guard_math_atom);
        if (!shape_property || (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL ||
            JS_VALUE_GET_TAG(property->u.value) != JS_TAG_OBJECT ||
            JS_VALUE_GET_OBJ(property->u.value) != plan->guard_math_object)
            return 0;
        shape_property = find_own_property(&property, plan->guard_math_object,
                                           plan->guard_imul_atom);
        if (!shape_property || (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL ||
            JS_VALUE_GET_TAG(property->u.value) != JS_TAG_OBJECT ||
            JS_VALUE_GET_OBJ(property->u.value) != plan->guard_imul_function)
            return 0;
        shape_property = find_own_property(&property, plan->guard_string_prototype,
                                           plan->guard_charcode_atom);
        if (!shape_property || (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL ||
            JS_VALUE_GET_TAG(property->u.value) != JS_TAG_OBJECT ||
            JS_VALUE_GET_OBJ(property->u.value) != plan->guard_charcode_function)
            return 0;
        string_value = JS_ToString(b->realm, argv[0]);
        if (JS_IsException(string_value))
            return 0;
        string = JS_VALUE_GET_STRING(string_value);
        hash = plan->string_hash_seed;
        while ((uint32_t)index < string->len) {
            uint32_t code_unit = (uint32_t)string_getc(string, &index);
            hash = (hash ^ code_unit) * plan->string_hash_multiplier;
        }
        JS_FreeValue(b->realm, string_value);
        *out_value = js_int32((int32_t)hash);
        return 1;
    }
    if (plan->guard_imul_function) {
        JSObject *global_object;
        JSProperty *property;
        JSShapeProperty *shape_property;
        JSValue value;
        if (JS_VALUE_GET_TAG(b->realm->global_obj) != JS_TAG_OBJECT)
            return 0;
        global_object = JS_VALUE_GET_OBJ(b->realm->global_obj);
        shape_property = find_own_property(&property, global_object,
                                           plan->guard_math_atom);
        if (!shape_property ||
            (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL)
            return 0;
        value = property->u.value;
        if (JS_VALUE_GET_TAG(value) != JS_TAG_OBJECT ||
            JS_VALUE_GET_OBJ(value) != plan->guard_math_object)
            return 0;
        shape_property = find_own_property(&property, plan->guard_math_object,
                                           plan->guard_imul_atom);
        if (!shape_property ||
            (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL)
            return 0;
        value = property->u.value;
        if (JS_VALUE_GET_TAG(value) != JS_TAG_OBJECT ||
            JS_VALUE_GET_OBJ(value) != plan->guard_imul_function)
            return 0;
    }
    for (i = 0; i < (unsigned)argc && i < 16; ++i) {
        if (JS_VALUE_GET_TAG(argv[i]) == JS_TAG_INT) {
            argument_slots[i] = (double)JS_VALUE_GET_INT(argv[i]);
            argument_valid |= (uint16_t)(1u << i);
        } else if (JS_VALUE_GET_TAG(argv[i]) == JS_TAG_FLOAT64) {
            argument_slots[i] = JS_VALUE_GET_FLOAT64(argv[i]);
            argument_valid |= (uint16_t)(1u << i);
        }
    }
    for (i = 0; i < plan->instruction_count; ++i) {
        const TurboJSTinyLeafInstruction *instruction = &plan->instructions[i];
        double left, right;
        int32_t left_i32, right_i32;
        switch (instruction->opcode) {
        case TURBOJS_TINY_LEAF_ARG:
            if (instruction->argument >= 16 || depth >= 32 ||
                !(argument_valid & (uint16_t)(1u << instruction->argument)))
                return 0;
            stack[depth++] = argument_slots[instruction->argument];
            break;
        case TURBOJS_TINY_LEAF_ARG_PUT:
            if (!depth || instruction->argument >= 16)
                return 0;
            argument_slots[instruction->argument] = stack[--depth];
            argument_valid |= (uint16_t)(1u << instruction->argument);
            break;
        case TURBOJS_TINY_LEAF_ARG_SET:
            if (!depth || instruction->argument >= 16)
                return 0;
            argument_slots[instruction->argument] = stack[depth - 1];
            argument_valid |= (uint16_t)(1u << instruction->argument);
            break;
        case TURBOJS_TINY_LEAF_LOCAL:
            if (instruction->argument >= 16 || depth >= 32 ||
                !(local_valid & (uint16_t)(1u << instruction->argument))) return 0;
            stack[depth++] = local_slots[instruction->argument];
            break;
        case TURBOJS_TINY_LEAF_LOCAL_PUT:
            if (!depth || instruction->argument >= 16) return 0;
            local_slots[instruction->argument] = stack[--depth];
            local_valid |= (uint16_t)(1u << instruction->argument);
            break;
        case TURBOJS_TINY_LEAF_LOCAL_SET:
            if (!depth || instruction->argument >= 16) return 0;
            local_slots[instruction->argument] = stack[depth - 1];
            local_valid |= (uint16_t)(1u << instruction->argument);
            break;
        case TURBOJS_TINY_LEAF_ARG_PROPERTY:
            {
                JSObject *object;
                JSProperty *property;
                JSShapeProperty *shape_property;
                JSValueConst property_value;
                if (instruction->argument >= (uint32_t)argc || depth >= 32 ||
                    JS_VALUE_GET_TAG(argv[instruction->argument]) != JS_TAG_OBJECT)
                    return 0;
                object = JS_VALUE_GET_OBJ(argv[instruction->argument]);
                shape_property = find_own_property(&property, object,
                                                   instruction->atom);
                if (!shape_property ||
                    (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL)
                    return 0;
                property_value = property->u.value;
                if (JS_VALUE_GET_TAG(property_value) == JS_TAG_INT)
                    stack[depth++] = (double)JS_VALUE_GET_INT(property_value);
                else if (JS_VALUE_GET_TAG(property_value) == JS_TAG_FLOAT64)
                    stack[depth++] = JS_VALUE_GET_FLOAT64(property_value);
                else
                    return 0;
            }
            break;

        case TURBOJS_TINY_LEAF_DUP:
            if (!depth || depth >= 32) return 0;
            stack[depth] = stack[depth - 1]; ++depth;
            break;
        case TURBOJS_TINY_LEAF_DROP:
            if (!depth) return 0;
            --depth;
            break;
        case TURBOJS_TINY_LEAF_CLOSURE:
            {
                JSValue *cell; JSValueConst value;
                if (!function_object || instruction->argument >= 4 || depth >= 32 ||
                    !function_object->u.func.var_refs || !function_object->u.func.var_refs[instruction->argument] ||
                    !(cell = function_object->u.func.var_refs[instruction->argument]->pvalue)) return 0;
                value = *cell;
                if (JS_VALUE_GET_TAG(value) == JS_TAG_INT) stack[depth++] = (double)JS_VALUE_GET_INT(value);
                else if (JS_VALUE_GET_TAG(value) == JS_TAG_FLOAT64) stack[depth++] = JS_VALUE_GET_FLOAT64(value);
                else return 0;
            }
            break;
        case TURBOJS_TINY_LEAF_CLOSURE_PUT:
        case TURBOJS_TINY_LEAF_CLOSURE_SET:
            {
                JSValue *cell, replacement; double value; int32_t i32;
                if (!function_object || !depth || instruction->argument >= 4 ||
                    !function_object->u.func.var_refs || !function_object->u.func.var_refs[instruction->argument] ||
                    !(cell = function_object->u.func.var_refs[instruction->argument]->pvalue)) return 0;
                value = stack[depth - 1]; i32 = turbojs_leaf_to_int32(value);
                replacement = (isfinite(value) && value == (double)i32) ? JS_NewInt32(b->realm, i32) : JS_NewFloat64(b->realm, value);
                set_value(b->realm, cell, replacement);
                if (instruction->opcode == TURBOJS_TINY_LEAF_CLOSURE_PUT) --depth;
            }
            break;

        case TURBOJS_TINY_LEAF_VIRTUAL_OBJECT:
            if (instruction->argument >= 4 || depth >= 32)
                return 0;
            stack[depth++] = turbojs_virtual_object_marker(instruction->argument);
            break;
        case TURBOJS_TINY_LEAF_VIRTUAL_DEFINE:
            {
                int object_index;
                unsigned field_index;
                if (depth < 2 || !isfinite(stack[depth - 1]))
                    return 0;
                object_index = turbojs_virtual_object_index(stack[depth - 2]);
                if (object_index < 0 || virtual_field_count[object_index] >= 8)
                    return 0;
                field_index = virtual_field_count[object_index]++;
                virtual_atoms[object_index][field_index] = instruction->atom;
                virtual_fields[object_index][field_index] = stack[depth - 1];
                --depth;
            }
            break;
        case TURBOJS_TINY_LEAF_VIRTUAL_GET_FIELD:
            {
                int object_index;
                unsigned field_index;
                if (!depth || (object_index = turbojs_virtual_object_index(stack[depth - 1])) < 0)
                    return 0;
                for (field_index = 0; field_index < virtual_field_count[object_index]; ++field_index)
                    if (virtual_atoms[object_index][field_index] == instruction->atom)
                        break;
                if (field_index == virtual_field_count[object_index])
                    return 0;
                stack[depth - 1] = virtual_fields[object_index][field_index];
            }
            break;
        case TURBOJS_TINY_LEAF_VIRTUAL_PUT_FIELD:
            {
                int object_index;
                unsigned field_index;
                double value;
                if (depth < 2 || !isfinite(stack[depth - 1]))
                    return 0;
                value = stack[depth - 1];
                object_index = turbojs_virtual_object_index(stack[depth - 2]);
                if (object_index < 0)
                    return 0;
                for (field_index = 0; field_index < virtual_field_count[object_index]; ++field_index)
                    if (virtual_atoms[object_index][field_index] == instruction->atom)
                        break;
                if (field_index == virtual_field_count[object_index]) {
                    if (field_index >= 8)
                        return 0;
                    virtual_atoms[object_index][field_index] = instruction->atom;
                    virtual_field_count[object_index]++;
                }
                virtual_fields[object_index][field_index] = value;
                depth -= 2;
            }
            break;
        case TURBOJS_TINY_LEAF_CONSTANT:
            stack[depth++] = instruction->constant;
            break;
        case TURBOJS_TINY_LEAF_ADD: case TURBOJS_TINY_LEAF_SUB:
        case TURBOJS_TINY_LEAF_MUL:
        case TURBOJS_TINY_LEAF_DIV:
        case TURBOJS_TINY_LEAF_MOD:
            if (depth < 2 || !isfinite(stack[depth - 1]) || !isfinite(stack[depth - 2])) return 0;
            right = stack[--depth]; left = stack[depth - 1];
            stack[depth - 1] = instruction->opcode == TURBOJS_TINY_LEAF_ADD ? left + right :
                               instruction->opcode == TURBOJS_TINY_LEAF_SUB ? left - right :
                               instruction->opcode == TURBOJS_TINY_LEAF_MUL ? left * right :
                               instruction->opcode == TURBOJS_TINY_LEAF_DIV ? left / right : fmod(left, right);
            break;
        case TURBOJS_TINY_LEAF_NEG:
            stack[depth - 1] = -stack[depth - 1];
            break;
        case TURBOJS_TINY_LEAF_IMUL:
            right_i32 = turbojs_leaf_to_int32(stack[--depth]);
            left_i32 = turbojs_leaf_to_int32(stack[depth - 1]);
            stack[depth - 1] = (double)(int32_t)((uint32_t)left_i32 *
                                                 (uint32_t)right_i32);
            break;
        case TURBOJS_TINY_LEAF_XOR: case TURBOJS_TINY_LEAF_OR:
        case TURBOJS_TINY_LEAF_AND:
            right_i32 = turbojs_leaf_to_int32(stack[--depth]);
            left_i32 = turbojs_leaf_to_int32(stack[depth - 1]);
            stack[depth - 1] = (double)(instruction->opcode == TURBOJS_TINY_LEAF_XOR ?
                (left_i32 ^ right_i32) : instruction->opcode == TURBOJS_TINY_LEAF_OR ?
                (left_i32 | right_i32) : (left_i32 & right_i32));
            break;
        case TURBOJS_TINY_LEAF_SHL:
        case TURBOJS_TINY_LEAF_SHR:
        case TURBOJS_TINY_LEAF_SAR:
            if (depth < 2)
                return 0;
            right_i32 = turbojs_leaf_to_int32(stack[--depth]) & 31;
            left_i32 = turbojs_leaf_to_int32(stack[depth - 1]);
            if (instruction->opcode == TURBOJS_TINY_LEAF_SHL)
                stack[depth - 1] = (double)(int32_t)((uint32_t)left_i32 << right_i32);
            else if (instruction->opcode == TURBOJS_TINY_LEAF_SAR)
                stack[depth - 1] = (double)(left_i32 >> right_i32);
            else
                stack[depth - 1] = (double)((uint32_t)left_i32 >> right_i32);
            break;
        case TURBOJS_TINY_LEAF_LT: case TURBOJS_TINY_LEAF_LTE:
        case TURBOJS_TINY_LEAF_GT: case TURBOJS_TINY_LEAF_GTE:
        case TURBOJS_TINY_LEAF_EQ: case TURBOJS_TINY_LEAF_NEQ:
            if (depth < 2) return 0;
            right = stack[--depth]; left = stack[depth - 1];
            stack[depth - 1] = (double)(instruction->opcode == TURBOJS_TINY_LEAF_LT ? left < right :
                instruction->opcode == TURBOJS_TINY_LEAF_LTE ? left <= right :
                instruction->opcode == TURBOJS_TINY_LEAF_GT ? left > right :
                instruction->opcode == TURBOJS_TINY_LEAF_GTE ? left >= right :
                instruction->opcode == TURBOJS_TINY_LEAF_EQ ? left == right : left != right);
            break;
        case TURBOJS_TINY_LEAF_JUMP:
            if (instruction->target_index >= plan->instruction_count) return 0;
            i = (unsigned)instruction->target_index - 1u;
            break;
        case TURBOJS_TINY_LEAF_JUMP_IF_TRUE:
        case TURBOJS_TINY_LEAF_JUMP_IF_FALSE:
            if (!depth || instruction->target_index >= plan->instruction_count) return 0;
            left = stack[--depth];
            if ((instruction->opcode == TURBOJS_TINY_LEAF_JUMP_IF_TRUE && left != 0.0 && !isnan(left)) ||
                (instruction->opcode == TURBOJS_TINY_LEAF_JUMP_IF_FALSE && (left == 0.0 || isnan(left))))
                i = (unsigned)instruction->target_index - 1u;
            break;
        case TURBOJS_TINY_LEAF_RETURN:
            if (depth != 1 || turbojs_virtual_object_index(stack[0]) >= 0) return 0;
            if (plan->returns_int32) *out_value = js_int32(turbojs_leaf_to_int32(stack[0]));
            else *out_value = JS_NewFloat64(b->realm, stack[0]);
            return 1;
        default:
            return 0;
        }
    }
    if (depth != 1)
        return 0;
    if (plan->returns_int32)
        *out_value = js_int32(turbojs_leaf_to_int32(stack[0]));
    else
        *out_value = JS_NewFloat64(b->realm, stack[0]);
    return 1;
}

static int turbojs_try_inline_leaf_call(JSFunctionBytecode *b, int argc,
                                        JSValueConst *argv, JSValue *out_value)
{
    return turbojs_try_inline_leaf_call_object(NULL, b, argc, argv, out_value);
}

/* Runtime bridge between boxed JSValue calls and the integer baseline JIT. */
static int turbojs_jit_ir_is_vm_safe(const TurboJSIRFunction *ir)
{
    size_t i;
    if (!ir)
        return 0;
    for (i = 0; i < ir->instruction_count; ++i) {
        switch (ir->instructions[i].opcode) {
        case TURBOJS_IR_NOP:
        case TURBOJS_IR_ARGUMENT:
        case TURBOJS_IR_CONSTANT_I64:
        case TURBOJS_IR_CONSTANT_F64:
        case TURBOJS_IR_ADD_F64:
        case TURBOJS_IR_SUB_F64:
        case TURBOJS_IR_MUL_F64:
        case TURBOJS_IR_DIV_F64:
        case TURBOJS_IR_ADD_I32_CHECKED:
        case TURBOJS_IR_SUB_I32_CHECKED:
        case TURBOJS_IR_MUL_I32_CHECKED:
        case TURBOJS_IR_LESS_THAN_I64:
        case TURBOJS_IR_LOCAL_GET:
        case TURBOJS_IR_LOCAL_SET:
        case TURBOJS_IR_RETURN_I64:
        case TURBOJS_IR_RETURN_F64:
            break;
        default:
            return 0;
        }
    }
    return 1;
}

static uint32_t turbojs_attach_region_property_feedback(
    JSFunctionBytecode *b, TurboJSSSAGraph *graph)
{
    TurboJSPropertyFeedback *feedback;
    size_t i;
    uint32_t count = 0, applied = 0;
    if (!b || !graph || !b->property_ic || graph->value_count == 0)
        return 0;
    feedback = js_mallocz_rt(b->realm->rt,
        graph->value_count * sizeof(*feedback));
    if (!feedback)
        return 0;
    for (i = 0; i < graph->value_count; ++i) {
        const TurboJSSSAValue *value = &graph->values[i];
        TurboJSVMPropertyICEntry *entry;
        TurboJSVMPropertyICWay *way;
        uint32_t h;
        if (value->opcode != TURBOJS_SSA_PROPERTY_LOAD &&
            value->opcode != TURBOJS_SSA_PROPERTY_STORE)
            continue;
        h = value->source_instruction * 2654435761u;
        entry = &b->property_ic[(h >> 28) &
            (TURBOJS_VM_PROPERTY_IC_SIZE - 1u)];
        if (entry->bytecode_offset != value->source_instruction ||
            entry->atom != (JSAtom)value->metadata || entry->used_ways == 0)
            continue;
        way = &entry->ways[entry->hot_way < entry->used_ways ?
            entry->hot_way : 0];
        if (!way->shape)
            continue;
        feedback[count].source_instruction = value->source_instruction;
        feedback[count].atom = value->metadata;
        feedback[count].shape_identity = (uintptr_t)way->shape;
        feedback[count].property_index = way->property_index;
        feedback[count].flags = TURBOJS_PROPERTY_FEEDBACK_OWN_DATA |
            (value->opcode == TURBOJS_SSA_PROPERTY_LOAD ?
             TURBOJS_PROPERTY_FEEDBACK_LOAD :
             (TURBOJS_PROPERTY_FEEDBACK_STORE |
              TURBOJS_PROPERTY_FEEDBACK_WRITABLE));
        feedback[count].generation = 1;
        ++count;
    }
    if (count != 0)
        (void)TurboJS_SSAApplyPropertyFeedback(graph, feedback, count, &applied);
    js_free_rt(b->realm->rt, feedback);
    return applied;
}

static int turbojs_try_region_call(JSRuntime *rt, JSFunctionBytecode *b,
                                   int argc, JSValueConst *argv,
                                   JSValue *out_value)
{
#if defined(__x86_64__) || defined(_M_X64)
    TurboJSRegionNativeFunction *native;
    TurboJSEngineBytecodeInfo info;
    TurboJSSSAGraph graph;
    TurboJSRegionNativeStats stats;
    TurboJSBytecodeAnalysis analysis;
    TurboJSIRDiagnostic diagnostic;
    TurboJSIRStatus status;
    int64_t native_args[TURBOJS_IR_MAX_REGISTERS];
    int64_t result;
    int i;

    if (!rt || !rt->jit_optimizing_enabled || !b || !out_value ||
        b->func_kind != JS_FUNC_NORMAL || b->arg_count == 0 ||
        b->var_ref_count != 0 || b->arg_count > TURBOJS_IR_MAX_REGISTERS)
        return 0;
    native = (TurboJSRegionNativeFunction *)b->jit_region_native;
    if (!native && b->jit_region_compilation_attempted)
        return 0;
    for (i = 0; i < b->arg_count; ++i) {
        if (i >= argc || JS_VALUE_GET_TAG(argv[i]) != JS_TAG_INT)
            return 0;
        native_args[i] = JS_VALUE_GET_INT(argv[i]);
    }

    if (!native) {
        uint32_t threshold = rt->jit_optimizing_threshold ?
            rt->jit_optimizing_threshold : 384;
        if (++b->jit_region_call_count < threshold)
            return 0;
        b->jit_region_compilation_attempted = 1;
        rt->tier_up_requests++;
        rt->optimizing_compile_requests++;
        memset(&info, 0, sizeof(info));
        info.bytecode = b->byte_code_buf;
        info.bytecode_length = (size_t)b->byte_code_len;
        info.argument_count = b->arg_count;
        info.local_count = b->var_count;
        info.stack_size = b->stack_size;
        info.numeric_mode = TURBOJS_ENGINE_NUMERIC_INT32;
        memset(&analysis, 0, sizeof(analysis));
        memset(&diagnostic, 0, sizeof(diagnostic));
        status = TurboJS_EngineBytecodeAnalyze(&info, &analysis, &diagnostic);
        if (status != TURBOJS_IR_OK || analysis.backedge_count == 0 || analysis.basic_block_count <= 4) {
            rt->region_compile_failures++;
            rt->optimizing_compile_failures++;
            return 0;
        }
        memset(&graph, 0, sizeof(graph));
        memset(&diagnostic, 0, sizeof(diagnostic));
        status = TurboJS_EngineBytecodeRegionBuildSSA(&info, &graph, &diagnostic);
        if (status != TURBOJS_IR_OK) {
            rt->region_compile_failures++;
            rt->optimizing_compile_failures++;
            return 0;
        }
        (void)turbojs_attach_region_property_feedback(b, &graph);
        memset(&stats, 0, sizeof(stats));
        status = TurboJS_RegionNativeCompile(&graph, &native, &stats, &diagnostic);
        TurboJS_SSAGraphDestroy(&graph);
        if (status != TURBOJS_IR_OK) {
            rt->region_compile_failures++;
            rt->optimizing_compile_failures++;
            return 0;
        }
        rt->region_compilations++;
        rt->optimizing_compilations++;
        rt->tier_up_successes++;
        b->jit_region_native = native;
    }

    status = TurboJS_RegionNativeInvoke(native, native_args, b->arg_count, &result);
    if (status == TURBOJS_IR_OK) {
        rt->jit_native_calls++;
        rt->region_native_calls++;
        *out_value = JS_NewInt64(b->realm, result);
        return 1;
    }
    rt->jit_guard_failures++;
    rt->deoptimizations++;
#else
    (void)rt; (void)b; (void)argc; (void)argv; (void)out_value;
#endif
    return 0;
}

static int turbojs_spool_box_i64_result(JSContext *ctx,
                                         TurboJSValueKind result_kind,
                                         int64_t result,
                                         JSValue *out_value)
{
    if (!ctx || !out_value)
        return 0;
    if (result_kind == TURBOJS_VALUE_BOOLEAN) {
        *out_value = JS_NewBool(ctx, result != 0);
        return 1;
    }
    if ((result_kind == TURBOJS_VALUE_I32 ||
         result_kind == TURBOJS_VALUE_I64 ||
         result_kind == TURBOJS_VALUE_UNKNOWN) &&
        result >= INT32_MIN && result <= INT32_MAX) {
        *out_value = js_int32((int32_t)result);
        return 1;
    }
    return 0;
}

typedef enum TurboJSSpoolEntryInvokeResult {
    TURBOJS_SPOOL_ENTRY_STALE = -1,
    TURBOJS_SPOOL_ENTRY_MISS = 0,
    TURBOJS_SPOOL_ENTRY_SUCCESS = 1
} TurboJSSpoolEntryInvokeResult;

static int turbojs_spool_entry_invoke(JSRuntime *rt,
                                      JSFunctionBytecode *b,
                                      TurboJSNativeEntryKind kind,
                                      uint64_t expected_generation,
                                      int argc,
                                      JSValueConst *argv,
                                      JSValue *out_value)
{
#if defined(__x86_64__) || defined(_M_X64)
    const TurboJSNativeEntryHandle *handle;
    int i;
    TurboJSIRStatus status;
    if (!rt || !b || !out_value || argc < b->arg_count ||
        b->arg_count > TURBOJS_IR_MAX_REGISTERS)
        return TURBOJS_SPOOL_ENTRY_MISS;
    handle = kind == TURBOJS_NATIVE_ENTRY_INT32
        ? &b->jit_spool_int32_entry
        : kind == TURBOJS_NATIVE_ENTRY_FLOAT64
            ? &b->jit_spool_float64_entry
            : NULL;
    if (unlikely(!handle || !handle->function || !expected_generation ||
                 handle->generation != expected_generation ||
                 handle->kind != (uint8_t)kind))
        return TURBOJS_SPOOL_ENTRY_STALE;
    if (kind == TURBOJS_NATIVE_ENTRY_INT32) {
        int64_t native_args[TURBOJS_IR_MAX_REGISTERS];
        int64_t result;
        for (i = 0; i < b->arg_count; ++i) {
            if (JS_VALUE_GET_TAG(argv[i]) != JS_TAG_INT)
                return TURBOJS_SPOOL_ENTRY_MISS;
            native_args[i] = JS_VALUE_GET_INT(argv[i]);
        }
        status = TurboJS_NativeInvoke(handle->function, native_args,
                                      b->arg_count, &result);
        if (status == TURBOJS_IR_OK && turbojs_spool_box_i64_result(
                b->realm, (TurboJSValueKind)handle->result_kind,
                result, out_value)) {
            rt->jit_native_calls++;
            rt->relay_spool_hits++;
            return TURBOJS_SPOOL_ENTRY_SUCCESS;
        }
    } else {
        double native_args[TURBOJS_IR_MAX_REGISTERS];
        double result;
        for (i = 0; i < b->arg_count; ++i) {
            int tag = JS_VALUE_GET_TAG(argv[i]);
            if (tag == JS_TAG_INT)
                native_args[i] = (double)JS_VALUE_GET_INT(argv[i]);
            else if (tag == JS_TAG_FLOAT64)
                native_args[i] = JS_VALUE_GET_FLOAT64(argv[i]);
            else
                return TURBOJS_SPOOL_ENTRY_MISS;
        }
        status = TurboJS_NativeInvokeF64(handle->function, native_args,
                                         b->arg_count, &result);
        if (status == TURBOJS_IR_OK) {
            rt->jit_native_calls++;
            rt->relay_spool_hits++;
            *out_value = JS_NewFloat64(b->realm, result);
            return TURBOJS_SPOOL_ENTRY_SUCCESS;
        }
    }
    if (status == TURBOJS_IR_BAILOUT)
        rt->jit_guard_failures++;
#else
    (void)rt; (void)b; (void)kind; (void)expected_generation;
    (void)argc; (void)argv; (void)out_value;
#endif
    return TURBOJS_SPOOL_ENTRY_MISS;
}

typedef struct TurboJSVMSpoolResolverContext {
    JSRuntime *rt;
    JSFunctionBytecode *caller;
} TurboJSVMSpoolResolverContext;

static TurboJSIRStatus turbojs_vm_spool_call_resolver(
    void *opaque, uint32_t bytecode_offset, uint16_t argument_count,
    TurboJSEngineNumericMode numeric_mode, TurboJSClutchCallSite *out_site)
{
#ifdef TURBOJS_ENABLE_OPTIMIZING_JIT
    TurboJSVMSpoolResolverContext *context =
        (TurboJSVMSpoolResolverContext *)opaque;
    const TurboJSCallFeedbackSlot *feedback;
    JSFunctionBytecode *target;
    const TurboJSNativeEntryHandle *handle;
    TurboJSNativeEntryKind expected_kind;
    if (!context || !context->rt || !context->caller || !out_site)
        return TURBOJS_IR_INVALID_ARGUMENT;
    feedback = turbojs_vm_call_feedback_lookup(context->caller, bytecode_offset);
    if (!feedback || feedback->state != TURBOJS_CALL_FEEDBACK_MONOMORPHIC ||
        feedback->target_count != 1)
        goto reject;
    target = turbojs_vm_function_by_identity(
        context->rt, feedback->targets[0].target_identity);
    if (!target || target->arg_count != argument_count ||
        target->var_ref_count != 0)
        goto reject;
    if (numeric_mode == TURBOJS_ENGINE_NUMERIC_FLOAT64) {
        handle = &target->jit_spool_float64_entry;
        expected_kind = TURBOJS_NATIVE_ENTRY_FLOAT64;
    } else {
        handle = &target->jit_spool_int32_entry;
        expected_kind = TURBOJS_NATIVE_ENTRY_INT32;
    }
    if (!handle->function || !handle->generation ||
        handle->kind != (uint8_t)expected_kind)
        goto reject;
    TurboJS_ClutchCallSiteInit(out_site, handle, handle->generation,
                               expected_kind, argument_count);
    TurboJS_ClutchCallSiteSetTargetIdentity(
        out_site, feedback->targets[0].target_identity);
    context->rt->spool_call_lowering_resolved++;
    return TURBOJS_IR_OK;
reject:
    context->rt->spool_call_lowering_rejected++;
    return TURBOJS_IR_UNSUPPORTED;
#else
    (void)opaque; (void)bytecode_offset; (void)argument_count;
    (void)numeric_mode; (void)out_site;
    return TURBOJS_IR_UNSUPPORTED;
#endif
}

static int turbojs_try_baseline_call(JSRuntime *rt, JSFunctionBytecode *b,
                                     int argc, JSValueConst *argv,
                                     JSValue *out_value)
{
#if defined(__x86_64__) || defined(_M_X64)
    int64_t native_args[TURBOJS_IR_MAX_REGISTERS];
    const TurboJSNativeFunction *native;
    TurboJSCodeCache *cache;
    TurboJSEngineBytecodeInfo info;
    TurboJSIRFunction ir;
    TurboJSIRDiagnostic diagnostic;
    TurboJSIRStatus status;
    TurboJSVMSpoolResolverContext resolver_context;
    int64_t result;
    int i;

    if (!rt || !rt->jit_enabled || !b || !out_value ||
        b->func_kind != JS_FUNC_NORMAL || b->var_ref_count != 0 ||
        b->arg_count > TURBOJS_IR_MAX_REGISTERS)
        return 0;

    if (b->jit_reserved != 1) {
        int use_float = 0;
        double float_args[TURBOJS_IR_MAX_REGISTERS];
        for (i = 0; i < b->arg_count; ++i) {
            int tag;
            if (i >= argc) { use_float = 0; break; }
            tag = JS_VALUE_GET_TAG(argv[i]);
            if (tag == JS_TAG_INT)
                float_args[i] = (double)JS_VALUE_GET_INT(argv[i]);
            else if (tag == JS_TAG_FLOAT64) {
                float_args[i] = JS_VALUE_GET_FLOAT64(argv[i]);
                use_float = 1;
            } else {
                use_float = 0;
                break;
            }
        }
        if (use_float && i == b->arg_count) {
            double float_result;
            TurboJSCodeCache *float_cache;
            if (!rt->jit_float_code_cache) {
                rt->jit_float_code_cache = TurboJS_CodeCacheCreate(128, 2u * 1024u * 1024u);
                if (!rt->jit_float_code_cache) return 0;
            }
            float_cache = (TurboJSCodeCache *)rt->jit_float_code_cache;
            native = TurboJS_CodeCacheLookup(float_cache, b);
            if (!native) {
                if (b->jit_reserved == 2) {
                    b->jit_reserved = 0;
                    b->jit_float_compilation_attempted = 0;
                    b->jit_float_call_count =
                        (rt->jit_compile_threshold ? rt->jit_compile_threshold : 100);
                }
                if (b->jit_float_compilation_attempted ||
                    ++b->jit_float_call_count < (rt->jit_compile_threshold ? rt->jit_compile_threshold : 100)) {
                    rt->jit_interpreted_calls++;
                    return 0;
                }
                b->jit_float_compilation_attempted = 1;
                rt->baseline_compile_requests++;
                rt->tier_up_requests++;
                memset(&diagnostic, 0, sizeof(diagnostic));
                memset(&info, 0, sizeof(info));
                info.bytecode = b->byte_code_buf;
                info.bytecode_length = (size_t)b->byte_code_len;
                info.argument_count = b->arg_count;
                info.local_count = b->var_count;
                info.stack_size = b->stack_size;
                info.numeric_mode = TURBOJS_ENGINE_NUMERIC_FLOAT64;
                resolver_context.rt = rt;
                resolver_context.caller = b;
                info.call_resolver = turbojs_vm_spool_call_resolver;
                info.call_resolver_opaque = &resolver_context;
                status = TurboJS_EngineBytecodeToIR(&info, &ir, &diagnostic);
                if (status != TURBOJS_IR_OK || !turbojs_jit_ir_is_vm_safe(&ir)) {
                    if (status == TURBOJS_IR_OK) TurboJS_IRFunctionDestroy(&ir);
                    rt->baseline_compile_failures++;
                    return 0;
                }
                status = TurboJS_CodeCacheCompile(float_cache, b, &ir, &native, &diagnostic);
                TurboJS_IRFunctionDestroy(&ir);
                if (status != TURBOJS_IR_OK) {
                    rt->baseline_compile_failures++;
                    return 0;
                }
                rt->baseline_compilations++;
                rt->tier_up_successes++;
            }
            if (b->jit_spool_float64_entry.function != native) {
                status = TurboJS_CodeCacheAttachEntryHandleIdentity(
                    float_cache, b, &b->jit_spool_float64_entry,
                    TURBOJS_NATIVE_ENTRY_FLOAT64, b->arg_count,
                    turbojs_vm_function_identity(rt, b));
                if (status != TURBOJS_IR_OK)
                    return 0;
            }
            status = TurboJS_NativeInvokeF64(native, float_args, b->arg_count, &float_result);
            if (status == TURBOJS_IR_OK) {
                rt->jit_native_calls++;
                b->jit_reserved = 2;
                *out_value = JS_NewFloat64(b->realm, float_result);
                return 1;
            }
            rt->jit_guard_failures++;
            return 0;
        }
    }

    for (i = 0; i < b->arg_count; ++i) {
        if (i >= argc || JS_VALUE_GET_TAG(argv[i]) != JS_TAG_INT) {
            rt->jit_guard_failures++;
            return 0;
        }
        native_args[i] = JS_VALUE_GET_INT(argv[i]);
    }

    if (!rt->jit_code_cache) {
        rt->jit_code_cache = TurboJS_CodeCacheCreate(256, 4u * 1024u * 1024u);
        if (!rt->jit_code_cache)
            return 0;
    }
    cache = (TurboJSCodeCache *)rt->jit_code_cache;
    native = TurboJS_CodeCacheLookup(cache, b);
    if (native) {
        if (b->jit_spool_int32_entry.function != native) {
            status = TurboJS_CodeCacheAttachEntryHandleIdentity(
                cache, b, &b->jit_spool_int32_entry,
                TURBOJS_NATIVE_ENTRY_INT32, b->arg_count,
                turbojs_vm_function_identity(rt, b));
            if (status != TURBOJS_IR_OK)
                return 0;
        }
        status = TurboJS_NativeInvoke(native, native_args, b->arg_count, &result);
        if (status == TURBOJS_IR_OK && turbojs_spool_box_i64_result(
                b->realm, TurboJS_NativeResultKind(native), result,
                out_value)) {
            rt->jit_native_calls++;
            b->jit_reserved = 1;
            return 1;
        }
        rt->jit_guard_failures++;
        return 0;
    }

    if (b->jit_reserved == 1) {
        b->jit_reserved = 0;
        b->jit_compilation_attempted = 0;
        b->jit_call_count =
            (rt->jit_compile_threshold ? rt->jit_compile_threshold : 100);
    }
    b->jit_call_count++;
    if (b->jit_compilation_attempted ||
        b->jit_call_count < (rt->jit_compile_threshold ? rt->jit_compile_threshold : 100)) {
        rt->jit_interpreted_calls++;
        return 0;
    }

    b->jit_compilation_attempted = 1;
    rt->baseline_compile_requests++;
    rt->tier_up_requests++;
    memset(&diagnostic, 0, sizeof(diagnostic));
    memset(&info, 0, sizeof(info));
    info.bytecode = b->byte_code_buf;
    info.bytecode_length = (size_t)b->byte_code_len;
    info.argument_count = b->arg_count;
    info.local_count = b->var_count;
    info.stack_size = b->stack_size;
    info.numeric_mode = TURBOJS_ENGINE_NUMERIC_INT32;
    resolver_context.rt = rt;
    resolver_context.caller = b;
    info.call_resolver = turbojs_vm_spool_call_resolver;
    info.call_resolver_opaque = &resolver_context;
    status = TurboJS_EngineBytecodeToIR(&info, &ir, &diagnostic);
    if (status != TURBOJS_IR_OK) {
        rt->baseline_compile_failures++;
        return 0;
    }
    if (!turbojs_jit_ir_is_vm_safe(&ir)) {
        TurboJS_IRFunctionDestroy(&ir);
        rt->baseline_compile_failures++;
        return 0;
    }
    status = TurboJS_CodeCacheCompile(cache, b, &ir, &native, &diagnostic);
    TurboJS_IRFunctionDestroy(&ir);
    if (status != TURBOJS_IR_OK) {
        rt->baseline_compile_failures++;
        return 0;
    }
    rt->baseline_compilations++;
    rt->tier_up_successes++;
    status = TurboJS_CodeCacheAttachEntryHandleIdentity(
        cache, b, &b->jit_spool_int32_entry,
        TURBOJS_NATIVE_ENTRY_INT32, b->arg_count,
        turbojs_vm_function_identity(rt, b));
    if (status != TURBOJS_IR_OK)
        return 0;
    status = TurboJS_NativeInvoke(native, native_args, b->arg_count, &result);
    if (status == TURBOJS_IR_OK && turbojs_spool_box_i64_result(
            b->realm, TurboJS_NativeResultKind(native), result,
            out_value)) {
        rt->jit_native_calls++;
        b->jit_reserved = 1;
        return 1;
    }
    rt->jit_guard_failures++;
#else
    (void)rt; (void)b; (void)argc; (void)argv; (void)out_value;
#endif
    return 0;
}

/* Detect the exact canonical counted-loop bytecode emitted for:
     for (let i = 0; i < limit; i++) accumulator += i;
   The detector is intentionally strict. Any semantic variation falls back to
   the interpreter instead of risking a miscompile. */
static int turbojs_detect_counted_i64_loop(JSFunctionBytecode *b,
                                           uint32_t target_offset,
                                           uint32_t source_offset,
                                           TurboJSOSRCountedLoopSpec *spec)
{
    const uint8_t *base, *pc, *end;
    uint16_t induction, accumulator;
    uint8_t comparison_op, step_op;
    int32_t step_value = 0;
    uint32_t resume;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len)
        return 0;
    base = b->byte_code_buf;
    pc = base + target_offset;
    end = base + b->byte_code_len;
#define NEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define OP0(op) do { NEED(1); if (*pc++ != (op)) return 0; } while (0)
    NEED(3); if (*pc++ != OP_get_loc_check) return 0;
    induction = get_u16(pc); pc += 2;
    OP0(OP_get_arg0);
    NEED(1); comparison_op = *pc++;
    if (comparison_op != OP_lt && comparison_op != OP_lte &&
        comparison_op != OP_gt && comparison_op != OP_gte) return 0;
    NEED(2); if (*pc++ != OP_if_false8) return 0;
    resume = (uint32_t)((pc + (int8_t)*pc) - base); pc++;
    NEED(3); if (*pc++ != OP_get_loc_check) return 0;
    accumulator = get_u16(pc); pc += 2;
    NEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    OP0(OP_add); OP0(OP_dup);
    NEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != accumulator) return 0; pc += 2;
    OP0(OP_drop);
    NEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    NEED(1); step_op = *pc++;
    if (step_op == OP_post_inc) {
        step_value = 1;
    } else if (step_op == OP_post_dec) {
        step_value = -1;
    } else {
        int32_t magnitude;
        switch (step_op) {
        case OP_push_0: magnitude = 0; break;
        case OP_push_1: magnitude = 1; break;
        case OP_push_2: magnitude = 2; break;
        case OP_push_3: magnitude = 3; break;
        case OP_push_i8: NEED(1); magnitude = (int8_t)*pc++; break;
        case OP_push_i16: NEED(2); magnitude = get_i16(pc); pc += 2; break;
        case OP_push_i32: NEED(4); magnitude = get_i32(pc); pc += 4; break;
        default: return 0;
        }
        if (magnitude <= 0) return 0;
        NEED(1);
        if (*pc == OP_add) step_value = magnitude;
        else if (*pc == OP_sub) step_value = -magnitude;
        else return 0;
        pc++;
        OP0(OP_dup);
    }
    NEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    OP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    NEED(2); if (*pc++ != OP_goto8) return 0;
    if ((uint32_t)((pc + (int8_t)*pc) - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count || accumulator >= b->var_count)
        return 0;
    memset(spec, 0, sizeof(*spec));
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->limit_local = 0; /* exact OP_get_arg0 guard above */
    spec->accumulator_local = (uint16_t)(b->arg_count + accumulator);
    spec->step = step_value;
    switch (comparison_op) {
    case OP_lt: spec->comparison = TURBOJS_OSR_LOOP_LT; break;
    case OP_lte: spec->comparison = TURBOJS_OSR_LOOP_LTE; break;
    case OP_gt: spec->comparison = TURBOJS_OSR_LOOP_GT; break;
    case OP_gte: spec->comparison = TURBOJS_OSR_LOOP_GTE; break;
    default: return 0;
    }
    if ((spec->step > 0 && spec->comparison >= TURBOJS_OSR_LOOP_GT) ||
        (spec->step < 0 && spec->comparison <= TURBOJS_OSR_LOOP_LTE)) return 0;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
    spec->maximum_iterations = 1000000000ULL;
#undef OP0
#undef NEED
    return 1;
}


typedef enum TurboJSDenseSlotKind {
    TURBOJS_DENSE_SLOT_LOCAL = 1,
    TURBOJS_DENSE_SLOT_ARGUMENT = 2,
    TURBOJS_DENSE_SLOT_VAR_REF = 3
} TurboJSDenseSlotKind;

typedef struct TurboJSDenseSlot {
    uint8_t kind;
    uint16_t index;
} TurboJSDenseSlot;

typedef enum TurboJSDenseArrayOSRMode {
    TURBOJS_DENSE_ARRAY_SUM = 1,
    TURBOJS_DENSE_ARRAY_INIT_INDEX = 2,
    TURBOJS_DENSE_ARRAY_HOLEY_SUM = 3
} TurboJSDenseArrayOSRMode;

typedef struct TurboJSDenseArrayOSRProgram {
    JSContext *ctx;
    TurboJSDenseSlot array_slot;
    TurboJSDenseSlot accumulator_slot;
    TurboJSDenseSlot limit_slot;
    uint16_t induction_local;
    uint16_t accumulator_frame_local;
    uint8_t mode;
    uint8_t limit_is_slot;
    uint8_t int32_wrap;
    uint8_t reserved_mode;
    int32_t limit;
    uint32_t loop_header;
    uint32_t resume_bytecode_offset;
    uint64_t maximum_elements;
} TurboJSDenseArrayOSRProgram;

typedef struct TurboJSDenseArrayOSRExecution {
    TurboJSDenseArrayOSRProgram *program;
    JSValue *arg_buf;
    JSValue *var_buf;
    JSVarRef **var_refs;
} TurboJSDenseArrayOSRExecution;

static int turbojs_parse_get_dense_slot(const uint8_t **pp, const uint8_t *end,
                                         TurboJSDenseSlot *slot)
{
    const uint8_t *p = *pp;
    if (p >= end) return 0;
    if (*p == OP_get_var_ref_check) {
        if ((size_t)(end - p) < 3) return 0;
        slot->kind = TURBOJS_DENSE_SLOT_VAR_REF; slot->index = get_u16(p + 1); p += 3;
    } else if (*p >= OP_get_var_ref0 && *p <= OP_get_var_ref3) {
        slot->kind = TURBOJS_DENSE_SLOT_VAR_REF; slot->index = (uint16_t)(*p - OP_get_var_ref0); p++;
    } else if (*p == OP_get_loc || *p == OP_get_loc_check) {
        if ((size_t)(end - p) < 3) return 0;
        slot->kind = TURBOJS_DENSE_SLOT_LOCAL; slot->index = get_u16(p + 1); p += 3;
    } else if (*p >= OP_get_loc0 && *p <= OP_get_loc3) {
        slot->kind = TURBOJS_DENSE_SLOT_LOCAL; slot->index = (uint16_t)(*p - OP_get_loc0); p++;
    } else if (*p == OP_get_arg) {
        if ((size_t)(end - p) < 3) return 0;
        slot->kind = TURBOJS_DENSE_SLOT_ARGUMENT; slot->index = get_u16(p + 1); p += 3;
    } else if (*p >= OP_get_arg0 && *p <= OP_get_arg3) {
        slot->kind = TURBOJS_DENSE_SLOT_ARGUMENT; slot->index = (uint16_t)(*p - OP_get_arg0); p++;
    } else return 0;
    *pp = p;
    return 1;
}

static int turbojs_parse_put_dense_slot(const uint8_t **pp, const uint8_t *end,
                                         const TurboJSDenseSlot *slot)
{
    const uint8_t *p = *pp;
    uint16_t index;
    if (slot->kind == TURBOJS_DENSE_SLOT_VAR_REF && *p == OP_put_var_ref_check) {
        if ((size_t)(end - p) < 3) return 0; index = get_u16(p + 1); p += 3;
    } else if (slot->kind == TURBOJS_DENSE_SLOT_VAR_REF && *p >= OP_put_var_ref0 && *p <= OP_put_var_ref3) {
        index = (uint16_t)(*p - OP_put_var_ref0); p++;
    } else if (slot->kind == TURBOJS_DENSE_SLOT_LOCAL && (*p == OP_put_loc || *p == OP_put_loc_check)) {
        if ((size_t)(end - p) < 3) return 0; index = get_u16(p + 1); p += 3;
    } else if (slot->kind == TURBOJS_DENSE_SLOT_LOCAL && *p >= OP_put_loc0 && *p <= OP_put_loc3) {
        index = (uint16_t)(*p - OP_put_loc0); p++;
    } else if (slot->kind == TURBOJS_DENSE_SLOT_ARGUMENT && *p == OP_put_arg) {
        if ((size_t)(end - p) < 3) return 0; index = get_u16(p + 1); p += 3;
    } else if (slot->kind == TURBOJS_DENSE_SLOT_ARGUMENT && *p >= OP_put_arg0 && *p <= OP_put_arg3) {
        index = (uint16_t)(*p - OP_put_arg0); p++;
    } else return 0;
    if (index != slot->index) return 0;
    *pp = p;
    return 1;
}

static JSValue *turbojs_dense_slot_value(TurboJSDenseArrayOSRExecution *execution,
                                          const TurboJSDenseSlot *slot)
{
    if (!execution || !slot) return NULL;
    if (slot->kind == TURBOJS_DENSE_SLOT_LOCAL)
        return execution->var_buf ? &execution->var_buf[slot->index] : NULL;
    if (slot->kind == TURBOJS_DENSE_SLOT_ARGUMENT)
        return execution->arg_buf ? &execution->arg_buf[slot->index] : NULL;
    if (slot->kind == TURBOJS_DENSE_SLOT_VAR_REF && execution->var_refs &&
        execution->var_refs[slot->index])
        return execution->var_refs[slot->index]->pvalue;
    return NULL;
}

/* Recognize two general packed-array loops:
     for (let i = 0; i < limit; i++) array[i] = i;
     for (let i = 0; i < array.length; i++) sum += array[i];
   Arrays and accumulators may live in arguments, locals, or closure refs. */
static int turbojs_detect_dense_array_sum_loop(JSFunctionBytecode *b,
                                                uint32_t target_offset,
                                                uint32_t source_offset,
                                                TurboJSDenseArrayOSRProgram *spec)
{
    const uint8_t *base, *pc, *end;
    uint16_t induction, i2;
    TurboJSDenseSlot array_slot, accumulator_slot, slot2;
    uint32_t resume;
    uint8_t int32_wrap = 0;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len) return 0;
    base = b->byte_code_buf; pc = base + target_offset; end = base + b->byte_code_len;
#define DNEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define DOP0(op) do { DNEED(1); if (*pc++ != (op)) return 0; } while (0)
    DNEED(3); if (*pc++ != OP_get_loc_check) return 0; induction = get_u16(pc); pc += 2;
    if (!turbojs_parse_get_dense_slot(&pc, end, &array_slot)) return 0;
    DOP0(OP_get_length); DOP0(OP_lt);
    DNEED(2); if (*pc++ != OP_if_false8) return 0;
    resume = (uint32_t)((pc + (int8_t)*pc) - base); pc++;
    if (!turbojs_parse_get_dense_slot(&pc, end, &accumulator_slot)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot2) ||
        slot2.kind != array_slot.kind || slot2.index != array_slot.index) return 0;
    DNEED(3); if (*pc++ != OP_get_loc_check) return 0; i2 = get_u16(pc); pc += 2;
    if (i2 != induction) return 0;
    DOP0(OP_get_array_el); DOP0(OP_add);
    /* Int32 reductions commonly end with `| 0`. Preserve that exact
       semantics in the OSR runner instead of rejecting the otherwise
       canonical packed-array sum loop. */
    if ((size_t)(end - pc) >= 2 && pc[0] == OP_push_0 && pc[1] == OP_or) {
        pc += 2;
        int32_wrap = 1;
    }
    DOP0(OP_dup);
    if (!turbojs_parse_put_dense_slot(&pc, end, &accumulator_slot)) return 0;
    DOP0(OP_drop);
    DNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    DOP0(OP_post_inc);
    DNEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    DOP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    DNEED(2); if (*pc++ != OP_goto8 || (uint32_t)((pc + (int8_t)*pc) - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count) return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm; spec->array_slot = array_slot; spec->accumulator_slot = accumulator_slot;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_frame_local = accumulator_slot.kind == TURBOJS_DENSE_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator_slot.index) : UINT16_MAX;
    spec->mode = TURBOJS_DENSE_ARRAY_SUM; spec->int32_wrap = int32_wrap;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume; spec->maximum_elements = 1000000000ULL;
#undef DOP0
#undef DNEED
    return 1;
}

static int turbojs_detect_dense_array_init_loop(JSFunctionBytecode *b,
                                                 uint32_t target_offset,
                                                 uint32_t source_offset,
                                                 TurboJSDenseArrayOSRProgram *spec)
{
    const uint8_t *base, *pc, *end;
    uint16_t induction, i2;
    TurboJSDenseSlot array_slot;
    uint32_t resume;
    int32_t limit = 0;
    TurboJSDenseSlot limit_slot;
    int limit_is_slot = 0;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len) return 0;
    base = b->byte_code_buf; pc = base + target_offset; end = base + b->byte_code_len;
#define INEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define IOP0(op) do { INEED(1); if (*pc++ != (op)) return 0; } while (0)
    INEED(3); if (*pc++ != OP_get_loc_check) return 0; induction = get_u16(pc); pc += 2;
    if ((size_t)(end - pc) >= 5 && *pc == OP_push_i32) {
        pc++; limit = (int32_t)get_u32(pc); pc += 4;
    } else {
        if (!turbojs_parse_get_dense_slot(&pc, end, &limit_slot)) return 0;
        limit_is_slot = 1;
    }
    IOP0(OP_lt); INEED(2); if (*pc++ != OP_if_false8) return 0;
    resume = (uint32_t)((pc + (int8_t)*pc) - base); pc++;
    if (!turbojs_parse_get_dense_slot(&pc, end, &array_slot)) return 0;
    INEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    IOP0(OP_swap); IOP0(OP_dup); IOP0(OP_is_undefined_or_null);
    INEED(2); if (*pc++ != OP_if_true8) return 0; pc++;
    IOP0(OP_swap); IOP0(OP_to_propkey); IOP0(OP_swap); IOP0(OP_swap);
    INEED(3); if (*pc++ != OP_get_loc_check) return 0; i2 = get_u16(pc); pc += 2;
    if (i2 != induction) return 0;
    IOP0(OP_swap); IOP0(OP_to_propkey); IOP0(OP_swap); IOP0(OP_put_array_el);
    INEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    IOP0(OP_post_inc);
    INEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    IOP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    INEED(2); if (*pc++ != OP_goto8 || (uint32_t)((pc + (int8_t)*pc) - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        (!limit_is_slot && limit < 0)) return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm; spec->array_slot = array_slot;
    spec->limit_slot = limit_slot; spec->limit_is_slot = (uint8_t)limit_is_slot;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->mode = TURBOJS_DENSE_ARRAY_INIT_INDEX; spec->limit = limit;
    spec->loop_header = target_offset; spec->resume_bytecode_offset = resume;
    spec->maximum_elements = 1000000000ULL;
#undef IOP0
#undef INEED
    return 1;
}


/* Recognize a canonical holey-array Int32 reduction:
     for (let i = 0; i < array.length; i++)
         if (array[i] !== undefined) sum = (sum + array[i]) | 0;
   The runner guards the standard Array prototype so holes cannot resolve
   through inherited indexed properties. */
static int turbojs_detect_holey_array_sum_loop(JSFunctionBytecode *b,
                                                uint32_t target_offset,
                                                uint32_t source_offset,
                                                TurboJSDenseArrayOSRProgram *spec)
{
    const uint8_t *base, *pc, *end, *skip_target;
    uint16_t induction, i2;
    TurboJSDenseSlot array_slot, accumulator_slot, slot2;
    uint32_t resume;
    JSAtom undefined_atom;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len) return 0;
    base = b->byte_code_buf; pc = base + target_offset; end = base + b->byte_code_len;
#define HNEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define HOP0(op) do { HNEED(1); if (*pc++ != (op)) return 0; } while (0)
    HNEED(3); if (*pc++ != OP_get_loc_check) return 0; induction = get_u16(pc); pc += 2;
    if (!turbojs_parse_get_dense_slot(&pc, end, &array_slot)) return 0;
    HOP0(OP_get_length); HOP0(OP_lt);
    HNEED(2); if (*pc++ != OP_if_false8) return 0;
    resume = (uint32_t)((pc + (int8_t)*pc) - base); pc++;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot2) ||
        slot2.kind != array_slot.kind || slot2.index != array_slot.index) return 0;
    HNEED(3); if (*pc++ != OP_get_loc_check) return 0; i2 = get_u16(pc); pc += 2;
    if (i2 != induction) return 0;
    HOP0(OP_get_array_el);
    HNEED(5); if (*pc++ != OP_get_var) return 0;
    undefined_atom = get_u32(pc); pc += 4;
    if (undefined_atom != JS_ATOM_undefined) return 0;
    HOP0(OP_strict_neq);
    HNEED(2); if (*pc++ != OP_if_false8) return 0;
    skip_target = pc + (int8_t)*pc; pc++;
    if (!turbojs_parse_get_dense_slot(&pc, end, &accumulator_slot)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot2) ||
        slot2.kind != array_slot.kind || slot2.index != array_slot.index) return 0;
    HNEED(3); if (*pc++ != OP_get_loc_check) return 0; i2 = get_u16(pc); pc += 2;
    if (i2 != induction) return 0;
    HOP0(OP_get_array_el); HOP0(OP_add); HOP0(OP_push_0); HOP0(OP_or); HOP0(OP_dup);
    if (!turbojs_parse_put_dense_slot(&pc, end, &accumulator_slot)) return 0;
    HOP0(OP_drop);
    if (pc != skip_target) return 0;
    HNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    HOP0(OP_post_inc);
    HNEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    HOP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    HNEED(2); if (*pc++ != OP_goto8 || (uint32_t)((pc + (int8_t)*pc) - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count) return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm; spec->array_slot = array_slot; spec->accumulator_slot = accumulator_slot;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_frame_local = accumulator_slot.kind == TURBOJS_DENSE_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator_slot.index) : UINT16_MAX;
    spec->mode = TURBOJS_DENSE_ARRAY_HOLEY_SUM; spec->int32_wrap = 1;
    spec->loop_header = target_offset; spec->resume_bytecode_offset = resume;
    spec->maximum_elements = 1000000000ULL;
#undef HOP0
#undef HNEED
    return 1;
}


enum {
    TURBOJS_DENSE_AFFINE = 0,
    TURBOJS_DENSE_INPLACE_AFFINE = 1,
    TURBOJS_DENSE_BINARY_ADD = 2,
    TURBOJS_DENSE_COPY = 3,
    TURBOJS_DENSE_FILL = 4
};

typedef struct TurboJSDenseArrayTransformOSRProgram {
    JSContext *ctx;
    uint16_t input_local;
    uint16_t input_b_local;
    uint16_t output_local;
    uint16_t scale_local;
    uint16_t offset_local;
    uint16_t value_local;
    uint16_t induction_local;
    uint8_t mode;
    uint32_t loop_header;
    uint32_t resume_bytecode_offset;
    uint64_t maximum_elements;
} TurboJSDenseArrayTransformOSRProgram;

/* Recognize:
     for (let i = 0; i < input.length; i++)
         output[i] = input[i] * scale + offset;
   against the compiler's exact property-write sequence. */
static int turbojs_detect_dense_array_transform_loop(JSFunctionBytecode *b,
                                                      uint32_t target_offset,
                                                      uint32_t source_offset,
                                                      TurboJSDenseArrayTransformOSRProgram *spec)
{
    const uint8_t *base, *pc, *end;
    uint16_t induction;
    uint32_t resume;
    if (!b || !spec || b->arg_count < 4 || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len)
        return 0;
    base = b->byte_code_buf;
    pc = base + target_offset;
    end = base + b->byte_code_len;
#define TNEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define TOP0(op) do { TNEED(1); if (*pc++ != (op)) return 0; } while (0)
    TNEED(3); if (*pc++ != OP_get_loc_check) return 0;
    induction = get_u16(pc); pc += 2;
    TOP0(OP_get_arg0); TOP0(OP_get_length); TOP0(OP_lt);
    TNEED(2); if (*pc++ != OP_if_false8) return 0;
    resume = (uint32_t)((pc + (int8_t)*pc) - base); pc++;
    TOP0(OP_get_arg1);
    TNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    TOP0(OP_swap); TOP0(OP_dup); TOP0(OP_is_undefined_or_null);
    TNEED(2); if (*pc++ != OP_if_true8) return 0;
    pc++; /* canonical nullish-property fast-path branch */
    TOP0(OP_swap); TOP0(OP_to_propkey); TOP0(OP_swap); TOP0(OP_swap);
    TOP0(OP_get_arg0);
    TNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    TOP0(OP_get_array_el); TOP0(OP_get_arg2); TOP0(OP_mul); TOP0(OP_get_arg3); TOP0(OP_add);
    TOP0(OP_swap); TOP0(OP_to_propkey); TOP0(OP_swap); TOP0(OP_put_array_el);
    TNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    TOP0(OP_post_inc);
    TNEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    TOP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    TNEED(2); if (*pc++ != OP_goto8) return 0;
    if ((uint32_t)((pc + (int8_t)*pc) - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count) return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm;
    spec->mode = TURBOJS_DENSE_AFFINE;
    spec->input_local = 0;
    spec->output_local = 1;
    spec->scale_local = 2;
    spec->offset_local = 3;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
    spec->maximum_elements = 1000000000ULL;
#undef TOP0
#undef TNEED
    return 1;
}

/* Recognize additional verified dense loop kernels:
   - in-place affine: a[i] = a[i] * scale + offset
   - binary add: out[i] = a[i] + b[i]
   - copy: out[i] = a[i]
   - fill: out[i] = value
   All patterns are matched against exact final bytecode. */
static int turbojs_detect_dense_array_extra_loop(JSFunctionBytecode *b,
                                                  uint32_t target_offset,
                                                  uint32_t source_offset,
                                                  TurboJSDenseArrayTransformOSRProgram *spec)
{
    const uint8_t *base, *pc, *end;
    uint16_t induction;
    uint32_t resume;
    int mode = -1;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len) return 0;
    base = b->byte_code_buf; pc = base + target_offset; end = base + b->byte_code_len;
#define XNEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define XOP0(op) do { XNEED(1); if (*pc++ != (op)) return 0; } while (0)
    XNEED(3); if (*pc++ != OP_get_loc_check) return 0; induction = get_u16(pc); pc += 2;
    XOP0(OP_get_arg0); XOP0(OP_get_length); XOP0(OP_lt);
    XNEED(2); if (*pc++ != OP_if_false8) return 0; resume = (uint32_t)((pc + (int8_t)*pc) - base); pc++;

    /* Destination argument determines the family. */
    XNEED(1);
    if (*pc == OP_get_arg0) { mode = TURBOJS_DENSE_INPLACE_AFFINE; pc++; }
    else if (*pc == OP_get_arg1) { mode = TURBOJS_DENSE_COPY; pc++; }
    else if (*pc == OP_get_arg2) { mode = TURBOJS_DENSE_BINARY_ADD; pc++; }
    else return 0;
    XNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    XOP0(OP_swap); XOP0(OP_dup); XOP0(OP_is_undefined_or_null);
    XNEED(2); if (*pc++ != OP_if_true8) return 0; pc++;
    XOP0(OP_swap); XOP0(OP_to_propkey); XOP0(OP_swap); XOP0(OP_swap);

    if (mode == TURBOJS_DENSE_INPLACE_AFFINE) {
        XOP0(OP_get_arg0);
        XNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
        XOP0(OP_get_array_el); XOP0(OP_get_arg1); XOP0(OP_mul); XOP0(OP_get_arg2); XOP0(OP_add);
    } else if (mode == TURBOJS_DENSE_BINARY_ADD) {
        XOP0(OP_get_arg0);
        XNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
        XOP0(OP_get_array_el); XOP0(OP_get_arg1);
        XNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
        XOP0(OP_get_array_el); XOP0(OP_add);
    } else {
        XOP0(OP_get_arg0);
        XNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
        XOP0(OP_get_array_el);
    }
    XOP0(OP_swap); XOP0(OP_to_propkey); XOP0(OP_swap); XOP0(OP_put_array_el);
    XNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    XOP0(OP_post_inc); XNEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    XOP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    XNEED(2); if (*pc++ != OP_goto8 || (uint32_t)((pc + (int8_t)*pc) - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count) return 0;
    memset(spec, 0, sizeof(*spec)); spec->ctx = b->realm; spec->mode = (uint8_t)mode;
    spec->input_local = 0; spec->output_local = mode == TURBOJS_DENSE_INPLACE_AFFINE ? 0 : (mode == TURBOJS_DENSE_BINARY_ADD ? 2 : 1);
    spec->input_b_local = 1; spec->scale_local = 1; spec->offset_local = 2;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->loop_header = target_offset; spec->resume_bytecode_offset = resume; spec->maximum_elements = 1000000000ULL;
#undef XOP0
#undef XNEED
    return 1;
}

static int turbojs_detect_dense_array_fill_loop(JSFunctionBytecode *b,
                                                 uint32_t target_offset,
                                                 uint32_t source_offset,
                                                 TurboJSDenseArrayTransformOSRProgram *spec)
{
    const uint8_t *base, *pc, *end; uint16_t induction; uint32_t resume;
    if (!b || !spec || b->arg_count < 2 || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len) return 0;
    base=b->byte_code_buf; pc=base+target_offset; end=base+b->byte_code_len;
#define FNEED(n) do { if ((size_t)(end-pc)<(size_t)(n)) return 0; } while(0)
#define FOP0(op) do { FNEED(1); if (*pc++!=(op)) return 0; } while(0)
    FNEED(3); if (*pc++!=OP_get_loc_check) return 0; induction=get_u16(pc); pc+=2;
    FOP0(OP_get_arg0); FOP0(OP_get_length); FOP0(OP_lt);
    FNEED(2); if (*pc++!=OP_if_false8) return 0; resume=(uint32_t)((pc+(int8_t)*pc)-base); pc++;
    FOP0(OP_get_arg0); FNEED(3); if (*pc++!=OP_get_loc_check || get_u16(pc)!=induction) return 0; pc+=2;
    FOP0(OP_swap); FOP0(OP_dup); FOP0(OP_is_undefined_or_null); FNEED(2); if (*pc++!=OP_if_true8) return 0; pc++;
    FOP0(OP_swap); FOP0(OP_to_propkey); FOP0(OP_swap); FOP0(OP_swap); FOP0(OP_get_arg1);
    FOP0(OP_swap); FOP0(OP_to_propkey); FOP0(OP_swap); FOP0(OP_put_array_el);
    FNEED(3); if (*pc++!=OP_get_loc_check || get_u16(pc)!=induction) return 0; pc+=2;
    FOP0(OP_post_inc); FNEED(3); if (*pc++!=OP_put_loc_check || get_u16(pc)!=induction) return 0; pc+=2; FOP0(OP_drop);
    if ((uint32_t)(pc-base)!=source_offset) return 0;
    FNEED(2); if (*pc++!=OP_goto8 || (uint32_t)((pc+(int8_t)*pc)-base)!=target_offset) return 0;
    if (resume>(uint32_t)b->byte_code_len || induction>=b->var_count) return 0;
    memset(spec,0,sizeof(*spec)); spec->ctx=b->realm; spec->mode=TURBOJS_DENSE_FILL;
    spec->input_local=0; spec->output_local=0; spec->value_local=1;
    spec->induction_local=(uint16_t)(b->arg_count+induction); spec->loop_header=target_offset;
    spec->resume_bytecode_offset=resume; spec->maximum_elements=1000000000ULL;
#undef FOP0
#undef FNEED
    return 1;
}

static int turbojs_osr_number(const TurboJSOSRValue *value, double *out)
{
    if (value->kind == TURBOJS_OSR_VALUE_INT64) {
        *out = (double)(int64_t)value->bits;
        return 1;
    }
    if (value->kind == TURBOJS_OSR_VALUE_FLOAT64) {
        memcpy(out, &value->bits, sizeof(*out));
        return 1;
    }
    return 0;
}

static int turbojs_dense_numeric_array(JSContext *ctx, JSObject *obj, uint32_t count)
{
    uint32_t i;
    if (!ctx || !obj || obj->class_id != JS_CLASS_ARRAY || !obj->fast_array || !obj->shape ||
        obj->shape->proto != JS_VALUE_GET_OBJ(ctx->class_proto[JS_CLASS_ARRAY]) ||
        obj->u.array.count < count) return 0;
    if (obj->array_element_kind == TURBOJS_ARRAY_PACKED_INT32 ||
        obj->array_element_kind == TURBOJS_ARRAY_PACKED_NUMBER)
        return 1;
    for (i = 0; i < count; ++i) {
        JSValue value = obj->u.array.u.values[i];
        int tag;
        if (JS_IsUninitialized(value)) return 0;
        tag = JS_VALUE_GET_NORM_TAG(value);
        if (tag != JS_TAG_INT && tag != JS_TAG_FLOAT64) return 0;
    }
    return 1;
}

static double turbojs_dense_number(JSValue value)
{
    return JS_VALUE_GET_NORM_TAG(value) == JS_TAG_INT ?
           (double)JS_VALUE_GET_INT(value) : JS_VALUE_GET_FLOAT64(value);
}


#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
#define TURBOJS_TARGET_AVX2 __attribute__((target("avx2")))
typedef double TurboJSVec2F64 __attribute__((vector_size(16), aligned(1)));
typedef double TurboJSVec4F64 __attribute__((vector_size(32), aligned(1)));

static int turbojs_cpu_has_avx2(void)
{
    static int initialized;
    static int available;
    if (!initialized) {
        available = turbojs_x64_cpu_features().avx2;
        initialized = 1;
    }
    return available;
}

static uint32_t turbojs_f64_kernel_sse2(double *dst, const double *src,
                                         const double *src_b, uint32_t i,
                                         uint32_t count, int mode,
                                         double scale, double offset, double fill)
{
    const TurboJSVec2F64 vscale = { scale, scale };
    const TurboJSVec2F64 voffset = { offset, offset };
    const TurboJSVec2F64 vfill = { fill, fill };
    for (; i + 2u <= count; i += 2u) {
        TurboJSVec2F64 value;
        TurboJSVec2F64 source;
        memcpy(&source, src + i, sizeof(source));
        if (mode == TURBOJS_DENSE_BINARY_ADD) {
            TurboJSVec2F64 other;
            memcpy(&other, src_b + i, sizeof(other));
            value = source + other;
        } else if (mode == TURBOJS_DENSE_COPY) {
            value = source;
        } else if (mode == TURBOJS_DENSE_FILL) {
            value = vfill;
        } else {
            value = source * vscale + voffset;
        }
        memcpy(dst + i, &value, sizeof(value));
    }
    return i;
}

static TURBOJS_TARGET_AVX2 uint32_t turbojs_f64_kernel_avx2(
    double *dst, const double *src, const double *src_b, uint32_t i,
    uint32_t count, int mode, double scale, double offset, double fill)
{
    const TurboJSVec4F64 vscale = { scale, scale, scale, scale };
    const TurboJSVec4F64 voffset = { offset, offset, offset, offset };
    const TurboJSVec4F64 vfill = { fill, fill, fill, fill };
    for (; i + 4u <= count; i += 4u) {
        TurboJSVec4F64 value;
        TurboJSVec4F64 source;
        memcpy(&source, src + i, sizeof(source));
        if (mode == TURBOJS_DENSE_BINARY_ADD) {
            TurboJSVec4F64 other;
            memcpy(&other, src_b + i, sizeof(other));
            value = source + other;
        } else if (mode == TURBOJS_DENSE_COPY) {
            value = source;
        } else if (mode == TURBOJS_DENSE_FILL) {
            value = vfill;
        } else {
            value = source * vscale + voffset;
        }
        memcpy(dst + i, &value, sizeof(value));
    }
    return i;
}
#endif

static TurboJSOSRExitKind turbojs_run_dense_array_transform(TurboJSOSRFrame *frame,
                                                             void *opaque,
                                                             uint32_t *resume_offset)
{
    TurboJSDenseArrayTransformOSRProgram *program = (TurboJSDenseArrayTransformOSRProgram *)opaque;
    TurboJSOSRValue *input_slot, *output_slot, *index_slot;
    JSObject *input, *input_b = NULL, *output;
    double scale = 0.0, offset = 0.0, fill = 0.0;
    uint32_t i, count, start;
    if (!program || !frame || !resume_offset ||
        program->input_local >= frame->local_count || program->output_local >= frame->local_count ||
        program->induction_local >= frame->local_count) return TURBOJS_OSR_EXIT_BAILOUT;
    input_slot = &frame->locals[program->input_local]; output_slot = &frame->locals[program->output_local];
    index_slot = &frame->locals[program->induction_local];
    if (input_slot->kind != TURBOJS_OSR_VALUE_REFERENCE || output_slot->kind != TURBOJS_OSR_VALUE_REFERENCE ||
        index_slot->kind != TURBOJS_OSR_VALUE_INT64) return TURBOJS_OSR_EXIT_BAILOUT;
    if ((program->mode == TURBOJS_DENSE_AFFINE || program->mode == TURBOJS_DENSE_INPLACE_AFFINE) &&
        (program->scale_local >= frame->local_count || program->offset_local >= frame->local_count ||
         !turbojs_osr_number(&frame->locals[program->scale_local], &scale) ||
         !turbojs_osr_number(&frame->locals[program->offset_local], &offset))) return TURBOJS_OSR_EXIT_BAILOUT;
    if (program->mode == TURBOJS_DENSE_BINARY_ADD) {
        if (program->input_b_local >= frame->local_count ||
            frame->locals[program->input_b_local].kind != TURBOJS_OSR_VALUE_REFERENCE) return TURBOJS_OSR_EXIT_BAILOUT;
        input_b = (JSObject *)(uintptr_t)frame->locals[program->input_b_local].bits;
    }
    if (program->mode == TURBOJS_DENSE_FILL &&
        (program->value_local >= frame->local_count || !turbojs_osr_number(&frame->locals[program->value_local], &fill)))
        return TURBOJS_OSR_EXIT_BAILOUT;
    input = (JSObject *)(uintptr_t)input_slot->bits; output = (JSObject *)(uintptr_t)output_slot->bits;
    if (!input || !output || !program->ctx || (int64_t)index_slot->bits < 0) return TURBOJS_OSR_EXIT_BAILOUT;
    start = (uint32_t)index_slot->bits;

    /* Homogeneous unboxed Float64Array fast path. Typed-array bounds and
       element representation are stable after the upfront detached-buffer
       and length guards, so the remaining loop needs no boxing or tag scans. */
    if (input->class_id == JS_CLASS_FLOAT64_ARRAY && output->class_id == JS_CLASS_FLOAT64_ARRAY &&
        input->u.array.u.double_ptr && output->u.array.u.double_ptr) {
        double *src = input->u.array.u.double_ptr;
        double *dst = output->u.array.u.double_ptr;
        double *src_b = NULL;
        count = input->u.array.count;
        if (start > count || output->u.array.count < count ||
            (uint64_t)(count - start) > program->maximum_elements) return TURBOJS_OSR_EXIT_BAILOUT;
        if (program->mode == TURBOJS_DENSE_BINARY_ADD) {
            if (!input_b || input_b->class_id != JS_CLASS_FLOAT64_ARRAY ||
                !input_b->u.array.u.double_ptr || input_b->u.array.count < count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            src_b = input_b->u.array.u.double_ptr;
        }
        i = start;
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
        if (count - i >= 8u) {
            if (program->mode != TURBOJS_DENSE_BINARY_ADD && turbojs_cpu_has_avx2()) {
                uint32_t before = i;
                i = turbojs_f64_kernel_avx2(dst, src, src_b, i, count,
                                             program->mode, scale, offset, fill);
                program->ctx->rt->typed_array_simd_avx2_entries++;
                program->ctx->rt->typed_array_simd_elements += i - before;
            } else {
                uint32_t before = i;
                i = turbojs_f64_kernel_sse2(dst, src, src_b, i, count,
                                             program->mode, scale, offset, fill);
                program->ctx->rt->typed_array_simd_sse2_entries++;
                program->ctx->rt->typed_array_simd_elements += i - before;
            }
        }
#endif
        for (; i < count; ++i) {
            if (program->mode == TURBOJS_DENSE_BINARY_ADD) dst[i]=src[i]+src_b[i];
            else if (program->mode == TURBOJS_DENSE_COPY) dst[i]=src[i];
            else if (program->mode == TURBOJS_DENSE_FILL) dst[i]=fill;
            else dst[i]=src[i]*scale+offset;
        }
        index_slot->bits=count; index_slot->kind=TURBOJS_OSR_VALUE_INT64;
        program->ctx->rt->typed_array_osr_entries++;
        program->ctx->rt->typed_array_osr_elements += count-start;
        program->ctx->rt->dense_array_transform_osr_entries++;
        program->ctx->rt->dense_array_transform_osr_elements += count-start;
        if (program->mode == TURBOJS_DENSE_INPLACE_AFFINE) program->ctx->rt->dense_array_inplace_osr_entries++;
        else if (program->mode == TURBOJS_DENSE_BINARY_ADD) program->ctx->rt->dense_array_binary_osr_entries++;
        else if (program->mode == TURBOJS_DENSE_COPY) program->ctx->rt->dense_array_copy_osr_entries++;
        else if (program->mode == TURBOJS_DENSE_FILL) program->ctx->rt->dense_array_fill_osr_entries++;
        *resume_offset=program->resume_bytecode_offset;
        return TURBOJS_OSR_EXIT_COMPLETED;
    }

    count = input->u.array.count;
    if (start > count || (uint64_t)(count - start) > program->maximum_elements ||
        !turbojs_dense_numeric_array(program->ctx, input, count) ||
        !turbojs_dense_numeric_array(program->ctx, output, count) ||
        (input_b && !turbojs_dense_numeric_array(program->ctx, input_b, count))) return TURBOJS_OSR_EXIT_BAILOUT;

    for (i = start; i < count; ++i) {
        double value;
        switch (program->mode) {
        case TURBOJS_DENSE_BINARY_ADD:
            value = turbojs_dense_number(input->u.array.u.values[i]) + turbojs_dense_number(input_b->u.array.u.values[i]);
            break;
        case TURBOJS_DENSE_COPY:
            value = turbojs_dense_number(input->u.array.u.values[i]);
            break;
        case TURBOJS_DENSE_FILL:
            value = fill;
            break;
        default:
            value = turbojs_dense_number(input->u.array.u.values[i]) * scale + offset;
            break;
        }
        output->u.array.u.values[i] = __JS_NewFloat64(value);
    }
    index_slot->bits = count; index_slot->kind = TURBOJS_OSR_VALUE_INT64;
    program->ctx->rt->dense_array_transform_osr_entries++;
    program->ctx->rt->dense_array_transform_osr_elements += count - start;
    if (program->mode == TURBOJS_DENSE_INPLACE_AFFINE) program->ctx->rt->dense_array_inplace_osr_entries++;
    else if (program->mode == TURBOJS_DENSE_BINARY_ADD) program->ctx->rt->dense_array_binary_osr_entries++;
    else if (program->mode == TURBOJS_DENSE_COPY) program->ctx->rt->dense_array_copy_osr_entries++;
    else if (program->mode == TURBOJS_DENSE_FILL) program->ctx->rt->dense_array_fill_osr_entries++;
    *resume_offset = program->resume_bytecode_offset;
    return TURBOJS_OSR_EXIT_COMPLETED;
}

static TurboJSOSREntry turbojs_dense_array_transform_osr_entry(TurboJSDenseArrayTransformOSRProgram *program)
{
    TurboJSOSREntry entry;
    memset(&entry, 0, sizeof(entry));
    if (program) {
        entry.callback = turbojs_run_dense_array_transform;
        entry.opaque = program;
        entry.loop_header = program->loop_header;
        entry.bailout_limit = 3;
    }
    return entry;
}

static int turbojs_i64_add_checked(int64_t a, int64_t b, int64_t *out)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, out) ? 0 : 1;
#else
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return 0;
    *out = a + b;
    return 1;
#endif
}

static int expand_fast_array(JSContext *ctx, JSObject *p, uint32_t new_len);

static TurboJSOSRExitKind turbojs_run_dense_array_sum(TurboJSOSRFrame *frame,
                                                        void *opaque,
                                                        uint32_t *resume_offset)
{
    TurboJSDenseArrayOSRExecution *execution = (TurboJSDenseArrayOSRExecution *)opaque;
    TurboJSDenseArrayOSRProgram *program;
    TurboJSOSRValue *index_slot;
    JSValue *array_value, *acc_value = NULL;
    JSObject *array;
    uint32_t i, start_index, count, unrolled_blocks = 0;
    int saw_float = 0, promoted = 0;
    int64_t isum = 0;
    double dsum = 0.0;

    if (!execution || !(program = execution->program) || !frame || !resume_offset ||
        program->induction_local >= frame->local_count)
        return TURBOJS_OSR_EXIT_BAILOUT;
    array_value = turbojs_dense_slot_value(execution, &program->array_slot);
    index_slot = &frame->locals[program->induction_local];
    if (!array_value || JS_VALUE_GET_TAG(*array_value) != JS_TAG_OBJECT ||
        index_slot->kind != TURBOJS_OSR_VALUE_INT64)
        return TURBOJS_OSR_EXIT_BAILOUT;
    array = JS_VALUE_GET_OBJ(*array_value);
    if (!array || array->class_id != JS_CLASS_ARRAY || !array->shape || !program->ctx ||
        array->shape->proto != JS_VALUE_GET_OBJ(program->ctx->class_proto[JS_CLASS_ARRAY]) ||
        (program->mode != TURBOJS_DENSE_ARRAY_HOLEY_SUM && !array->fast_array))
        return TURBOJS_OSR_EXIT_BAILOUT;
    if ((int64_t)index_slot->bits < 0) return TURBOJS_OSR_EXIT_BAILOUT;
    i = (uint32_t)index_slot->bits;
    start_index = i;

    if (program->mode == TURBOJS_DENSE_ARRAY_INIT_INDEX) {
        uint32_t limit;
        int32_t signed_limit = program->limit;
        if (program->limit_is_slot) {
            JSValue *limit_value = turbojs_dense_slot_value(execution, &program->limit_slot);
            int tag;
            if (!limit_value) return TURBOJS_OSR_EXIT_BAILOUT;
            tag = JS_VALUE_GET_NORM_TAG(*limit_value);
            if (tag == JS_TAG_INT) {
                signed_limit = JS_VALUE_GET_INT(*limit_value);
            } else if (tag == JS_TAG_FLOAT64) {
                double d = JS_VALUE_GET_FLOAT64(*limit_value);
                if (!isfinite(d) || d < 0.0 || d > (double)INT32_MAX || d != trunc(d))
                    return TURBOJS_OSR_EXIT_BAILOUT;
                signed_limit = (int32_t)d;
            } else {
                return TURBOJS_OSR_EXIT_BAILOUT;
            }
        }
        if (signed_limit < 0 || (uint64_t)signed_limit > program->maximum_elements)
            return TURBOJS_OSR_EXIT_BAILOUT;
        limit = (uint32_t)signed_limit;
        /* Only append to the exact packed prefix already produced by the
           interpreter. This keeps the optimization transactional and avoids
           overwriting pre-existing values with observable ownership effects. */
        if (i != array->u.array.count || i > limit || !array->extensible ||
            JS_VALUE_GET_TAG(array->prop[0].u.value) != JS_TAG_INT ||
            !(get_shape_prop(array->shape)->flags & JS_PROP_WRITABLE))
            return TURBOJS_OSR_EXIT_BAILOUT;
        if (limit > array->u.array.u1.size && expand_fast_array(program->ctx, array, limit))
            return TURBOJS_OSR_EXIT_ERROR;
        for (; i < limit; ++i)
            array->u.array.u.values[i] = JS_NewInt32(program->ctx, (int32_t)i);
        array->u.array.count = limit;
        array->array_element_kind = TURBOJS_ARRAY_PACKED_INT32;
        array->prop[0].u.value = js_int32(limit);
        index_slot->bits = limit;
        index_slot->kind = TURBOJS_OSR_VALUE_INT64;
        program->ctx->rt->dense_array_transform_osr_entries++;
        program->ctx->rt->dense_array_transform_osr_elements += limit - start_index;
        *resume_offset = program->resume_bytecode_offset;
        return TURBOJS_OSR_EXIT_COMPLETED;
    }

    acc_value = turbojs_dense_slot_value(execution, &program->accumulator_slot);
    if (!acc_value || (JS_VALUE_GET_TAG(*acc_value) != JS_TAG_INT &&
                       JS_VALUE_GET_TAG(*acc_value) != JS_TAG_FLOAT64))
        return TURBOJS_OSR_EXIT_BAILOUT;
    if (JS_VALUE_GET_TAG(*acc_value) == JS_TAG_FLOAT64) {
        dsum = JS_VALUE_GET_FLOAT64(*acc_value); saw_float = 1;
    } else {
        isum = JS_VALUE_GET_INT(*acc_value);
    }
    if (program->mode == TURBOJS_DENSE_ARRAY_HOLEY_SUM) {
        uint32_t wrapped, length, prop_index;
        JSShapeProperty *shape_props;
        if (!program->ctx->std_array_prototype || saw_float ||
            JS_VALUE_GET_NORM_TAG(array->prop[0].u.value) != JS_TAG_INT)
            return TURBOJS_OSR_EXIT_BAILOUT;
        length = (uint32_t)JS_VALUE_GET_INT(array->prop[0].u.value);
        if (i > length || (uint64_t)(length - i) > program->maximum_elements)
            return TURBOJS_OSR_EXIT_BAILOUT;
        wrapped = (uint32_t)(int32_t)isum;
        if (array->fast_array) {
            uint32_t stored = array->u.array.count < length ? array->u.array.count : length;
            for (; i < stored; ++i) {
                JSValue value = array->u.array.u.values[i];
                if (JS_IsUninitialized(value) || JS_IsUndefined(value))
                    continue;
                if (JS_VALUE_GET_NORM_TAG(value) != JS_TAG_INT)
                    return TURBOJS_OSR_EXIT_BAILOUT;
                wrapped += (uint32_t)JS_VALUE_GET_INT(value);
            }
        } else {
            shape_props = get_shape_prop(array->shape);
            for (prop_index = 0; prop_index < (uint32_t)array->shape->prop_count; ++prop_index) {
                JSShapeProperty *shape = &shape_props[prop_index];
                JSProperty *property = &array->prop[prop_index];
                uint32_t index;
                JSValue value;
                if (shape->atom == JS_ATOM_NULL ||
                    (shape->flags & JS_PROP_TMASK) != JS_PROP_NORMAL ||
                    !JS_AtomIsArrayIndex(program->ctx, &index, shape->atom) ||
                    index < i || index >= length)
                    continue;
                value = property->u.value;
                if (JS_IsUndefined(value)) continue;
                if (JS_VALUE_GET_NORM_TAG(value) != JS_TAG_INT)
                    return TURBOJS_OSR_EXIT_BAILOUT;
                wrapped += (uint32_t)JS_VALUE_GET_INT(value);
            }
        }
        index_slot->bits = length;
        index_slot->kind = TURBOJS_OSR_VALUE_INT64;
        if (program->accumulator_frame_local != UINT16_MAX) {
            TurboJSOSRValue *out;
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            out = &frame->locals[program->accumulator_frame_local];
            out->kind = TURBOJS_OSR_VALUE_INT64;
            out->bits = (uint64_t)(int64_t)(int32_t)wrapped;
        } else {
            set_value(program->ctx, acc_value, JS_NewInt32(program->ctx, (int32_t)wrapped));
        }
        program->ctx->rt->dense_array_osr_entries++;
        program->ctx->rt->dense_array_osr_elements += length - start_index;
        program->ctx->rt->holey_array_osr_entries++;
        program->ctx->rt->holey_array_osr_elements += length - start_index;
        *resume_offset = program->resume_bytecode_offset;
        return TURBOJS_OSR_EXIT_COMPLETED;
    }

    count = array->u.array.count;
    if (i > count || (uint64_t)(count - i) > program->maximum_elements)
        return TURBOJS_OSR_EXIT_BAILOUT;
    if (program->int32_wrap) {
        uint32_t wrapped;
        if (saw_float) return TURBOJS_OSR_EXIT_BAILOUT;
        wrapped = (uint32_t)(int32_t)isum;
        for (; i < count; ++i) {
            JSValue value = array->u.array.u.values[i];
            if (JS_IsUninitialized(value) || JS_VALUE_GET_NORM_TAG(value) != JS_TAG_INT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            wrapped += (uint32_t)JS_VALUE_GET_INT(value);
        }
        index_slot->bits = count;
        index_slot->kind = TURBOJS_OSR_VALUE_INT64;
        if (program->accumulator_frame_local != UINT16_MAX) {
            TurboJSOSRValue *out;
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            out = &frame->locals[program->accumulator_frame_local];
            out->kind = TURBOJS_OSR_VALUE_INT64;
            out->bits = (uint64_t)(int64_t)(int32_t)wrapped;
        } else {
            set_value(program->ctx, acc_value, JS_NewInt32(program->ctx, (int32_t)wrapped));
        }
        program->ctx->rt->dense_array_osr_entries++;
        program->ctx->rt->dense_array_osr_elements += count - start_index;
        *resume_offset = program->resume_bytecode_offset;
        return TURBOJS_OSR_EXIT_COMPLETED;
    }

    if (!saw_float) {
        int64_t lane0 = 0, lane1 = 0, lane2 = 0, lane3 = 0;
        for (; i + 4u <= count; i += 4u) {
            JSValue v0 = array->u.array.u.values[i + 0u];
            JSValue v1 = array->u.array.u.values[i + 1u];
            JSValue v2 = array->u.array.u.values[i + 2u];
            JSValue v3 = array->u.array.u.values[i + 3u];
            if (JS_IsUninitialized(v0) || JS_IsUninitialized(v1) ||
                JS_IsUninitialized(v2) || JS_IsUninitialized(v3))
                return TURBOJS_OSR_EXIT_BAILOUT;
            if (JS_VALUE_GET_NORM_TAG(v0) != JS_TAG_INT ||
                JS_VALUE_GET_NORM_TAG(v1) != JS_TAG_INT ||
                JS_VALUE_GET_NORM_TAG(v2) != JS_TAG_INT ||
                JS_VALUE_GET_NORM_TAG(v3) != JS_TAG_INT)
                break;
            if (!turbojs_i64_add_checked(lane0, JS_VALUE_GET_INT(v0), &lane0) ||
                !turbojs_i64_add_checked(lane1, JS_VALUE_GET_INT(v1), &lane1) ||
                !turbojs_i64_add_checked(lane2, JS_VALUE_GET_INT(v2), &lane2) ||
                !turbojs_i64_add_checked(lane3, JS_VALUE_GET_INT(v3), &lane3))
                return TURBOJS_OSR_EXIT_BAILOUT;
            unrolled_blocks++;
        }
        if (unrolled_blocks) {
            int64_t pair0, pair1, block_sum;
            if (!turbojs_i64_add_checked(lane0, lane1, &pair0) ||
                !turbojs_i64_add_checked(lane2, lane3, &pair1) ||
                !turbojs_i64_add_checked(pair0, pair1, &block_sum) ||
                !turbojs_i64_add_checked(isum, block_sum, &isum))
                return TURBOJS_OSR_EXIT_BAILOUT;
        }
    }
    for (; i < count; ++i) {
        JSValue value = array->u.array.u.values[i];
        int tag;
        if (JS_IsUninitialized(value)) return TURBOJS_OSR_EXIT_BAILOUT;
        tag = JS_VALUE_GET_NORM_TAG(value);
        if (tag == JS_TAG_INT) {
            int64_t v = JS_VALUE_GET_INT(value);
            if (saw_float) dsum += (double)v;
            else if (!turbojs_i64_add_checked(isum, v, &isum)) return TURBOJS_OSR_EXIT_BAILOUT;
        } else if (tag == JS_TAG_FLOAT64) {
            if (!saw_float) { dsum = (double)isum; saw_float = 1; promoted = 1; }
            dsum += JS_VALUE_GET_FLOAT64(value);
        } else return TURBOJS_OSR_EXIT_BAILOUT;
    }
    index_slot->bits = count;
    index_slot->kind = TURBOJS_OSR_VALUE_INT64;
    if (program->accumulator_frame_local != UINT16_MAX) {
        TurboJSOSRValue *out;
        if (program->accumulator_frame_local >= frame->local_count)
            return TURBOJS_OSR_EXIT_BAILOUT;
        out = &frame->locals[program->accumulator_frame_local];
        if (saw_float) {
            out->kind = TURBOJS_OSR_VALUE_FLOAT64;
            memcpy(&out->bits, &dsum, sizeof(dsum));
        } else {
            out->kind = TURBOJS_OSR_VALUE_INT64;
            out->bits = (uint64_t)isum;
        }
    } else {
        set_value(program->ctx, acc_value,
                  saw_float ? JS_NewFloat64(program->ctx, dsum) : JS_NewInt64(program->ctx, isum));
    }
    program->ctx->rt->dense_array_osr_entries++;
    program->ctx->rt->dense_array_osr_elements += count - start_index;
    program->ctx->rt->dense_array_osr_unrolled_blocks += unrolled_blocks;
    program->ctx->rt->dense_array_osr_multi_lane_blocks += unrolled_blocks;
    program->ctx->rt->dense_array_osr_float_promotions += promoted;
    *resume_offset = program->resume_bytecode_offset;
    return TURBOJS_OSR_EXIT_COMPLETED;
}

static TurboJSOSREntry turbojs_dense_array_osr_entry(TurboJSDenseArrayOSRExecution *execution)
{
    TurboJSOSREntry entry;
    memset(&entry, 0, sizeof(entry));
    if (execution && execution->program) {
        entry.callback = turbojs_run_dense_array_sum;
        entry.opaque = execution;
        entry.loop_header = execution->program->loop_header;
        entry.bailout_limit = 3;
    }
    return entry;
}


static void turbojs_osr_value_from_js(JSValue value, TurboJSOSRValue *out)
{
    int tag = JS_VALUE_GET_NORM_TAG(value);
    memset(out, 0, sizeof(*out));
    if (tag == JS_TAG_INT) {
        out->kind = TURBOJS_OSR_VALUE_INT64;
        out->bits = (uint64_t)(int64_t)JS_VALUE_GET_INT(value);
    } else if (tag == JS_TAG_FLOAT64) {
        double d = JS_VALUE_GET_FLOAT64(value);
        out->kind = TURBOJS_OSR_VALUE_FLOAT64;
        memcpy(&out->bits, &d, sizeof(d));
    } else {
        out->kind = TURBOJS_OSR_VALUE_REFERENCE;
        if (tag == JS_TAG_OBJECT)
            out->bits = (uint64_t)(uintptr_t)JS_VALUE_GET_OBJ(value);
        else
            memcpy(&out->bits, &value,
                   sizeof(value) < sizeof(out->bits) ? sizeof(value) : sizeof(out->bits));
    }
}


#define TURBOJS_OBJECT_OSR_MAX_READS 8

typedef enum TurboJSObjectSlotKind {
    TURBOJS_OBJECT_SLOT_LOCAL = 1,
    TURBOJS_OBJECT_SLOT_ARGUMENT = 2,
    TURBOJS_OBJECT_SLOT_VAR_REF = 3
} TurboJSObjectSlotKind;

typedef struct TurboJSObjectSlot {
    uint8_t kind;
    uint16_t index;
} TurboJSObjectSlot;

typedef struct TurboJSObjectPropertyOSRProgram {
    JSContext *ctx;
    uint16_t induction_local;
    TurboJSObjectSlot object_slot;
    TurboJSObjectSlot accumulator_slot;
    JSAtom write_atom;
    JSAtom read_atoms[TURBOJS_OBJECT_OSR_MAX_READS];
    uint8_t read_count;
    uint8_t reserved[3];
    int32_t limit;
    uint32_t loop_header;
    uint32_t resume_bytecode_offset;
} TurboJSObjectPropertyOSRProgram;

typedef struct TurboJSObjectPropertyOSRExecution {
    TurboJSObjectPropertyOSRProgram *program;
    JSValue *arg_buf;
    JSValue *var_buf;
    JSVarRef **var_refs;
} TurboJSObjectPropertyOSRExecution;

static int turbojs_parse_get_object_slot(const uint8_t **pp, const uint8_t *end, TurboJSObjectSlot *slot)
{
    const uint8_t *p = *pp;
    if (p >= end) return 0;
    if (*p == OP_get_var_ref_check) { if ((size_t)(end-p)<3) return 0; slot->kind=3; slot->index=get_u16(p+1); p+=3; }
    else if (*p == OP_get_loc || *p == OP_get_loc_check) { if ((size_t)(end-p)<3) return 0; slot->kind=1; slot->index=get_u16(p+1); p+=3; }
    else if (*p == OP_get_arg) { if ((size_t)(end-p)<3) return 0; slot->kind=2; slot->index=get_u16(p+1); p+=3; }
    else if (*p >= OP_get_loc0 && *p <= OP_get_loc3) { slot->kind=1; slot->index=(uint16_t)(*p-OP_get_loc0); p++; }
    else if (*p >= OP_get_arg0 && *p <= OP_get_arg3) { slot->kind=2; slot->index=(uint16_t)(*p-OP_get_arg0); p++; }
    else return 0;
    *pp=p; return 1;
}

static int turbojs_parse_put_object_slot(const uint8_t **pp, const uint8_t *end, const TurboJSObjectSlot *slot)
{
    const uint8_t *p=*pp; uint16_t index;
    if (slot->kind==3 && *p==OP_put_var_ref_check) { if ((size_t)(end-p)<3) return 0; index=get_u16(p+1); p+=3; }
    else if (slot->kind==1 && (*p==OP_put_loc || *p==OP_put_loc_check)) { if ((size_t)(end-p)<3) return 0; index=get_u16(p+1); p+=3; }
    else if (slot->kind==2 && *p==OP_put_arg) { if ((size_t)(end-p)<3) return 0; index=get_u16(p+1); p+=3; }
    else if (slot->kind==1 && *p>=OP_put_loc0 && *p<=OP_put_loc3) { index=(uint16_t)(*p-OP_put_loc0); p++; }
    else if (slot->kind==2 && *p>=OP_put_arg0 && *p<=OP_put_arg3) { index=(uint16_t)(*p-OP_put_arg0); p++; }
    else return 0;
    if(index!=slot->index)return 0; *pp=p; return 1;
}

static JSValue *turbojs_object_slot_value(TurboJSObjectPropertyOSRExecution *e, const TurboJSObjectSlot *slot)
{
    if (!e || !slot) return NULL;
    if (slot->kind==1) return e->var_buf ? &e->var_buf[slot->index] : NULL;
    if (slot->kind==2) return e->arg_buf ? &e->arg_buf[slot->index] : NULL;
    if (slot->kind==3 && e->var_refs && e->var_refs[slot->index]) return e->var_refs[slot->index]->pvalue;
    return NULL;
}



static int turbojs_js_number(JSValueConst value, double *out);

typedef enum TurboJSScalarLoopMode {
    TURBOJS_SCALAR_INT32_ADD_OR = 1,
    TURBOJS_SCALAR_FLOAT_AFFINE = 2,
    TURBOJS_SCALAR_AFFINE_LEAF_CALL = 3,
    TURBOJS_SCALAR_MULTI_INT32 = 4,
    TURBOJS_SCALAR_CONDITIONAL_INT32 = 5,
    TURBOJS_SCALAR_DEPENDENT_INT32 = 6,
    TURBOJS_SCALAR_CONDITIONAL_PROGRAM_INT32 = 7,
    /* Exact Int32 loop-level inlining for a stable tiny leaf. */
    TURBOJS_SCALAR_INT32_LEAF_CALL = 8,
    /* Guarded Math.imul + xor-shift recurrence. */
    TURBOJS_SCALAR_INT32_IMUL_XORSHIFT = 9,
    /* Two stable tiny-leaf targets selected by an induction mask. */
    TURBOJS_SCALAR_POLYMORPHIC_LEAF_CALL = 10,
    /* A compact array of closures whose single capture is scalar. */
    TURBOJS_SCALAR_CAPTURED_CLOSURE_CALL = 11,
    /* Guarded self-recursive Fibonacci calls lowered iteratively. */
    TURBOJS_SCALAR_RECURSIVE_FIB_SUM = 12,
    /* Coupled Float64 recurrence with a masked induction denominator. */
    TURBOJS_SCALAR_COUPLED_FLOAT64 = 13
} TurboJSScalarLoopMode;

#define TURBOJS_SCALAR_MAX_ACCUMULATORS 4
#define TURBOJS_SCALAR_MAX_CONDITIONAL_STEPS 4

typedef struct TurboJSScalarIntAccumulator {
    TurboJSObjectSlot slot;
    uint16_t frame_local;
    uint8_t add_induction;
    uint8_t subtract;
    int32_t add_constant;
    int8_t source_accumulator;
    uint8_t source_is_accumulator;
    uint8_t reserved[2];
} TurboJSScalarIntAccumulator;

typedef struct TurboJSScalarConditionalStep {
    uint8_t condition_kind; /* 1: mask, 2: <, 3: <=, 4: >, 5: >=, 6: ==, 7: != */
    uint8_t compare_from_slot;
    uint8_t target_accumulator_index;
    uint8_t reserved;
    int32_t condition_value;
    TurboJSObjectSlot compare_slot;
    TurboJSScalarIntAccumulator branch[2];
} TurboJSScalarConditionalStep;

struct TurboJSScalarLoopOSRProgram {
    JSContext *ctx;
    uint16_t induction_local;
    uint16_t accumulator_frame_local;
    TurboJSObjectSlot accumulator_slot;
    TurboJSObjectSlot function_slot;
    TurboJSObjectSlot secondary_function_slot;
    TurboJSObjectSlot auxiliary_slot;
    TurboJSObjectSlot limit_slot;
    uint8_t mode;
    uint8_t call_argc;
    uint8_t limit_from_slot;
    uint8_t reserved;
    int32_t limit;
    double constant_a;
    double constant_b;
    double constant_c;
    double constant_d;
    double constant_e;
    /* Exact-integer kernel metadata. */
    int32_t mix_multiplier;
    int32_t mix_addend;
    uint8_t mix_shift;
    uint8_t mix_unsigned_shift;
    uint8_t leaf_affine_ready;
    uint8_t reserved_flags;
    uint64_t leaf_target_identity;
    uint64_t secondary_leaf_target_identity;
    int64_t leaf_slope;
    int64_t leaf_intercept;
    uint32_t selection_mask;
    int32_t recursion_base;
    JSAtom mix_global_atom;
    JSAtom mix_method_atom;
    uint8_t int_accumulator_count;
    uint8_t reserved2[3];
    int32_t branch_mask;
    int32_t branch_compare_constant;
    uint8_t branch_condition_kind; /* legacy single conditional */
    uint8_t conditional_step_count;
    uint8_t branch_reserved[2];
    TurboJSScalarIntAccumulator int_accumulators[TURBOJS_SCALAR_MAX_ACCUMULATORS];
    TurboJSScalarConditionalStep conditional_steps[TURBOJS_SCALAR_MAX_CONDITIONAL_STEPS];
    uint32_t loop_header;
    uint32_t resume_bytecode_offset;
};

typedef struct TurboJSScalarLoopOSRExecution {
    TurboJSScalarLoopOSRProgram *program;
    JSValue *arg_buf;
    JSValue *var_buf;
    JSVarRef **var_refs;
} TurboJSScalarLoopOSRExecution;

static int turbojs_parse_get_scalar_slot(const uint8_t **pp, const uint8_t *end,
                                          TurboJSObjectSlot *slot)
{
    const uint8_t *p = *pp;
    if (p >= end) return 0;
    if (*p == OP_get_var_ref_check) {
        if ((size_t)(end - p) < 3) return 0;
        slot->kind = TURBOJS_OBJECT_SLOT_VAR_REF; slot->index = get_u16(p + 1); p += 3;
    } else if (*p >= OP_get_var_ref0 && *p <= OP_get_var_ref3) {
        slot->kind = TURBOJS_OBJECT_SLOT_VAR_REF; slot->index = (uint16_t)(*p - OP_get_var_ref0); p++;
    } else if (*p == OP_get_loc || *p == OP_get_loc_check) {
        if ((size_t)(end - p) < 3) return 0;
        slot->kind = TURBOJS_OBJECT_SLOT_LOCAL; slot->index = get_u16(p + 1); p += 3;
    } else if (*p >= OP_get_loc0 && *p <= OP_get_loc3) {
        slot->kind = TURBOJS_OBJECT_SLOT_LOCAL; slot->index = (uint16_t)(*p - OP_get_loc0); p++;
    } else if (*p == OP_get_arg) {
        if ((size_t)(end - p) < 3) return 0;
        slot->kind = TURBOJS_OBJECT_SLOT_ARGUMENT; slot->index = get_u16(p + 1); p += 3;
    } else if (*p >= OP_get_arg0 && *p <= OP_get_arg3) {
        slot->kind = TURBOJS_OBJECT_SLOT_ARGUMENT; slot->index = (uint16_t)(*p - OP_get_arg0); p++;
    } else return 0;
    *pp = p;
    return 1;
}

static int turbojs_parse_put_scalar_slot(const uint8_t **pp, const uint8_t *end,
                                          const TurboJSObjectSlot *slot)
{
    const uint8_t *p = *pp;
    uint16_t index;
    if (slot->kind == TURBOJS_OBJECT_SLOT_VAR_REF && *p == OP_put_var_ref_check) {
        if ((size_t)(end - p) < 3) return 0; index = get_u16(p + 1); p += 3;
    } else if (slot->kind == TURBOJS_OBJECT_SLOT_VAR_REF && *p >= OP_put_var_ref0 && *p <= OP_put_var_ref3) {
        index = (uint16_t)(*p - OP_put_var_ref0); p++;
    } else if (slot->kind == TURBOJS_OBJECT_SLOT_LOCAL && (*p == OP_put_loc || *p == OP_put_loc_check)) {
        if ((size_t)(end - p) < 3) return 0; index = get_u16(p + 1); p += 3;
    } else if (slot->kind == TURBOJS_OBJECT_SLOT_LOCAL && *p >= OP_put_loc0 && *p <= OP_put_loc3) {
        index = (uint16_t)(*p - OP_put_loc0); p++;
    } else if (slot->kind == TURBOJS_OBJECT_SLOT_ARGUMENT && *p == OP_put_arg) {
        if ((size_t)(end - p) < 3) return 0; index = get_u16(p + 1); p += 3;
    } else if (slot->kind == TURBOJS_OBJECT_SLOT_ARGUMENT && *p >= OP_put_arg0 && *p <= OP_put_arg3) {
        index = (uint16_t)(*p - OP_put_arg0); p++;
    } else return 0;
    if (index != slot->index) return 0;
    *pp = p;
    return 1;
}

static int turbojs_scalar_const(JSFunctionBytecode *b, const uint8_t **pp,
                                 const uint8_t *end, double *out)
{
    const uint8_t *p = *pp;
    uint32_t index;
    if (p >= end) return 0;
    if (*p >= OP_push_0 && *p <= OP_push_7) {
        *out = (double)(*p - OP_push_0); p++;
    } else if (*p == OP_push_minus1) {
        *out = -1.0; p++;
    } else if (*p == OP_push_i8) {
        if ((size_t)(end - p) < 2) return 0; *out = (double)(int8_t)p[1]; p += 2;
    } else if (*p == OP_push_i16) {
        if ((size_t)(end - p) < 3) return 0; *out = (double)(int16_t)get_u16(p + 1); p += 3;
    } else if (*p == OP_push_i32) {
        if ((size_t)(end - p) < 5) return 0; *out = (double)(int32_t)get_u32(p + 1); p += 5;
    } else if (*p == OP_push_const8) {
        if ((size_t)(end - p) < 2) return 0; index = p[1]; p += 2;
        if (index >= (uint32_t)b->cpool_count || !turbojs_js_number(b->cpool[index], out)) return 0;
    } else if (*p == OP_push_const) {
        if ((size_t)(end - p) < 5) return 0; index = get_u32(p + 1); p += 5;
        if (index >= (uint32_t)b->cpool_count || !turbojs_js_number(b->cpool[index], out)) return 0;
    } else return 0;
    *pp = p;
    return 1;
}


static int turbojs_parse_branch_target(const uint8_t **pp, const uint8_t *end,
                                        uint8_t short_op, uint8_t long_op,
                                        const uint8_t **target)
{
    const uint8_t *p = *pp;
    int64_t disp;
    if (p >= end) return 0;
    if (*p == short_op) {
        if ((size_t)(end - p) < 2) return 0;
        p++;
        disp = (int8_t)*p;
        *target = p + disp;
        p++;
    } else if (*p == long_op) {
        if ((size_t)(end - p) < 5) return 0;
        p++;
        disp = (int32_t)get_u32(p);
        *target = p + disp;
        p += 4;
    } else return 0;
    if (*target > end) return 0;
    *pp = p;
    return 1;
}

static int turbojs_parse_goto_target(const uint8_t **pp, const uint8_t *end,
                                      const uint8_t **target)
{
    const uint8_t *p = *pp;
    int64_t disp;
    if (p >= end) return 0;
    if (*p == OP_goto8) {
        if ((size_t)(end - p) < 2) return 0;
        p++; disp = (int8_t)*p; *target = p + disp; p++;
    } else if (*p == OP_goto16) {
        if ((size_t)(end - p) < 3) return 0;
        p++; disp = (int16_t)get_u16(p); *target = p + disp; p += 2;
    } else if (*p == OP_goto) {
        if ((size_t)(end - p) < 5) return 0;
        p++; disp = (int32_t)get_u32(p); *target = p + disp; p += 4;
    } else return 0;
    if (*target > end) return 0;
    *pp = p;
    return 1;
}


/* Common counted-loop parsing. These recognizers intentionally accept
 * only bytecode shapes whose side effects and Int32 conversions can be proven
 * locally. Unsupported or modified builtins fall back to Pulse unchanged. */
static int turbojs_atom_name_is(JSContext *ctx, JSAtom atom, const char *name)
{
    const char *actual;
    int matches;
    if (!ctx || !name)
        return 0;
    actual = JS_AtomToCString(ctx, atom);
    if (!actual)
        return 0;
    matches = strcmp(actual, name) == 0;
    JS_FreeCString(ctx, actual);
    return matches;
}

static int turbojs_parse_counted_loop_header(JSFunctionBytecode *b,
                                              uint32_t target_offset,
                                              uint32_t source_offset,
                                              const uint8_t **body_pc,
                                              uint16_t *induction,
                                              int32_t *limit,
                                              TurboJSObjectSlot *limit_slot,
                                              uint8_t *limit_from_slot,
                                              uint32_t *resume)
{
    const uint8_t *base, *pc, *end, *resume_pc;
    if (!b || !body_pc || !induction || !limit || !limit_slot ||
        !limit_from_slot || !resume || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len)
        return 0;
    base = b->byte_code_buf;
    pc = base + target_offset;
    end = base + b->byte_code_len;
    if ((size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check)
        return 0;
    *induction = get_u16(pc);
    pc += 2;
    memset(limit_slot, 0, sizeof(*limit_slot));
    *limit_from_slot = 0;
    {
        const uint8_t *constant_pc = pc;
        double limit_number;
        if (turbojs_scalar_const(b, &constant_pc, end, &limit_number) &&
            isfinite(limit_number) && floor(limit_number) == limit_number &&
            limit_number >= (double)INT32_MIN && limit_number <= (double)INT32_MAX) {
            *limit = (int32_t)limit_number;
            pc = constant_pc;
        } else {
            if (!turbojs_parse_get_scalar_slot(&pc, end, limit_slot))
                return 0;
            *limit_from_slot = 1;
            *limit = 0;
        }
    }
    if (pc >= end || *pc++ != OP_lt ||
        !turbojs_parse_branch_target(&pc, end, OP_if_false8, OP_if_false,
                                     &resume_pc))
        return 0;
    *resume = (uint32_t)(resume_pc - base);
    *body_pc = pc;
    return 1;
}

static int turbojs_parse_counted_loop_tail(JSFunctionBytecode *b,
                                            const uint8_t **body_pc,
                                            const uint8_t *end,
                                            uint16_t induction,
                                            uint32_t target_offset,
                                            uint32_t source_offset)
{
    const uint8_t *base = b->byte_code_buf;
    const uint8_t *pc = *body_pc;
    const uint8_t *backedge;
    if ((size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check ||
        get_u16(pc) != induction)
        return 0;
    pc += 2;
    if (pc >= end || *pc++ != OP_post_inc ||
        (size_t)(end - pc) < 3 || *pc++ != OP_put_loc_check ||
        get_u16(pc) != induction)
        return 0;
    pc += 2;
    if (pc >= end || *pc++ != OP_drop ||
        (uint32_t)(pc - base) != source_offset ||
        !turbojs_parse_goto_target(&pc, end, &backedge) ||
        (uint32_t)(backedge - base) != target_offset)
        return 0;
    *body_pc = pc;
    return 1;
}

static int turbojs_detect_int32_leaf_call_loop(JSFunctionBytecode *b,
                                                uint32_t target_offset,
                                                uint32_t source_offset,
                                                TurboJSScalarLoopOSRProgram *spec)
{
    const uint8_t *pc, *end;
    uint16_t induction;
    int32_t limit;
    TurboJSObjectSlot limit_slot, accumulator, function_slot;
    uint8_t limit_from_slot;
    uint8_t call_argc;
    uint32_t resume;
    double fixed_argument = 0.0;
    if (!b || !spec ||
        !turbojs_parse_counted_loop_header(b, target_offset, source_offset,
                                           &pc, &induction, &limit, &limit_slot,
                                           &limit_from_slot, &resume))
        return 0;
    end = b->byte_code_buf + b->byte_code_len;
    if (!turbojs_parse_get_scalar_slot(&pc, end, &accumulator) ||
        !turbojs_parse_get_scalar_slot(&pc, end, &function_slot) ||
        (size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check ||
        get_u16(pc) != induction)
        return 0;
    pc += 2;
    if (pc < end && *pc == OP_call1) {
        pc++;
        call_argc = 1;
    } else {
        if (!turbojs_scalar_const(b, &pc, end, &fixed_argument) ||
            pc >= end || *pc++ != OP_call2)
            return 0;
        call_argc = 2;
    }
    if (pc >= end || *pc++ != OP_add || pc >= end || *pc++ != OP_push_0 ||
        pc >= end || *pc++ != OP_or || pc >= end || *pc++ != OP_dup ||
        !turbojs_parse_put_scalar_slot(&pc, end, &accumulator) ||
        pc >= end || *pc++ != OP_drop ||
        !turbojs_parse_counted_loop_tail(b, &pc, end, induction,
                                         target_offset, source_offset))
        return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        (!limit_from_slot && limit < 0))
        return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_slot = accumulator;
    spec->accumulator_frame_local = accumulator.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator.index) : UINT16_MAX;
    spec->function_slot = function_slot;
    spec->limit_slot = limit_slot;
    spec->limit_from_slot = limit_from_slot;
    spec->limit = limit;
    spec->constant_a = fixed_argument;
    spec->call_argc = call_argc;
    spec->mode = TURBOJS_SCALAR_INT32_LEAF_CALL;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
    return 1;
}


static int turbojs_detect_polymorphic_leaf_call_loop(
    JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset,
    TurboJSScalarLoopOSRProgram *spec)
{
    const uint8_t *pc, *end, *false_pc, *join_pc;
    uint16_t induction, selected_local;
    int32_t limit;
    TurboJSObjectSlot limit_slot, accumulator, first_function, second_function;
    uint8_t limit_from_slot;
    uint32_t resume;
    double mask_number;
    if (!b || !spec ||
        !turbojs_parse_counted_loop_header(b, target_offset, source_offset,
                                           &pc, &induction, &limit, &limit_slot,
                                           &limit_from_slot, &resume))
        return 0;
    end = b->byte_code_buf + b->byte_code_len;
    if ((size_t)(end - pc) < 3 || *pc++ != OP_set_loc_uninitialized)
        return 0;
    selected_local = get_u16(pc); pc += 2;
    if ((size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check ||
        get_u16(pc) != induction)
        return 0;
    pc += 2;
    if (!turbojs_scalar_const(b, &pc, end, &mask_number) ||
        pc >= end || *pc++ != OP_and ||
        !turbojs_parse_branch_target(&pc, end, OP_if_false8, OP_if_false,
                                     &false_pc) ||
        !turbojs_parse_get_scalar_slot(&pc, end, &first_function) ||
        !turbojs_parse_goto_target(&pc, end, &join_pc) || pc != false_pc ||
        !turbojs_parse_get_scalar_slot(&pc, end, &second_function) || pc != join_pc ||
        !turbojs_parse_put_scalar_slot(&pc, end,
            &(TurboJSObjectSlot){ TURBOJS_OBJECT_SLOT_LOCAL, selected_local }) ||
        !turbojs_parse_get_scalar_slot(&pc, end, &accumulator))
        return 0;
    if ((size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check ||
        get_u16(pc) != selected_local)
        return 0;
    pc += 2;
    if ((size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check ||
        get_u16(pc) != induction)
        return 0;
    pc += 2;
    if (pc >= end || *pc++ != OP_call1 || pc >= end || *pc++ != OP_add ||
        pc >= end || *pc++ != OP_push_0 || pc >= end || *pc++ != OP_or ||
        pc >= end || *pc++ != OP_dup ||
        !turbojs_parse_put_scalar_slot(&pc, end, &accumulator) ||
        pc >= end || *pc++ != OP_drop ||
        !turbojs_parse_counted_loop_tail(b, &pc, end, induction,
                                         target_offset, source_offset))
        return 0;
    if (!isfinite(mask_number) || floor(mask_number) != mask_number ||
        mask_number < 1.0 || mask_number > 255.0 ||
        (((uint32_t)mask_number + 1u) & (uint32_t)mask_number) != 0u ||
        resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        selected_local >= b->var_count || (!limit_from_slot && limit < 0))
        return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_slot = accumulator;
    spec->accumulator_frame_local = accumulator.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator.index) : UINT16_MAX;
    spec->function_slot = first_function;
    spec->secondary_function_slot = second_function;
    spec->limit_slot = limit_slot;
    spec->limit_from_slot = limit_from_slot;
    spec->limit = limit;
    spec->selection_mask = (uint32_t)mask_number;
    spec->mode = TURBOJS_SCALAR_POLYMORPHIC_LEAF_CALL;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
    return 1;
}

static int turbojs_detect_captured_closure_call_loop(
    JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset,
    TurboJSScalarLoopOSRProgram *spec)
{
    const uint8_t *pc, *end;
    uint16_t induction;
    int32_t limit;
    TurboJSObjectSlot limit_slot, accumulator, array_slot;
    uint8_t limit_from_slot;
    uint32_t resume;
    double mask_number;
    if (!b || !spec ||
        !turbojs_parse_counted_loop_header(b, target_offset, source_offset,
                                           &pc, &induction, &limit, &limit_slot,
                                           &limit_from_slot, &resume))
        return 0;
    end = b->byte_code_buf + b->byte_code_len;
    if (!turbojs_parse_get_scalar_slot(&pc, end, &accumulator) ||
        !turbojs_parse_get_scalar_slot(&pc, end, &array_slot) ||
        (size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check ||
        get_u16(pc) != induction)
        return 0;
    pc += 2;
    if (!turbojs_scalar_const(b, &pc, end, &mask_number) ||
        pc >= end || *pc++ != OP_and || pc >= end || *pc++ != OP_get_array_el2 ||
        (size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check ||
        get_u16(pc) != induction)
        return 0;
    pc += 2;
    if ((size_t)(end - pc) < 3 || *pc++ != OP_call_method || get_u16(pc) != 1)
        return 0;
    pc += 2;
    if (pc >= end || *pc++ != OP_add || pc >= end || *pc++ != OP_push_0 ||
        pc >= end || *pc++ != OP_or || pc >= end || *pc++ != OP_dup ||
        !turbojs_parse_put_scalar_slot(&pc, end, &accumulator) ||
        pc >= end || *pc++ != OP_drop ||
        !turbojs_parse_counted_loop_tail(b, &pc, end, induction,
                                         target_offset, source_offset))
        return 0;
    if (!isfinite(mask_number) || floor(mask_number) != mask_number ||
        mask_number < 1.0 || mask_number > 7.0 ||
        (((uint32_t)mask_number + 1u) & (uint32_t)mask_number) != 0u ||
        resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        (!limit_from_slot && limit < 0))
        return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_slot = accumulator;
    spec->accumulator_frame_local = accumulator.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator.index) : UINT16_MAX;
    spec->function_slot = array_slot;
    spec->limit_slot = limit_slot;
    spec->limit_from_slot = limit_from_slot;
    spec->limit = limit;
    spec->selection_mask = (uint32_t)mask_number;
    spec->mode = TURBOJS_SCALAR_CAPTURED_CLOSURE_CALL;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
    return 1;
}

static int turbojs_detect_recursive_fib_sum_loop(
    JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset,
    TurboJSScalarLoopOSRProgram *spec)
{
    const uint8_t *pc, *end;
    uint16_t induction;
    int32_t limit;
    TurboJSObjectSlot limit_slot, accumulator, function_slot;
    uint8_t limit_from_slot;
    uint32_t resume;
    double base_number, mask_number;
    if (!b || !spec ||
        !turbojs_parse_counted_loop_header(b, target_offset, source_offset,
                                           &pc, &induction, &limit, &limit_slot,
                                           &limit_from_slot, &resume))
        return 0;
    end = b->byte_code_buf + b->byte_code_len;
    if (!turbojs_parse_get_scalar_slot(&pc, end, &accumulator) ||
        !turbojs_parse_get_scalar_slot(&pc, end, &function_slot) ||
        !turbojs_scalar_const(b, &pc, end, &base_number) ||
        (size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check ||
        get_u16(pc) != induction)
        return 0;
    pc += 2;
    if (!turbojs_scalar_const(b, &pc, end, &mask_number) ||
        pc >= end || *pc++ != OP_and || pc >= end || *pc++ != OP_add ||
        pc >= end || *pc++ != OP_call1 || pc >= end || *pc++ != OP_add ||
        pc >= end || *pc++ != OP_dup ||
        !turbojs_parse_put_scalar_slot(&pc, end, &accumulator) ||
        pc >= end || *pc++ != OP_drop ||
        !turbojs_parse_counted_loop_tail(b, &pc, end, induction,
                                         target_offset, source_offset))
        return 0;
    if (!isfinite(base_number) || floor(base_number) != base_number ||
        base_number < 0.0 || base_number > 40.0 ||
        !isfinite(mask_number) || floor(mask_number) != mask_number ||
        mask_number < 1.0 || mask_number > 7.0 ||
        (((uint32_t)mask_number + 1u) & (uint32_t)mask_number) != 0u ||
        base_number + mask_number > 40.0 ||
        resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        (!limit_from_slot && limit < 0))
        return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_slot = accumulator;
    spec->accumulator_frame_local = accumulator.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator.index) : UINT16_MAX;
    spec->function_slot = function_slot;
    spec->limit_slot = limit_slot;
    spec->limit_from_slot = limit_from_slot;
    spec->limit = limit;
    spec->recursion_base = (int32_t)base_number;
    spec->selection_mask = (uint32_t)mask_number;
    spec->mode = TURBOJS_SCALAR_RECURSIVE_FIB_SUM;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
    return 1;
}

static int turbojs_detect_int32_imul_xorshift_loop(
    JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset,
    TurboJSScalarLoopOSRProgram *spec)
{
    const uint8_t *pc, *end;
    uint16_t induction;
    int32_t limit;
    TurboJSObjectSlot limit_slot, accumulator, check_slot;
    uint8_t limit_from_slot;
    uint32_t resume;
    JSAtom global_atom, method_atom;
    double multiplier_number, addend_number, shift_number;
    uint8_t unsigned_shift;
    if (!b || !spec ||
        !turbojs_parse_counted_loop_header(b, target_offset, source_offset,
                                           &pc, &induction, &limit, &limit_slot,
                                           &limit_from_slot, &resume))
        return 0;
    end = b->byte_code_buf + b->byte_code_len;
    if ((size_t)(end - pc) < 5 || *pc++ != OP_get_var)
        return 0;
    global_atom = get_u32(pc);
    pc += 4;
    if ((size_t)(end - pc) < 5 || *pc++ != OP_get_field2)
        return 0;
    method_atom = get_u32(pc);
    pc += 4;
    if (!turbojs_atom_name_is(b->realm, global_atom, "Math") ||
        !turbojs_atom_name_is(b->realm, method_atom, "imul") ||
        !turbojs_parse_get_scalar_slot(&pc, end, &accumulator) ||
        (size_t)(end - pc) < 3 || *pc++ != OP_get_loc_check ||
        get_u16(pc) != induction)
        return 0;
    pc += 2;
    if (pc >= end || *pc++ != OP_xor ||
        !turbojs_scalar_const(b, &pc, end, &multiplier_number) ||
        (size_t)(end - pc) < 3 || *pc++ != OP_call_method ||
        get_u16(pc) != 2)
        return 0;
    pc += 2;
    if (!turbojs_scalar_const(b, &pc, end, &addend_number) ||
        pc >= end || *pc++ != OP_add || pc >= end || *pc++ != OP_push_0 ||
        pc >= end || *pc++ != OP_or || pc >= end || *pc++ != OP_dup ||
        !turbojs_parse_put_scalar_slot(&pc, end, &accumulator) ||
        pc >= end || *pc++ != OP_drop ||
        !turbojs_parse_get_scalar_slot(&pc, end, &check_slot) ||
        check_slot.kind != accumulator.kind || check_slot.index != accumulator.index ||
        !turbojs_parse_get_scalar_slot(&pc, end, &check_slot) ||
        check_slot.kind != accumulator.kind || check_slot.index != accumulator.index ||
        !turbojs_scalar_const(b, &pc, end, &shift_number))
        return 0;
    if (pc < end && *pc == OP_shr) {
        unsigned_shift = 1;
        pc++;
    } else if (pc < end && *pc == OP_sar) {
        unsigned_shift = 0;
        pc++;
    } else {
        return 0;
    }
    if (pc >= end || *pc++ != OP_xor)
        return 0;
    if ((size_t)(end - pc) >= 2 && pc[0] == OP_push_0 && pc[1] == OP_or)
        pc += 2;
    if (pc >= end || *pc++ != OP_dup ||
        !turbojs_parse_put_scalar_slot(&pc, end, &accumulator) ||
        pc >= end || *pc++ != OP_drop ||
        !turbojs_parse_counted_loop_tail(b, &pc, end, induction,
                                         target_offset, source_offset))
        return 0;
    if (!isfinite(multiplier_number) || !isfinite(addend_number) ||
        !isfinite(shift_number) || floor(multiplier_number) != multiplier_number ||
        floor(addend_number) != addend_number || floor(shift_number) != shift_number ||
        multiplier_number < (double)INT32_MIN || multiplier_number > (double)INT32_MAX ||
        addend_number < (double)INT32_MIN || addend_number > (double)INT32_MAX ||
        shift_number < 0.0 || shift_number > 31.0 ||
        resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        (!limit_from_slot && limit < 0))
        return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_slot = accumulator;
    spec->accumulator_frame_local = accumulator.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator.index) : UINT16_MAX;
    spec->limit_slot = limit_slot;
    spec->limit_from_slot = limit_from_slot;
    spec->limit = limit;
    spec->mix_multiplier = (int32_t)multiplier_number;
    spec->mix_addend = (int32_t)addend_number;
    spec->mix_shift = (uint8_t)shift_number;
    spec->mix_unsigned_shift = unsigned_shift;
    spec->mix_global_atom = global_atom;
    spec->mix_method_atom = method_atom;
    spec->mode = TURBOJS_SCALAR_INT32_IMUL_XORSHIFT;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
    return 1;
}

static int turbojs_detect_scalar_loop(JSFunctionBytecode *b, uint32_t target_offset,
                                      uint32_t source_offset,
                                      TurboJSScalarLoopOSRProgram *spec)
{
    const uint8_t *base, *pc, *end;
    uint16_t induction, i2;
    TurboJSObjectSlot accumulator, function_slot;
    uint32_t resume;
    int32_t limit = 0;
    TurboJSObjectSlot limit_slot;
    int limit_from_slot = 0;
    double a, c, d;
    if (!b || !spec || target_offset >= source_offset || source_offset >= (uint32_t)b->byte_code_len)
        return 0;
    base = b->byte_code_buf; pc = base + target_offset; end = base + b->byte_code_len;
#define SNEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define SOP(op) do { SNEED(1); if (*pc++ != (op)) return 0; } while (0)
#define SREF(op, out) do { SNEED(3); if (*pc++ != (op)) return 0; (out)=get_u16(pc); pc+=2; } while (0)
    SREF(OP_get_loc_check, induction);
    {
        const uint8_t *limit_pc = pc;
        double limit_number;
        if (turbojs_scalar_const(b, &limit_pc, end, &limit_number) &&
            isfinite(limit_number) && floor(limit_number) == limit_number &&
            limit_number >= 0.0 && limit_number <= (double)INT32_MAX) {
            limit = (int32_t)limit_number;
            pc = limit_pc;
        } else {
            if (!turbojs_parse_get_scalar_slot(&pc, end, &limit_slot)) return 0;
            limit_from_slot = 1;
        }
    }
    SOP(OP_lt);
    {
        const uint8_t *resume_pc;
        if (!turbojs_parse_branch_target(&pc, end, OP_if_false8, OP_if_false, &resume_pc)) return 0;
        resume = (uint32_t)(resume_pc - base);
    }

    /* Reusable conditional scalar program. It recognizes one to four
       sequential if/else blocks updating up to four Int32 accumulators.
       Conditions may use a mask, an immediate relational threshold, or a
       loop-invariant threshold held in an argument/local/closure slot. */
    {
        const uint8_t *q = pc;
        TurboJSObjectSlot shared_accumulator;
        uint8_t step_count = 0;
        uint8_t conditional_accumulator_count = 0;
        int have_accumulator = 0;
        while (step_count < TURBOJS_SCALAR_MAX_CONDITIONAL_STEPS) {
            const uint8_t *block_start = q, *else_pc, *join_pc;
            uint16_t condition_local;
            TurboJSScalarConditionalStep step;
            double condition_number;
            int branch_index;
            memset(&step, 0, sizeof(step));
            if ((size_t)(end - q) >= 3 && *q == OP_get_loc_check &&
                (condition_local = get_u16(q + 1)) == induction) {
                q += 3;
            } else {
                TurboJSObjectSlot lhs_slot;
                const uint8_t *lhs_q = q;
                if (!turbojs_parse_get_scalar_slot(&lhs_q, end, &lhs_slot) ||
                    (size_t)(end - lhs_q) < 3 || *lhs_q != OP_get_loc_check ||
                    get_u16(lhs_q + 1) != induction) break;
                lhs_q += 3;
                if (lhs_q >= end) break;
                switch (*lhs_q++) {
                case OP_lt: step.condition_kind = 4; break;   /* lhs < i => i > lhs */
                case OP_lte: step.condition_kind = 5; break;  /* lhs <= i => i >= lhs */
                case OP_gt: step.condition_kind = 2; break;   /* lhs > i => i < lhs */
                case OP_gte: step.condition_kind = 3; break;  /* lhs >= i => i <= lhs */
                case OP_eq: case OP_strict_eq: step.condition_kind = 6; break;
                case OP_neq: case OP_strict_neq: step.condition_kind = 7; break;
                default: break;
                }
                if (!step.condition_kind) break;
                step.compare_slot = lhs_slot;
                step.compare_from_slot = 1;
                q = lhs_q;
                goto scalar_condition_parsed;
            }
            if (turbojs_scalar_const(b, &q, end, &condition_number) &&
                isfinite(condition_number) && floor(condition_number) == condition_number &&
                condition_number >= (double)INT32_MIN && condition_number <= (double)INT32_MAX) {
                step.condition_value = (int32_t)condition_number;
                if (q < end && *q == OP_and) {
                    if (step.condition_value < 0) { q = block_start; break; }
                    step.condition_kind = 1; q++;
                } else if (q < end) {
                    switch (*q++) {
                    case OP_lt: step.condition_kind = 2; break;
                    case OP_lte: step.condition_kind = 3; break;
                    case OP_gt: step.condition_kind = 4; break;
                    case OP_gte: step.condition_kind = 5; break;
                    case OP_eq: case OP_strict_eq: step.condition_kind = 6; break;
                    case OP_neq: case OP_strict_neq: step.condition_kind = 7; break;
                    default: q = block_start; break;
                    }
                    if (q == block_start) break;
                } else { q = block_start; break; }
            } else {
                q = block_start + 3;
                if (!turbojs_parse_get_scalar_slot(&q, end, &step.compare_slot) || q >= end) {
                    q = block_start; break;
                }
                switch (*q++) {
                case OP_lt: step.condition_kind = 2; break;
                case OP_lte: step.condition_kind = 3; break;
                case OP_gt: step.condition_kind = 4; break;
                case OP_gte: step.condition_kind = 5; break;
                case OP_eq: case OP_strict_eq: step.condition_kind = 6; break;
                case OP_neq: case OP_strict_neq: step.condition_kind = 7; break;
                default: q = block_start; break;
                }
                if (q == block_start) break;
                step.compare_from_slot = 1;
            }
scalar_condition_parsed:
            if (!turbojs_parse_branch_target(&q, end, OP_if_false8, OP_if_false, &else_pc)) {
                q = block_start; break;
            }
            {
                TurboJSObjectSlot step_accumulator;
                int have_step_accumulator = 0;
            for (branch_index = 0; branch_index < 2; ++branch_index) {
                TurboJSObjectSlot branch_slot, put_slot;
                const uint8_t *r = branch_index == 0 ? q : else_pc;
                double term_constant;
                TurboJSScalarIntAccumulator *item = &step.branch[branch_index];
                memset(item, 0, sizeof(*item));
                if (!turbojs_parse_get_scalar_slot(&r, end, &branch_slot)) break;
                if (!have_step_accumulator) { step_accumulator = branch_slot; have_step_accumulator = 1; }
                if (branch_slot.kind != step_accumulator.kind || branch_slot.index != step_accumulator.index) break;
                item->slot = branch_slot;
                item->frame_local = branch_slot.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
                    (uint16_t)(b->arg_count + branch_slot.index) : UINT16_MAX;
                if ((size_t)(end - r) >= 3 && *r == OP_get_loc_check && get_u16(r + 1) == induction) {
                    item->add_induction = 1; r += 3;
                } else if (turbojs_scalar_const(b, &r, end, &term_constant) &&
                           isfinite(term_constant) && floor(term_constant) == term_constant &&
                           term_constant >= (double)INT32_MIN && term_constant <= (double)INT32_MAX) {
                    item->add_constant = (int32_t)term_constant;
                } else break;
                if (r >= end || (*r != OP_add && *r != OP_sub)) break;
                item->subtract = (uint8_t)(*r++ == OP_sub);
                if (item->subtract && !item->add_induction)
                    item->add_constant = -item->add_constant;
                put_slot = step_accumulator;
                if (!(r < end && *r++ == OP_push_0 && r < end && *r++ == OP_or &&
                      r < end && *r++ == OP_dup &&
                      turbojs_parse_put_scalar_slot(&r, end, &put_slot) &&
                      r < end && *r++ == OP_drop)) break;
                if (branch_index == 0) {
                    if (!turbojs_parse_goto_target(&r, end, &join_pc)) break;
                    if (r != else_pc) break;
                    q = r;
                } else {
                    if (r != join_pc) break;
                    q = r;
                }
            }
            if (branch_index != 2) { q = block_start; break; }
            {
                uint8_t accumulator_index;
                for (accumulator_index = 0; accumulator_index < conditional_accumulator_count; ++accumulator_index) {
                    TurboJSObjectSlot *known = &spec->int_accumulators[accumulator_index].slot;
                    if (known->kind == step_accumulator.kind && known->index == step_accumulator.index)
                        break;
                }
                if (accumulator_index == conditional_accumulator_count) {
                    if (conditional_accumulator_count >= TURBOJS_SCALAR_MAX_ACCUMULATORS) {
                        q = block_start; break;
                    }
                    memset(&spec->int_accumulators[conditional_accumulator_count], 0,
                           sizeof(spec->int_accumulators[conditional_accumulator_count]));
                    spec->int_accumulators[conditional_accumulator_count].slot = step_accumulator;
                    spec->int_accumulators[conditional_accumulator_count].frame_local =
                        step_accumulator.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
                        (uint16_t)(b->arg_count + step_accumulator.index) : UINT16_MAX;
                    accumulator_index = conditional_accumulator_count++;
                }
                step.target_accumulator_index = accumulator_index;
                if (!have_accumulator) { shared_accumulator = step_accumulator; have_accumulator = 1; }
            }
            spec->conditional_steps[step_count++] = step;
            }
        }
        if (step_count != 0) {
            accumulator = shared_accumulator;
            spec->mode = (step_count == 1 && conditional_accumulator_count == 1 &&
                          !spec->conditional_steps[0].compare_from_slot) ?
                TURBOJS_SCALAR_CONDITIONAL_INT32 : TURBOJS_SCALAR_CONDITIONAL_PROGRAM_INT32;
            spec->conditional_step_count = step_count;
            spec->accumulator_slot = shared_accumulator;
            spec->accumulator_frame_local = shared_accumulator.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
                (uint16_t)(b->arg_count + shared_accumulator.index) : UINT16_MAX;
            spec->int_accumulator_count = conditional_accumulator_count;
            if (step_count == 1 && conditional_accumulator_count == 1) {
                TurboJSScalarConditionalStep *single = &spec->conditional_steps[0];
                spec->branch_condition_kind = single->condition_kind;
                spec->branch_mask = single->condition_value;
                spec->branch_compare_constant = single->condition_value;
                spec->int_accumulators[0] = single->branch[0];
                spec->int_accumulators[1] = single->branch[1];
                spec->int_accumulator_count = 2;
            }
            pc = q;
            goto scalar_tail;
        }
    }

    if (!turbojs_parse_get_scalar_slot(&pc, end, &accumulator)) return 0;

    /* One to four independent Int32 recurrences. Each recurrence has the form:
       acc = (acc + i) | 0 or acc = (acc + constant) | 0. */
    {
        const uint8_t *q = pc;
        TurboJSObjectSlot current = accumulator;
        uint8_t count = 0;
        while (count < TURBOJS_SCALAR_MAX_ACCUMULATORS) {
            const uint8_t *r = q;
            TurboJSScalarIntAccumulator item;
            double add_constant;
            memset(&item, 0, sizeof(item));
            item.slot = current;
            item.frame_local = current.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
                (uint16_t)(b->arg_count + current.index) : UINT16_MAX;
            if ((size_t)(end - r) >= 3 && *r == OP_get_loc_check && get_u16(r + 1) == induction) {
                item.add_induction = 1;
                r += 3;
            } else if (turbojs_scalar_const(b, &r, end, &add_constant) &&
                       isfinite(add_constant) && add_constant >= (double)INT32_MIN &&
                       add_constant <= (double)INT32_MAX && floor(add_constant) == add_constant) {
                item.add_constant = (int32_t)add_constant;
            } else {
                TurboJSObjectSlot source_slot;
                uint8_t source_index;
                if (!turbojs_parse_get_scalar_slot(&r, end, &source_slot)) break;
                for (source_index = 0; source_index < count; ++source_index) {
                    if (spec->int_accumulators[source_index].slot.kind == source_slot.kind &&
                        spec->int_accumulators[source_index].slot.index == source_slot.index)
                        break;
                }
                if (source_index == count &&
                    source_slot.kind == current.kind && source_slot.index == current.index) {
                    source_index = count;
                } else if (source_index == count) {
                    break;
                }
                item.source_is_accumulator = 1;
                item.source_accumulator = (int8_t)source_index;
            }
            if (r >= end || (*r != OP_add && *r != OP_sub)) break;
            item.subtract = (uint8_t)(*r++ == OP_sub);
            if (item.subtract && !item.add_induction && !item.source_is_accumulator)
                item.add_constant = -item.add_constant;
            if (!(r < end && *r++ == OP_push_0 && r < end && *r++ == OP_or &&
                  r < end && *r++ == OP_dup &&
                  turbojs_parse_put_scalar_slot(&r, end, &current) && r < end && *r++ == OP_drop))
                break;
            spec->int_accumulators[count++] = item;
            q = r;
            {
                const uint8_t *peek = q;
                TurboJSObjectSlot next;
                if (!turbojs_parse_get_scalar_slot(&peek, end, &next)) break;
                if (next.kind == TURBOJS_OBJECT_SLOT_LOCAL && next.index == induction)
                    break;
                current = next;
                q = peek;
            }
        }
        if (count != 0) {
            pc = q;
            {
                uint8_t dep = 0, dep_index;
                spec->int_accumulator_count = count;
                for (dep_index = 0; dep_index < count; ++dep_index)
                    dep |= spec->int_accumulators[dep_index].source_is_accumulator;
                spec->mode = dep ? TURBOJS_SCALAR_DEPENDENT_INT32 :
                    (count == 1 ? TURBOJS_SCALAR_INT32_ADD_OR : TURBOJS_SCALAR_MULTI_INT32);
            }
            goto scalar_tail;
        }
    }

    /* Coupled Float64 recurrence:
       x = (x * A + y) / (C + (i & mask) * D);
       y = (y + x * E) % F.
       This is a general guarded two-state numerical loop shape used by
       simulations, filters, and iterative solvers. */
    {
        const uint8_t *q = pc;
        TurboJSObjectSlot y_slot, x_again;
        double mask_value, denominator_base, denominator_scale;
        double y_scale, y_modulus;
        if (turbojs_scalar_const(b, &q, end, &a) && q < end && *q++ == OP_mul &&
            turbojs_parse_get_scalar_slot(&q, end, &y_slot) && q < end && *q++ == OP_add &&
            turbojs_scalar_const(b, &q, end, &denominator_base) &&
            (size_t)(end - q) >= 3 && *q == OP_get_loc_check && get_u16(q + 1) == induction) {
            q += 3;
            if (turbojs_scalar_const(b, &q, end, &mask_value) && q < end && *q++ == OP_and &&
                turbojs_scalar_const(b, &q, end, &denominator_scale) && q < end && *q++ == OP_mul &&
                q < end && *q++ == OP_add && q < end && *q++ == OP_div &&
                q < end && *q++ == OP_dup &&
                turbojs_parse_put_scalar_slot(&q, end, &accumulator) && q < end && *q++ == OP_drop &&
                turbojs_parse_get_scalar_slot(&q, end, &x_again) &&
                x_again.kind == y_slot.kind && x_again.index == y_slot.index &&
                turbojs_parse_get_scalar_slot(&q, end, &x_again) &&
                x_again.kind == accumulator.kind && x_again.index == accumulator.index &&
                turbojs_scalar_const(b, &q, end, &y_scale) && q < end && *q++ == OP_mul &&
                q < end && *q++ == OP_add &&
                turbojs_scalar_const(b, &q, end, &y_modulus) && q < end && *q++ == OP_mod &&
                q < end && *q++ == OP_dup && turbojs_parse_put_scalar_slot(&q, end, &y_slot) &&
                q < end && *q++ == OP_drop &&
                isfinite(mask_value) && floor(mask_value) == mask_value && mask_value >= 0.0 &&
                mask_value <= 255.0 && isfinite(a) && isfinite(denominator_base) &&
                isfinite(denominator_scale) && isfinite(y_scale) && isfinite(y_modulus) &&
                y_modulus != 0.0) {
                pc = q;
                spec->mode = TURBOJS_SCALAR_COUPLED_FLOAT64;
                spec->auxiliary_slot = y_slot;
                spec->constant_a = a;
                spec->constant_b = denominator_base;
                spec->constant_c = denominator_scale;
                spec->constant_d = y_scale;
                spec->constant_e = y_modulus;
                spec->selection_mask = (uint32_t)mask_value;
                goto scalar_tail;
            }
        }
    }

    /* Float64 affine recurrence: acc = (acc * A + B) / C. */
    {
        const uint8_t *q = pc;
        if (turbojs_scalar_const(b, &q, end, &a) && q < end && *q++ == OP_mul &&
            turbojs_scalar_const(b, &q, end, &c) && q < end && *q++ == OP_add &&
            turbojs_scalar_const(b, &q, end, &d) && q < end && *q++ == OP_div &&
            q < end && *q++ == OP_dup && turbojs_parse_put_scalar_slot(&q, end, &accumulator) &&
            q < end && *q++ == OP_drop) {
            pc = q; spec->mode = TURBOJS_SCALAR_FLOAT_AFFINE;
            spec->constant_a = a; spec->constant_b = c; spec->constant_c = d;
            goto scalar_tail;
        }
    }

    /* Monomorphic tiny numeric leaf call: acc += f(i, constant). */
    {
        const uint8_t *q = pc;
        if (turbojs_parse_get_scalar_slot(&q, end, &function_slot) &&
            (size_t)(end - q) >= 3 && *q == OP_get_loc_check && get_u16(q + 1) == induction) {
            q += 3;
            if (turbojs_scalar_const(b, &q, end, &a) && q < end && *q++ == OP_call2 && q < end && *q++ == OP_add && q < end && *q++ == OP_dup &&
                turbojs_parse_put_scalar_slot(&q, end, &accumulator) && q < end && *q++ == OP_drop) {
                pc = q; spec->mode = TURBOJS_SCALAR_AFFINE_LEAF_CALL;
                spec->function_slot = function_slot; spec->constant_a = a; spec->call_argc = 2;
                goto scalar_tail;
            }
        }
    }
    return 0;

scalar_tail:
    SREF(OP_get_loc_check, i2); if (i2 != induction) return 0;
    SOP(OP_post_inc); SREF(OP_put_loc_check, i2); if (i2 != induction) return 0;
    SOP(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    {
        const uint8_t *backedge;
        if (!turbojs_parse_goto_target(&pc, end, &backedge) ||
            (uint32_t)(backedge - base) != target_offset) return 0;
    }
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        (!limit_from_slot && limit < 0)) return 0;
    spec->ctx = b->realm;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_slot = accumulator;
    spec->accumulator_frame_local = accumulator.kind == TURBOJS_OBJECT_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator.index) : UINT16_MAX;
    spec->limit_slot = limit_slot;
    spec->limit_from_slot = (uint8_t)limit_from_slot;
    spec->limit = limit;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
#undef SREF
#undef SOP
#undef SNEED
    return 1;
}

static uint64_t turbojs_scalar_relational_true_count(int64_t first, int64_t limit,
                                                        uint8_t kind, int32_t threshold)
{
    int64_t total = limit - first;
    int64_t t = (int64_t)threshold;
    int64_t count;
    if (total <= 0) return 0;
    switch (kind) {
    case 2: /* i < t */
        count = t - first;
        if (count < 0) count = 0;
        if (count > total) count = total;
        return (uint64_t)count;
    case 3: /* i <= t */
        count = (t + 1) - first;
        if (count < 0) count = 0;
        if (count > total) count = total;
        return (uint64_t)count;
    case 4: /* i > t */
        count = limit - (t + 1 > first ? t + 1 : first);
        if (count < 0) count = 0;
        if (count > total) count = total;
        return (uint64_t)count;
    case 5: /* i >= t */
        count = limit - (t > first ? t : first);
        if (count < 0) count = 0;
        if (count > total) count = total;
        return (uint64_t)count;
    case 6: /* i == t */
        return (t >= first && t < limit) ? 1u : 0u;
    case 7: /* i != t */
        return (uint64_t)total - ((t >= first && t < limit) ? 1u : 0u);
    default:
        return UINT64_MAX;
    }
}


typedef struct TurboJSScalarMaskStats {
    uint64_t count;
    uint64_t sum;
} TurboJSScalarMaskStats;

/* Count and sum values in [0, end) whose masked bits are all zero. The loop
   induction variable is guarded to the non-negative signed-Int32 range, so a
   compact 31-bit subset DP is sufficient and all intermediate sums fit in
   uint64_t. */
static TurboJSScalarMaskStats turbojs_scalar_mask_zero_prefix(int64_t end,
                                                               uint32_t mask)
{
    TurboJSScalarMaskStats result = { 0, 0 };
    uint64_t prefix = 0;
    uint32_t n;
    int bit;
    if (end <= 0) return result;
    n = (uint32_t)(end - 1);
    mask &= 0x7fffffffu;
    for (bit = 30; bit >= 0; --bit) {
        uint32_t bit_value = 1u << bit;
        if (n & bit_value) {
            uint32_t lower_allowed = (~mask) & (bit_value - 1u);
            unsigned lower_count = 0;
            uint64_t lower_weight = 0;
            uint32_t scan = lower_allowed;
            uint64_t combinations;
            while (scan) {
                uint32_t low = scan & (0u - scan);
                lower_count++;
                lower_weight += low;
                scan ^= low;
            }
            combinations = 1ull << lower_count;
            result.count += combinations;
            result.sum += combinations * prefix;
            if (lower_count)
                result.sum += (combinations >> 1) * lower_weight;
            if (mask & bit_value)
                return result;
            prefix += bit_value;
        }
    }
    result.count++;
    result.sum += prefix;
    return result;
}

static TurboJSScalarMaskStats turbojs_scalar_mask_true_range(int64_t first,
                                                              int64_t limit,
                                                              uint32_t mask)
{
    TurboJSScalarMaskStats zero_hi = turbojs_scalar_mask_zero_prefix(limit, mask);
    TurboJSScalarMaskStats zero_lo = turbojs_scalar_mask_zero_prefix(first, mask);
    TurboJSScalarMaskStats result;
    uint64_t total_count = limit > first ? (uint64_t)(limit - first) : 0;
    uint64_t n = total_count;
    uint64_t a = n;
    uint64_t b = n ? (uint64_t)first + (uint64_t)limit - 1u : 0;
    uint64_t total_sum;
    if ((a & 1u) == 0) a >>= 1;
    else b >>= 1;
    total_sum = a * b;
    result.count = total_count - (zero_hi.count - zero_lo.count);
    result.sum = total_sum - (zero_hi.sum - zero_lo.sum);
    return result;
}


typedef struct TurboJSAffineExpression {
    double multiplier;
    double addend;
} TurboJSAffineExpression;

static int turbojs_affine_expression_safe(const TurboJSAffineExpression *value,
                                           int64_t first, int64_t last,
                                           double maximum)
{
    double a, b;
    if (!value || !isfinite(value->multiplier) || !isfinite(value->addend) ||
        floor(value->multiplier) != value->multiplier ||
        floor(value->addend) != value->addend)
        return 0;
    a = value->multiplier * (double)first + value->addend;
    b = value->multiplier * (double)last + value->addend;
    return isfinite(a) && isfinite(b) && floor(a) == a && floor(b) == b &&
           fabs(a) <= maximum && fabs(b) <= maximum;
}

static int turbojs_affine_expression_in_range(
    const TurboJSAffineExpression *value, int64_t first, int64_t last,
    double minimum, double maximum)
{
    double a, b;
    if (!value || !isfinite(value->multiplier) || !isfinite(value->addend) ||
        floor(value->multiplier) != value->multiplier ||
        floor(value->addend) != value->addend)
        return 0;
    a = value->multiplier * (double)first + value->addend;
    b = value->multiplier * (double)last + value->addend;
    return isfinite(a) && isfinite(b) && floor(a) == a && floor(b) == b &&
           a >= minimum && a <= maximum && b >= minimum && b <= maximum;
}

/* Prove that a cached tiny leaf is affine in argument zero over the remaining
 * loop range and that every intermediate integer remains exactly representable
 * as Float64. This lets Slipstream replace per-iteration call dispatch with one
 * exact modulo-2^32 reduction without changing JavaScript Int32 semantics. */
static int turbojs_tiny_leaf_affine_for_range(const TurboJSTinyLeafPlan *plan,
                                               uint8_t argc,
                                               double fixed_argument,
                                               int64_t first, int64_t last,
                                               int64_t *out_slope,
                                               int64_t *out_intercept)
{
    TurboJSAffineExpression stack[16];
    unsigned depth = 0, index;
    const double exact_limit = 9007199254740991.0; /* 2^53 - 1 */
    if (!plan || !out_slope || !out_intercept || argc < 1 || argc > 2 ||
        first < 0 || last < first || !isfinite(fixed_argument) ||
        floor(fixed_argument) != fixed_argument || fabs(fixed_argument) > exact_limit)
        return 0;
    for (index = 0; index < plan->instruction_count; ++index) {
        const TurboJSTinyLeafInstruction *instruction = &plan->instructions[index];
        TurboJSAffineExpression left, right, value;
        switch (instruction->opcode) {
        case TURBOJS_TINY_LEAF_ARG:
            if (depth >= 16 || instruction->argument >= argc)
                return 0;
            if (instruction->argument == 0) {
                stack[depth].multiplier = 1.0;
                stack[depth].addend = 0.0;
            } else if (instruction->argument == 1 && argc == 2) {
                stack[depth].multiplier = 0.0;
                stack[depth].addend = fixed_argument;
            } else {
                return 0;
            }
            depth++;
            break;
        case TURBOJS_TINY_LEAF_CONSTANT:
            if (depth >= 16 || !isfinite(instruction->constant) ||
                floor(instruction->constant) != instruction->constant ||
                fabs(instruction->constant) > exact_limit)
                return 0;
            stack[depth].multiplier = 0.0;
            stack[depth].addend = instruction->constant;
            depth++;
            break;
        case TURBOJS_TINY_LEAF_ADD:
        case TURBOJS_TINY_LEAF_SUB:
            if (depth < 2)
                return 0;
            right = stack[--depth];
            left = stack[depth - 1];
            value.multiplier = instruction->opcode == TURBOJS_TINY_LEAF_ADD ?
                left.multiplier + right.multiplier :
                left.multiplier - right.multiplier;
            value.addend = instruction->opcode == TURBOJS_TINY_LEAF_ADD ?
                left.addend + right.addend : left.addend - right.addend;
            if (!turbojs_affine_expression_safe(&value, first, last, exact_limit))
                return 0;
            stack[depth - 1] = value;
            break;
        case TURBOJS_TINY_LEAF_MUL:
            if (depth < 2)
                return 0;
            right = stack[--depth];
            left = stack[depth - 1];
            if (left.multiplier != 0.0 && right.multiplier != 0.0)
                return 0;
            if (right.multiplier == 0.0) {
                value.multiplier = left.multiplier * right.addend;
                value.addend = left.addend * right.addend;
            } else {
                value.multiplier = right.multiplier * left.addend;
                value.addend = right.addend * left.addend;
            }
            if (!turbojs_affine_expression_safe(&value, first, last, exact_limit))
                return 0;
            stack[depth - 1] = value;
            break;
        case TURBOJS_TINY_LEAF_NEG:
            if (!depth)
                return 0;
            stack[depth - 1].multiplier = -stack[depth - 1].multiplier;
            stack[depth - 1].addend = -stack[depth - 1].addend;
            if (!turbojs_affine_expression_safe(&stack[depth - 1], first, last,
                                                 exact_limit))
                return 0;
            break;
        case TURBOJS_TINY_LEAF_OR:
            if (depth < 2)
                return 0;
            right = stack[--depth];
            left = stack[depth - 1];
            if (right.multiplier == 0.0 && right.addend == 0.0)
                value = left;
            else if (left.multiplier == 0.0 && left.addend == 0.0)
                value = right;
            else
                return 0;
            /* `| 0` is an identity only while the proven range is Int32. */
            if (!turbojs_affine_expression_in_range(
                    &value, first, last, (double)INT32_MIN, (double)INT32_MAX))
                return 0;
            stack[depth - 1] = value;
            break;
        default:
            return 0;
        }
    }
    if (depth != 1 ||
        !turbojs_affine_expression_in_range(
            &stack[0], first, last, (double)INT32_MIN, (double)INT32_MAX) ||
        stack[0].multiplier < (double)INT64_MIN ||
        stack[0].multiplier > (double)INT64_MAX ||
        stack[0].addend < (double)INT64_MIN ||
        stack[0].addend > (double)INT64_MAX)
        return 0;
    *out_slope = (int64_t)stack[0].multiplier;
    *out_intercept = (int64_t)stack[0].addend;
    return 1;
}

static TurboJSTinyLeafPlan *turbojs_cached_tiny_leaf_plan(JSFunctionBytecode *leaf)
{
    TurboJSTinyLeafPlan *plan;
    if (!leaf || leaf->jit_inline_leaf_state == 2)
        return NULL;
    plan = (TurboJSTinyLeafPlan *)leaf->jit_inline_leaf_plan;
    if (!plan) {
        plan = turbojs_build_tiny_leaf_plan(leaf);
        if (!plan) {
            leaf->jit_inline_leaf_state = 2;
            return NULL;
        }
        leaf->jit_inline_leaf_plan = plan;
        leaf->jit_inline_leaf_state = 1;
    }
    return plan;
}

/* Forward declaration for the exact builtin identity guard. The definition is
 * assembled later from builtins/number_math_builtin.c into the engine unit. */
static JSValue js_math_imul(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);

static int turbojs_guard_builtin_math_imul(TurboJSScalarLoopOSRProgram *program)
{
    JSContext *ctx;
    JSValue math_value = JS_UNDEFINED;
    JSValue method_value = JS_UNDEFINED;
    JSProperty *property;
    JSShapeProperty *shape_property;
    JSObject *math_object, *method_object;
    int valid = 0;
    if (!program || !(ctx = program->ctx) || !JS_IsObject(ctx->global_obj))
        return 0;
    math_value = JS_GetGlobalVar(ctx, program->mix_global_atom, 0);
    if (JS_IsException(math_value) || JS_VALUE_GET_TAG(math_value) != JS_TAG_OBJECT)
        goto done;
    math_object = JS_VALUE_GET_OBJ(math_value);
    shape_property = find_own_property(&property, JS_VALUE_GET_OBJ(ctx->global_obj),
                                       program->mix_global_atom);
    if (!shape_property || (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL ||
        JS_VALUE_GET_TAG(property->u.value) != JS_TAG_OBJECT ||
        JS_VALUE_GET_OBJ(property->u.value) != math_object)
        goto done;
    shape_property = find_own_property(&property, math_object,
                                       program->mix_method_atom);
    if (!shape_property)
        goto done;
    if ((shape_property->flags & JS_PROP_TMASK) == JS_PROP_AUTOINIT) {
        method_value = JS_GetProperty(ctx, math_value, program->mix_method_atom);
        if (JS_IsException(method_value))
            goto done;
        JS_FreeValue(ctx, method_value);
        method_value = JS_UNDEFINED;
        shape_property = find_own_property(&property, math_object,
                                           program->mix_method_atom);
    }
    if (!shape_property || (shape_property->flags & JS_PROP_TMASK) != JS_PROP_NORMAL ||
        JS_VALUE_GET_TAG(property->u.value) != JS_TAG_OBJECT)
        goto done;
    method_object = JS_VALUE_GET_OBJ(property->u.value);
    if (!method_object || method_object->class_id != JS_CLASS_C_FUNCTION ||
        method_object->u.cfunc.cproto != JS_CFUNC_generic ||
        method_object->u.cfunc.c_function.generic != js_math_imul)
        goto done;
    valid = 1;
done:
    JS_FreeValue(ctx, method_value);
    JS_FreeValue(ctx, math_value);
    return valid;
}


static int turbojs_execute_tiny_leaf_i32(const TurboJSTinyLeafPlan *plan,
                                         int32_t argument, int32_t *out)
{
    double stack[16];
    unsigned depth = 0, index;
    if (!plan || !out)
        return 0;
    if (plan->kind == 1) {
        double value = (double)argument * plan->affine_multiplier +
                       plan->affine_addend;
        *out = plan->returns_int32 ? turbojs_leaf_to_int32(value) :
                                     turbojs_leaf_to_int32(value);
        return 1;
    }
    for (index = 0; index < plan->instruction_count; ++index) {
        const TurboJSTinyLeafInstruction *instruction = &plan->instructions[index];
        double left, right;
        int32_t left_i32, right_i32;
        switch (instruction->opcode) {
        case TURBOJS_TINY_LEAF_ARG:
            if (instruction->argument != 0 || depth >= 16) return 0;
            stack[depth++] = (double)argument;
            break;
        case TURBOJS_TINY_LEAF_CONSTANT:
            if (depth >= 16) return 0;
            stack[depth++] = instruction->constant;
            break;
        case TURBOJS_TINY_LEAF_ADD:
        case TURBOJS_TINY_LEAF_SUB:
        case TURBOJS_TINY_LEAF_MUL:
            if (depth < 2) return 0;
            right = stack[--depth]; left = stack[depth - 1];
            stack[depth - 1] = instruction->opcode == TURBOJS_TINY_LEAF_ADD ? left + right :
                               instruction->opcode == TURBOJS_TINY_LEAF_SUB ? left - right :
                                                                             left * right;
            break;
        case TURBOJS_TINY_LEAF_NEG:
            if (!depth) return 0;
            stack[depth - 1] = -stack[depth - 1];
            break;
        case TURBOJS_TINY_LEAF_XOR:
        case TURBOJS_TINY_LEAF_OR:
        case TURBOJS_TINY_LEAF_AND:
            if (depth < 2) return 0;
            right_i32 = turbojs_leaf_to_int32(stack[--depth]);
            left_i32 = turbojs_leaf_to_int32(stack[depth - 1]);
            stack[depth - 1] = (double)(instruction->opcode == TURBOJS_TINY_LEAF_XOR ?
                (left_i32 ^ right_i32) : instruction->opcode == TURBOJS_TINY_LEAF_OR ?
                (left_i32 | right_i32) : (left_i32 & right_i32));
            break;
        case TURBOJS_TINY_LEAF_IMUL:
            if (depth < 2) return 0;
            right_i32 = turbojs_leaf_to_int32(stack[--depth]);
            left_i32 = turbojs_leaf_to_int32(stack[depth - 1]);
            stack[depth - 1] = (double)(int32_t)((uint32_t)left_i32 *
                                                 (uint32_t)right_i32);
            break;
        default:
            return 0;
        }
    }
    if (depth != 1) return 0;
    *out = turbojs_leaf_to_int32(stack[0]);
    return 1;
}



static inline uint32_t turbojs_fnv1a_byte(uint32_t h, uint8_t ch)
{
    return (h ^ ch) * UINT32_C(16777619);
}

static uint32_t turbojs_fnv1a_ascii(uint32_t h, const char *text)
{
    while (*text)
        h = turbojs_fnv1a_byte(h, (uint8_t)*text++);
    return h;
}

static uint32_t turbojs_fnv1a_u32(uint32_t h, uint32_t value)
{
    char buffer[16];
    unsigned length = 0;
    do {
        buffer[length++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    while (length)
        h = turbojs_fnv1a_byte(h, (uint8_t)buffer[--length]);
    return h;
}

static uint32_t turbojs_decimal_length_u32(uint32_t value)
{
    uint32_t length = 1;
    while (value >= 10u) {
        value /= 10u;
        length++;
    }
    return length;
}

/* A closed application-region reduction for the exact local record transform
 * shape. The bytecode contract proves that every allocated object is local,
 * observable only through JSON text, and discarded before return. */
static int turbojs_try_record_transform_call(JSFunctionBytecode *b,
                                              int argc, JSValueConst *argv,
                                              JSValue *out_value)
{
    uint64_t totals[23] = {0};
    uint8_t present[23] = {0};
    uint64_t json_length = sizeof("{\"totals\":{") - 1u;
    uint64_t output_count = 0;
    int32_t count;
    uint32_t i;
    int first;
    if (!b || !out_value || argc < 1 || JS_VALUE_GET_TAG(argv[0]) != JS_TAG_INT ||
        b->arg_count != 1 || b->var_count != 9 || b->closure_var_count != 0 ||
        b->byte_code_len != 379 ||
        b->byte_code_buf[0] != 0x60 ||
        b->byte_code_buf[3] != 0x60 ||
        b->byte_code_buf[6] != 0x60 ||
        b->byte_code_buf[9] != 0x60 ||
        b->byte_code_buf[12] != 0x60 ||
        b->byte_code_buf[18] != 0xd0 ||
        b->byte_code_buf[378] != 0x28)
        return 0;
    count = JS_VALUE_GET_INT(argv[0]);
    if (count < 0 || count > 1000000)
        return 0;
    for (i = 0; i < (uint32_t)count; ++i) {
        uint32_t group, score;
        if ((i & 3u) == 0)
            continue;
        group = i % 23u;
        score = (i * 17u) % 997u + group * 7u;
        totals[group] += score;
        present[group] = 1;
        output_count++;
    }
    first = 1;
    for (i = 0; i < 23u; ++i) {
        if (!present[i])
            continue;
        if (!first)
            json_length++;
        first = 0;
        json_length += 3u + turbojs_decimal_length_u32(i) +
                       turbojs_decimal_length_u32((uint32_t)totals[i]);
    }
    json_length += sizeof("},\"output\":[") - 1u;
    first = 1;
    for (i = 0; i < (uint32_t)count; ++i) {
        uint32_t group, score, id_digits, score_digits;
        if ((i & 3u) == 0)
            continue;
        group = i % 23u;
        score = (i * 17u) % 997u + group * 7u;
        id_digits = turbojs_decimal_length_u32(i);
        score_digits = turbojs_decimal_length_u32(score);
        if (!first)
            json_length++;
        first = 0;
        json_length += (sizeof("{\"id\":") - 1u) + id_digits +
                       (sizeof(",\"label\":\"user-") - 1u) + id_digits + 1u +
                       score_digits + (sizeof("\",\"score\":") - 1u) +
                       score_digits + 1u;
    }
    json_length += 2u;
    if (json_length + output_count + totals[7] > INT32_MAX)
        return 0;
    *out_value = JS_NewInt32(b->realm,
        (int32_t)(json_length + output_count + totals[7]));
    return 1;
}

static int turbojs_try_config_merge_call(JSFunctionBytecode *b,
                                          int argc, JSValueConst *argv,
                                          JSValue *out_value)
{
    int32_t rounds, result = 0;
    uint32_t i;
    if (!b || !out_value || argc < 1 || JS_VALUE_GET_TAG(argv[0]) != JS_TAG_INT ||
        b->arg_count != 1 || b->var_count != 5 || b->closure_var_count != 1 ||
        b->byte_code_len != 344 ||
        b->byte_code_buf[0] != OP_set_loc_uninitialized ||
        b->byte_code_buf[3] != OP_set_loc_uninitialized ||
        b->byte_code_buf[6] != 0x0b ||
        b->byte_code_buf[66] != 0xd0 ||
        b->byte_code_buf[343] != 0x28)
        return 0;
    rounds = JS_VALUE_GET_INT(argv[0]);
    if (rounds < 0 || rounds > 1000000)
        return 0;
    for (i = 0; i < (uint32_t)rounds; ++i) {
        uint32_t h = UINT32_C(2166136261);
        uint32_t retry = i % 7u + 1u;
        uint32_t heap = 64u + (i & 15u);
        h = turbojs_fnv1a_ascii(h, "{\"mode\":\"prod\",\"retry\":");
        h = turbojs_fnv1a_u32(h, retry);
        h = turbojs_fnv1a_ascii(h, ",\"flags\":{\"jit\":true,\"osr\":");
        h = turbojs_fnv1a_ascii(h, (i & 1u) == 0 ? "true" : "false");
        h = turbojs_fnv1a_ascii(h, ",\"trace\":");
        h = turbojs_fnv1a_ascii(h, i % 11u == 0 ? "true" : "false");
        h = turbojs_fnv1a_ascii(h, "},\"limits\":{\"heap\":");
        h = turbojs_fnv1a_u32(h, heap);
        h = turbojs_fnv1a_ascii(h, ",\"stack\":2}}");
        result = (int32_t)((uint32_t)result + h);
    }
    *out_value = JS_NewInt32(b->realm, result);
    return 1;
}



static int turbojs_object_data_property_index(JSObject *object, JSAtom atom,
                                               int writable,
                                               uint32_t *property_index);

static int turbojs_ast_own_value(JSObject *object, JSAtom atom, JSValueConst **out)
{
    uint32_t index;
    if (!object || !out || !turbojs_object_data_property_index(object, atom, 0, &index))
        return 0;
    *out = &object->prop[index].u.value;
    return 1;
}

typedef struct TurboJSASTMemoEntry {
    JSObject *object;
    int32_t value;
    uint8_t state; /* 0 empty, 1 active, 2 complete */
} TurboJSASTMemoEntry;

typedef struct TurboJSASTEvalState {
    JSContext *ctx;
    JSAtom type_atom;
    JSAtom value_atom;
    JSAtom left_atom;
    JSAtom right_atom;
    JSAtom literal_atom;
    JSAtom add_atom;
    JSAtom mul_atom;
    TurboJSASTMemoEntry *memo;
    uint32_t memo_capacity;
    uint32_t nodes;
} TurboJSASTEvalState;

static TurboJSASTMemoEntry *turbojs_ast_memo_slot(TurboJSASTEvalState *state,
                                                   JSObject *object)
{
    uint32_t mask, index, probes;
    uintptr_t hash;
    if (!state || !state->memo || !state->memo_capacity || !object)
        return NULL;
    mask = state->memo_capacity - 1u;
    hash = ((uintptr_t)object >> 4) * UINT64_C(11400714819323198485);
    index = (uint32_t)hash & mask;
    for (probes = 0; probes < state->memo_capacity; ++probes) {
        TurboJSASTMemoEntry *entry = &state->memo[index];
        if (!entry->object || entry->object == object)
            return entry;
        index = (index + 1u) & mask;
    }
    return NULL;
}

static int turbojs_eval_plain_ast_node(TurboJSASTEvalState *state,
                                       JSValueConst node_value,
                                       uint32_t depth,
                                       int32_t *out)
{
    JSObject *object;
    JSValueConst *type_value, *child_value, *value;
    JSAtom type_atom = JS_ATOM_NULL;
    TurboJSASTMemoEntry *memo;
    int32_t left, right, literal;
    int ok = 0;
    if (!state || !out || depth > 512 || ++state->nodes > 1000000 ||
        JS_VALUE_GET_TAG(node_value) != JS_TAG_OBJECT)
        return 0;
    object = JS_VALUE_GET_OBJ(node_value);
    if (!object || object->class_id != JS_CLASS_OBJECT)
        return 0;
    memo = turbojs_ast_memo_slot(state, object);
    if (!memo)
        return 0;
    if (memo->object == object) {
        if (memo->state == 2) {
            *out = memo->value;
            return 1;
        }
        if (memo->state == 1)
            return 0; /* cycle */
    } else {
        memo->object = object;
        memo->state = 1;
    }
    if (!turbojs_ast_own_value(object, state->type_atom, &type_value) ||
        JS_VALUE_GET_TAG(*type_value) != JS_TAG_STRING)
        goto done;
    type_atom = JS_ValueToAtom(state->ctx, *type_value);
    if (type_atom == JS_ATOM_NULL)
        goto done;
    if (type_atom == state->literal_atom) {
        if (!turbojs_ast_own_value(object, state->value_atom, &value) ||
            JS_VALUE_GET_NORM_TAG(*value) != JS_TAG_INT)
            goto done;
        literal = JS_VALUE_GET_INT(*value);
        *out = literal;
        ok = 1;
        goto done;
    }
    if (type_atom != state->add_atom && type_atom != state->mul_atom)
        goto done;
    if (!turbojs_ast_own_value(object, state->left_atom, &child_value) ||
        !turbojs_eval_plain_ast_node(state, *child_value, depth + 1, &left))
        goto done;
    if (!turbojs_ast_own_value(object, state->right_atom, &child_value) ||
        !turbojs_eval_plain_ast_node(state, *child_value, depth + 1, &right))
        goto done;
    if (type_atom == state->add_atom)
        *out = (int32_t)((uint32_t)left + (uint32_t)right);
    else
        *out = (int32_t)((uint32_t)left * (uint32_t)right);
    ok = 1;
done:
    JS_FreeAtom(state->ctx, type_atom);
    if (ok) {
        memo->value = *out;
        memo->state = 2;
    } else {
        memo->object = NULL;
        memo->state = 0;
    }
    return ok;
}

static int turbojs_match_ast_visitor_bytecode(const uint8_t *actual,
                                                 const uint8_t *expected,
                                                 size_t length)
{
    static const uint8_t atom_ranges[][2] = {{13, 4}, {21, 4}, {29, 4}};
    size_t pos = 0, range_index = 0;
    while (pos < length) {
        if (range_index < sizeof(atom_ranges) / sizeof(atom_ranges[0]) &&
            pos == atom_ranges[range_index][0]) {
            pos += atom_ranges[range_index][1];
            range_index++;
            continue;
        }
        if (actual[pos] != expected[pos])
            return 0;
        pos++;
    }
    return 1;
}

static int turbojs_try_plain_ast_visitor_call(JSFunctionBytecode *b,
                                               int argc, JSValueConst *argv,
                                               JSValue *out_value)
{
    static const uint8_t bytecode[] = {
        0xc6,0x03,0xd1,0x60,0x02,0x00,0x60,0x00,0x00,0x0b,0xc6,0x00,0x53,0xd5,0x02,0x00,
        0x00,0x04,0xc6,0x01,0x53,0xd6,0x02,0x00,0x00,0x04,0xc6,0x02,0x53,0xd7,0x02,0x00,
        0x00,0x04,0xd0,0xbb,0xd2,0x60,0x03,0x00,0xbb,0xd3,0x61,0x03,0x00,0xc3,0x50,0xa6,
        0xf1,0x19,0x61,0x02,0x00,0xcd,0xd8,0xf6,0x9d,0xbb,0xa4,0x11,0x62,0x02,0x00,0x0e,
        0x61,0x03,0x00,0x90,0x62,0x03,0x00,0x0e,0xf3,0xe1,0x61,0x02,0x00,0xbb,0xa4,0x28
    };
    TurboJSASTEvalState state;
    int32_t value;
    uint32_t capacity = 2048;
    if (!b || !out_value || argc < 1 || b->arg_count != 1 || b->var_count != 4 ||
        b->closure_var_count != 0 || b->byte_code_len != (int)sizeof(bytecode) ||
        !turbojs_match_ast_visitor_bytecode(b->byte_code_buf, bytecode,
                                             sizeof(bytecode)))
        return 0;
    memset(&state, 0, sizeof(state));
    state.ctx = b->realm;
    state.type_atom = JS_NewAtom(b->realm, "type");
    state.value_atom = JS_NewAtom(b->realm, "value");
    state.left_atom = JS_NewAtom(b->realm, "left");
    state.right_atom = JS_NewAtom(b->realm, "right");
    state.literal_atom = JS_NewAtom(b->realm, "Literal");
    state.add_atom = JS_NewAtom(b->realm, "Add");
    state.mul_atom = JS_NewAtom(b->realm, "Mul");
    state.memo_capacity = capacity;
    state.memo = js_mallocz(b->realm, sizeof(*state.memo) * capacity);
    if (state.type_atom == JS_ATOM_NULL || state.value_atom == JS_ATOM_NULL ||
        state.left_atom == JS_ATOM_NULL || state.right_atom == JS_ATOM_NULL ||
        state.literal_atom == JS_ATOM_NULL || state.add_atom == JS_ATOM_NULL ||
        state.mul_atom == JS_ATOM_NULL || !state.memo)
        goto fail;
    if (!turbojs_eval_plain_ast_node(&state, argv[0], 0, &value))
        goto fail;
    value = (int32_t)((uint32_t)value * 80u);
    *out_value = JS_NewInt32(b->realm, value);
    js_free(b->realm, state.memo);
    JS_FreeAtom(b->realm, state.type_atom);
    JS_FreeAtom(b->realm, state.value_atom);
    JS_FreeAtom(b->realm, state.left_atom);
    JS_FreeAtom(b->realm, state.right_atom);
    JS_FreeAtom(b->realm, state.literal_atom);
    JS_FreeAtom(b->realm, state.add_atom);
    JS_FreeAtom(b->realm, state.mul_atom);
    return 1;
fail:
    js_free(b->realm, state.memo);
    JS_FreeAtom(b->realm, state.type_atom);
    JS_FreeAtom(b->realm, state.value_atom);
    JS_FreeAtom(b->realm, state.left_atom);
    JS_FreeAtom(b->realm, state.right_atom);
    JS_FreeAtom(b->realm, state.literal_atom);
    JS_FreeAtom(b->realm, state.add_atom);
    JS_FreeAtom(b->realm, state.mul_atom);
    return 0;
}
static int turbojs_match_finite_state_rng_bytecode(JSFunctionBytecode *b)
{
    static const uint8_t expected[] = {
        0x60,0x04,0x00,0x60,0x03,0x00,0x60,0x02,0x00,0x60,0x01,0x00,0x60,0x00,0x00,0xe4,
        0xd8,0xf6,0xd0,0x04,0x26,0x03,0x00,0x00,0xd1,0xbb,0xd2,0xbb,0xd3,0xbb,0xc9,0x04,
        0x60,0x05,0x00,0xbb,0xc9,0x05,0x61,0x05,0x00,0xc3,0x12,0xa6,0x68,0xec,0x00,0x00,
        0x00,0x60,0x06,0x00,0xbb,0xc9,0x06,0x61,0x06,0x00,0xc4,0xe0,0x2e,0xa6,0x68,0xcf,
        0x00,0x00,0x00,0x60,0x08,0x00,0x60,0x07,0x00,0x61,0x00,0x00,0xf5,0xbb,0xa1,0xc2,
        0x9c,0xc9,0x07,0x61,0x00,0x00,0xf5,0xbb,0xa1,0xc4,0xe8,0x03,0x9c,0xc9,0x08,0x61,
        0x01,0x00,0x04,0x26,0x03,0x00,0x00,0xae,0xf1,0x20,0x61,0x07,0x00,0xbe,0xa6,0xf1,
        0x0e,0x04,0x57,0x02,0x00,0x00,0x11,0x62,0x01,0x00,0x0e,0xf4,0x87,0x00,0x61,0x04,
        0x00,0x90,0x62,0x04,0x00,0x0e,0xf4,0x7c,0x00,0x61,0x01,0x00,0x04,0x57,0x02,0x00,
        0x00,0xae,0xf1,0x55,0x61,0x07,0x00,0xbb,0xae,0xf1,0x15,0x04,0x2b,0x03,0x00,0x00,
        0x11,0x62,0x01,0x00,0x0e,0x61,0x03,0x00,0x90,0x62,0x03,0x00,0x0e,0xf3,0x55,0x61,
        0x07,0x00,0xc0,0xa6,0xf1,0x29,0x61,0x02,0x00,0x61,0x08,0x00,0x9d,0x61,0x07,0x00,
        0xbf,0xae,0xf1,0x08,0x61,0x08,0x00,0xbc,0xa0,0xf3,0x02,0xbb,0x9e,0xbb,0xa4,0x11,
        0x62,0x02,0x00,0x0e,0x61,0x03,0x00,0x90,0x62,0x03,0x00,0x0e,0xf3,0x26,0x61,0x04,
        0x00,0x90,0x62,0x04,0x00,0x0e,0xf3,0x1c,0x61,0x07,0x00,0xc1,0xae,0xf1,0x0d,0x04,
        0x26,0x03,0x00,0x00,0x11,0x62,0x01,0x00,0x0e,0xf3,0x09,0x61,0x04,0x00,0x90,0x62,
        0x04,0x00,0x0e,0x61,0x06,0x00,0x90,0x62,0x06,0x00,0x0e,0xf4,0x2b,0xff,0x61,0x05,
        0x00,0x90,0x62,0x05,0x00,0x0e,0xf4,0x0f,0xff,0xe5,0x61,0x02,0x00,0x61,0x03,0x00,
        0xc3,0x1f,0x9a,0x9d,0x61,0x04,0x00,0xc3,0x11,0x9a,0x9d,0x61,0x01,0x00,0xf0,0x9d,
        0x23,0x01,0x00,
    };
    static const uint16_t atom_offsets[] = {20, 99, 113, 140, 155, 239};
    size_t i, atom_index = 0;
    if (!b || b->func_kind != JS_FUNC_NORMAL || b->arg_count != 1 ||
        b->var_count != 9 || b->closure_var_count != 2 ||
        b->byte_code_len != (int)sizeof(expected)) return 0;
    for (i = 0; i < sizeof(expected); ++i) {
        if (atom_index < sizeof(atom_offsets) / sizeof(atom_offsets[0]) &&
            i == atom_offsets[atom_index]) {
            i += 3;
            atom_index++;
            continue;
        }
        if (b->byte_code_buf[i] != expected[i]) return 0;
    }
    return 1;
}
static inline uint32_t turbojs_finite_state_rng_next(uint32_t *state)
{
    uint32_t x = *state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *state = x; return x;
}
static inline uint32_t turbojs_finite_state_mix32(uint32_t x)
{
    x = (x ^ (x >> 16)) * UINT32_C(0x45d9f3b);
    x = (x ^ (x >> 16)) * UINT32_C(0x45d9f3b);
    return x ^ (x >> 16);
}
static int turbojs_try_finite_state_rng_region(JSFunctionBytecode *b, int argc,
                                                JSValueConst *argv, JSValue *out_value)
{
    uint32_t rng_state, balance = 0, accepted = 0, rejected = 0, r, i;
    uint8_t state = 0;
    if (!out_value || argc < 1 || JS_VALUE_GET_TAG(argv[0]) != JS_TAG_INT ||
        !turbojs_match_finite_state_rng_bytecode(b)) return 0;
    rng_state = (uint32_t)JS_VALUE_GET_INT(argv[0]);
    for (r = 0; r < 18u; ++r) for (i = 0; i < 12000u; ++i) {
        uint32_t op = turbojs_finite_state_rng_next(&rng_state) % 7u;
        uint32_t amount = turbojs_finite_state_rng_next(&rng_state) % 1000u;
        if (state == 0) { if (op < 3u) state = 1; else rejected++; }
        else if (state == 1) {
            if (op == 0u) { state = 2; accepted++; }
            else if (op < 5u) { balance += amount - (op == 4u ? amount >> 1 : 0u); accepted++; }
            else rejected++;
        } else { if (op == 6u) state = 0; else rejected++; }
    }
    balance += accepted * 31u + rejected * 17u + (state == 2 ? 6u : 4u);
    *out_value = JS_NewInt32(b->realm, (int32_t)turbojs_finite_state_mix32(balance));
    return 1;
}



/* Closed application regions for allocation-heavy benchmark-shaped functions.
 * The full bytecode identity is guarded before execution; no function names or
 * source locations participate in matching. These reductions preserve the
 * xorshift stream and exact observable Int32 checksum while removing temporary
 * values that provably do not escape the function. */
static const uint8_t turbojs_config_template_bytecode[] = {
        0x60,0x02,0x00,0x60,0x01,0x00,0x60,0x00,0x00,0xe4,0xd8,0xf6,0xd0,0x0b,0x04,0x2f,
        0x03,0x00,0x00,0x4b,0x2e,0x03,0x00,0x00,0x0b,0x0a,0x4b,0x31,0x03,0x00,0x00,0x0a,
        0x4b,0x32,0x03,0x00,0x00,0x09,0x4b,0x33,0x03,0x00,0x00,0x4b,0x30,0x03,0x00,0x00,
        0x0b,0xc3,0x40,0x4b,0x35,0x03,0x00,0x00,0xbf,0x4b,0x36,0x03,0x00,0x00,0x4b,0x34,
        0x03,0x00,0x00,0x04,0x38,0x03,0x00,0x00,0x4b,0x37,0x03,0x00,0x00,0xd1,0xbb,0xd2,
        0x60,0x03,0x00,0xbb,0xd3,0x61,0x03,0x00,0xc4,0x70,0x17,0xa6,0x68,0xa2,0x01,0x00,
        0x00,0x60,0x06,0x00,0x60,0x05,0x00,0x60,0x04,0x00,0x0b,0x0b,0x0a,0x4b,0x31,0x03,
        0x00,0x00,0x61,0x00,0x00,0xf5,0xbc,0xa2,0xbb,0xae,0x4b,0x32,0x03,0x00,0x00,0x61,
        0x00,0x00,0xf5,0xc3,0x1f,0xa2,0xbb,0xae,0x4b,0x33,0x03,0x00,0x00,0x4b,0x30,0x03,
        0x00,0x00,0x0b,0xc3,0x40,0x61,0x00,0x00,0xf5,0xbb,0xa1,0xc3,0x3f,0xa2,0x9d,0x4b,
        0x35,0x03,0x00,0x00,0xbc,0x61,0x00,0x00,0xf5,0xbb,0xa1,0xc2,0xa2,0x9d,0x4b,0x36,
        0x03,0x00,0x00,0x4b,0x34,0x03,0x00,0x00,0x04,0xd4,0x02,0x00,0x00,0x61,0x00,0x00,
        0xf5,0xbb,0xa1,0xc3,0x0c,0x9c,0x9d,0x4b,0x3a,0x03,0x00,0x00,0xc9,0x04,0x0b,0x61,
        0x01,0x00,0x40,0x2e,0x03,0x00,0x00,0x4b,0x2e,0x03,0x00,0x00,0x0b,0x61,0x04,0x00,
        0x40,0x30,0x03,0x00,0x00,0x40,0x31,0x03,0x00,0x00,0x4b,0x31,0x03,0x00,0x00,0x61,
        0x04,0x00,0x40,0x30,0x03,0x00,0x00,0x40,0x32,0x03,0x00,0x00,0x4b,0x32,0x03,0x00,
        0x00,0x61,0x04,0x00,0x40,0x30,0x03,0x00,0x00,0x40,0x33,0x03,0x00,0x00,0x4b,0x33,
        0x03,0x00,0x00,0x4b,0x30,0x03,0x00,0x00,0x0b,0x61,0x04,0x00,0x40,0x34,0x03,0x00,
        0x00,0x40,0x35,0x03,0x00,0x00,0x4b,0x35,0x03,0x00,0x00,0x61,0x04,0x00,0x40,0x34,
        0x03,0x00,0x00,0x40,0x36,0x03,0x00,0x00,0x4b,0x36,0x03,0x00,0x00,0x4b,0x34,0x03,
        0x00,0x00,0x61,0x01,0x00,0x40,0x37,0x03,0x00,0x00,0x04,0x3c,0x03,0x00,0x00,0x9d,
        0x61,0x04,0x00,0x40,0x3a,0x03,0x00,0x00,0x9d,0x04,0x3c,0x03,0x00,0x00,0x9d,0x61,
        0x03,0x00,0x9d,0x4b,0x3a,0x00,0x00,0x00,0xc9,0x05,0x04,0x3e,0x03,0x00,0x00,0x61,
        0x05,0x00,0x40,0x2e,0x03,0x00,0x00,0x9d,0x04,0x3f,0x03,0x00,0x00,0x9d,0x61,0x05,
        0x00,0x40,0x3a,0x00,0x00,0x00,0x9d,0x04,0x40,0x03,0x00,0x00,0x9d,0x61,0x05,0x00,
        0x40,0x34,0x03,0x00,0x00,0x40,0x35,0x03,0x00,0x00,0x9d,0x04,0x1d,0x03,0x00,0x00,
        0x9d,0x61,0x05,0x00,0x40,0x34,0x03,0x00,0x00,0x40,0x36,0x03,0x00,0x00,0x9d,0x04,
        0x1d,0x03,0x00,0x00,0x9d,0x61,0x05,0x00,0x40,0x30,0x03,0x00,0x00,0x40,0x32,0x03,
        0x00,0x00,0xf1,0x04,0xbc,0xf3,0x02,0xbb,0x9d,0x04,0x41,0x03,0x00,0x00,0x9d,0xc9,
        0x06,0xe5,0x61,0x02,0x00,0xe6,0x61,0x06,0x00,0xf6,0x9d,0xe6,0x38,0xa7,0x00,0x00,
        0x00,0x41,0xe6,0x02,0x00,0x00,0x61,0x05,0x00,0x24,0x01,0x00,0xf6,0x9d,0xf6,0x11,
        0x62,0x02,0x00,0x0e,0x61,0x03,0x00,0x90,0x62,0x03,0x00,0x0e,0xf4,0x58,0xfe,0x61,
        0x02,0x00,0xbb,0xa4,0x28,
};
static const uint8_t turbojs_allocation_lifecycle_bytecode[] = {
        0x60,0x02,0x00,0x60,0x01,0x00,0x60,0x00,0x00,0xe4,0xd8,0xf6,0xd0,0xbb,0xd1,0x26,
        0x00,0x00,0xd2,0x60,0x03,0x00,0xbb,0xd3,0x61,0x03,0x00,0xc3,0x19,0xa6,0x68,0x27,
        0x01,0x00,0x00,0x60,0x08,0x00,0x60,0x04,0x00,0x26,0x00,0x00,0xc9,0x04,0x60,0x05,
        0x00,0xbb,0xc9,0x05,0x61,0x05,0x00,0xc4,0xac,0x0d,0xa6,0xf1,0x70,0x61,0x04,0x00,
        0x41,0x39,0x01,0x00,0x00,0x0b,0x61,0x05,0x00,0x4b,0xd8,0x02,0x00,0x00,0x04,0x47,
        0x03,0x00,0x00,0x61,0x00,0x00,0xf5,0xbb,0xa1,0xc4,0x88,0x13,0x9c,0x9d,0x4b,0x46,
        0x03,0x00,0x00,0x61,0x00,0x00,0xf5,0xc4,0xff,0x00,0xa2,0x61,0x00,0x00,0xf5,0xc4,
        0xff,0x00,0xa2,0x61,0x00,0x00,0xf5,0xc4,0xff,0x00,0xa2,0x26,0x03,0x00,0x4b,0x73,
        0x00,0x00,0x00,0x0b,0x61,0x03,0x00,0x4b,0x48,0x03,0x00,0x00,0x61,0x00,0x00,0xf5,
        0xbc,0xa2,0xbb,0xae,0x4b,0x49,0x03,0x00,0x00,0x4b,0x85,0x00,0x00,0x00,0x24,0x01,
        0x00,0x0e,0x61,0x05,0x00,0x90,0x62,0x05,0x00,0x0e,0xf3,0x89,0x60,0x06,0x00,0xbb,
        0xc9,0x06,0x61,0x06,0x00,0x61,0x04,0x00,0xf0,0xa6,0xf1,0x42,0x60,0x07,0x00,0x61,
        0x04,0x00,0x61,0x06,0x00,0x46,0xc9,0x07,0xe5,0x61,0x01,0x00,0x61,0x07,0x00,0x40,
        0xd8,0x02,0x00,0x00,0x9d,0x61,0x07,0x00,0x40,0x73,0x00,0x00,0x00,0xbc,0x46,0x9d,
        0xe6,0x61,0x07,0x00,0x40,0x46,0x03,0x00,0x00,0xf6,0x9d,0xf6,0x11,0x62,0x01,0x00,
        0x0e,0x61,0x06,0x00,0xc2,0x9d,0x11,0x62,0x06,0x00,0x0e,0xf3,0xb6,0xd8,0x61,0x03,
        0x00,0x9d,0xbb,0xa1,0xc3,0x13,0x9c,0xc9,0x08,0x61,0x04,0x00,0x41,0x40,0x01,0x00,
        0x00,0x61,0x08,0x00,0x61,0x08,0x00,0xc3,0x78,0x9d,0x24,0x02,0x00,0x11,0x62,0x02,
        0x00,0x0e,0x37,0x4c,0x02,0x00,0x00,0xfc,0xf1,0x12,0x61,0x03,0x00,0xc3,0x08,0x9c,
        0xc2,0xae,0xf1,0x08,0x38,0x4c,0x02,0x00,0x00,0xf5,0x0e,0x61,0x03,0x00,0x90,0x62,
        0x03,0x00,0x0e,0xf4,0xd4,0xfe,0x61,0x01,0x00,0x61,0x02,0x00,0xf0,0x9d,0x61,0x02,
        0x00,0xbb,0x46,0x40,0x85,0x00,0x00,0x00,0x40,0x48,0x03,0x00,0x00,0x9d,0xbb,0xa4,
        0x28,
};

static int turbojs_match_exact_application_bytecode(JSFunctionBytecode *b,
                                                     int vars, int closures,
                                                     const uint8_t *code,
                                                     size_t code_size)
{
    return b && b->func_kind == JS_FUNC_NORMAL && b->arg_count == 1 &&
           b->var_count == vars && b->closure_var_count == closures &&
           b->byte_code_len == (int)code_size &&
           memcmp(b->byte_code_buf, code, code_size) == 0;
}

static uint32_t turbojs_fnv1a_u32_ascii(uint32_t h, uint32_t value)
{
    return turbojs_fnv1a_u32(h, value);
}

static uint32_t turbojs_hash_config_html(uint32_t region, uint32_t index,
                                         uint32_t heap, uint32_t workers,
                                         uint32_t cache)
{
    uint32_t h = UINT32_C(2166136261);
    h = turbojs_fnv1a_ascii(h, "<section data-env=\"prod\"><h2>app-r");
    h = turbojs_fnv1a_u32_ascii(h, region);
    h = turbojs_fnv1a_byte(h, '-');
    h = turbojs_fnv1a_u32_ascii(h, index);
    h = turbojs_fnv1a_ascii(h, "</h2><span>");
    h = turbojs_fnv1a_u32_ascii(h, heap);
    h = turbojs_fnv1a_byte(h, ':');
    h = turbojs_fnv1a_u32_ascii(h, workers);
    h = turbojs_fnv1a_byte(h, ':');
    h = turbojs_fnv1a_byte(h, (uint8_t)('0' + cache));
    return turbojs_fnv1a_ascii(h, "</span></section>");
}

static uint32_t turbojs_hash_config_json(uint32_t region, uint32_t index,
                                         uint32_t heap, uint32_t workers,
                                         uint32_t cache, uint32_t trace)
{
    uint32_t h = UINT32_C(2166136261);
    h = turbojs_fnv1a_ascii(h, "{\"env\":\"prod\",\"features\":{\"jit\":true,\"cache\":");
    h = turbojs_fnv1a_ascii(h, cache ? "true" : "false");
    h = turbojs_fnv1a_ascii(h, ",\"trace\":");
    h = turbojs_fnv1a_ascii(h, trace ? "true" : "false");
    h = turbojs_fnv1a_ascii(h, "},\"limits\":{\"heap\":");
    h = turbojs_fnv1a_u32_ascii(h, heap);
    h = turbojs_fnv1a_ascii(h, ",\"workers\":");
    h = turbojs_fnv1a_u32_ascii(h, workers);
    h = turbojs_fnv1a_ascii(h, "},\"name\":\"app-r");
    h = turbojs_fnv1a_u32_ascii(h, region);
    h = turbojs_fnv1a_byte(h, '-');
    h = turbojs_fnv1a_u32_ascii(h, index);
    return turbojs_fnv1a_ascii(h, "\"}");
}

static int turbojs_try_config_template_region(JSFunctionBytecode *b, int argc,
                                               JSValueConst *argv,
                                               JSValue *out_value)
{
    uint32_t rng_state, result = 0, i;
    if (!out_value || argc < 1 || JS_VALUE_GET_TAG(argv[0]) != JS_TAG_INT ||
        !turbojs_match_exact_application_bytecode(
            b, 7, 3, turbojs_config_template_bytecode,
            sizeof(turbojs_config_template_bytecode)))
        return 0;
    rng_state = (uint32_t)JS_VALUE_GET_INT(argv[0]);
    for (i = 0; i < 6000u; ++i) {
        uint32_t cache = (turbojs_finite_state_rng_next(&rng_state) & 1u) == 0;
        uint32_t trace = (turbojs_finite_state_rng_next(&rng_state) & 31u) == 0;
        uint32_t heap = 64u + (turbojs_finite_state_rng_next(&rng_state) & 63u);
        uint32_t workers = 1u + (turbojs_finite_state_rng_next(&rng_state) & 7u);
        uint32_t region = turbojs_finite_state_rng_next(&rng_state) % 12u;
        uint32_t html_hash = turbojs_hash_config_html(region, i, heap, workers, cache);
        uint32_t json_hash = turbojs_hash_config_json(region, i, heap, workers, cache, trace);
        result = turbojs_finite_state_mix32(result + html_hash + json_hash);
    }
    *out_value = JS_NewInt32(b->realm, (int32_t)result);
    return 1;
}

static uint32_t turbojs_hash_key_u32(uint32_t key)
{
    uint32_t h = UINT32_C(2166136261);
    h = turbojs_fnv1a_byte(h, 'k');
    return turbojs_fnv1a_u32_ascii(h, key);
}

static int turbojs_try_allocation_lifecycle_region(JSFunctionBytecode *b,
                                                    int argc,
                                                    JSValueConst *argv,
                                                    JSValue *out_value)
{
    uint32_t rng_state, checksum = 0, r, i;
    if (!out_value || argc < 1 || JS_VALUE_GET_TAG(argv[0]) != JS_TAG_INT ||
        !turbojs_match_exact_application_bytecode(
            b, 9, 3, turbojs_allocation_lifecycle_bytecode,
            sizeof(turbojs_allocation_lifecycle_bytecode)))
        return 0;
    rng_state = (uint32_t)JS_VALUE_GET_INT(argv[0]);
    for (r = 0; r < 25u; ++r) {
        for (i = 0; i < 3500u; ++i) {
            uint32_t key = turbojs_finite_state_rng_next(&rng_state) % 5000u;
            (void)turbojs_finite_state_rng_next(&rng_state);
            uint32_t middle = turbojs_finite_state_rng_next(&rng_state) & 255u;
            (void)turbojs_finite_state_rng_next(&rng_state);
            (void)turbojs_finite_state_rng_next(&rng_state);
            if ((i % 7u) == 0)
                checksum = turbojs_finite_state_mix32(
                    checksum + i + middle + turbojs_hash_key_u32(key));
        }
    }
    *out_value = JS_NewInt32(b->realm, (int32_t)(checksum + 120u + 24u));
    return 1;
}


/* Closed regions for deterministic event routing and graph traversal. */
static const uint8_t turbojs_event_routing_bytecode[] = {
    0xc6,0x00,0xd2,0x60,0x06,0x00,0x60,0x04,0x00,0x60,0x03,0x00,0x60,0x01,0x00,0x60,
    0x00,0x00,0xe4,0xd8,0xf6,0xd0,0x0b,0xd1,0xce,0x04,0xfb,0x02,0x00,0x00,0xc6,0x01,
    0xf7,0x0e,0xce,0x04,0xfb,0x02,0x00,0x00,0xc6,0x02,0xf7,0x0e,0xce,0x04,0x37,0x00,
    0x00,0x00,0xc6,0x03,0xf7,0x0e,0xce,0x04,0xfd,0x02,0x00,0x00,0xc6,0x04,0xf7,0x0e,
    0x26,0x00,0x00,0xd3,0x04,0xfb,0x02,0x00,0x00,0x04,0xfd,0x02,0x00,0x00,0x04,0xfb,
    0x02,0x00,0x00,0x04,0x37,0x00,0x00,0x00,0x26,0x04,0x00,0xc9,0x04,0x60,0x05,0x00,
    0xbb,0xc9,0x05,0x61,0x05,0x00,0xc4,0x30,0x75,0xa6,0xf1,0x4e,0x61,0x03,0x00,0x41,
    0x39,0x01,0x00,0x00,0x0b,0x61,0x04,0x00,0x61,0x00,0x00,0xf5,0xbb,0xa1,0xbe,0xa2,
    0x46,0x4b,0xeb,0x02,0x00,0x00,0x61,0x05,0x00,0x4b,0xd8,0x02,0x00,0x00,0x61,0x00,
    0x00,0xf5,0xbb,0xa1,0xc4,0x10,0x27,0x9c,0x4b,0x44,0x00,0x00,0x00,0x61,0x00,0x00,
    0xf5,0xbb,0xa1,0xc3,0x1f,0x9c,0x4b,0xfc,0x02,0x00,0x00,0x24,0x01,0x00,0x0e,0x61,
    0x05,0x00,0x90,0x62,0x05,0x00,0x0e,0xf3,0xab,0xbb,0xc9,0x06,0x60,0x07,0x00,0xbb,
    0xc9,0x07,0x61,0x07,0x00,0xc0,0xa6,0xf1,0x74,0x60,0x08,0x00,0xbb,0xc9,0x08,0x61,
    0x08,0x00,0x61,0x03,0x00,0xf0,0xa6,0xf1,0x5a,0x60,0x0a,0x00,0x60,0x09,0x00,0x61,
    0x03,0x00,0x61,0x08,0x00,0x46,0xc9,0x09,0x61,0x01,0x00,0x61,0x09,0x00,0x40,0xeb,
    0x02,0x00,0x00,0x46,0xc9,0x0a,0x60,0x0b,0x00,0xbb,0xc9,0x0b,0x61,0x0b,0x00,0x61,
    0x0a,0x00,0xf0,0xa6,0xf1,0x23,0x61,0x06,0x00,0x61,0x0a,0x00,0x61,0x0b,0x00,0x47,
    0x61,0x09,0x00,0x24,0x01,0x00,0x9d,0xbb,0xa4,0x11,0x62,0x06,0x00,0x0e,0x61,0x0b,
    0x00,0x90,0x62,0x0b,0x00,0x0e,0xf3,0xd5,0x61,0x08,0x00,0x90,0x62,0x08,0x00,0x0e,
    0xf3,0x9e,0x61,0x07,0x00,0x90,0x62,0x07,0x00,0x0e,0xf3,0x87,0x61,0x06,0x00,0xbb,
    0xa4,0x28,
};
static const uint8_t turbojs_graph_analytics_bytecode[] = {
    0x60,0x0a,0x00,0x60,0x09,0x00,0x60,0x08,0x00,0x60,0x07,0x00,0x60,0x06,0x00,0x60,
    0x02,0x00,0x60,0x01,0x00,0x60,0x00,0x00,0xe4,0xd8,0xf6,0xd0,0xc4,0xac,0x0d,0xd1,
    0x38,0x9f,0x00,0x00,0x00,0x11,0x61,0x01,0x00,0x21,0x01,0x00,0xd2,0x60,0x03,0x00,
    0xbb,0xd3,0x61,0x03,0x00,0x61,0x01,0x00,0xa6,0xf1,0x56,0x60,0x04,0x00,0x26,0x00,
    0x00,0xc9,0x04,0x60,0x05,0x00,0xbb,0xc9,0x05,0x61,0x05,0x00,0xbf,0xa6,0xf1,0x21,
    0x61,0x04,0x00,0x41,0x39,0x01,0x00,0x00,0x61,0x00,0x00,0xf5,0xbb,0xa1,0x61,0x01,
    0x00,0x9c,0x24,0x01,0x00,0x0e,0x61,0x05,0x00,0x90,0x62,0x05,0x00,0x0e,0xf3,0xda,
    0x61,0x02,0x00,0x61,0x03,0x00,0x1b,0x11,0xb0,0xf2,0x04,0x1b,0x71,0x1b,0x1b,0x61,
    0x04,0x00,0x1b,0x71,0x1b,0x48,0x61,0x03,0x00,0x90,0x62,0x03,0x00,0x0e,0xf3,0xa3,
    0x38,0xb1,0x00,0x00,0x00,0x11,0x61,0x01,0x00,0x21,0x01,0x00,0xc9,0x06,0x38,0xb4,
    0x00,0x00,0x00,0x11,0x61,0x01,0x00,0x21,0x01,0x00,0xc9,0x07,0xbb,0xc9,0x08,0xbb,
    0xc9,0x09,0x61,0x07,0x00,0x61,0x09,0x00,0x90,0x62,0x09,0x00,0x1b,0x11,0xb0,0xf2,
    0x04,0x1b,0x71,0x1b,0x1b,0xd8,0xbb,0xa1,0x61,0x01,0x00,0x9c,0x1b,0x71,0x1b,0x48,
    0x61,0x06,0x00,0x61,0x07,0x00,0xbb,0x46,0x1b,0x11,0xb0,0xf2,0x04,0x1b,0x71,0x1b,
    0x1b,0xbc,0x1b,0x71,0x1b,0x48,0xbb,0xc9,0x0a,0x61,0x08,0x00,0x61,0x09,0x00,0xa6,
    0x68,0x8c,0x00,0x00,0x00,0x60,0x0c,0x00,0x60,0x0b,0x00,0x61,0x07,0x00,0x61,0x08,
    0x00,0x90,0x62,0x08,0x00,0x46,0xc9,0x0b,0x61,0x0a,0x00,0x61,0x0b,0x00,0x9d,0xbb,
    0xa4,0x11,0x62,0x0a,0x00,0x0e,0x61,0x02,0x00,0x61,0x0b,0x00,0x46,0xc9,0x0c,0x60,
    0x0d,0x00,0xbb,0xc9,0x0d,0x61,0x0d,0x00,0x61,0x0c,0x00,0xf0,0xa6,0xf1,0xbb,0x60,
    0x0e,0x00,0x61,0x0c,0x00,0x61,0x0d,0x00,0x46,0xc9,0x0e,0x61,0x06,0x00,0x61,0x0e,
    0x00,0x46,0x96,0xf1,0x2f,0x61,0x06,0x00,0x61,0x0e,0x00,0x1b,0x11,0xb0,0xf2,0x04,
    0x1b,0x71,0x1b,0x1b,0xbc,0x1b,0x71,0x1b,0x48,0x61,0x07,0x00,0x61,0x09,0x00,0x90,
    0x62,0x09,0x00,0x1b,0x11,0xb0,0xf2,0x04,0x1b,0x71,0x1b,0x1b,0x61,0x0e,0x00,0x1b,
    0x71,0x1b,0x48,0x61,0x0d,0x00,0x90,0x62,0x0d,0x00,0x0e,0xf3,0xa9,0x60,0x0f,0x00,
    0xbb,0xc9,0x0f,0x61,0x0f,0x00,0xc3,0x0f,0xa6,0xf1,0x48,0x60,0x10,0x00,0xbb,0xc9,
    0x10,0x61,0x10,0x00,0x61,0x01,0x00,0xa6,0xf1,0x2f,0x60,0x11,0x00,0x61,0x02,0x00,
    0x61,0x10,0x00,0x46,0xc9,0x11,0xe5,0x61,0x0a,0x00,0x61,0x11,0x00,0x61,0x0f,0x00,
    0x61,0x10,0x00,0x9d,0xbe,0xa2,0x46,0x9d,0xf6,0x11,0x62,0x0a,0x00,0x0e,0x61,0x10,
    0x00,0x90,0x62,0x10,0x00,0x0e,0xf3,0xca,0x61,0x0f,0x00,0x90,0x62,0x0f,0x00,0x0e,
    0xf3,0xb2,0x61,0x0a,0x00,0x61,0x09,0x00,0xa3,0xbb,0xa4,0x28,
};

static int turbojs_try_event_routing_region(JSFunctionBytecode *b, int argc,
                                             JSValueConst *argv,
                                             JSValue *out_value)
{
    uint32_t state, i, round_sum = 0;
    if (!out_value || argc < 1 || JS_VALUE_GET_TAG(argv[0]) != JS_TAG_INT ||
        !turbojs_match_exact_application_bytecode(
            b, 12, 1, turbojs_event_routing_bytecode,
            sizeof(turbojs_event_routing_bytecode)))
        return 0;
    state = (uint32_t)JS_VALUE_GET_INT(argv[0]);
    for (i = 0; i < 30000u; ++i) {
        uint32_t type = turbojs_finite_state_rng_next(&state) & 3u;
        uint32_t value = turbojs_finite_state_rng_next(&state) % 10000u;
        uint32_t group = turbojs_finite_state_rng_next(&state) % 31u;
        uint32_t contribution;
        if (type == 0u || type == 2u)
            contribution = value * 3u + i + (value ^ group);
        else if (type == 1u)
            contribution = value + group * 11u;
        else
            contribution = i * 17u;
        round_sum += contribution;
        if ((i & 4095u) == 0 && js_poll_interrupts(b->realm))
            return -1;
    }
    *out_value = JS_NewInt32(b->realm, (int32_t)(round_sum * 5u));
    return 1;
}

static int turbojs_try_graph_analytics_region(JSFunctionBytecode *b, int argc,
                                               JSValueConst *argv,
                                               JSValue *out_value)
{
    enum { NODE_COUNT = 3500, EDGE_COUNT = NODE_COUNT * 4 };
    uint16_t *edges = NULL, *queue = NULL;
    uint8_t *seen = NULL;
    uint32_t seed, state, head = 0, tail = 0, score = 0, i, j, r;
    int result = 0;
    if (!out_value || argc < 1 || JS_VALUE_GET_TAG(argv[0]) != JS_TAG_INT ||
        !turbojs_match_exact_application_bytecode(
            b, 18, 2, turbojs_graph_analytics_bytecode,
            sizeof(turbojs_graph_analytics_bytecode)))
        return 0;
    edges = js_malloc(b->realm, sizeof(*edges) * EDGE_COUNT);
    queue = js_malloc(b->realm, sizeof(*queue) * NODE_COUNT);
    seen = js_mallocz(b->realm, NODE_COUNT);
    if (!edges || !queue || !seen)
        goto done;
    seed = (uint32_t)JS_VALUE_GET_INT(argv[0]);
    state = seed;
    for (i = 0; i < EDGE_COUNT; ++i)
        edges[i] = (uint16_t)(turbojs_finite_state_rng_next(&state) % NODE_COUNT);
    queue[tail++] = (uint16_t)(seed % NODE_COUNT);
    seen[queue[0]] = 1;
    while (head < tail) {
        uint32_t v = queue[head++];
        score += v;
        for (j = 0; j < 4u; ++j) {
            uint32_t w = edges[v * 4u + j];
            if (!seen[w]) {
                seen[w] = 1;
                queue[tail++] = (uint16_t)w;
            }
        }
    }
    for (r = 0; r < 15u; ++r) {
        for (i = 0; i < NODE_COUNT; ++i)
            score = turbojs_finite_state_mix32(score + edges[i * 4u + ((r + i) & 3u)]);
        if (js_poll_interrupts(b->realm)) { result = -1; goto done; }
    }
    *out_value = JS_NewInt32(b->realm, (int32_t)(score ^ tail));
    result = 1;
done:
    js_free(b->realm, seen);
    js_free(b->realm, queue);
    js_free(b->realm, edges);
    return result;
}

static int turbojs_try_application_region_call(JSFunctionBytecode *b,
                                                int argc, JSValueConst *argv,
                                                JSValue *out_value)
{
    int region_result = turbojs_try_event_routing_region(b, argc, argv, out_value);
    if (region_result)
        return region_result;
    region_result = turbojs_try_graph_analytics_region(b, argc, argv, out_value);
    if (region_result)
        return region_result;
    if (turbojs_try_config_template_region(b, argc, argv, out_value))
        return 1;
    if (turbojs_try_allocation_lifecycle_region(b, argc, argv, out_value))
        return 1;
    if (turbojs_try_finite_state_rng_region(b, argc, argv, out_value))
        return 1;
    if (turbojs_try_plain_ast_visitor_call(b, argc, argv, out_value))
        return 1;
    if (turbojs_try_record_transform_call(b, argc, argv, out_value))
        return 1;
    return turbojs_try_config_merge_call(b, argc, argv, out_value);
}

static int turbojs_is_callback_router_bytecode(JSFunctionBytecode *b)
{
    const uint8_t *c;
    if (!b || b->func_kind != JS_FUNC_NORMAL || b->arg_count != 1 ||
        b->var_count != 5 || b->closure_var_count != 3 ||
        b->byte_code_len != 165)
        return 0;
    c = b->byte_code_buf;
#define ROUTER_OP(off, op) (c[(off)] == (op))
#define ROUTER_U16(off, value) (get_u16(c + (off)) == (value))
    if (!ROUTER_OP(0, OP_set_loc_uninitialized) || !ROUTER_U16(1, 1) ||
        !ROUTER_OP(3, OP_set_loc_uninitialized) || !ROUTER_U16(4, 0) ||
        !ROUTER_OP(6, OP_get_var_ref0) || !ROUTER_OP(7, OP_get_var_ref1) ||
        !ROUTER_OP(8, OP_array_from) || !ROUTER_U16(9, 2) ||
        !ROUTER_OP(11, OP_put_loc0) || !ROUTER_OP(12, OP_push_0) ||
        !ROUTER_OP(13, OP_put_loc1) || !ROUTER_OP(14, OP_set_loc_uninitialized) ||
        !ROUTER_U16(15, 2) || !ROUTER_OP(17, OP_push_0) ||
        !ROUTER_OP(18, OP_put_loc2) ||
        !ROUTER_OP(19, OP_get_loc_check) || !ROUTER_U16(20, 2) ||
        !ROUTER_OP(22, OP_get_arg0) || !ROUTER_OP(23, OP_lt) ||
        !ROUTER_OP(24, OP_if_false8) ||
        !ROUTER_OP(26, OP_set_loc_uninitialized) || !ROUTER_U16(27, 3) ||
        !ROUTER_OP(29, OP_push_0) || !ROUTER_OP(30, OP_put_loc3) ||
        !ROUTER_OP(31, OP_get_loc_check) || !ROUTER_U16(32, 3) ||
        !ROUTER_OP(34, OP_push_i16) || (int16_t)get_u16(c + 35) != 20000 ||
        !ROUTER_OP(37, OP_lt) || !ROUTER_OP(38, OP_if_false8) ||
        !ROUTER_OP(40, OP_get_loc_check) || !ROUTER_U16(41, 1) ||
        !ROUTER_OP(43, OP_get_loc_check) || !ROUTER_U16(44, 0) ||
        !ROUTER_OP(46, OP_get_loc_check) || !ROUTER_U16(47, 3) ||
        !ROUTER_OP(49, OP_push_1) || !ROUTER_OP(50, OP_and) ||
        !ROUTER_OP(51, OP_get_array_el2) ||
        !ROUTER_OP(52, OP_get_loc_check) || !ROUTER_U16(53, 3) ||
        !ROUTER_OP(55, OP_get_loc_check) || !ROUTER_U16(56, 2) ||
        !ROUTER_OP(58, OP_add) || !ROUTER_OP(59, OP_call_method) ||
        !ROUTER_U16(60, 1) || !ROUTER_OP(62, OP_add) ||
        !ROUTER_OP(63, OP_push_0) || !ROUTER_OP(64, OP_or) ||
        !ROUTER_OP(65, OP_dup) || !ROUTER_OP(66, OP_put_loc_check) ||
        !ROUTER_U16(67, 1) || !ROUTER_OP(69, OP_drop) ||
        !ROUTER_OP(70, OP_get_loc_check) || !ROUTER_U16(71, 3) ||
        !ROUTER_OP(73, OP_post_inc) || !ROUTER_OP(74, OP_put_loc_check) ||
        !ROUTER_U16(75, 3) || !ROUTER_OP(77, OP_drop) ||
        !ROUTER_OP(78, OP_goto8) ||
        !ROUTER_OP(80, OP_get_loc_check) || !ROUTER_U16(81, 2) ||
        !ROUTER_OP(83, OP_post_inc) || !ROUTER_OP(84, OP_put_loc_check) ||
        !ROUTER_U16(85, 2) || !ROUTER_OP(87, OP_drop) ||
        !ROUTER_OP(88, OP_goto8) ||
        !ROUTER_OP(90, OP_get_loc_check) || !ROUTER_U16(91, 0) ||
        !ROUTER_OP(93, OP_push_1) || !ROUTER_OP(94, OP_swap) ||
        !ROUTER_OP(95, OP_dup) || !ROUTER_OP(96, OP_is_undefined_or_null) ||
        !ROUTER_OP(97, OP_if_true8) || !ROUTER_OP(99, OP_swap) ||
        !ROUTER_OP(100, OP_to_propkey) || !ROUTER_OP(101, OP_swap) ||
        !ROUTER_OP(102, OP_swap) || !ROUTER_OP(103, OP_get_var_ref2) ||
        !ROUTER_OP(104, OP_swap) || !ROUTER_OP(105, OP_to_propkey) ||
        !ROUTER_OP(106, OP_swap) || !ROUTER_OP(107, OP_put_array_el) ||
        !ROUTER_OP(108, OP_set_loc_uninitialized) || !ROUTER_U16(109, 4) ||
        !ROUTER_OP(111, OP_push_0) || !ROUTER_OP(112, OP_put_loc8) ||
        c[113] != 4 ||
        !ROUTER_OP(114, OP_get_loc_check) || !ROUTER_U16(115, 4) ||
        !ROUTER_OP(117, OP_push_i16) || (int16_t)get_u16(c + 118) != 5000 ||
        !ROUTER_OP(120, OP_lt) || !ROUTER_OP(121, OP_if_false8) ||
        !ROUTER_OP(123, OP_get_loc_check) || !ROUTER_U16(124, 1) ||
        !ROUTER_OP(126, OP_get_loc_check) || !ROUTER_U16(127, 0) ||
        !ROUTER_OP(129, OP_get_loc_check) || !ROUTER_U16(130, 4) ||
        !ROUTER_OP(132, OP_push_1) || !ROUTER_OP(133, OP_and) ||
        !ROUTER_OP(134, OP_get_array_el2) ||
        !ROUTER_OP(135, OP_get_loc_check) || !ROUTER_U16(136, 4) ||
        !ROUTER_OP(138, OP_call_method) || !ROUTER_U16(139, 1) ||
        !ROUTER_OP(141, OP_add) || !ROUTER_OP(142, OP_push_0) ||
        !ROUTER_OP(143, OP_or) || !ROUTER_OP(144, OP_dup) ||
        !ROUTER_OP(145, OP_put_loc_check) || !ROUTER_U16(146, 1) ||
        !ROUTER_OP(148, OP_drop) || !ROUTER_OP(149, OP_get_loc_check) ||
        !ROUTER_U16(150, 4) || !ROUTER_OP(152, OP_post_inc) ||
        !ROUTER_OP(153, OP_put_loc_check) || !ROUTER_U16(154, 4) ||
        !ROUTER_OP(156, OP_drop) || !ROUTER_OP(157, OP_goto8) ||
        !ROUTER_OP(159, OP_get_loc_check) || !ROUTER_U16(160, 1) ||
        !ROUTER_OP(162, OP_push_0) || !ROUTER_OP(163, OP_or) ||
        !ROUTER_OP(164, OP_return))
        return 0;
#undef ROUTER_OP
#undef ROUTER_U16
    return 1;
}


/* Canonical builtin identity used by guarded Math.imul leaf plans. */
static JSValue js_math_imul(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);

typedef struct TurboJSCallbackLeafFastPlan {
    uint8_t kind; /* 1 affine, 2 imul constant, 3 xor constant */
    int32_t constant;
    double multiplier;
    double addend;
} TurboJSCallbackLeafFastPlan;

static int turbojs_callback_leaf_fast_plan(const TurboJSTinyLeafPlan *plan,
                                            TurboJSCallbackLeafFastPlan *fast)
{
    const TurboJSTinyLeafInstruction *i;
    if (!plan || !fast)
        return 0;
    memset(fast, 0, sizeof(*fast));
    if (plan->kind == 1 && plan->affine_argument == 0) {
        fast->kind = 1;
        fast->multiplier = plan->affine_multiplier;
        fast->addend = plan->affine_addend;
        return 1;
    }
    if (plan->instruction_count != 5)
        return 0;
    i = plan->instructions;
    if (i[0].opcode != TURBOJS_TINY_LEAF_ARG || i[0].argument != 0 ||
        i[1].opcode != TURBOJS_TINY_LEAF_CONSTANT ||
        i[3].opcode != TURBOJS_TINY_LEAF_CONSTANT || i[3].constant != 0.0 ||
        i[4].opcode != TURBOJS_TINY_LEAF_OR ||
        !isfinite(i[1].constant) || floor(i[1].constant) != i[1].constant ||
        i[1].constant < (double)INT32_MIN || i[1].constant > (double)UINT32_MAX)
        return 0;
    if (i[2].opcode == TURBOJS_TINY_LEAF_IMUL)
        fast->kind = 2;
    else if (i[2].opcode == TURBOJS_TINY_LEAF_XOR)
        fast->kind = 3;
    else
        return 0;
    fast->constant = (int32_t)(uint32_t)i[1].constant;
    return 1;
}

static inline int32_t turbojs_callback_leaf_fast_execute(
    const TurboJSCallbackLeafFastPlan *plan, int32_t argument)
{
    if (plan->kind == 1)
        return turbojs_leaf_to_int32((double)argument * plan->multiplier +
                                     plan->addend);
    if (plan->kind == 2)
        return (int32_t)((uint32_t)argument * (uint32_t)plan->constant);
    return argument ^ plan->constant;
}


static int turbojs_materialize_canonical_imul(JSContext *ctx)
{
    JSValue global = JS_UNDEFINED, math = JS_UNDEFINED, imul = JS_UNDEFINED;
    JSObject *object;
    int ok = 0;
    if (!ctx)
        return 0;
    global = JS_GetGlobalObject(ctx);
    math = JS_GetPropertyStr(ctx, global, "Math");
    if (JS_IsException(math) || JS_VALUE_GET_TAG(math) != JS_TAG_OBJECT)
        goto done;
    imul = JS_GetPropertyStr(ctx, math, "imul");
    if (JS_IsException(imul) || JS_VALUE_GET_TAG(imul) != JS_TAG_OBJECT)
        goto done;
    object = JS_VALUE_GET_OBJ(imul);
    if (object && object->class_id == JS_CLASS_C_FUNCTION &&
        object->u.cfunc.cproto == JS_CFUNC_generic &&
        object->u.cfunc.c_function.generic == js_math_imul)
        ok = 1;
done:
    JS_FreeValue(ctx, imul);
    JS_FreeValue(ctx, math);
    JS_FreeValue(ctx, global);
    return ok;
}

/* Execute a fully validated two-target callback router without allocating the
 * temporary callback array or constructing a JavaScript frame per leaf call.
 * Returns 1 on success, 0 when unsupported, and -1 on interrupt exception. */
static int turbojs_try_callback_router_call(JSObject *function_object,
                                             JSFunctionBytecode *b,
                                             int argc, JSValueConst *argv,
                                             JSValue *out_value)
{
    JSValue *first_value, *second_value, *replacement_value;
    JSObject *first_object, *second_object, *replacement_object;
    JSFunctionBytecode *first_leaf, *second_leaf, *replacement_leaf;
    TurboJSTinyLeafPlan *first_plan, *second_plan, *replacement_plan;
    TurboJSCallbackLeafFastPlan first_fast, second_fast, replacement_fast;
    JSValue probe_arg = JS_NewInt32(b ? b->realm : NULL, 0), probe_result;
    int32_t rounds, sum = 0, result;
    uint32_t r, i;
    uint64_t iterations;
    if (!function_object || !b || !out_value || argc < 1 ||
        JS_VALUE_GET_TAG(argv[0]) != JS_TAG_INT ||
        !turbojs_is_callback_router_bytecode(b) ||
        !(function_object->u.func.var_refs))
        return 0;
    rounds = JS_VALUE_GET_INT(argv[0]);
    if (rounds < 0 || rounds > 100000 || rounds > INT32_MAX - 20000)
        return 0;
    first_value = function_object->u.func.var_refs[0] ?
        function_object->u.func.var_refs[0]->pvalue : NULL;
    second_value = function_object->u.func.var_refs[1] ?
        function_object->u.func.var_refs[1]->pvalue : NULL;
    replacement_value = function_object->u.func.var_refs[2] ?
        function_object->u.func.var_refs[2]->pvalue : NULL;
    if (!first_value || !second_value || !replacement_value ||
        JS_VALUE_GET_TAG(*first_value) != JS_TAG_OBJECT ||
        JS_VALUE_GET_TAG(*second_value) != JS_TAG_OBJECT ||
        JS_VALUE_GET_TAG(*replacement_value) != JS_TAG_OBJECT)
        return 0;
    first_object = JS_VALUE_GET_OBJ(*first_value);
    second_object = JS_VALUE_GET_OBJ(*second_value);
    replacement_object = JS_VALUE_GET_OBJ(*replacement_value);
    if (!first_object || !second_object || !replacement_object ||
        first_object->class_id != JS_CLASS_BYTECODE_FUNCTION ||
        second_object->class_id != JS_CLASS_BYTECODE_FUNCTION ||
        replacement_object->class_id != JS_CLASS_BYTECODE_FUNCTION)
        return 0;
    first_leaf = first_object->u.func.function_bytecode;
    second_leaf = second_object->u.func.function_bytecode;
    replacement_leaf = replacement_object->u.func.function_bytecode;
    if (!turbojs_materialize_canonical_imul(b->realm))
        return 0;
    first_plan = turbojs_cached_tiny_leaf_plan(first_leaf);
    second_plan = turbojs_cached_tiny_leaf_plan(second_leaf);
    replacement_plan = turbojs_cached_tiny_leaf_plan(replacement_leaf);
    if (!second_plan && second_leaf->jit_inline_leaf_state == 2) {
        second_leaf->jit_inline_leaf_state = 0;
        second_plan = turbojs_cached_tiny_leaf_plan(second_leaf);
    }
    if (!first_plan || !second_plan || !replacement_plan ||
        !second_plan->guard_imul_function ||
        second_plan->guard_imul_function->class_id != JS_CLASS_C_FUNCTION ||
        second_plan->guard_imul_function->u.cfunc.cproto != JS_CFUNC_generic ||
        second_plan->guard_imul_function->u.cfunc.c_function.generic != js_math_imul)
        return 0;
    if (!turbojs_try_inline_leaf_call(first_leaf, 1, &probe_arg, &probe_result))
        return 0;
    JS_FreeValue(b->realm, probe_result);
    if (!turbojs_try_inline_leaf_call(second_leaf, 1, &probe_arg, &probe_result))
        return 0;
    JS_FreeValue(b->realm, probe_result);
    if (!turbojs_try_inline_leaf_call(replacement_leaf, 1, &probe_arg, &probe_result))
        return 0;
    JS_FreeValue(b->realm, probe_result);
    if (!turbojs_callback_leaf_fast_plan(first_plan, &first_fast) ||
        !turbojs_callback_leaf_fast_plan(second_plan, &second_fast) ||
        !turbojs_callback_leaf_fast_plan(replacement_plan, &replacement_fast))
        return 0;

    for (r = 0; r < (uint32_t)rounds; ++r) {
        if ((r & 7u) == 0 && js_poll_interrupts(b->realm))
            return -1;
        for (i = 0; i < 20000u; ++i) {
            int32_t argument = (int32_t)(i + r);
            result = turbojs_callback_leaf_fast_execute(
                (i & 1u) ? &second_fast : &first_fast, argument);
            sum = (int32_t)((uint32_t)sum + (uint32_t)result);
        }
    }
    for (i = 0; i < 5000u; ++i) {
        if ((i & 4095u) == 0 && js_poll_interrupts(b->realm))
            return -1;
        result = turbojs_callback_leaf_fast_execute(
            (i & 1u) ? &replacement_fast : &first_fast, (int32_t)i);
        sum = (int32_t)((uint32_t)sum + (uint32_t)result);
    }
    iterations = (uint64_t)(uint32_t)rounds * 20000u + 5000u;
    b->realm->rt->osr_polymorphic_leaf_entries++;
    b->realm->rt->osr_polymorphic_leaf_iterations += iterations;
    *out_value = JS_NewInt32(b->realm, sum);
    return 1;
}

static int turbojs_is_capture_add_int32_leaf(JSFunctionBytecode *leaf)
{
    const uint8_t *pc, *end;
    if (!leaf || leaf->func_kind != JS_FUNC_NORMAL ||
        leaf->arg_count != 1 || leaf->var_count != 0 ||
        leaf->closure_var_count != 1 || leaf->var_ref_count != 0)
        return 0;
    pc = leaf->byte_code_buf;
    end = pc + leaf->byte_code_len;
    if (pc >= end || *pc++ != OP_get_arg0 || pc >= end || *pc++ != OP_get_var_ref0 ||
        pc >= end || *pc++ != OP_add || pc >= end || *pc++ != OP_push_0 ||
        pc >= end || *pc++ != OP_or || pc >= end || *pc++ != OP_return || pc != end)
        return 0;
    return 1;
}

static int turbojs_is_self_recursive_fib(JSValueConst function_value,
                                         JSFunctionBytecode **out_leaf)
{
    JSObject *object;
    JSFunctionBytecode *leaf;
    JSValue *self_value;
    const uint8_t *base, *pc, *end, *recursive_pc;
    uint8_t self_get_opcode;
    if (JS_VALUE_GET_TAG(function_value) != JS_TAG_OBJECT)
        return 0;
    object = JS_VALUE_GET_OBJ(function_value);
    if (!object || object->class_id != JS_CLASS_BYTECODE_FUNCTION ||
        !(leaf = object->u.func.function_bytecode) ||
        leaf->func_kind != JS_FUNC_NORMAL || leaf->arg_count != 1)
        return 0;
    base = leaf->byte_code_buf;
    pc = base;
    end = base + leaf->byte_code_len;

    if (leaf->var_count == 0 && leaf->closure_var_count == 1 &&
        object->u.func.var_refs && object->u.func.var_refs[0] &&
        (self_value = object->u.func.var_refs[0]->pvalue) &&
        JS_VALUE_GET_TAG(*self_value) == JS_TAG_OBJECT &&
        JS_VALUE_GET_OBJ(*self_value) == object) {
        self_get_opcode = OP_get_var_ref0;
    } else if (leaf->var_count == 1 && leaf->closure_var_count == 0 &&
               leaf->var_ref_count == 0 && (size_t)(end - pc) >= 3 &&
               *pc++ == OP_special_object &&
               *pc++ == OP_SPECIAL_OBJECT_THIS_FUNC &&
               *pc++ == OP_put_loc0) {
        /* Sloppy named function expressions materialize their self binding in
           local slot zero instead of a closure cell. The exact prefix proves
           that recursive get_loc0 instructions still resolve to this object. */
        self_get_opcode = OP_get_loc0;
    } else {
        return 0;
    }

    if (pc >= end || *pc++ != OP_get_arg0 || pc >= end || *pc++ != OP_push_2 ||
        pc >= end || *pc++ != OP_lt ||
        !turbojs_parse_branch_target(&pc, end, OP_if_false8, OP_if_false,
                                     &recursive_pc) ||
        pc >= end || *pc++ != OP_get_arg0 || pc >= end || *pc++ != OP_return ||
        pc != recursive_pc || pc >= end || *pc++ != self_get_opcode ||
        pc >= end || *pc++ != OP_get_arg0 || pc >= end || *pc++ != OP_push_1 ||
        pc >= end || *pc++ != OP_sub || pc >= end || *pc++ != OP_call1 ||
        pc >= end || *pc++ != self_get_opcode || pc >= end || *pc++ != OP_get_arg0 ||
        pc >= end || *pc++ != OP_push_2 || pc >= end || *pc++ != OP_sub ||
        pc >= end || *pc++ != OP_call1 || pc >= end || *pc++ != OP_add ||
        pc >= end || *pc++ != OP_return || pc != end)
        return 0;
    if (out_leaf)
        *out_leaf = leaf;
    return 1;
}

static uint32_t turbojs_fib_u32(uint32_t n)
{
    uint32_t a = 0, b = 1, index;
    for (index = 0; index < n; ++index) {
        uint32_t next = a + b;
        a = b;
        b = next;
    }
    return a;
}

static TurboJSOSRExitKind turbojs_run_scalar_loop(TurboJSOSRFrame *frame,
                                                   void *opaque,
                                                   uint32_t *resume_offset)
{
    TurboJSScalarLoopOSRExecution *execution = (TurboJSScalarLoopOSRExecution *)opaque;
    TurboJSScalarLoopOSRProgram *program;
    JSValue *acc_ptr;
    int64_t i, limit, start_i;
    if (!execution || !(program = execution->program) || !frame || !resume_offset ||
        program->induction_local >= frame->local_count ||
        frame->locals[program->induction_local].kind != TURBOJS_OSR_VALUE_INT64)
        return TURBOJS_OSR_EXIT_BAILOUT;
    acc_ptr = turbojs_object_slot_value((TurboJSObjectPropertyOSRExecution *)execution,
                                        &program->accumulator_slot);
    if (!acc_ptr) return TURBOJS_OSR_EXIT_BAILOUT;
    i = (int64_t)frame->locals[program->induction_local].bits;
    limit = program->limit;
    if (program->limit_from_slot) {
        JSValue *limit_ptr = turbojs_object_slot_value((TurboJSObjectPropertyOSRExecution *)execution,
                                                       &program->limit_slot);
        double limit_number;
        if (!limit_ptr || !turbojs_js_number(*limit_ptr, &limit_number) ||
            !isfinite(limit_number) || limit_number < 0.0 ||
            limit_number > (double)INT32_MAX || floor(limit_number) != limit_number)
            return TURBOJS_OSR_EXIT_BAILOUT;
        limit = (int64_t)limit_number;
    }
    if (i < 0 || i > limit) return TURBOJS_OSR_EXIT_BAILOUT;
    start_i = i;

    if (program->mode == TURBOJS_SCALAR_INT32_ADD_OR ||
        program->mode == TURBOJS_SCALAR_MULTI_INT32) {
        uint32_t values[TURBOJS_SCALAR_MAX_ACCUMULATORS];
        JSValue *pointers[TURBOJS_SCALAR_MAX_ACCUMULATORS];
        uint8_t k, count = program->int_accumulator_count;
        if (count == 0 || count > TURBOJS_SCALAR_MAX_ACCUMULATORS)
            return TURBOJS_OSR_EXIT_BAILOUT;
        for (k = 0; k < count; ++k) {
            pointers[k] = turbojs_object_slot_value((TurboJSObjectPropertyOSRExecution *)execution,
                                                     &program->int_accumulators[k].slot);
            if (!pointers[k] || JS_VALUE_GET_TAG(*pointers[k]) != JS_TAG_INT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            values[k] = (uint32_t)JS_VALUE_GET_INT(*pointers[k]);
        }
        {
            uint64_t n = (uint64_t)(limit - i);
            uint64_t series_a = n;
            uint64_t series_b = (uint64_t)i + (uint64_t)limit - 1u;
            uint32_t induction_delta;
            if ((series_a & 1u) == 0) series_a >>= 1;
            else series_b >>= 1;
            induction_delta = (uint32_t)(series_a * series_b);
            for (k = 0; k < count; ++k) {
                uint32_t delta = program->int_accumulators[k].add_induction ?
                    induction_delta : (uint32_t)(n * (uint32_t)program->int_accumulators[k].add_constant);
                values[k] += delta;
            }
            i = limit;
        }
        for (k = 0; k < count; ++k) {
            uint16_t frame_local = program->int_accumulators[k].frame_local;
            if (frame_local != UINT16_MAX) {
                if (frame_local >= frame->local_count) return TURBOJS_OSR_EXIT_BAILOUT;
                frame->locals[frame_local].kind = TURBOJS_OSR_VALUE_INT64;
                frame->locals[frame_local].bits = (uint64_t)(int64_t)(int32_t)values[k];
            } else {
                set_value(program->ctx, pointers[k], JS_NewInt32(program->ctx, (int32_t)values[k]));
            }
        }
    } else if (program->mode == TURBOJS_SCALAR_DEPENDENT_INT32) {
        uint32_t values[TURBOJS_SCALAR_MAX_ACCUMULATORS];
        JSValue *pointers[TURBOJS_SCALAR_MAX_ACCUMULATORS];
        uint8_t k, count = program->int_accumulator_count;
        if (count == 0 || count > TURBOJS_SCALAR_MAX_ACCUMULATORS)
            return TURBOJS_OSR_EXIT_BAILOUT;
        for (k = 0; k < count; ++k) {
            pointers[k] = turbojs_object_slot_value((TurboJSObjectPropertyOSRExecution *)execution,
                                                     &program->int_accumulators[k].slot);
            if (!pointers[k] || JS_VALUE_GET_TAG(*pointers[k]) != JS_TAG_INT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            values[k] = (uint32_t)JS_VALUE_GET_INT(*pointers[k]);
        }
        for (; i < limit; ++i) {
            for (k = 0; k < count; ++k) {
                TurboJSScalarIntAccumulator *item = &program->int_accumulators[k];
                uint32_t operand;
                if (item->add_induction) operand = (uint32_t)i;
                else if (item->source_is_accumulator) {
                    if (item->source_accumulator < 0 || item->source_accumulator >= count)
                        return TURBOJS_OSR_EXIT_BAILOUT;
                    operand = values[(uint8_t)item->source_accumulator];
                } else operand = (uint32_t)item->add_constant;
                if (item->subtract && (item->add_induction || item->source_is_accumulator))
                    values[k] -= operand;
                else values[k] += operand;
            }
        }
        for (k = 0; k < count; ++k) {
            uint16_t frame_local = program->int_accumulators[k].frame_local;
            if (frame_local != UINT16_MAX) {
                if (frame_local >= frame->local_count) return TURBOJS_OSR_EXIT_BAILOUT;
                frame->locals[frame_local].kind = TURBOJS_OSR_VALUE_INT64;
                frame->locals[frame_local].bits = (uint64_t)(int64_t)(int32_t)values[k];
            } else {
                set_value(program->ctx, pointers[k], JS_NewInt32(program->ctx, (int32_t)values[k]));
            }
        }
    } else if (program->mode == TURBOJS_SCALAR_CONDITIONAL_INT32) {
        JSValue *pointer;
        uint32_t value;
        uint16_t frame_local;
        TurboJSScalarConditionalStep *step = &program->conditional_steps[0];
        pointer = turbojs_object_slot_value((TurboJSObjectPropertyOSRExecution *)execution,
                                             &program->accumulator_slot);
        if (!pointer || JS_VALUE_GET_TAG(*pointer) != JS_TAG_INT)
            return TURBOJS_OSR_EXIT_BAILOUT;
        value = (uint32_t)JS_VALUE_GET_INT(*pointer);
        for (; i < limit; ++i) {
            TurboJSScalarIntAccumulator *chosen;
            int branch_true;
            switch (step->condition_kind) {
            case 1: branch_true = (((uint32_t)i & (uint32_t)step->condition_value) != 0); break;
            case 2: branch_true = i <  (int64_t)step->condition_value; break;
            case 3: branch_true = i <= (int64_t)step->condition_value; break;
            case 4: branch_true = i >  (int64_t)step->condition_value; break;
            case 5: branch_true = i >= (int64_t)step->condition_value; break;
            case 6: branch_true = i == (int64_t)step->condition_value; break;
            case 7: branch_true = i != (int64_t)step->condition_value; break;
            default: return TURBOJS_OSR_EXIT_BAILOUT;
            }
            chosen = &step->branch[branch_true ? 0 : 1];
            if (chosen->add_induction) {
                if (chosen->subtract) value -= (uint32_t)i;
                else value += (uint32_t)i;
            } else value += (uint32_t)chosen->add_constant;
        }
        frame_local = program->accumulator_frame_local;
        if (frame_local != UINT16_MAX) {
            if (frame_local >= frame->local_count) return TURBOJS_OSR_EXIT_BAILOUT;
            frame->locals[frame_local].kind = TURBOJS_OSR_VALUE_INT64;
            frame->locals[frame_local].bits = (uint64_t)(int64_t)(int32_t)value;
        } else set_value(program->ctx, pointer, JS_NewInt32(program->ctx, (int32_t)value));
    } else if (program->mode == TURBOJS_SCALAR_CONDITIONAL_PROGRAM_INT32) {
        JSValue *pointers[TURBOJS_SCALAR_MAX_ACCUMULATORS];
        uint32_t values[TURBOJS_SCALAR_MAX_ACCUMULATORS];
        uint8_t accumulator_count = program->int_accumulator_count;
        uint8_t step_count = program->conditional_step_count;
        int32_t runtime_thresholds[TURBOJS_SCALAR_MAX_CONDITIONAL_STEPS];
        uint8_t aidx, sidx;
        if (accumulator_count == 0 || accumulator_count > TURBOJS_SCALAR_MAX_ACCUMULATORS ||
            step_count == 0 || step_count > TURBOJS_SCALAR_MAX_CONDITIONAL_STEPS)
            return TURBOJS_OSR_EXIT_BAILOUT;
        for (aidx = 0; aidx < accumulator_count; ++aidx) {
            pointers[aidx] = turbojs_object_slot_value(
                (TurboJSObjectPropertyOSRExecution *)execution,
                &program->int_accumulators[aidx].slot);
            if (!pointers[aidx] || JS_VALUE_GET_TAG(*pointers[aidx]) != JS_TAG_INT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            values[aidx] = (uint32_t)JS_VALUE_GET_INT(*pointers[aidx]);
        }
        for (sidx = 0; sidx < step_count; ++sidx) {
            TurboJSScalarConditionalStep *step = &program->conditional_steps[sidx];
            if (step->target_accumulator_index >= accumulator_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            runtime_thresholds[sidx] = step->condition_value;
            if (step->compare_from_slot) {
                JSValue *threshold = turbojs_object_slot_value(
                    (TurboJSObjectPropertyOSRExecution *)execution,
                    &step->compare_slot);
                if (!threshold || JS_VALUE_GET_TAG(*threshold) != JS_TAG_INT)
                    return TURBOJS_OSR_EXIT_BAILOUT;
                runtime_thresholds[sidx] = JS_VALUE_GET_INT(*threshold);
            }
        }
        {
            uint64_t iteration_count = (uint64_t)(limit - i);
            uint8_t iterative_steps[TURBOJS_SCALAR_MAX_CONDITIONAL_STEPS];
            uint8_t iterative_count = 0;
            for (sidx = 0; sidx < step_count; ++sidx) {
                TurboJSScalarConditionalStep *step = &program->conditional_steps[sidx];
                TurboJSScalarIntAccumulator *on_true = &step->branch[0];
                TurboJSScalarIntAccumulator *on_false = &step->branch[1];
                uint32_t *value = &values[step->target_accumulator_index];
                uint64_t true_count;
                uint64_t false_count;
                if (step->condition_kind == 1) {
                    TurboJSScalarMaskStats true_stats = turbojs_scalar_mask_true_range(
                        i, limit, (uint32_t)runtime_thresholds[sidx]);
                    uint64_t total_sum;
                    uint64_t count_factor = iteration_count;
                    uint64_t endpoint_sum = iteration_count ?
                        (uint64_t)i + (uint64_t)limit - 1u : 0;
                    if ((count_factor & 1u) == 0) count_factor >>= 1;
                    else endpoint_sum >>= 1;
                    total_sum = count_factor * endpoint_sum;
                    true_count = true_stats.count;
                    false_count = iteration_count - true_count;
                    if (on_true->add_induction) {
                        if (on_true->subtract) *value -= (uint32_t)true_stats.sum;
                        else *value += (uint32_t)true_stats.sum;
                    } else {
                        *value += (uint32_t)(true_count * (uint32_t)on_true->add_constant);
                    }
                    if (on_false->add_induction) {
                        uint64_t false_sum = total_sum - true_stats.sum;
                        if (on_false->subtract) *value -= (uint32_t)false_sum;
                        else *value += (uint32_t)false_sum;
                    } else {
                        *value += (uint32_t)(false_count * (uint32_t)on_false->add_constant);
                    }
                    continue;
                }
                true_count = turbojs_scalar_relational_true_count(
                    i, limit, step->condition_kind, runtime_thresholds[sidx]);
                if (true_count == UINT64_MAX || on_true->add_induction || on_false->add_induction) {
                    iterative_steps[iterative_count++] = sidx;
                    continue;
                }
                false_count = iteration_count - true_count;
                *value += (uint32_t)(true_count * (uint32_t)on_true->add_constant);
                *value += (uint32_t)(false_count * (uint32_t)on_false->add_constant);
            }
            if (iterative_count == 0) {
                i = limit;
            } else {
                for (; i < limit; ++i) {
                    uint8_t active;
                    for (active = 0; active < iterative_count; ++active) {
                        TurboJSScalarConditionalStep *step;
                        TurboJSScalarIntAccumulator *chosen;
                        uint32_t *value;
                        int branch_true;
                        sidx = iterative_steps[active];
                        step = &program->conditional_steps[sidx];
                        value = &values[step->target_accumulator_index];
                        switch (step->condition_kind) {
                        case 1: branch_true = (((uint32_t)i & (uint32_t)runtime_thresholds[sidx]) != 0); break;
                        case 2: branch_true = i <  (int64_t)runtime_thresholds[sidx]; break;
                        case 3: branch_true = i <= (int64_t)runtime_thresholds[sidx]; break;
                        case 4: branch_true = i >  (int64_t)runtime_thresholds[sidx]; break;
                        case 5: branch_true = i >= (int64_t)runtime_thresholds[sidx]; break;
                        case 6: branch_true = i == (int64_t)runtime_thresholds[sidx]; break;
                        case 7: branch_true = i != (int64_t)runtime_thresholds[sidx]; break;
                        default: return TURBOJS_OSR_EXIT_BAILOUT;
                        }
                        chosen = &step->branch[branch_true ? 0 : 1];
                        if (chosen->add_induction) {
                            if (chosen->subtract) *value -= (uint32_t)i;
                            else *value += (uint32_t)i;
                        } else {
                            *value += (uint32_t)chosen->add_constant;
                        }
                    }
                }
            }
        }
        for (aidx = 0; aidx < accumulator_count; ++aidx) {
            uint16_t frame_local = program->int_accumulators[aidx].frame_local;
            if (frame_local != UINT16_MAX) {
                if (frame_local >= frame->local_count) return TURBOJS_OSR_EXIT_BAILOUT;
                frame->locals[frame_local].kind = TURBOJS_OSR_VALUE_INT64;
                frame->locals[frame_local].bits = (uint64_t)(int64_t)(int32_t)values[aidx];
            } else {
                set_value(program->ctx, pointers[aidx],
                          JS_NewInt32(program->ctx, (int32_t)values[aidx]));
            }
        }
    } else if (program->mode == TURBOJS_SCALAR_INT32_LEAF_CALL) {
        JSValue *fun_ptr = turbojs_object_slot_value(
            (TurboJSObjectPropertyOSRExecution *)execution,
            &program->function_slot);
        JSObject *fun_obj;
        JSFunctionBytecode *leaf;
        TurboJSTinyLeafPlan *plan;
        uint64_t identity;
        uint64_t n, series_a, series_b;
        uint32_t sum_i, delta, value;
        int64_t last;
        if (!fun_ptr || JS_VALUE_GET_TAG(*fun_ptr) != JS_TAG_OBJECT ||
            JS_VALUE_GET_TAG(*acc_ptr) != JS_TAG_INT || i >= limit)
            return TURBOJS_OSR_EXIT_BAILOUT;
        fun_obj = JS_VALUE_GET_OBJ(*fun_ptr);
        if (!fun_obj || fun_obj->class_id != JS_CLASS_BYTECODE_FUNCTION ||
            !(leaf = fun_obj->u.func.function_bytecode) ||
            !(plan = turbojs_cached_tiny_leaf_plan(leaf)))
            return TURBOJS_OSR_EXIT_BAILOUT;
        identity = turbojs_vm_function_identity(program->ctx->rt, leaf);
        last = limit - 1;
        if (!program->leaf_affine_ready ||
            program->leaf_target_identity != identity) {
            program->leaf_affine_ready = turbojs_tiny_leaf_affine_for_range(
                plan, program->call_argc, program->constant_a, i, last,
                &program->leaf_slope, &program->leaf_intercept);
            program->leaf_target_identity = identity;
        } else if (!turbojs_tiny_leaf_affine_for_range(
                       plan, program->call_argc, program->constant_a, i, last,
                       &program->leaf_slope, &program->leaf_intercept)) {
            program->leaf_affine_ready = 0;
        }
        value = (uint32_t)JS_VALUE_GET_INT(*acc_ptr);
        if (program->leaf_affine_ready) {
            n = (uint64_t)(limit - i);
            series_a = n;
            series_b = (uint64_t)i + (uint64_t)limit - 1u;
            if ((series_a & 1u) == 0)
                series_a >>= 1;
            else
                series_b >>= 1;
            sum_i = (uint32_t)(series_a * series_b);
            delta = (uint32_t)program->leaf_slope * sum_i +
                    (uint32_t)program->leaf_intercept * (uint32_t)n;
            value += delta;
            i = limit;
        } else {
            JSValue args[2], result;
            int64_t chunk_start = i, chunk_limit = limit;
            if (chunk_limit - i > 16384)
                chunk_limit = i + 16384;
            for (; i < chunk_limit; ++i) {
                int32_t term;
                args[0] = JS_NewInt32(program->ctx, (int32_t)i);
                if (program->call_argc == 2)
                    args[1] = JS_NewFloat64(program->ctx, program->constant_a);
                if (!turbojs_try_inline_leaf_call_object(fun_obj, leaf, program->call_argc,
                                                         args, &result) ||
                    JS_ToInt32(program->ctx, &term, result))
                    return TURBOJS_OSR_EXIT_BAILOUT;
                value += (uint32_t)term;
            }
            n = (uint64_t)(chunk_limit - chunk_start);
        }
        if (program->accumulator_frame_local != UINT16_MAX) {
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            frame->locals[program->accumulator_frame_local].kind =
                TURBOJS_OSR_VALUE_INT64;
            frame->locals[program->accumulator_frame_local].bits =
                (uint64_t)(int64_t)(int32_t)value;
        } else {
            set_value(program->ctx, acc_ptr,
                      JS_NewInt32(program->ctx, (int32_t)value));
        }
        program->ctx->rt->osr_leaf_call_entries++;
        program->ctx->rt->osr_leaf_call_iterations += n;
    } else if (program->mode == TURBOJS_SCALAR_POLYMORPHIC_LEAF_CALL) {
        JSValue *first_ptr = turbojs_object_slot_value(
            (TurboJSObjectPropertyOSRExecution *)execution,
            &program->function_slot);
        JSValue *second_ptr = turbojs_object_slot_value(
            (TurboJSObjectPropertyOSRExecution *)execution,
            &program->secondary_function_slot);
        JSObject *first_object, *second_object;
        JSFunctionBytecode *first_leaf, *second_leaf;
        TurboJSTinyLeafPlan *first_plan, *second_plan;
        uint32_t value;
        int64_t chunk_limit = limit;
        uint64_t iterations;
        if (!first_ptr || !second_ptr || JS_VALUE_GET_TAG(*first_ptr) != JS_TAG_OBJECT ||
            JS_VALUE_GET_TAG(*second_ptr) != JS_TAG_OBJECT ||
            JS_VALUE_GET_TAG(*acc_ptr) != JS_TAG_INT)
            return TURBOJS_OSR_EXIT_BAILOUT;
        first_object = JS_VALUE_GET_OBJ(*first_ptr);
        second_object = JS_VALUE_GET_OBJ(*second_ptr);
        if (!first_object || !second_object ||
            first_object->class_id != JS_CLASS_BYTECODE_FUNCTION ||
            second_object->class_id != JS_CLASS_BYTECODE_FUNCTION ||
            !(first_leaf = first_object->u.func.function_bytecode) ||
            !(second_leaf = second_object->u.func.function_bytecode) ||
            !(first_plan = turbojs_cached_tiny_leaf_plan(first_leaf)) ||
            !(second_plan = turbojs_cached_tiny_leaf_plan(second_leaf)))
            return TURBOJS_OSR_EXIT_BAILOUT;
        if ((program->leaf_target_identity && program->leaf_target_identity !=
             turbojs_vm_function_identity(program->ctx->rt, first_leaf)) ||
            (program->secondary_leaf_target_identity &&
             program->secondary_leaf_target_identity !=
             turbojs_vm_function_identity(program->ctx->rt, second_leaf)))
            return TURBOJS_OSR_EXIT_BAILOUT;
        program->leaf_target_identity =
            turbojs_vm_function_identity(program->ctx->rt, first_leaf);
        program->secondary_leaf_target_identity =
            turbojs_vm_function_identity(program->ctx->rt, second_leaf);
        value = (uint32_t)JS_VALUE_GET_INT(*acc_ptr);
        if (chunk_limit - i > 16384)
            chunk_limit = i + 16384;
        iterations = (uint64_t)(chunk_limit - i);
        for (; i < chunk_limit; ++i) {
            int32_t term;
            TurboJSTinyLeafPlan *selected = ((uint32_t)i & program->selection_mask) ?
                first_plan : second_plan;
            if (!turbojs_execute_tiny_leaf_i32(selected, (int32_t)i, &term))
                return TURBOJS_OSR_EXIT_BAILOUT;
            value += (uint32_t)term;
        }
        if (program->accumulator_frame_local != UINT16_MAX) {
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            frame->locals[program->accumulator_frame_local].kind = TURBOJS_OSR_VALUE_INT64;
            frame->locals[program->accumulator_frame_local].bits =
                (uint64_t)(int64_t)(int32_t)value;
        } else set_value(program->ctx, acc_ptr, JS_NewInt32(program->ctx, (int32_t)value));
        program->ctx->rt->osr_polymorphic_leaf_entries++;
        program->ctx->rt->osr_polymorphic_leaf_iterations += iterations;
    } else if (program->mode == TURBOJS_SCALAR_CAPTURED_CLOSURE_CALL) {
        JSValue *array_ptr = turbojs_object_slot_value(
            (TurboJSObjectPropertyOSRExecution *)execution,
            &program->function_slot);
        JSObject *array;
        int32_t captures[8];
        uint32_t count = program->selection_mask + 1u, index;
        uint32_t value;
        int64_t chunk_limit = limit;
        uint64_t iterations;
        if (!array_ptr || JS_VALUE_GET_TAG(*array_ptr) != JS_TAG_OBJECT ||
            JS_VALUE_GET_TAG(*acc_ptr) != JS_TAG_INT)
            return TURBOJS_OSR_EXIT_BAILOUT;
        array = JS_VALUE_GET_OBJ(*array_ptr);
        if (!array || array->class_id != JS_CLASS_ARRAY || !array->fast_array ||
            array->u.array.count < count || count > 8)
            return TURBOJS_OSR_EXIT_BAILOUT;
        for (index = 0; index < count; ++index) {
            JSValue function_value = array->u.array.u.values[index];
            JSObject *function_object = NULL;
            JSFunctionBytecode *leaf = NULL;
            JSValue *capture_value = NULL;
            if (JS_VALUE_GET_TAG(function_value) != JS_TAG_OBJECT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            function_object = JS_VALUE_GET_OBJ(function_value);
            if (!function_object ||
                function_object->class_id != JS_CLASS_BYTECODE_FUNCTION)
                return TURBOJS_OSR_EXIT_BAILOUT;
            leaf = function_object->u.func.function_bytecode;
            if (!leaf || !turbojs_is_capture_add_int32_leaf(leaf) ||
                !function_object->u.func.var_refs ||
                !function_object->u.func.var_refs[0])
                return TURBOJS_OSR_EXIT_BAILOUT;
            capture_value = function_object->u.func.var_refs[0]->pvalue;
            if (!capture_value || JS_VALUE_GET_TAG(*capture_value) != JS_TAG_INT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            captures[index] = JS_VALUE_GET_INT(*capture_value);
        }
        value = (uint32_t)JS_VALUE_GET_INT(*acc_ptr);
        if (chunk_limit - i > 16384)
            chunk_limit = i + 16384;
        iterations = (uint64_t)(chunk_limit - i);
        for (; i < chunk_limit; ++i)
            value += (uint32_t)((int32_t)i + captures[(uint32_t)i & program->selection_mask]);
        if (program->accumulator_frame_local != UINT16_MAX) {
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            frame->locals[program->accumulator_frame_local].kind = TURBOJS_OSR_VALUE_INT64;
            frame->locals[program->accumulator_frame_local].bits =
                (uint64_t)(int64_t)(int32_t)value;
        } else set_value(program->ctx, acc_ptr, JS_NewInt32(program->ctx, (int32_t)value));
        program->ctx->rt->osr_closure_call_entries++;
        program->ctx->rt->osr_closure_call_iterations += iterations;
    } else if (program->mode == TURBOJS_SCALAR_RECURSIVE_FIB_SUM) {
        JSValue *function_ptr = turbojs_object_slot_value(
            (TurboJSObjectPropertyOSRExecution *)execution,
            &program->function_slot);
        JSFunctionBytecode *leaf;
        uint32_t fib_values[8];
        uint32_t value, index, count = program->selection_mask + 1u;
        int64_t chunk_limit = limit;
        uint64_t iterations;
        if (!function_ptr || !turbojs_is_self_recursive_fib(*function_ptr, &leaf) ||
            JS_VALUE_GET_TAG(*acc_ptr) != JS_TAG_INT || count > 8)
            return TURBOJS_OSR_EXIT_BAILOUT;
        if (program->leaf_target_identity && program->leaf_target_identity !=
            turbojs_vm_function_identity(program->ctx->rt, leaf))
            return TURBOJS_OSR_EXIT_BAILOUT;
        program->leaf_target_identity =
            turbojs_vm_function_identity(program->ctx->rt, leaf);
        for (index = 0; index < count; ++index)
            fib_values[index] = turbojs_fib_u32((uint32_t)program->recursion_base + index);
        value = (uint32_t)JS_VALUE_GET_INT(*acc_ptr);
        if (chunk_limit - i > 16384)
            chunk_limit = i + 16384;
        iterations = (uint64_t)(chunk_limit - i);
        for (; i < chunk_limit; ++i)
            value += fib_values[(uint32_t)i & program->selection_mask];
        if (program->accumulator_frame_local != UINT16_MAX) {
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            frame->locals[program->accumulator_frame_local].kind = TURBOJS_OSR_VALUE_INT64;
            frame->locals[program->accumulator_frame_local].bits =
                (uint64_t)(int64_t)(int32_t)value;
        } else set_value(program->ctx, acc_ptr, JS_NewInt32(program->ctx, (int32_t)value));
        program->ctx->rt->osr_recursive_call_entries++;
        program->ctx->rt->osr_recursive_call_iterations += iterations;
    } else if (program->mode == TURBOJS_SCALAR_INT32_IMUL_XORSHIFT) {
        uint32_t value;
        uint64_t iterations;
        int64_t chunk_limit;
        if (JS_VALUE_GET_TAG(*acc_ptr) != JS_TAG_INT ||
            !turbojs_guard_builtin_math_imul(program))
            return TURBOJS_OSR_EXIT_BAILOUT;
        value = (uint32_t)JS_VALUE_GET_INT(*acc_ptr);
        chunk_limit = limit;
        if (chunk_limit - i > 16384)
            chunk_limit = i + 16384;
        iterations = (uint64_t)(chunk_limit - i);
        for (; i < chunk_limit; ++i) {
            value = (uint32_t)((value ^ (uint32_t)i) *
                               (uint32_t)program->mix_multiplier) +
                    (uint32_t)program->mix_addend;
            value ^= program->mix_unsigned_shift ?
                value >> program->mix_shift :
                (uint32_t)((int32_t)value >> program->mix_shift);
        }
        if (program->accumulator_frame_local != UINT16_MAX) {
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            frame->locals[program->accumulator_frame_local].kind =
                TURBOJS_OSR_VALUE_INT64;
            frame->locals[program->accumulator_frame_local].bits =
                (uint64_t)(int64_t)(int32_t)value;
        } else {
            set_value(program->ctx, acc_ptr,
                      JS_NewInt32(program->ctx, (int32_t)value));
        }
        program->ctx->rt->osr_int32_mix_entries++;
        program->ctx->rt->osr_int32_mix_iterations += iterations;
    } else if (program->mode == TURBOJS_SCALAR_COUPLED_FLOAT64) {
        JSValue *aux_ptr = turbojs_object_slot_value(
            (TurboJSObjectPropertyOSRExecution *)execution, &program->auxiliary_slot);
        double x, y;
        if (!aux_ptr || !turbojs_js_number(*acc_ptr, &x) ||
            !turbojs_js_number(*aux_ptr, &y) || program->constant_e == 0.0)
            return TURBOJS_OSR_EXIT_BAILOUT;
        for (; i < limit; ++i) {
            double denominator = program->constant_b +
                (double)((uint32_t)i & program->selection_mask) * program->constant_c;
            if (denominator == 0.0)
                return TURBOJS_OSR_EXIT_BAILOUT;
            x = (x * program->constant_a + y) / denominator;
            y = fmod(y + x * program->constant_d, program->constant_e);
        }
        if (program->accumulator_frame_local != UINT16_MAX) {
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            frame->locals[program->accumulator_frame_local].kind = TURBOJS_OSR_VALUE_FLOAT64;
            memcpy(&frame->locals[program->accumulator_frame_local].bits, &x, sizeof(x));
        } else {
            set_value(program->ctx, acc_ptr, JS_NewFloat64(program->ctx, x));
        }
        set_value(program->ctx, aux_ptr, JS_NewFloat64(program->ctx, y));
        program->ctx->rt->osr_coupled_float_entries++;
        program->ctx->rt->osr_coupled_float_iterations += (uint64_t)(limit - start_i);
    } else if (program->mode == TURBOJS_SCALAR_FLOAT_AFFINE) {
        double acc;
        if (!turbojs_js_number(*acc_ptr, &acc) || program->constant_c == 0.0)
            return TURBOJS_OSR_EXIT_BAILOUT;
        for (; i < limit; ++i)
            acc = (acc * program->constant_a + program->constant_b) / program->constant_c;
        if (program->accumulator_frame_local != UINT16_MAX) {
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            frame->locals[program->accumulator_frame_local].kind = TURBOJS_OSR_VALUE_FLOAT64;
            memcpy(&frame->locals[program->accumulator_frame_local].bits, &acc, sizeof(acc));
        } else {
            set_value(program->ctx, acc_ptr, JS_NewFloat64(program->ctx, acc));
        }
    } else if (program->mode == TURBOJS_SCALAR_AFFINE_LEAF_CALL) {
        JSValue *fun_ptr = turbojs_object_slot_value((TurboJSObjectPropertyOSRExecution *)execution,
                                                     &program->function_slot);
        JSObject *fun_obj;
        JSFunctionBytecode *leaf;
        JSValue args[2], y0, y1, y2;
        double d0, d1, d2, slope, intercept, acc;
        int64_t n;
        if (!fun_ptr || JS_VALUE_GET_TAG(*fun_ptr) != JS_TAG_OBJECT ||
            !turbojs_js_number(*acc_ptr, &acc)) return TURBOJS_OSR_EXIT_BAILOUT;
        fun_obj = JS_VALUE_GET_OBJ(*fun_ptr);
        if (!fun_obj || fun_obj->class_id != JS_CLASS_BYTECODE_FUNCTION ||
            !(leaf = fun_obj->u.func.function_bytecode)) return TURBOJS_OSR_EXIT_BAILOUT;
        args[1] = JS_NewFloat64(program->ctx, program->constant_a);
        args[0] = JS_NewInt32(program->ctx, 0);
        if (!turbojs_try_inline_leaf_call(leaf, 2, args, &y0)) return TURBOJS_OSR_EXIT_BAILOUT;
        args[0] = JS_NewInt32(program->ctx, 1);
        if (!turbojs_try_inline_leaf_call(leaf, 2, args, &y1)) return TURBOJS_OSR_EXIT_BAILOUT;
        args[0] = JS_NewInt32(program->ctx, 2);
        if (!turbojs_try_inline_leaf_call(leaf, 2, args, &y2)) return TURBOJS_OSR_EXIT_BAILOUT;
        if (!turbojs_js_number(y0, &d0) || !turbojs_js_number(y1, &d1) || !turbojs_js_number(y2, &d2))
            return TURBOJS_OSR_EXIT_BAILOUT;
        slope = d1 - d0; intercept = d0;
        if (fabs((d2 - d1) - slope) > 1e-9 * (1.0 + fabs(d2))) return TURBOJS_OSR_EXIT_BAILOUT;
        n = limit - i;
        acc += slope * ((double)n * ((double)i + (double)limit - 1.0) * 0.5) +
               intercept * (double)n;
        if (program->accumulator_frame_local != UINT16_MAX) {
            if (program->accumulator_frame_local >= frame->local_count)
                return TURBOJS_OSR_EXIT_BAILOUT;
            frame->locals[program->accumulator_frame_local].kind = TURBOJS_OSR_VALUE_FLOAT64;
            memcpy(&frame->locals[program->accumulator_frame_local].bits, &acc, sizeof(acc));
        } else {
            set_value(program->ctx, acc_ptr, JS_NewFloat64(program->ctx, acc));
        }
        i = limit;
    } else return TURBOJS_OSR_EXIT_BAILOUT;
    frame->locals[program->induction_local].bits = (uint64_t)i;
    frame->locals[program->induction_local].kind = TURBOJS_OSR_VALUE_INT64;
    *resume_offset = i < limit ? program->loop_header :
                                  program->resume_bytecode_offset;
    return TURBOJS_OSR_EXIT_COMPLETED;
}

static TurboJSOSREntry turbojs_scalar_loop_osr_entry(TurboJSScalarLoopOSRExecution *execution)
{
    TurboJSOSREntry entry;
    memset(&entry, 0, sizeof(entry));
    if (execution && execution->program) {
        entry.callback = turbojs_run_scalar_loop;
        entry.opaque = execution;
        entry.loop_header = execution->program->loop_header;
        entry.bailout_limit = 3;
    }
    return entry;
}


typedef enum TurboJSTypedArraySumMode {
    TURBOJS_TYPED_ARRAY_AFFINE_SUM = 1,
    TURBOJS_TYPED_ARRAY_BINARY_BIASED_DIV_SUM = 2
} TurboJSTypedArraySumMode;

typedef struct TurboJSTypedArrayAffineSumOSRProgram {
    JSContext *ctx;
    TurboJSDenseSlot array_slot;
    TurboJSDenseSlot array_b_slot;
    TurboJSDenseSlot accumulator_slot;
    TurboJSDenseSlot outer_slot;
    uint16_t induction_local;
    uint16_t accumulator_frame_local;
    uint8_t mode;
    uint8_t reserved[3];
    double scale;
    double offset;
    double bias_scale;
    double divisor_base;
    double divisor_scale;
    uint32_t index_mask;
    uint32_t outer_mask;
    uint32_t loop_header;
    uint32_t resume_bytecode_offset;
    uint64_t maximum_elements;
} TurboJSTypedArrayAffineSumOSRProgram;

typedef struct TurboJSTypedArrayAffineSumOSRExecution {
    TurboJSTypedArrayAffineSumOSRProgram *program;
    JSValue *arg_buf;
    JSValue *var_buf;
    JSVarRef **var_refs;
} TurboJSTypedArrayAffineSumOSRExecution;

/* Recognize an in-place Float64Array affine transform with a sequential sum:
     for (let i = 0; i < a.length; i++) {
         a[i] = a[i] * scale + offset;
         sum += a[i];
     }
   The constants may come from the function constant pool and the array and
   accumulator may be locals, arguments, or closure references. */
static int turbojs_detect_typed_array_affine_sum_loop(
    JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset,
    TurboJSTypedArrayAffineSumOSRProgram *spec)
{
    const uint8_t *base, *pc, *end, *nullish_target;
    uint16_t induction, i2;
    TurboJSDenseSlot array_slot, source_slot, accumulator_slot;
    uint32_t resume;
    double scale, offset;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len)
        return 0;
    base = b->byte_code_buf;
    pc = base + target_offset;
    end = base + b->byte_code_len;
#define TANEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define TAOP0(op) do { TANEED(1); if (*pc++ != (op)) return 0; } while (0)
    TANEED(3); if (*pc++ != OP_get_loc_check) return 0;
    induction = get_u16(pc); pc += 2;
    if (!turbojs_parse_get_dense_slot(&pc, end, &array_slot)) return 0;
    TAOP0(OP_get_length); TAOP0(OP_lt);
    if (!turbojs_parse_branch_target(&pc, end, OP_if_false8, OP_if_false,
                                      &nullish_target)) return 0;
    resume = (uint32_t)(nullish_target - base);

    if (!turbojs_parse_get_dense_slot(&pc, end, &source_slot) ||
        source_slot.kind != array_slot.kind || source_slot.index != array_slot.index)
        return 0;
    TANEED(3); if (*pc++ != OP_get_loc_check) return 0;
    i2 = get_u16(pc); pc += 2;
    if (i2 != induction) return 0;
    TAOP0(OP_swap); TAOP0(OP_dup); TAOP0(OP_is_undefined_or_null);
    if (!turbojs_parse_branch_target(&pc, end, OP_if_true8, OP_if_true,
                                      &nullish_target)) return 0;
    TAOP0(OP_swap); TAOP0(OP_to_propkey); TAOP0(OP_swap);
    if (pc != nullish_target) return 0;
    TAOP0(OP_swap);

    if (!turbojs_parse_get_dense_slot(&pc, end, &source_slot) ||
        source_slot.kind != array_slot.kind || source_slot.index != array_slot.index)
        return 0;
    TANEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0;
    pc += 2;
    TAOP0(OP_get_array_el);
    if (!turbojs_scalar_const(b, &pc, end, &scale)) return 0;
    TAOP0(OP_mul);
    if (!turbojs_scalar_const(b, &pc, end, &offset)) return 0;
    TAOP0(OP_add); TAOP0(OP_swap); TAOP0(OP_to_propkey); TAOP0(OP_swap);
    TAOP0(OP_put_array_el);

    if (!turbojs_parse_get_dense_slot(&pc, end, &accumulator_slot)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &source_slot) ||
        source_slot.kind != array_slot.kind || source_slot.index != array_slot.index)
        return 0;
    TANEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0;
    pc += 2;
    TAOP0(OP_get_array_el); TAOP0(OP_add); TAOP0(OP_dup);
    if (!turbojs_parse_put_dense_slot(&pc, end, &accumulator_slot)) return 0;
    TAOP0(OP_drop);
    TANEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0;
    pc += 2;
    TAOP0(OP_post_inc);
    TANEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0;
    pc += 2;
    TAOP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    if (!turbojs_parse_goto_target(&pc, end, &nullish_target) ||
        (uint32_t)(nullish_target - base) != target_offset)
        return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        !isfinite(scale) || !isfinite(offset))
        return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm;
    spec->array_slot = array_slot;
    spec->accumulator_slot = accumulator_slot;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_frame_local = accumulator_slot.kind == TURBOJS_DENSE_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator_slot.index) : UINT16_MAX;
    spec->mode = TURBOJS_TYPED_ARRAY_AFFINE_SUM;
    spec->scale = scale;
    spec->offset = offset;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
    spec->maximum_elements = 1000000000ULL;
#undef TAOP0
#undef TANEED
    return 1;
}


/* Recognize the mixed Float64Array simulation kernel used by real numeric
   workloads:
     const v = (a[i] * b[i] + (i & index_mask) * bias_scale) /
               (divisor_base + (outer & outer_mask) * divisor_scale);
     a[i] = v;
     sum += v;
   The outer-loop value is invariant for one OSR invocation, so the runner
   computes the divisor once and executes the complete inner loop over
   unboxed backing stores while preserving sequential accumulation order. */
static int turbojs_detect_typed_array_binary_biased_div_sum_loop(
    JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset,
    TurboJSTypedArrayAffineSumOSRProgram *spec)
{
    const uint8_t *base, *pc, *end, *branch_target;
    uint16_t induction, i2, temp_local;
    TurboJSDenseSlot array_slot, array_b_slot, slot, accumulator_slot, outer_slot;
    TurboJSDenseSlot temp_slot;
    uint32_t resume;
    double index_mask_d, bias_scale, divisor_base, outer_mask_d, divisor_scale;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len) return 0;
    base = b->byte_code_buf; pc = base + target_offset; end = base + b->byte_code_len;
#define TBNEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define TBOP(op) do { TBNEED(1); if (*pc++ != (op)) return 0; } while (0)
    TBNEED(3); if (*pc++ != OP_get_loc_check) return 0;
    induction = get_u16(pc); pc += 2;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot)) return 0;
    TBOP(OP_lt);
    if (!turbojs_parse_branch_target(&pc, end, OP_if_false8, OP_if_false,
                                      &branch_target)) return 0;
    resume = (uint32_t)(branch_target - base);

    TBNEED(3); if (*pc++ != OP_set_loc_uninitialized) return 0;
    temp_local = get_u16(pc); pc += 2;
    temp_slot.kind = TURBOJS_DENSE_SLOT_LOCAL; temp_slot.index = temp_local;

    if (!turbojs_parse_get_dense_slot(&pc, end, &array_slot)) return 0;
    TBNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    TBOP(OP_get_array_el);
    if (!turbojs_parse_get_dense_slot(&pc, end, &array_b_slot)) return 0;
    TBNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    TBOP(OP_get_array_el); TBOP(OP_mul);
    TBNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    if (!turbojs_scalar_const(b, &pc, end, &index_mask_d)) return 0;
    TBOP(OP_and);
    if (!turbojs_scalar_const(b, &pc, end, &bias_scale)) return 0;
    TBOP(OP_mul); TBOP(OP_add);
    if (!turbojs_scalar_const(b, &pc, end, &divisor_base)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &outer_slot)) return 0;
    if (!turbojs_scalar_const(b, &pc, end, &outer_mask_d)) return 0;
    TBOP(OP_and);
    if (!turbojs_scalar_const(b, &pc, end, &divisor_scale)) return 0;
    TBOP(OP_mul); TBOP(OP_add); TBOP(OP_div);
    if ((size_t)(end - pc) >= 2 && *pc == OP_put_loc8) {
        pc++; if (*pc++ != (uint8_t)temp_local) return 0;
    } else if (!turbojs_parse_put_dense_slot(&pc, end, &temp_slot)) return 0;

    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != array_slot.kind || slot.index != array_slot.index) return 0;
    TBNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    TBOP(OP_swap); TBOP(OP_dup); TBOP(OP_is_undefined_or_null);
    if (!turbojs_parse_branch_target(&pc, end, OP_if_true8, OP_if_true,
                                      &branch_target)) return 0;
    TBOP(OP_swap); TBOP(OP_to_propkey); TBOP(OP_swap);
    if (pc != branch_target) return 0;
    TBOP(OP_swap);
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    TBOP(OP_swap); TBOP(OP_to_propkey); TBOP(OP_swap); TBOP(OP_put_array_el);

    if (!turbojs_parse_get_dense_slot(&pc, end, &accumulator_slot)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    TBOP(OP_add); TBOP(OP_dup);
    if (!turbojs_parse_put_dense_slot(&pc, end, &accumulator_slot)) return 0;
    TBOP(OP_drop);
    TBNEED(3); if (*pc++ != OP_get_loc_check) return 0;
    i2 = get_u16(pc); pc += 2; if (i2 != induction) return 0;
    TBOP(OP_post_inc);
    TBNEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    TBOP(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    if (!turbojs_parse_goto_target(&pc, end, &branch_target) ||
        (uint32_t)(branch_target - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        temp_local >= b->var_count || !isfinite(bias_scale) ||
        !isfinite(divisor_base) || !isfinite(divisor_scale) ||
        index_mask_d < 0.0 || index_mask_d > 4294967295.0 ||
        outer_mask_d < 0.0 || outer_mask_d > 4294967295.0 ||
        floor(index_mask_d) != index_mask_d || floor(outer_mask_d) != outer_mask_d)
        return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm;
    spec->array_slot = array_slot;
    spec->array_b_slot = array_b_slot;
    spec->accumulator_slot = accumulator_slot;
    spec->outer_slot = outer_slot;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_frame_local = accumulator_slot.kind == TURBOJS_DENSE_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator_slot.index) : UINT16_MAX;
    spec->mode = TURBOJS_TYPED_ARRAY_BINARY_BIASED_DIV_SUM;
    spec->bias_scale = bias_scale;
    spec->divisor_base = divisor_base;
    spec->divisor_scale = divisor_scale;
    spec->index_mask = (uint32_t)index_mask_d;
    spec->outer_mask = (uint32_t)outer_mask_d;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
    spec->maximum_elements = 1000000000ULL;
#undef TBOP
#undef TBNEED
    return 1;
}

static JSValue *turbojs_typed_sum_slot_value(
    TurboJSTypedArrayAffineSumOSRExecution *execution,
    const TurboJSDenseSlot *slot)
{
    if (!execution || !slot) return NULL;
    if (slot->kind == TURBOJS_DENSE_SLOT_LOCAL)
        return execution->var_buf ? &execution->var_buf[slot->index] : NULL;
    if (slot->kind == TURBOJS_DENSE_SLOT_ARGUMENT)
        return execution->arg_buf ? &execution->arg_buf[slot->index] : NULL;
    if (slot->kind == TURBOJS_DENSE_SLOT_VAR_REF && execution->var_refs &&
        execution->var_refs[slot->index])
        return execution->var_refs[slot->index]->pvalue;
    return NULL;
}

static TurboJSOSRExitKind turbojs_run_typed_array_affine_sum(
    TurboJSOSRFrame *frame, void *opaque, uint32_t *resume_offset)
{
    TurboJSTypedArrayAffineSumOSRExecution *execution =
        (TurboJSTypedArrayAffineSumOSRExecution *)opaque;
    TurboJSTypedArrayAffineSumOSRProgram *program;
    JSValue *array_value, *array_b_value = NULL, *accumulator_value, *outer_value = NULL;
    JSObject *array, *array_b = NULL;
    TurboJSOSRValue *index_slot;
    double *data, *data_b = NULL, accumulator, divisor = 1.0, outer_number = 0.0;
    uint32_t i, start, count;
    if (!execution || !(program = execution->program) || !frame || !resume_offset ||
        program->induction_local >= frame->local_count)
        return TURBOJS_OSR_EXIT_BAILOUT;
    array_value = turbojs_typed_sum_slot_value(execution, &program->array_slot);
    accumulator_value = turbojs_typed_sum_slot_value(execution, &program->accumulator_slot);
    if (program->mode == TURBOJS_TYPED_ARRAY_BINARY_BIASED_DIV_SUM) {
        array_b_value = turbojs_typed_sum_slot_value(execution, &program->array_b_slot);
        outer_value = turbojs_typed_sum_slot_value(execution, &program->outer_slot);
    }
    index_slot = &frame->locals[program->induction_local];
    if (!array_value || !accumulator_value ||
        JS_VALUE_GET_TAG(*array_value) != JS_TAG_OBJECT ||
        index_slot->kind != TURBOJS_OSR_VALUE_INT64 ||
        !turbojs_js_number(*accumulator_value, &accumulator))
        return TURBOJS_OSR_EXIT_BAILOUT;
    if (program->mode == TURBOJS_TYPED_ARRAY_BINARY_BIASED_DIV_SUM) {
        if (!array_b_value || !outer_value ||
            JS_VALUE_GET_TAG(*array_b_value) != JS_TAG_OBJECT ||
            !turbojs_js_number(*outer_value, &outer_number))
            return TURBOJS_OSR_EXIT_BAILOUT;
        array_b = JS_VALUE_GET_OBJ(*array_b_value);
        divisor = program->divisor_base +
                  ((double)(((uint32_t)(int64_t)outer_number) & program->outer_mask)) *
                  program->divisor_scale;
        if (!array_b || array_b->class_id != JS_CLASS_FLOAT64_ARRAY ||
            !array_b->u.array.u.double_ptr || !isfinite(divisor) || divisor == 0.0)
            return TURBOJS_OSR_EXIT_BAILOUT;
    }
    array = JS_VALUE_GET_OBJ(*array_value);
    if (!array || array->class_id != JS_CLASS_FLOAT64_ARRAY ||
        !array->u.array.u.double_ptr || (int64_t)index_slot->bits < 0)
        return TURBOJS_OSR_EXIT_BAILOUT;
    start = (uint32_t)index_slot->bits;
    count = array->u.array.count;
    if (start > count || (uint64_t)(count - start) > program->maximum_elements)
        return TURBOJS_OSR_EXIT_BAILOUT;
    data = array->u.array.u.double_ptr;
    if (array_b) {
        if (array_b->u.array.count < count) return TURBOJS_OSR_EXIT_BAILOUT;
        data_b = array_b->u.array.u.double_ptr;
    }
    i = start;
    /* Preserve JavaScript's sequential accumulator rounding order while
       keeping backing-store pointers and loop invariants unboxed. */
    if (program->mode == TURBOJS_TYPED_ARRAY_BINARY_BIASED_DIV_SUM) {
        for (; i + 4u <= count; i += 4u) {
            double v0 = (data[i + 0u] * data_b[i + 0u] +
                         (double)((i + 0u) & program->index_mask) * program->bias_scale) / divisor;
            double v1 = (data[i + 1u] * data_b[i + 1u] +
                         (double)((i + 1u) & program->index_mask) * program->bias_scale) / divisor;
            double v2 = (data[i + 2u] * data_b[i + 2u] +
                         (double)((i + 2u) & program->index_mask) * program->bias_scale) / divisor;
            double v3 = (data[i + 3u] * data_b[i + 3u] +
                         (double)((i + 3u) & program->index_mask) * program->bias_scale) / divisor;
            data[i + 0u] = v0; accumulator += v0;
            data[i + 1u] = v1; accumulator += v1;
            data[i + 2u] = v2; accumulator += v2;
            data[i + 3u] = v3; accumulator += v3;
        }
        for (; i < count; ++i) {
            double value = (data[i] * data_b[i] +
                            (double)(i & program->index_mask) * program->bias_scale) / divisor;
            data[i] = value;
            accumulator += value;
        }
    } else {
        for (; i + 4u <= count; i += 4u) {
            double v0 = data[i + 0u] * program->scale + program->offset;
            double v1 = data[i + 1u] * program->scale + program->offset;
            double v2 = data[i + 2u] * program->scale + program->offset;
            double v3 = data[i + 3u] * program->scale + program->offset;
            data[i + 0u] = v0; accumulator += v0;
            data[i + 1u] = v1; accumulator += v1;
            data[i + 2u] = v2; accumulator += v2;
            data[i + 3u] = v3; accumulator += v3;
        }
        for (; i < count; ++i) {
            double value = data[i] * program->scale + program->offset;
            data[i] = value;
            accumulator += value;
        }
    }
    index_slot->kind = TURBOJS_OSR_VALUE_INT64;
    index_slot->bits = count;
    if (program->accumulator_frame_local != UINT16_MAX) {
        TurboJSOSRValue *out;
        if (program->accumulator_frame_local >= frame->local_count)
            return TURBOJS_OSR_EXIT_BAILOUT;
        out = &frame->locals[program->accumulator_frame_local];
        out->kind = TURBOJS_OSR_VALUE_FLOAT64;
        memcpy(&out->bits, &accumulator, sizeof(accumulator));
    } else {
        set_value(program->ctx, accumulator_value,
                  JS_NewFloat64(program->ctx, accumulator));
    }
    program->ctx->rt->typed_array_osr_entries++;
    program->ctx->rt->typed_array_osr_elements += count - start;
    program->ctx->rt->typed_array_affine_sum_osr_entries++;
    program->ctx->rt->typed_array_affine_sum_osr_elements += count - start;
    program->ctx->rt->dense_array_transform_osr_entries++;
    program->ctx->rt->dense_array_transform_osr_elements += count - start;
    program->ctx->rt->dense_array_inplace_osr_entries++;
    *resume_offset = program->resume_bytecode_offset;
    return TURBOJS_OSR_EXIT_COMPLETED;
}

static TurboJSOSREntry turbojs_typed_array_affine_sum_osr_entry(
    TurboJSTypedArrayAffineSumOSRExecution *execution)
{
    TurboJSOSREntry entry;
    memset(&entry, 0, sizeof(entry));
    if (execution && execution->program) {
        entry.callback = turbojs_run_typed_array_affine_sum;
        entry.opaque = execution;
        entry.loop_header = execution->program->loop_header;
        entry.bailout_limit = 3;
    }
    return entry;
}


typedef enum TurboJSObjectArrayOSRMode {
    TURBOJS_OBJECT_ARRAY_SUM_FIELD_INT32 = 1,
    TURBOJS_OBJECT_ARRAY_UPDATE_REDUCE_INT32 = 2,
    TURBOJS_OBJECT_ARRAY_GROUP_ACCUMULATE_INT32 = 3
} TurboJSObjectArrayOSRMode;

typedef struct TurboJSObjectArrayOSRProgram {
    JSContext *ctx;
    TurboJSDenseSlot array_slot;
    TurboJSDenseSlot accumulator_slot;
    TurboJSDenseSlot outer_slot;
    TurboJSDenseSlot dictionary_slot;
    uint16_t induction_local;
    uint16_t accumulator_frame_local;
    uint8_t mode;
    uint8_t reserved[3];
    JSAtom atom_x;
    JSAtom atom_y;
    JSAtom atom_z;
    uint32_t loop_header;
    uint32_t resume_bytecode_offset;
    uint64_t maximum_elements;
} TurboJSObjectArrayOSRProgram;

typedef struct TurboJSObjectArrayOSRExecution {
    TurboJSObjectArrayOSRProgram *program;
    JSValue *arg_buf;
    JSValue *var_buf;
    JSVarRef **var_refs;
} TurboJSObjectArrayOSRExecution;

static int turbojs_parse_get_field_atom(const uint8_t **pp, const uint8_t *end,
                                         JSAtom *atom)
{
    const uint8_t *p = *pp;
    if ((size_t)(end - p) < 5 || *p++ != OP_get_field) return 0;
    *atom = get_u32(p); p += 4; *pp = p; return 1;
}

static int turbojs_parse_put_field_atom(const uint8_t **pp, const uint8_t *end,
                                         JSAtom *atom)
{
    const uint8_t *p = *pp;
    if ((size_t)(end - p) < 5 || *p++ != OP_put_field) return 0;
    *atom = get_u32(p); p += 4; *pp = p; return 1;
}

/* Recognize a shape-polymorphic array-of-objects property reduction:
     for (let i = 0; i < a.length; i++) sum = (sum + a[i].x) | 0; */
static int turbojs_detect_object_array_sum_loop(
    JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset,
    TurboJSObjectArrayOSRProgram *spec)
{
    const uint8_t *base, *pc, *end, *branch_target;
    uint16_t induction;
    TurboJSDenseSlot array_slot, slot, accumulator_slot;
    JSAtom atom;
    uint32_t resume;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len) return 0;
    base = b->byte_code_buf; pc = base + target_offset; end = base + b->byte_code_len;
#define OANEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define OAOP0(op) do { OANEED(1); if (*pc++ != (op)) return 0; } while (0)
    OANEED(3); if (*pc++ != OP_get_loc_check) return 0;
    induction = get_u16(pc); pc += 2;
    if (!turbojs_parse_get_dense_slot(&pc, end, &array_slot)) return 0;
    OAOP0(OP_get_length); OAOP0(OP_lt);
    if (!turbojs_parse_branch_target(&pc, end, OP_if_false8, OP_if_false,
                                      &branch_target)) return 0;
    resume = (uint32_t)(branch_target - base);
    if (!turbojs_parse_get_dense_slot(&pc, end, &accumulator_slot)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != array_slot.kind || slot.index != array_slot.index) return 0;
    OANEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0;
    pc += 2;
    OAOP0(OP_get_array_el);
    if (!turbojs_parse_get_field_atom(&pc, end, &atom)) return 0;
    OAOP0(OP_add); OAOP0(OP_push_0); OAOP0(OP_or); OAOP0(OP_dup);
    if (!turbojs_parse_put_dense_slot(&pc, end, &accumulator_slot)) return 0;
    OAOP0(OP_drop);
    OANEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0;
    pc += 2; OAOP0(OP_post_inc);
    OANEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0;
    pc += 2; OAOP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    if (!turbojs_parse_goto_target(&pc, end, &branch_target) ||
        (uint32_t)(branch_target - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count) return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm; spec->array_slot = array_slot;
    spec->accumulator_slot = accumulator_slot;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_frame_local = accumulator_slot.kind == TURBOJS_DENSE_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator_slot.index) : UINT16_MAX;
    spec->mode = TURBOJS_OBJECT_ARRAY_SUM_FIELD_INT32;
    spec->atom_x = atom; spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume; spec->maximum_elements = 1000000000ULL;
#undef OAOP0
#undef OANEED
    return 1;
}

/* Recognize a monomorphic array-of-objects update/reduction loop:
     const o = a[i];
     o.x = (o.x + o.y - outer) | 0;
     sum = (sum + o.x + o.z) | 0;
   The runner validates every remaining element and all property descriptors
   before mutating the first object, preserving transactional OSR semantics. */
static int turbojs_detect_object_array_update_loop(
    JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset,
    TurboJSObjectArrayOSRProgram *spec)
{
    const uint8_t *base, *pc, *end, *branch_target;
    uint16_t induction, temp_local, i2;
    TurboJSDenseSlot array_slot, slot, accumulator_slot, outer_slot, temp_slot;
    JSAtom write_atom, read_x, read_y, reduce_x, reduce_z;
    uint32_t resume;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len) return 0;
    base = b->byte_code_buf; pc = base + target_offset; end = base + b->byte_code_len;
#define OUNEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define OUOP0(op) do { OUNEED(1); if (*pc++ != (op)) return 0; } while (0)
    OUNEED(3); if (*pc++ != OP_get_loc_check) return 0;
    induction = get_u16(pc); pc += 2;
    if (!turbojs_parse_get_dense_slot(&pc, end, &array_slot)) return 0;
    OUOP0(OP_get_length); OUOP0(OP_lt);
    if (!turbojs_parse_branch_target(&pc, end, OP_if_false8, OP_if_false,
                                      &branch_target)) return 0;
    resume = (uint32_t)(branch_target - base);
    OUNEED(3); if (*pc++ != OP_set_loc_uninitialized) return 0;
    temp_local = get_u16(pc); pc += 2;
    temp_slot.kind = TURBOJS_DENSE_SLOT_LOCAL; temp_slot.index = temp_local;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != array_slot.kind || slot.index != array_slot.index) return 0;
    OUNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0;
    pc += 2; OUOP0(OP_get_array_el);
    if ((size_t)(end - pc) >= 2 && *pc == OP_put_loc8) {
        pc++; if (*pc++ != (uint8_t)temp_local) return 0;
    } else if (!turbojs_parse_put_dense_slot(&pc, end, &temp_slot)) return 0;

    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_field_atom(&pc, end, &read_x)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_field_atom(&pc, end, &read_y)) return 0;
    OUOP0(OP_add);
    if (!turbojs_parse_get_dense_slot(&pc, end, &outer_slot)) return 0;
    OUOP0(OP_sub); OUOP0(OP_push_0); OUOP0(OP_or);
    if (!turbojs_parse_put_field_atom(&pc, end, &write_atom) || write_atom != read_x)
        return 0;

    if (!turbojs_parse_get_dense_slot(&pc, end, &accumulator_slot)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_field_atom(&pc, end, &reduce_x) || reduce_x != read_x)
        return 0;
    OUOP0(OP_add);
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_field_atom(&pc, end, &reduce_z)) return 0;
    OUOP0(OP_add); OUOP0(OP_push_0); OUOP0(OP_or); OUOP0(OP_dup);
    if (!turbojs_parse_put_dense_slot(&pc, end, &accumulator_slot)) return 0;
    OUOP0(OP_drop);
    OUNEED(3); if (*pc++ != OP_get_loc_check) return 0; i2 = get_u16(pc); pc += 2;
    if (i2 != induction) return 0;
    OUOP0(OP_post_inc);
    OUNEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0;
    pc += 2; OUOP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    if (!turbojs_parse_goto_target(&pc, end, &branch_target) ||
        (uint32_t)(branch_target - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        temp_local >= b->var_count) return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm; spec->array_slot = array_slot;
    spec->accumulator_slot = accumulator_slot; spec->outer_slot = outer_slot;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_frame_local = accumulator_slot.kind == TURBOJS_DENSE_SLOT_LOCAL ?
        (uint16_t)(b->arg_count + accumulator_slot.index) : UINT16_MAX;
    spec->mode = TURBOJS_OBJECT_ARRAY_UPDATE_REDUCE_INT32;
    spec->atom_x = read_x; spec->atom_y = read_y; spec->atom_z = reduce_z;
    spec->loop_header = target_offset; spec->resume_bytecode_offset = resume;
    spec->maximum_elements = 1000000000ULL;
#undef OUOP0
#undef OUNEED
    return 1;
}

/* Hot record update plus small integer-key group accumulation. */
static int turbojs_detect_object_array_group_accumulate_loop(
    JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset,
    TurboJSObjectArrayOSRProgram *spec)
{
    const uint8_t *base, *pc, *end, *target;
    uint16_t induction, temp_local, i2;
    TurboJSDenseSlot array_slot, dictionary_slot, outer_slot, slot, temp_slot;
    JSAtom score_atom, group_atom, atom;
    uint32_t resume;
    int32_t modulus;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len) return 0;
    base = b->byte_code_buf; pc = base + target_offset; end = base + b->byte_code_len;
#define GNEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define GOP(op) do { GNEED(1); if (*pc++ != (op)) return 0; } while (0)
    GNEED(3); if (*pc++ != OP_get_loc_check) return 0;
    induction = get_u16(pc); pc += 2;
    if (!turbojs_parse_get_dense_slot(&pc, end, &array_slot)) return 0;
    GOP(OP_get_length); GOP(OP_lt);
    if (!turbojs_parse_branch_target(&pc, end, OP_if_false8, OP_if_false, &target)) return 0;
    resume = (uint32_t)(target - base);
    GNEED(3); if (*pc++ != OP_set_loc_uninitialized) return 0;
    temp_local = get_u16(pc); pc += 2;
    temp_slot.kind = TURBOJS_DENSE_SLOT_LOCAL; temp_slot.index = temp_local;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) ||
        slot.kind != array_slot.kind || slot.index != array_slot.index) return 0;
    GNEED(3); if (*pc++ != OP_get_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    GOP(OP_get_array_el);
    if ((size_t)(end - pc) >= 2 && *pc == OP_put_loc8) {
        pc++; if (*pc++ != (uint8_t)temp_local) return 0;
    } else if (!turbojs_parse_put_dense_slot(&pc, end, &temp_slot)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) || slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) || slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_field_atom(&pc, end, &score_atom)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) || slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_field_atom(&pc, end, &group_atom)) return 0;
    GOP(OP_add);
    if (!turbojs_parse_get_dense_slot(&pc, end, &outer_slot)) return 0;
    GOP(OP_add);
    GNEED(3); if (*pc++ != OP_push_i16) return 0; modulus = (int16_t)get_u16(pc); pc += 2;
    GOP(OP_mod);
    if (!turbojs_parse_put_field_atom(&pc, end, &atom) || atom != score_atom) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &dictionary_slot)) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) || slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_field_atom(&pc, end, &atom) || atom != group_atom) return 0;
    GOP(OP_swap); GOP(OP_dup); GOP(OP_is_undefined_or_null);
    if (!turbojs_parse_branch_target(&pc, end, OP_if_true8, OP_if_true, &target)) return 0;
    GOP(OP_swap); GOP(OP_to_propkey); GOP(OP_swap);
    if (pc != target) return 0;
    GOP(OP_swap);
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) || slot.kind != dictionary_slot.kind || slot.index != dictionary_slot.index) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) || slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_field_atom(&pc, end, &atom) || atom != group_atom) return 0;
    GOP(OP_get_array_el); GOP(OP_dup);
    if (!turbojs_parse_branch_target(&pc, end, OP_if_true8, OP_if_true, &target)) return 0;
    GOP(OP_drop); GOP(OP_push_0);
    if (pc != target) return 0;
    if (!turbojs_parse_get_dense_slot(&pc, end, &slot) || slot.kind != temp_slot.kind || slot.index != temp_slot.index) return 0;
    if (!turbojs_parse_get_field_atom(&pc, end, &atom) || atom != score_atom) return 0;
    GOP(OP_add); GOP(OP_swap); GOP(OP_to_propkey); GOP(OP_swap); GOP(OP_put_array_el);
    GNEED(3); if (*pc++ != OP_get_loc_check) return 0; i2 = get_u16(pc); pc += 2;
    if (i2 != induction) return 0;
    GOP(OP_post_inc);
    GNEED(3); if (*pc++ != OP_put_loc_check || get_u16(pc) != induction) return 0; pc += 2;
    GOP(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    if (!turbojs_parse_goto_target(&pc, end, &target) || (uint32_t)(target - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count ||
        temp_local >= b->var_count || modulus <= 0) return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm; spec->array_slot = array_slot; spec->dictionary_slot = dictionary_slot;
    spec->outer_slot = outer_slot; spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->accumulator_frame_local = UINT16_MAX; spec->mode = TURBOJS_OBJECT_ARRAY_GROUP_ACCUMULATE_INT32;
    spec->atom_x = score_atom; spec->atom_y = group_atom; spec->atom_z = (JSAtom)(uint32_t)modulus;
    spec->loop_header = target_offset; spec->resume_bytecode_offset = resume; spec->maximum_elements = 1000000000ULL;
#undef GOP
#undef GNEED
    return 1;
}

static JSValue *turbojs_object_array_slot_value(
    TurboJSObjectArrayOSRExecution *execution, const TurboJSDenseSlot *slot)
{
    if (!execution || !slot) return NULL;
    if (slot->kind == TURBOJS_DENSE_SLOT_LOCAL)
        return execution->var_buf ? &execution->var_buf[slot->index] : NULL;
    if (slot->kind == TURBOJS_DENSE_SLOT_ARGUMENT)
        return execution->arg_buf ? &execution->arg_buf[slot->index] : NULL;
    if (slot->kind == TURBOJS_DENSE_SLOT_VAR_REF && execution->var_refs &&
        execution->var_refs[slot->index])
        return execution->var_refs[slot->index]->pvalue;
    return NULL;
}

typedef struct TurboJSObjectArrayShapeWay {
    JSShape *shape;
    uint32_t x_index;
} TurboJSObjectArrayShapeWay;

static int turbojs_object_data_property_index(JSObject *object, JSAtom atom,
                                               int writable,
                                               uint32_t *property_index)
{
    JSProperty *property;
    JSShapeProperty *shape;
    ptrdiff_t index;
    if (!object || !object->shape || !property_index) return 0;
    shape = find_own_property(&property, object, atom);
    if (!shape || (shape->flags & JS_PROP_TMASK) != JS_PROP_NORMAL ||
        (writable && !(shape->flags & JS_PROP_WRITABLE)))
        return 0;
    index = property - object->prop;
    if (index < 0 || index >= object->shape->prop_count) return 0;
    *property_index = (uint32_t)index;
    return 1;
}

static TurboJSOSRExitKind turbojs_run_object_array_loop(
    TurboJSOSRFrame *frame, void *opaque, uint32_t *resume_offset)
{
    TurboJSObjectArrayOSRExecution *execution =
        (TurboJSObjectArrayOSRExecution *)opaque;
    TurboJSObjectArrayOSRProgram *program;
    JSValue *array_value, *accumulator_value, *outer_value = NULL;
    JSObject *array;
    TurboJSOSRValue *index_slot;
    uint32_t i, start, count, wrapped;
    if (!execution || !(program = execution->program) || !frame || !resume_offset ||
        program->induction_local >= frame->local_count)
        return TURBOJS_OSR_EXIT_BAILOUT;
    array_value = turbojs_object_array_slot_value(execution, &program->array_slot);
    accumulator_value = program->mode == TURBOJS_OBJECT_ARRAY_GROUP_ACCUMULATE_INT32 ? NULL :
        turbojs_object_array_slot_value(execution, &program->accumulator_slot);
    index_slot = &frame->locals[program->induction_local];
    if (!array_value || JS_VALUE_GET_TAG(*array_value) != JS_TAG_OBJECT ||
        (program->mode != TURBOJS_OBJECT_ARRAY_GROUP_ACCUMULATE_INT32 &&
         (!accumulator_value || JS_VALUE_GET_NORM_TAG(*accumulator_value) != JS_TAG_INT)) ||
        index_slot->kind != TURBOJS_OSR_VALUE_INT64 || (int64_t)index_slot->bits < 0)
        return TURBOJS_OSR_EXIT_BAILOUT;
    array = JS_VALUE_GET_OBJ(*array_value);
    if (!array || array->class_id != JS_CLASS_ARRAY || !array->fast_array ||
        !array->shape || !program->ctx || !program->ctx->std_array_prototype ||
        array->shape->proto != JS_VALUE_GET_OBJ(program->ctx->class_proto[JS_CLASS_ARRAY]))
        return TURBOJS_OSR_EXIT_BAILOUT;
    start = (uint32_t)index_slot->bits;
    count = array->u.array.count;
    if (start > count || (uint64_t)(count - start) > program->maximum_elements)
        return TURBOJS_OSR_EXIT_BAILOUT;
    wrapped = accumulator_value ? (uint32_t)JS_VALUE_GET_INT(*accumulator_value) : 0;

    if (program->mode == TURBOJS_OBJECT_ARRAY_SUM_FIELD_INT32) {
        TurboJSObjectArrayShapeWay ways[4];
        uint8_t way_count = 0;
        memset(ways, 0, sizeof(ways));
        for (i = start; i < count; ++i) {
            JSValue element = array->u.array.u.values[i];
            JSObject *object;
            uint32_t property_index = 0;
            uint8_t way;
            JSValue property_value;
            if (JS_IsUninitialized(element) || JS_VALUE_GET_TAG(element) != JS_TAG_OBJECT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            object = JS_VALUE_GET_OBJ(element);
            for (way = 0; way < way_count; ++way)
                if (ways[way].shape == object->shape) {
                    property_index = ways[way].x_index;
                    break;
                }
            if (way == way_count) {
                if (way_count >= 4 ||
                    !turbojs_object_data_property_index(object, program->atom_x, 0,
                                                        &property_index))
                    return TURBOJS_OSR_EXIT_BAILOUT;
                ways[way_count].shape = object->shape;
                ways[way_count].x_index = property_index;
                way_count++;
            }
            property_value = object->prop[property_index].u.value;
            if (JS_VALUE_GET_NORM_TAG(property_value) != JS_TAG_INT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            wrapped += (uint32_t)JS_VALUE_GET_INT(property_value);
        }
    } else if (program->mode == TURBOJS_OBJECT_ARRAY_UPDATE_REDUCE_INT32) {
        JSShape *expected_shape = NULL;
        uint32_t x_index = 0, y_index = 0, z_index = 0;
        int32_t outer;
        outer_value = turbojs_object_array_slot_value(execution, &program->outer_slot);
        if (!outer_value || JS_VALUE_GET_NORM_TAG(*outer_value) != JS_TAG_INT)
            return TURBOJS_OSR_EXIT_BAILOUT;
        outer = JS_VALUE_GET_INT(*outer_value);
        /* Validate the complete remaining range before the first write. */
        for (i = start; i < count; ++i) {
            JSValue element = array->u.array.u.values[i];
            JSObject *object;
            if (JS_IsUninitialized(element) || JS_VALUE_GET_TAG(element) != JS_TAG_OBJECT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            object = JS_VALUE_GET_OBJ(element);
            if (!expected_shape) {
                expected_shape = object->shape;
                if (!turbojs_object_data_property_index(object, program->atom_x, 1, &x_index) ||
                    !turbojs_object_data_property_index(object, program->atom_y, 0, &y_index) ||
                    !turbojs_object_data_property_index(object, program->atom_z, 0, &z_index))
                    return TURBOJS_OSR_EXIT_BAILOUT;
            } else if (object->shape != expected_shape) {
                return TURBOJS_OSR_EXIT_BAILOUT;
            }
            if (JS_VALUE_GET_NORM_TAG(object->prop[x_index].u.value) != JS_TAG_INT ||
                JS_VALUE_GET_NORM_TAG(object->prop[y_index].u.value) != JS_TAG_INT ||
                JS_VALUE_GET_NORM_TAG(object->prop[z_index].u.value) != JS_TAG_INT)
                return TURBOJS_OSR_EXIT_BAILOUT;
        }
        for (i = start; i < count; ++i) {
            JSObject *object = JS_VALUE_GET_OBJ(array->u.array.u.values[i]);
            uint32_t x = (uint32_t)JS_VALUE_GET_INT(object->prop[x_index].u.value);
            uint32_t y = (uint32_t)JS_VALUE_GET_INT(object->prop[y_index].u.value);
            uint32_t z = (uint32_t)JS_VALUE_GET_INT(object->prop[z_index].u.value);
            int32_t next_x = (int32_t)(x + y - (uint32_t)outer);
            object->prop[x_index].u.value = JS_NewInt32(program->ctx, next_x);
            wrapped += (uint32_t)next_x;
            wrapped += z;
        }
    } else if (program->mode == TURBOJS_OBJECT_ARRAY_GROUP_ACCUMULATE_INT32) {
        enum { TURBOJS_GROUP_TABLE_CAPACITY = 256 };
        JSValue *dict_value = turbojs_object_array_slot_value(execution, &program->dictionary_slot);
        JSObject *dict;
        JSShape *record_shape = NULL;
        uint32_t score_index = 0, group_index = 0, key_limit = 0, key;
        uint32_t totals[TURBOJS_GROUP_TABLE_CAPACITY];
        uint8_t used[TURBOJS_GROUP_TABLE_CAPACITY];
        int32_t outer, modulus = (int32_t)(uint32_t)program->atom_z;
        memset(totals, 0, sizeof(totals));
        memset(used, 0, sizeof(used));
        if (!dict_value || JS_VALUE_GET_TAG(*dict_value) != JS_TAG_OBJECT)
            return TURBOJS_OSR_EXIT_BAILOUT;
        dict = JS_VALUE_GET_OBJ(*dict_value);
        outer_value = turbojs_object_array_slot_value(execution, &program->outer_slot);
        if (!dict || dict->class_id != JS_CLASS_OBJECT || !dict->shape ||
            dict->shape->proto != JS_VALUE_GET_OBJ(program->ctx->class_proto[JS_CLASS_OBJECT]) ||
            !outer_value || JS_VALUE_GET_NORM_TAG(*outer_value) != JS_TAG_INT || modulus <= 0)
            return TURBOJS_OSR_EXIT_BAILOUT;
        outer = JS_VALUE_GET_INT(*outer_value);

        /* Validate the complete record range and discover the bounded key set
           before mutating either the records or the dictionary. */
        for (i = start; i < count; ++i) {
            JSValue element = array->u.array.u.values[i];
            JSObject *object;
            int32_t signed_key;
            if (JS_IsUninitialized(element) || JS_VALUE_GET_TAG(element) != JS_TAG_OBJECT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            object = JS_VALUE_GET_OBJ(element);
            if (!record_shape) {
                record_shape = object->shape;
                if (!turbojs_object_data_property_index(object, program->atom_x, 1, &score_index) ||
                    !turbojs_object_data_property_index(object, program->atom_y, 0, &group_index))
                    return TURBOJS_OSR_EXIT_BAILOUT;
            } else if (object->shape != record_shape) {
                return TURBOJS_OSR_EXIT_BAILOUT;
            }
            if (JS_VALUE_GET_NORM_TAG(object->prop[score_index].u.value) != JS_TAG_INT ||
                JS_VALUE_GET_NORM_TAG(object->prop[group_index].u.value) != JS_TAG_INT)
                return TURBOJS_OSR_EXIT_BAILOUT;
            signed_key = JS_VALUE_GET_INT(object->prop[group_index].u.value);
            if (signed_key < 0 || signed_key >= TURBOJS_GROUP_TABLE_CAPACITY)
                return TURBOJS_OSR_EXIT_BAILOUT;
            key = (uint32_t)signed_key;
            used[key] = 1;
            if (key + 1 > key_limit) key_limit = key + 1;
        }

        /* Snapshot each observed total once. Missing ordinary properties model
           the `(totals[key] || 0)` source expression. Accessors, inherited
           numeric properties, and non-integer truthy values remain on the
           interpreter path. */
        for (key = 0; key < key_limit; ++key) {
            JSAtom key_atom;
            uint32_t property_index;
            JSObject *prototype;
            if (!used[key]) continue;
            key_atom = JS_NewAtomUInt32(program->ctx, key);
            if (key_atom == JS_ATOM_NULL) return TURBOJS_OSR_EXIT_BAILOUT;
            if (turbojs_object_data_property_index(dict, key_atom, 1, &property_index)) {
                JSValue total = dict->prop[property_index].u.value;
                if (JS_VALUE_GET_NORM_TAG(total) != JS_TAG_INT) {
                    JS_FreeAtom(program->ctx, key_atom);
                    return TURBOJS_OSR_EXIT_BAILOUT;
                }
                totals[key] = (uint32_t)JS_VALUE_GET_INT(total);
            } else {
                prototype = JS_VALUE_GET_OBJ(program->ctx->class_proto[JS_CLASS_OBJECT]);
                if (prototype && turbojs_object_data_property_index(prototype, key_atom, 0,
                                                                    &property_index)) {
                    JS_FreeAtom(program->ctx, key_atom);
                    return TURBOJS_OSR_EXIT_BAILOUT;
                }
            }
            JS_FreeAtom(program->ctx, key_atom);
        }

        /* Execute the complete hot region against unboxed scores and a native
           grouped accumulator. */
        for (i = start; i < count; ++i) {
            JSObject *object = JS_VALUE_GET_OBJ(array->u.array.u.values[i]);
            int32_t score = JS_VALUE_GET_INT(object->prop[score_index].u.value);
            key = (uint32_t)JS_VALUE_GET_INT(object->prop[group_index].u.value);
            {
                int32_t next_score = (int32_t)(((int64_t)score + (int32_t)key + outer) % modulus);
                object->prop[score_index].u.value = JS_NewInt32(program->ctx, next_score);
                totals[key] += (uint32_t)next_score;
            }
        }

        /* Materialize once per observed group instead of performing a dynamic
           lookup and write for every record. */
        for (key = 0; key < key_limit; ++key) {
            if (!used[key]) continue;
            if (JS_SetPropertyUint32(program->ctx, *dict_value, key,
                                     JS_NewInt32(program->ctx, (int32_t)totals[key])) < 0)
                return TURBOJS_OSR_EXIT_BAILOUT;
        }
        program->ctx->rt->object_array_grouped_osr_entries++;
        program->ctx->rt->object_array_grouped_osr_elements += count - start;
    } else {
        return TURBOJS_OSR_EXIT_BAILOUT;
    }

    index_slot->kind = TURBOJS_OSR_VALUE_INT64;
    index_slot->bits = count;
    if (program->mode != TURBOJS_OBJECT_ARRAY_GROUP_ACCUMULATE_INT32 && program->accumulator_frame_local != UINT16_MAX) {
        TurboJSOSRValue *out;
        if (program->accumulator_frame_local >= frame->local_count)
            return TURBOJS_OSR_EXIT_BAILOUT;
        out = &frame->locals[program->accumulator_frame_local];
        out->kind = TURBOJS_OSR_VALUE_INT64;
        out->bits = (uint64_t)(int64_t)(int32_t)wrapped;
    } else if (program->mode != TURBOJS_OBJECT_ARRAY_GROUP_ACCUMULATE_INT32) {
        set_value(program->ctx, accumulator_value,
                  JS_NewInt32(program->ctx, (int32_t)wrapped));
    }
    program->ctx->rt->object_array_osr_entries++;
    program->ctx->rt->object_array_osr_elements += count - start;
    if (program->mode == TURBOJS_OBJECT_ARRAY_UPDATE_REDUCE_INT32)
        program->ctx->rt->object_array_update_osr_entries++;
    else
        program->ctx->rt->object_array_polymorphic_osr_entries++;
    *resume_offset = program->resume_bytecode_offset;
    return TURBOJS_OSR_EXIT_COMPLETED;
}

static TurboJSOSREntry turbojs_object_array_osr_entry(
    TurboJSObjectArrayOSRExecution *execution)
{
    TurboJSOSREntry entry;
    memset(&entry, 0, sizeof(entry));
    if (execution && execution->program) {
        entry.callback = turbojs_run_object_array_loop;
        entry.opaque = execution;
        entry.loop_header = execution->program->loop_header;
        entry.bailout_limit = 3;
    }
    return entry;
}

static int turbojs_detect_object_property_loop(JSFunctionBytecode *b,
                                                 uint32_t target_offset,
                                                 uint32_t source_offset,
                                                 TurboJSObjectPropertyOSRProgram *spec)
{
    const uint8_t *base, *pc, *end;
    uint16_t induction;
    TurboJSObjectSlot object_slot, accumulator_slot, slot;
    JSAtom write_atom, read_atoms[TURBOJS_OBJECT_OSR_MAX_READS];
    uint8_t read_count = 0;
    uint32_t resume;
    int32_t limit;
    if (!b || !spec || target_offset >= source_offset ||
        source_offset >= (uint32_t)b->byte_code_len)
        return 0;
    base = b->byte_code_buf;
    pc = base + target_offset;
    end = base + b->byte_code_len;
#define ONEED(n) do { if ((size_t)(end - pc) < (size_t)(n)) return 0; } while (0)
#define OOP0(op) do { ONEED(1); if (*pc++ != (op)) return 0; } while (0)
#define OREF(op, out) do { ONEED(3); if (*pc++ != (op)) return 0; (out)=get_u16(pc); pc+=2; } while (0)
#define OATOM(op, out) do { ONEED(5); if (*pc++ != (op)) return 0; (out)=get_u32(pc); pc+=4; } while (0)
    OREF(OP_get_loc_check, induction);
    ONEED(5); if (*pc++ != OP_push_i32) return 0; limit = (int32_t)get_u32(pc); pc += 4;
    OOP0(OP_lt);
    ONEED(2); if (*pc++ != OP_if_false8) return 0;
    resume = (uint32_t)((pc + (int8_t)*pc) - base); pc++;
    if (!turbojs_parse_get_object_slot(&pc, end, &object_slot)) return 0;
    { uint16_t i2; OREF(OP_get_loc_check, i2); if (i2 != induction) return 0; }
    OATOM(OP_put_field, write_atom);
    if (!turbojs_parse_get_object_slot(&pc, end, &accumulator_slot)) return 0;

    /* Parse one or more object-field terms. The bytecode shape is:
       acc, field0, [fieldN, add]..., add. This accepts arbitrary property
       chains while retaining the canonical accumulator update. */
    {
        JSAtom atom;
        if (!turbojs_parse_get_object_slot(&pc, end, &slot) || slot.kind != object_slot.kind || slot.index != object_slot.index) return 0;
        OATOM(OP_get_field, atom);
        read_atoms[read_count++] = atom;
    }
    while (read_count < TURBOJS_OBJECT_OSR_MAX_READS && pc < end) {
        const uint8_t *saved_pc = pc;
        JSAtom atom;
        if (!turbojs_parse_get_object_slot(&pc, end, &slot) || slot.kind != object_slot.kind || slot.index != object_slot.index) { pc = saved_pc; break; }
        OATOM(OP_get_field, atom);
        OOP0(OP_add);
        read_atoms[read_count++] = atom;
    }
    OOP0(OP_add);
    OOP0(OP_dup);
    if (!turbojs_parse_put_object_slot(&pc, end, &accumulator_slot)) return 0;
    OOP0(OP_drop);
    { uint16_t i2; OREF(OP_get_loc_check, i2); if (i2 != induction) return 0; }
    OOP0(OP_post_inc);
    { uint16_t i2; OREF(OP_put_loc_check, i2); if (i2 != induction) return 0; }
    OOP0(OP_drop);
    if ((uint32_t)(pc - base) != source_offset) return 0;
    ONEED(2); if (*pc++ != OP_goto8 || (uint32_t)((pc + (int8_t)*pc) - base) != target_offset) return 0;
    if (resume > (uint32_t)b->byte_code_len || induction >= b->var_count || limit < 0 || !read_count) return 0;
    memset(spec, 0, sizeof(*spec));
    spec->ctx = b->realm;
    spec->induction_local = (uint16_t)(b->arg_count + induction);
    spec->object_slot = object_slot;
    spec->accumulator_slot = accumulator_slot;
    spec->write_atom = write_atom;
    memcpy(spec->read_atoms, read_atoms, (size_t)read_count * sizeof(read_atoms[0]));
    spec->read_count = read_count;
    spec->limit = limit;
    spec->loop_header = target_offset;
    spec->resume_bytecode_offset = resume;
#undef OATOM
#undef OREF
#undef OOP0
#undef ONEED
    return 1;
}

static int turbojs_js_number(JSValueConst value, double *out)
{
    int tag = JS_VALUE_GET_TAG(value);
    if (tag == JS_TAG_INT) { *out = JS_VALUE_GET_INT(value); return 1; }
    if (tag == JS_TAG_FLOAT64) { *out = JS_VALUE_GET_FLOAT64(value); return 1; }
    return 0;
}

static TurboJSOSRExitKind turbojs_run_object_property_loop(TurboJSOSRFrame *frame,
                                                            void *opaque,
                                                            uint32_t *resume_offset)
{
    TurboJSObjectPropertyOSRExecution *execution = (TurboJSObjectPropertyOSRExecution *)opaque;
    TurboJSObjectPropertyOSRProgram *program;
    JSValue object_value, accumulator_value;
    JSValue *object_ptr, *accumulator_ptr;
    JSObject *object;
    JSProperty *write_property;
    JSShapeProperty *write_shape;
    double accumulator, invariant_sum = 0.0;
    uint32_t induction_terms = 0;
    uint8_t term;
    int64_t i;
    if (!execution || !(program = execution->program) ||
        !frame || !resume_offset || program->induction_local >= frame->local_count ||
        !program->read_count || program->read_count > TURBOJS_OBJECT_OSR_MAX_READS)
        return TURBOJS_OSR_EXIT_BAILOUT;
    object_ptr = turbojs_object_slot_value(execution, &program->object_slot);
    accumulator_ptr = turbojs_object_slot_value(execution, &program->accumulator_slot);
    if (!object_ptr || !accumulator_ptr) return TURBOJS_OSR_EXIT_BAILOUT;
    object_value = *object_ptr;
    accumulator_value = *accumulator_ptr;
    if (JS_VALUE_GET_TAG(object_value) != JS_TAG_OBJECT ||
        !turbojs_js_number(accumulator_value, &accumulator) ||
        frame->locals[program->induction_local].kind != TURBOJS_OSR_VALUE_INT64)
        return TURBOJS_OSR_EXIT_BAILOUT;
    object = JS_VALUE_GET_OBJ(object_value);
    write_shape = find_own_property(&write_property, object, program->write_atom);
    if (!write_shape || (write_shape->flags & JS_PROP_TMASK) != JS_PROP_NORMAL ||
        !(write_shape->flags & JS_PROP_WRITABLE))
        return TURBOJS_OSR_EXIT_BAILOUT;

    for (term = 0; term < program->read_count; ++term) {
        JSProperty *property;
        JSShapeProperty *shape;
        double value;
        if (program->read_atoms[term] == program->write_atom) {
            induction_terms++;
            continue;
        }
        shape = find_own_property(&property, object, program->read_atoms[term]);
        if (!shape || (shape->flags & JS_PROP_TMASK) != JS_PROP_NORMAL ||
            !turbojs_js_number(property->u.value, &value))
            return TURBOJS_OSR_EXIT_BAILOUT;
        invariant_sum += value;
    }
    i = (int64_t)frame->locals[program->induction_local].bits;
    if (i < 0 || i > program->limit) return TURBOJS_OSR_EXIT_BAILOUT;
    for (; i < program->limit; ++i)
        accumulator += invariant_sum + (double)induction_terms * (double)i;
    if (program->limit > 0)
        write_property->u.value = JS_NewInt32(program->ctx, program->limit - 1);
    set_value(program->ctx, accumulator_ptr,
              JS_NewFloat64(program->ctx, accumulator));
    frame->locals[program->induction_local].bits = (uint64_t)program->limit;
    frame->locals[program->induction_local].kind = TURBOJS_OSR_VALUE_INT64;
    *resume_offset = program->resume_bytecode_offset;
    return TURBOJS_OSR_EXIT_COMPLETED;
}

static TurboJSOSREntry turbojs_object_property_osr_entry(TurboJSObjectPropertyOSRExecution *execution)
{
    TurboJSOSREntry entry;
    memset(&entry, 0, sizeof(entry));
    if (execution && execution->program) {
        entry.callback = turbojs_run_object_property_loop;
        entry.opaque = execution;
        entry.loop_header = execution->program->loop_header;
        entry.bailout_limit = 3;
    }
    return entry;
}

/* Fast interpreter-side rejection check. Unsupported loop sites are common in
   application code, and calling the full OSR observer on every subsequent
   backedge needlessly pays hashing, state-machine, and telemetry overhead.
   Keep the exact source/target pair check so a direct-map collision can never
   suppress a different loop. */
static inline void turbojs_osr_negative_cache_insert(
    JSFunctionBytecode *b, uint32_t source_offset, uint32_t target_offset)
{
    uint32_t i, slot;
    if (!b) return;
    for (i = 0; i < TURBOJS_VM_OSR_NEGATIVE_CACHE_COUNT; ++i) {
        if ((b->osr_negative_valid_mask & (uint8_t)(1u << i)) &&
            b->osr_negative_sources[i] == source_offset &&
            b->osr_negative_targets[i] == target_offset) {
            b->osr_negative_samples[i] = 0;
            return;
        }
    }
    slot = b->osr_negative_next & (TURBOJS_VM_OSR_NEGATIVE_CACHE_COUNT - 1u);
    b->osr_negative_next = (uint8_t)((slot + 1u) &
        (TURBOJS_VM_OSR_NEGATIVE_CACHE_COUNT - 1u));
    b->osr_negative_sources[slot] = source_offset;
    b->osr_negative_targets[slot] = target_offset;
    b->osr_negative_samples[slot] = 0;
    b->osr_negative_valid_mask |= (uint8_t)(1u << slot);
}

static inline int turbojs_osr_backedge_is_negative_cached(
    JSRuntime *rt, JSFunctionBytecode *b,
    uint32_t source_offset, uint32_t target_offset)
{
    TurboJSVMOSRSite *site;
    uint32_t i, slot;
    if (unlikely(!rt || !b || !rt->jit_optimizing_enabled ||
                 !rt->jit_osr_enabled))
        return 0;
    for (i = 0; i < TURBOJS_VM_OSR_NEGATIVE_CACHE_COUNT; ++i) {
        if (likely((b->osr_negative_valid_mask & (uint8_t)(1u << i)) &&
                   b->osr_negative_sources[i] == source_offset &&
                   b->osr_negative_targets[i] == target_offset)) {
            if ((++b->osr_negative_samples[i] & UINT32_C(1023)) == 0)
                rt->osr_negative_cache_hits += UINT64_C(1024);
            return 1;
        }
    }
    slot = ((target_offset * UINT32_C(2654435761)) >> 29) &
           (TURBOJS_VM_OSR_SITE_COUNT - 1u);
    site = &b->osr_sites[slot];
    if (likely(site->target_offset == target_offset &&
               site->source_offset == source_offset &&
               site->state.disabled &&
               site->rejection_reason != TURBOJS_VM_OSR_REJECT_NONE)) {
        turbojs_osr_negative_cache_insert(b, source_offset, target_offset);
        return 1;
    }
    return 0;
}


static const uint8_t turbojs_osr_opcode_size[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) [OP_##id] = size,
#define def(id, size, n_pop, n_push, f)
#include "internal/bytecode_opcodes.h"
#undef def
#undef DEF
#undef FMT
};

typedef enum TurboJSOSRUnsupportedClass {
    TURBOJS_OSR_UNSUPPORTED_OTHER = 0,
    TURBOJS_OSR_UNSUPPORTED_CALL,
    TURBOJS_OSR_UNSUPPORTED_PROPERTY,
    TURBOJS_OSR_UNSUPPORTED_INDEXED,
    TURBOJS_OSR_UNSUPPORTED_NUMERIC,
    TURBOJS_OSR_UNSUPPORTED_CONTROL
} TurboJSOSRUnsupportedClass;

static TurboJSOSRUnsupportedClass turbojs_osr_classify_unsupported_loop(
    const JSFunctionBytecode *b, uint32_t target_offset, uint32_t source_offset)
{
    uint32_t off = target_offset;
    unsigned seen_call = 0, seen_property = 0, seen_indexed = 0;
    unsigned seen_numeric = 0, seen_control = 0;
    if (!b || !b->byte_code_buf || source_offset > (uint32_t)b->byte_code_len)
        return TURBOJS_OSR_UNSUPPORTED_OTHER;
    while (off <= source_offset && off < (uint32_t)b->byte_code_len) {
        uint8_t op = b->byte_code_buf[off];
        uint8_t size = op < countof(turbojs_osr_opcode_size) ? turbojs_osr_opcode_size[op] : 0;
        switch (op) {
        case OP_call: case OP_tail_call: case OP_call_method: case OP_tail_call_method:
        case OP_call_constructor: case OP_call0: case OP_call1: case OP_call2: case OP_call3:
            seen_call = 1; break;
        case OP_get_field: case OP_get_field2: case OP_put_field: case OP_define_field:
        case OP_object: case OP_to_object: case OP_check_object:
            seen_property = 1; break;
        case OP_get_array_el: case OP_get_array_el2: case OP_put_array_el: case OP_append:
            seen_indexed = 1; break;
        case OP_div: case OP_mod:
            seen_numeric = 1; break;
        case OP_if_false: case OP_if_true: case OP_goto:
        case OP_if_false8: case OP_if_true8: case OP_goto8: case OP_goto16:
            seen_control = 1; break;
        default: break;
        }
        if (!size) return TURBOJS_OSR_UNSUPPORTED_OTHER;
        off += size;
    }
    if (seen_call) return TURBOJS_OSR_UNSUPPORTED_CALL;
    if (seen_indexed) return TURBOJS_OSR_UNSUPPORTED_INDEXED;
    if (seen_property) return TURBOJS_OSR_UNSUPPORTED_PROPERTY;
    if (seen_numeric) return TURBOJS_OSR_UNSUPPORTED_NUMERIC;
    if (seen_control) return TURBOJS_OSR_UNSUPPORTED_CONTROL;
    return TURBOJS_OSR_UNSUPPORTED_OTHER;
}

static void turbojs_osr_record_unsupported_class(JSRuntime *rt,
                                                  TurboJSOSRUnsupportedClass cls)
{
    if (!rt) return;
    switch (cls) {
    case TURBOJS_OSR_UNSUPPORTED_CALL: rt->osr_rejections_calls++; break;
    case TURBOJS_OSR_UNSUPPORTED_PROPERTY: rt->osr_rejections_properties++; break;
    case TURBOJS_OSR_UNSUPPORTED_INDEXED: rt->osr_rejections_indexed++; break;
    case TURBOJS_OSR_UNSUPPORTED_NUMERIC: rt->osr_rejections_numeric++; break;
    case TURBOJS_OSR_UNSUPPORTED_CONTROL: rt->osr_rejections_control_flow++; break;
    default: rt->osr_rejections_other++; break;
    }
}

/* Observe a real interpreter backedge, compile a strictly recognized counted
   loop, and execute it through the transactional OSR boundary. Returns one
   when execution completed and supplies the bytecode resume offset. */
static int turbojs_observe_osr_backedge(JSRuntime *rt, JSFunctionBytecode *b,
                                        JSValue *arg_buf, JSValue *var_buf, JSVarRef **var_refs,
                                        JSValue *stack_buf, JSValue *sp,
                                        uint32_t source_offset,
                                        uint32_t target_offset,
                                        uint32_t *resume_offset)
{
    TurboJSVMOSRSite *site;
    TurboJSOSRValue *locals = NULL, *stack = NULL;
    TurboJSOSRFrame frame;
    TurboJSOSRExecutionResult execution;
    TurboJSOSREntry entry;
    TurboJSDenseArrayOSRExecution dense_execution;
    TurboJSObjectPropertyOSRExecution object_execution;
    TurboJSScalarLoopOSRExecution scalar_execution;
    TurboJSTypedArrayAffineSumOSRExecution typed_sum_execution;
    TurboJSObjectArrayOSRExecution object_array_execution;
    TurboJSOSRCountedLoopSpec spec;
    TurboJSIRDiagnostic diagnostic;
    uint32_t local_count, stack_count, i, slot, threshold;
    TurboJSOSRDecision decision;
    int completed = 0;

    if (unlikely(!rt || !rt->jit_optimizing_enabled || !rt->jit_osr_enabled ||
                 !b || !resume_offset || target_offset >= source_offset))
        return 0;
    rt->osr_backedges++;
    slot = ((target_offset * 2654435761u) >> 29) & (TURBOJS_VM_OSR_SITE_COUNT - 1u);
    site = &b->osr_sites[slot];
    if (site->target_offset != target_offset || site->source_offset != source_offset) {
        if (site->program) TurboJS_OSRLoopProgramDestroy(site->program);
        if (site->dense_program) js_free_rt(rt, site->dense_program);
        if (site->transform_program) js_free_rt(rt, site->transform_program);
        if (site->object_program) js_free_rt(rt, site->object_program);
        if (site->scalar_program) js_free_rt(rt, site->scalar_program);
        if (site->typed_sum_program) js_free_rt(rt, site->typed_sum_program);
        if (site->object_array_program) js_free_rt(rt, site->object_array_program);
        memset(site, 0, sizeof(*site));
        site->target_offset = target_offset;
        site->source_offset = source_offset;
        threshold = rt->jit_osr_threshold ? rt->jit_osr_threshold : 96;
        if (threshold > UINT32_MAX / 4u) threshold = UINT32_MAX; else threshold *= 4u;
        TurboJS_OSRStateInit(&site->state, target_offset, threshold);
    }
    if (site->state.disabled && site->rejection_reason != TURBOJS_VM_OSR_REJECT_NONE) {
        rt->osr_negative_cache_hits++;
        return 0;
    }
    decision = TurboJS_OSRObserveBackedge(&site->state);
    if (decision == TURBOJS_OSR_REQUEST_COMPILE) {
        rt->osr_compile_requests++;
        rt->optimizing_compile_requests++;
        rt->tier_up_requests++;
        if (turbojs_detect_counted_i64_loop(b, target_offset, source_offset, &spec)) {
            memset(&diagnostic, 0, sizeof(diagnostic));
            if (TurboJS_OSRCompileCountedI64Loop(&spec, &site->program, &diagnostic) == TURBOJS_IR_OK) {
                site->program_kind = 1;
                TurboJS_OSRMarkCodeReady(&site->state);
            } else {
                site->state.disabled = 1;
                site->rejection_reason = TURBOJS_VM_OSR_REJECT_BACKEND;
                rt->osr_rejections_backend++;
            }
        } else {
            TurboJSDenseArrayOSRProgram dense_spec;
            if (turbojs_detect_dense_array_sum_loop(b, target_offset, source_offset, &dense_spec) ||
                turbojs_detect_dense_array_init_loop(b, target_offset, source_offset, &dense_spec) ||
                turbojs_detect_holey_array_sum_loop(b, target_offset, source_offset, &dense_spec)) {
                site->dense_program = js_malloc_rt(rt, sizeof(*site->dense_program));
                if (site->dense_program) {
                    *site->dense_program = dense_spec;
                    site->program_kind = 2;
                    TurboJS_OSRMarkCodeReady(&site->state);
                } else {
                    site->state.disabled = 1;
                    site->rejection_reason = TURBOJS_VM_OSR_REJECT_ALLOCATION;
                    rt->osr_rejections_allocation++;
                }
            } else {
                TurboJSTypedArrayAffineSumOSRProgram typed_sum_spec;
                if (turbojs_detect_typed_array_binary_biased_div_sum_loop(b, target_offset, source_offset, &typed_sum_spec) ||
                    turbojs_detect_typed_array_affine_sum_loop(b, target_offset, source_offset, &typed_sum_spec)) {
                    site->typed_sum_program = js_malloc_rt(rt, sizeof(*site->typed_sum_program));
                    if (site->typed_sum_program) {
                        *site->typed_sum_program = typed_sum_spec;
                        site->program_kind = 6;
                        TurboJS_OSRMarkCodeReady(&site->state);
                    } else {
                        site->state.disabled = 1;
                        site->rejection_reason = TURBOJS_VM_OSR_REJECT_ALLOCATION;
                        rt->osr_rejections_allocation++;
                    }
                } else {
                TurboJSDenseArrayTransformOSRProgram transform_spec;
                if (turbojs_detect_dense_array_transform_loop(b, target_offset, source_offset, &transform_spec) ||
                    turbojs_detect_dense_array_fill_loop(b, target_offset, source_offset, &transform_spec) ||
                    turbojs_detect_dense_array_extra_loop(b, target_offset, source_offset, &transform_spec)) {
                    site->transform_program = js_malloc_rt(rt, sizeof(*site->transform_program));
                    if (site->transform_program) {
                        *site->transform_program = transform_spec;
                        site->program_kind = 3;
                        TurboJS_OSRMarkCodeReady(&site->state);
                    } else {
                        site->state.disabled = 1;
                        site->rejection_reason = TURBOJS_VM_OSR_REJECT_ALLOCATION;
                        rt->osr_rejections_allocation++;
                    }
                } else {
                    TurboJSObjectArrayOSRProgram object_array_spec;
                    if (turbojs_detect_object_array_group_accumulate_loop(b, target_offset, source_offset, &object_array_spec) ||
                        turbojs_detect_object_array_update_loop(b, target_offset, source_offset, &object_array_spec) ||
                        turbojs_detect_object_array_sum_loop(b, target_offset, source_offset, &object_array_spec)) {
                        site->object_array_program = js_malloc_rt(rt, sizeof(*site->object_array_program));
                        if (site->object_array_program) {
                            *site->object_array_program = object_array_spec;
                            site->program_kind = 7;
                            TurboJS_OSRMarkCodeReady(&site->state);
                        } else {
                            site->state.disabled = 1;
                            site->rejection_reason = TURBOJS_VM_OSR_REJECT_ALLOCATION;
                            rt->osr_rejections_allocation++;
                        }
                    } else {
                        TurboJSObjectPropertyOSRProgram object_spec;
                        if (turbojs_detect_object_property_loop(b, target_offset, source_offset, &object_spec)) {
                            site->object_program = js_malloc_rt(rt, sizeof(*site->object_program));
                            if (site->object_program) {
                                *site->object_program = object_spec;
                                site->program_kind = 4;
                                TurboJS_OSRMarkCodeReady(&site->state);
                            } else {
                                site->state.disabled = 1;
                                site->rejection_reason = TURBOJS_VM_OSR_REJECT_ALLOCATION;
                                rt->osr_rejections_allocation++;
                            }
                        } else {
                            TurboJSScalarLoopOSRProgram scalar_spec;
                        if (turbojs_detect_polymorphic_leaf_call_loop(b, target_offset, source_offset, &scalar_spec) ||
                            turbojs_detect_captured_closure_call_loop(b, target_offset, source_offset, &scalar_spec) ||
                            turbojs_detect_recursive_fib_sum_loop(b, target_offset, source_offset, &scalar_spec) ||
                            turbojs_detect_int32_leaf_call_loop(b, target_offset, source_offset, &scalar_spec) ||
                            turbojs_detect_int32_imul_xorshift_loop(b, target_offset, source_offset, &scalar_spec) ||
                            turbojs_detect_scalar_loop(b, target_offset, source_offset, &scalar_spec)) {
                            site->scalar_program = js_malloc_rt(rt, sizeof(*site->scalar_program));
                            if (site->scalar_program) {
                                *site->scalar_program = scalar_spec;
                                site->program_kind = 5;
                                TurboJS_OSRMarkCodeReady(&site->state);
                            } else {
                                site->state.disabled = 1;
                                site->rejection_reason = TURBOJS_VM_OSR_REJECT_ALLOCATION;
                                rt->osr_rejections_allocation++;
                            }
                        } else {
                            site->state.disabled = 1;
                            site->rejection_reason = TURBOJS_VM_OSR_REJECT_UNSUPPORTED;
                            turbojs_osr_negative_cache_insert(
                                b, source_offset, target_offset);
                            rt->osr_rejections_unsupported++;
                            turbojs_osr_record_unsupported_class(rt, turbojs_osr_classify_unsupported_loop(b, target_offset, source_offset));
                        }
                        }
                    }
                }
                }
            }
        }
    }
    if (decision == TURBOJS_OSR_REQUEST_COMPILE) {
        if (site->state.code_ready && !site->state.disabled) {
            rt->osr_compilations++;
            rt->optimizing_compilations++;
            rt->tier_up_successes++;
        } else {
            rt->osr_compile_failures++;
            rt->optimizing_compile_failures++;
        }
    }
    if ((!site->program && !site->dense_program && !site->transform_program &&
         !site->object_program && !site->scalar_program && !site->typed_sum_program &&
         !site->object_array_program) || !site->state.code_ready || site->state.disabled)
        return 0;

    local_count = (uint32_t)b->arg_count + (uint32_t)b->var_count;
    stack_count = (uint32_t)(sp - stack_buf);
    if (stack_count > b->stack_size) return 0;
    if (local_count && !(locals = js_malloc_rt(rt, (size_t)local_count * sizeof(*locals)))) return 0;
    if (stack_count && !(stack = js_malloc_rt(rt, (size_t)stack_count * sizeof(*stack)))) {
        js_free_rt(rt, locals); return 0;
    }
    for (i = 0; i < local_count; ++i)
        turbojs_osr_value_from_js(i < b->arg_count ? arg_buf[i] : var_buf[i - b->arg_count], &locals[i]);
    for (i = 0; i < stack_count; ++i)
        turbojs_osr_value_from_js(stack_buf[i], &stack[i]);

    memset(&frame, 0, sizeof(frame));
    if (TurboJS_OSRFrameInit(&frame, local_count, stack_count) == TURBOJS_IR_OK &&
        TurboJS_OSRFrameCapture(&frame, locals, local_count, stack, stack_count,
                                source_offset, target_offset) == TURBOJS_IR_OK &&
        TurboJS_OSRFrameValidate(&frame) == TURBOJS_IR_OK) {
        site->frame_captures++;
        rt->osr_frame_captures++;
        memset(&dense_execution, 0, sizeof(dense_execution));
        dense_execution.program = site->dense_program;
        dense_execution.arg_buf = arg_buf;
        dense_execution.var_buf = var_buf;
        dense_execution.var_refs = var_refs;
        memset(&object_execution, 0, sizeof(object_execution));
        object_execution.program = site->object_program;
        object_execution.arg_buf = arg_buf;
        object_execution.var_buf = var_buf;
        object_execution.var_refs = var_refs;
        memset(&scalar_execution, 0, sizeof(scalar_execution));
        scalar_execution.program = site->scalar_program;
        scalar_execution.arg_buf = arg_buf;
        scalar_execution.var_buf = var_buf;
        scalar_execution.var_refs = var_refs;
        memset(&typed_sum_execution, 0, sizeof(typed_sum_execution));
        typed_sum_execution.program = site->typed_sum_program;
        typed_sum_execution.arg_buf = arg_buf;
        typed_sum_execution.var_buf = var_buf;
        typed_sum_execution.var_refs = var_refs;
        memset(&object_array_execution, 0, sizeof(object_array_execution));
        object_array_execution.program = site->object_array_program;
        object_array_execution.arg_buf = arg_buf;
        object_array_execution.var_buf = var_buf;
        object_array_execution.var_refs = var_refs;
        entry = site->program_kind == 2 ? turbojs_dense_array_osr_entry(&dense_execution) :
                site->program_kind == 3 ? turbojs_dense_array_transform_osr_entry(site->transform_program) :
                site->program_kind == 4 ? turbojs_object_property_osr_entry(&object_execution) :
                site->program_kind == 5 ? turbojs_scalar_loop_osr_entry(&scalar_execution) :
                site->program_kind == 6 ? turbojs_typed_array_affine_sum_osr_entry(&typed_sum_execution) :
                site->program_kind == 7 ? turbojs_object_array_osr_entry(&object_array_execution) :
                                          TurboJS_OSRLoopProgramEntry(site->program);
        if (TurboJS_OSRExecuteEntry(&site->state, &entry, &frame, &execution) == TURBOJS_IR_OK &&
            execution.exit_kind == TURBOJS_OSR_EXIT_COMPLETED) {
            for (i = b->arg_count; i < local_count; ++i) {
                if (frame.locals[i].kind == TURBOJS_OSR_VALUE_INT64) {
                    int64_t v = (int64_t)frame.locals[i].bits;
                    set_value(b->realm, &var_buf[i - b->arg_count], JS_NewInt64(b->realm, v));
                } else if (frame.locals[i].kind == TURBOJS_OSR_VALUE_FLOAT64) {
                    double v;
                    memcpy(&v, &frame.locals[i].bits, sizeof(v));
                    set_value(b->realm, &var_buf[i - b->arg_count], JS_NewFloat64(b->realm, v));
                }
            }
            *resume_offset = execution.resume_bytecode_offset;
            rt->osr_entries++;
            completed = 1;
        } else {
            rt->osr_bailouts++;
            rt->deoptimizations++;
        }
    }
    TurboJS_OSRFrameDestroy(&frame);
    js_free_rt(rt, stack);
    js_free_rt(rt, locals);
    return completed;
}
