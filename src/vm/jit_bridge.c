/* Engine domain source: vm/jit_bridge.c -> guarded baseline JIT bridge.
 * Ownership: vm subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

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
        case TURBOJS_IR_ADD_I32_CHECKED:
        case TURBOJS_IR_SUB_I32_CHECKED:
        case TURBOJS_IR_MUL_I32_CHECKED:
        case TURBOJS_IR_LESS_THAN_I64:
        case TURBOJS_IR_LOCAL_GET:
        case TURBOJS_IR_LOCAL_SET:
        case TURBOJS_IR_RETURN_I64:
            break;
        default:
            return 0;
        }
    }
    return 1;
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
    int64_t result;
    int i;

    if (!rt || !b || !out_value || b->func_kind != JS_FUNC_NORMAL ||
        b->var_ref_count != 0 || b->arg_count > TURBOJS_IR_MAX_REGISTERS)
        return 0;

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
        status = TurboJS_NativeInvoke(native, native_args, b->arg_count, &result);
        if (status == TURBOJS_IR_OK && result >= INT32_MIN && result <= INT32_MAX) {
            rt->jit_native_calls++;
            *out_value = js_int32((int32_t)result);
            return 1;
        }
        rt->jit_guard_failures++;
        return 0;
    }

    b->jit_call_count++;
    if (b->jit_compilation_attempted ||
        b->jit_call_count < (rt->jit_compile_threshold ? rt->jit_compile_threshold : 100)) {
        rt->jit_interpreted_calls++;
        return 0;
    }

    b->jit_compilation_attempted = 1;
    memset(&diagnostic, 0, sizeof(diagnostic));
    info.bytecode = b->byte_code_buf;
    info.bytecode_length = (size_t)b->byte_code_len;
    info.argument_count = b->arg_count;
    info.local_count = b->var_count;
    info.stack_size = b->stack_size;
    status = TurboJS_EngineBytecodeToIR(&info, &ir, &diagnostic);
    if (status != TURBOJS_IR_OK)
        return 0;
    if (!turbojs_jit_ir_is_vm_safe(&ir)) {
        TurboJS_IRFunctionDestroy(&ir);
        return 0;
    }
    status = TurboJS_CodeCacheCompile(cache, b, &ir, &native, &diagnostic);
    TurboJS_IRFunctionDestroy(&ir);
    if (status != TURBOJS_IR_OK)
        return 0;
    status = TurboJS_NativeInvoke(native, native_args, b->arg_count, &result);
    if (status == TURBOJS_IR_OK && result >= INT32_MIN && result <= INT32_MAX) {
        rt->jit_native_calls++;
        *out_value = js_int32((int32_t)result);
        return 1;
    }
    rt->jit_guard_failures++;
#else
    (void)rt; (void)b; (void)argc; (void)argv; (void)out_value;
#endif
    return 0;
}
