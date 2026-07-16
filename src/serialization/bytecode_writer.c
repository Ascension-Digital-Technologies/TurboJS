/* Engine domain source: serialization/binary_io.inc -> bytecode_writer.
 * Ownership: serialization subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

static int bc_get_buf(BCReaderState *s, void *buf, uint32_t buf_len)
{
    if (buf_len != 0) {
        if (unlikely(!buf || s->buf_end - s->ptr < buf_len))
            return bc_read_error_end(s);
        memcpy(buf, s->ptr, buf_len);
        s->ptr += buf_len;
    }
    return 0;
}

static int bc_idx_to_atom(BCReaderState *s, JSAtom *patom, uint32_t idx)
{
    JSAtom atom;

    if (__JS_AtomIsTaggedInt(idx)) {
        atom = idx;
    } else if (idx < s->first_atom) {
        atom = JS_DupAtom(s->ctx, idx);
    } else {
        idx -= s->first_atom;
        if (idx >= s->idx_to_atom_count) {
            JS_ThrowSyntaxError(s->ctx, "invalid atom index (pos=%u)",
                                (unsigned int)(s->ptr - s->buf_start));
            *patom = JS_ATOM_NULL;
            return s->error_state = -1;
        }
        atom = JS_DupAtom(s->ctx, s->idx_to_atom[idx]);
    }
    *patom = atom;
    return 0;
}

static int bc_get_atom(BCReaderState *s, JSAtom *patom)
{
    uint32_t v;
    if (bc_get_leb128(s, &v))
        return -1;
    if (v & 1) {
        *patom = __JS_AtomFromUInt32(v >> 1);
        return 0;
    } else {
        return bc_idx_to_atom(s, patom, v >> 1);
    }
}

static JSString *JS_ReadString(BCReaderState *s)
{
    uint32_t len;
    size_t size;
    bool is_wide_char;
    JSString *p;

    if (bc_get_leb128(s, &len))
        return NULL;
    is_wide_char = len & 1;
    len >>= 1;
    if (len > JS_STRING_LEN_MAX) {
        JS_ThrowInternalError(s->ctx, "string too long");
        return NULL;
    }
    p = js_alloc_string(s->ctx, len, is_wide_char);
    if (!p) {
        s->error_state = -1;
        return NULL;
    }
    size = (size_t)len << is_wide_char;
    if ((s->buf_end - s->ptr) < size) {
        bc_read_error_end(s);
        js_free_string(s->ctx->rt, p);
        return NULL;
    }
    memcpy(str8(p), s->ptr, size);
    s->ptr += size;
    if (!is_wide_char)
        str8(p)[size] = '\0'; /* add the trailing zero for 8 bit strings */
#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
    if (check_dump_flag(s->ctx->rt, JS_DUMP_READ_OBJECT)) {
        bc_read_trace(s, "%s", ""); // hex dump and indentation
        JS_DumpString(s->ctx->rt, p);
        printf("\n");
    }
#endif
    return p;
}

static uint32_t bc_get_flags(uint32_t flags, int *pidx, int n)
{
    uint32_t val;
    /* XXX: this does not work for n == 32 */
    val = (flags >> *pidx) & ((1U << n) - 1);
    *pidx += n;
    return val;
}

static int JS_ReadFunctionBytecode(BCReaderState *s, JSFunctionBytecode *b,
                                   int byte_code_offset, uint32_t bc_len)
{
    uint8_t *bc_buf;
    int pos, len, op;
    JSAtom atom;
    uint32_t idx;

    bc_buf = (uint8_t*)b + byte_code_offset;
    if (bc_get_buf(s, bc_buf, bc_len))
        return -1;
    b->byte_code_buf = bc_buf;

    pos = 0;
    while (pos < bc_len) {
        op = bc_buf[pos];
        len = short_opcode_info(op).size;
        switch(short_opcode_info(op).fmt) {
        case OP_FMT_atom:
        case OP_FMT_atom_u8:
        case OP_FMT_atom_u16:
        case OP_FMT_atom_label_u8:
        case OP_FMT_atom_label_u16:
            idx = get_u32(bc_buf + pos + 1);
            if (bc_idx_to_atom(s, &atom, idx)) {
                /* Note: the atoms will be freed up to this position */
                b->byte_code_len = pos;
                return -1;
            }
            put_u32(bc_buf + pos + 1, atom);
            break;
        default:
            break;
        }
#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
        if (check_dump_flag(s->ctx->rt, JS_DUMP_READ_OBJECT)) {
            const uint8_t *save_ptr = s->ptr;
            s->ptr = s->ptr_last + len;
            s->level -= 4;
            bc_read_trace(s, "%s", "");  // hex dump + indent
            dump_single_byte_code(s->ctx, bc_buf + pos, b,
                                  s->ptr - s->buf_start - len);
            s->level += 4;
            s->ptr = save_ptr;
        }
#endif
        pos += len;
    }
    return 0;
}

static JSValue JS_ReadBigInt(BCReaderState *s)
{
    JSValue obj = JS_UNDEFINED;
    uint32_t len, i, n;
    JSBigInt *p;
    js_limb_t v;
    uint8_t v8;

    if (bc_get_leb128(s, &len))
        goto fail;
    bc_read_trace(s, "len=%" PRId64 "\n", (int64_t)len);
    if (len == 0) {
        /* zero case */
        bc_read_trace(s, "}\n");
        return __JS_NewShortBigInt(s->ctx, 0);
    }
    p = js_bigint_new(s->ctx, (len - 1) / (JS_LIMB_BITS / 8) + 1);
    if (!p)
        goto fail;
    for(i = 0; i < len / (JS_LIMB_BITS / 8); i++) {
        if (bc_get_u32(s, &v))
            goto fail;
        p->tab[i] = v;
    }
    n = len % (JS_LIMB_BITS / 8);
    if (n != 0) {
        int shift;
        v = 0;
        for(i = 0; i < n; i++) {
            if (bc_get_u8(s, &v8))
                goto fail;
            v |= (js_limb_t)v8 << (i * 8);
        }
        shift = JS_LIMB_BITS - n * 8;
        /* extend the sign */
        if (shift != 0) {
            v = (js_slimb_t)(v << shift) >> shift;
        }
        p->tab[p->len - 1] = v;
    }
    bc_read_trace(s, "}\n");
    return JS_CompactBigInt(s->ctx, p);
 fail:
    JS_FreeValue(s->ctx, obj);
    return JS_EXCEPTION;
}

static JSValue JS_ReadObjectRec(BCReaderState *s);

static int BC_add_object_ref1(BCReaderState *s, JSObject *p)
{
    if (s->allow_reference) {
        if (js_resize_array(s->ctx, (void *)&s->objects,
                            sizeof(s->objects[0]),
                            &s->objects_size, s->objects_count + 1))
            return -1;
        s->objects[s->objects_count++] = p;
    }
    return 0;
}

static int BC_add_object_ref(BCReaderState *s, JSValue obj)
{
    return BC_add_object_ref1(s, JS_VALUE_GET_OBJ(obj));
}

static JSValue JS_ReadFunctionTag(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    JSFunctionBytecode bc, *b;
    JSValue obj = JS_UNDEFINED;
    uint16_t v16;
    uint8_t v8;
    int idx, i, local_count, has_debug_info;
    int function_size, cpool_offset, byte_code_offset;
    int closure_var_offset, vardefs_offset;

    memset(&bc, 0, sizeof(bc));
    //bc.gc_header.mark = 0;

    if (bc_get_u16(s, &v16))
        goto fail;
    idx = 0;
    bc.has_prototype = bc_get_flags(v16, &idx, 1);
    bc.has_simple_parameter_list = bc_get_flags(v16, &idx, 1);
    bc.is_derived_class_constructor = bc_get_flags(v16, &idx, 1);
    bc.need_home_object = bc_get_flags(v16, &idx, 1);
    bc.func_kind = bc_get_flags(v16, &idx, 2);
    bc.new_target_allowed = bc_get_flags(v16, &idx, 1);
    bc.super_call_allowed = bc_get_flags(v16, &idx, 1);
    bc.super_allowed = bc_get_flags(v16, &idx, 1);
    bc.arguments_allowed = bc_get_flags(v16, &idx, 1);
    bc.backtrace_barrier = bc_get_flags(v16, &idx, 1);
    has_debug_info = bc_get_flags(v16, &idx, 1);
    if (bc_get_u8(s, &v8))
        goto fail;
    bc.is_strict_mode = (v8 > 0);
    if (bc_get_atom(s, &bc.func_name))
        goto fail;
    if (bc_get_leb128_u16(s, &bc.arg_count))
        goto fail;
    if (bc_get_leb128_u16(s, &bc.var_count))
        goto fail;
    if (bc_get_leb128_u16(s, &bc.defined_arg_count))
        goto fail;
    if (bc_get_leb128_u16(s, &bc.stack_size))
        goto fail;
    if (bc_get_leb128_u16(s, &bc.var_ref_count))
        goto fail;
    if (bc_get_leb128_u16(s, &bc.closure_var_count))
        goto fail;
    if (bc_get_leb128_int(s, &bc.cpool_count))
        goto fail;
    if (bc_get_leb128_int(s, &bc.byte_code_len))
        goto fail;
    if (bc_get_leb128_int(s, &local_count))
        goto fail;

    function_size = sizeof(*b);
    cpool_offset = function_size;
    function_size += bc.cpool_count * sizeof(*bc.cpool);
    vardefs_offset = function_size;
    function_size += local_count * sizeof(*bc.vardefs);
    closure_var_offset = function_size;
    function_size += bc.closure_var_count * sizeof(*bc.closure_var);
    byte_code_offset = function_size;
    function_size += bc.byte_code_len;

    b = js_mallocz(ctx, function_size);
    if (!b)
        goto fail;

    memcpy(b, &bc, sizeof(*b));
    bc.func_name = JS_ATOM_NULL;
    JS_REF_COUNT(b) = 1;
    if (local_count != 0) {
        b->vardefs = (void *)((uint8_t*)b + vardefs_offset);
    }
    if (b->closure_var_count != 0) {
        b->closure_var = (void *)((uint8_t*)b + closure_var_offset);
    }
    if (b->cpool_count != 0) {
        b->cpool = (void *)((uint8_t*)b + cpool_offset);
    }

    add_gc_object(ctx->rt, &b->header, JS_GC_OBJ_TYPE_FUNCTION_BYTECODE);

    obj = JS_MKPTR(JS_TAG_FUNCTION_BYTECODE, b);

#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
    if (check_dump_flag(s->ctx->rt, JS_DUMP_READ_OBJECT)) {
        if (b->func_name) {
            bc_read_trace(s, "name: ");
            print_atom(s->ctx, b->func_name);
            printf("\n");
        }
    }
#endif
    bc_read_trace(s, "args=%d vars=%d defargs=%d closures=%d cpool=%d\n",
                  b->arg_count, b->var_count, b->defined_arg_count,
                  b->closure_var_count, b->cpool_count);
    bc_read_trace(s, "stack=%d bclen=%d locals=%d\n",
                  b->stack_size, b->byte_code_len, local_count);

    if (local_count != 0) {
        bc_read_trace(s, "vars {\n");
        bc_read_trace(s, "off flags scope name\n");
        for(i = 0; i < local_count; i++) {
            JSVarDef *vd = &b->vardefs[i];
            if (bc_get_atom(s, &vd->var_name))
                goto fail;
            if (bc_get_leb128_int(s, &vd->scope_level))
                goto fail;
            if (bc_get_leb128_int(s, &vd->scope_next))
                goto fail;
            vd->scope_next--;
            if (bc_get_u8(s, &v8))
                goto fail;
            idx = 0;
            vd->var_kind = bc_get_flags(v8, &idx, 4);
            vd->is_const = bc_get_flags(v8, &idx, 1);
            vd->is_lexical = bc_get_flags(v8, &idx, 1);
            vd->is_captured = bc_get_flags(v8, &idx, 1);
            if (vd->is_captured) {
                if (bc_get_leb128_u16(s, &vd->var_ref_idx))
                    goto fail;
            }
#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
            if (check_dump_flag(s->ctx->rt, JS_DUMP_READ_OBJECT)) {
                bc_read_trace(s, "%3d  %d%c%c%c %4d  ",
                              i, vd->var_kind,
                              vd->is_const ? 'C' : '.',
                              vd->is_lexical ? 'L' : '.',
                              vd->is_captured ? 'X' : '.',
                              vd->scope_level);
                print_atom(s->ctx, vd->var_name);
                printf("\n");
            }
#endif
        }
        bc_read_trace(s, "}\n");
    }
    if (b->closure_var_count != 0) {
        bc_read_trace(s, "closure vars {\n");
        bc_read_trace(s, "off  flags idx  name\n");
        for(i = 0; i < b->closure_var_count; i++) {
            JSClosureVar *cv = &b->closure_var[i];
            int var_idx, flags;
            if (bc_get_atom(s, &cv->var_name))
                goto fail;
            if (bc_get_leb128_int(s, &var_idx))
                goto fail;
            cv->var_idx = var_idx;
            if (bc_get_leb128_int(s, &flags))
                goto fail;
            idx = 0;
            cv->closure_type = bc_get_flags(flags, &idx, 3);
            cv->is_const = bc_get_flags(flags, &idx, 1);
            cv->is_lexical = bc_get_flags(flags, &idx, 1);
            cv->var_kind = bc_get_flags(flags, &idx, 4);
#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
            if (check_dump_flag(s->ctx->rt, JS_DUMP_READ_OBJECT)) {
                bc_read_trace(s, "%3d  %d:%d%c%c %3d  ",
                              i, cv->var_kind, cv->closure_type,
                              cv->is_const ? 'C' : '.',
                              cv->is_lexical ? 'X' : '.',
                              cv->var_idx);
                print_atom(s->ctx, cv->var_name);
                printf("\n");
            }
#endif
        }
        bc_read_trace(s, "}\n");
    }
    if (b->cpool_count != 0) {
        bc_read_trace(s, "cpool {\n");
        for(i = 0; i < b->cpool_count; i++) {
            JSValue val;
            val = JS_ReadObjectRec(s);
            if (JS_IsException(val))
                goto fail;
            b->cpool[i] = val;
        }
        bc_read_trace(s, "}\n");
    }
    {
        bc_read_trace(s, "bytecode {\n");
        if (JS_ReadFunctionBytecode(s, b, byte_code_offset, b->byte_code_len))
            goto fail;
        bc_read_trace(s, "}\n");
    }
    if (!has_debug_info)
        goto nodebug;

    /* read optional debug information */
    bc_read_trace(s, "debug {\n");
    if (bc_get_atom(s, &b->filename))
        goto fail;
    if (bc_get_leb128_int(s, &b->line_num))
        goto fail;
    if (bc_get_leb128_int(s, &b->col_num))
        goto fail;
#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
    if (check_dump_flag(s->ctx->rt, JS_DUMP_READ_OBJECT)) {
        bc_read_trace(s, "filename: ");
        print_atom(s->ctx, b->filename);
        printf(", line: %d, column: %d\n", b->line_num, b->col_num);
    }
#endif
    if (bc_get_leb128_int(s, &b->pc2line_len))
        goto fail;
    if (b->pc2line_len) {
        bc_read_trace(s, "positions: %d bytes\n", b->pc2line_len);
        b->pc2line_buf = js_mallocz(ctx, b->pc2line_len);
        if (!b->pc2line_buf)
            goto fail;
        if (bc_get_buf(s, b->pc2line_buf, b->pc2line_len))
            goto fail;
    }
    if (bc_get_leb128_int(s, &b->source_len))
        goto fail;
    if (b->source_len) {
        bc_read_trace(s, "source: %d bytes\n", b->source_len);
        if (s->ptr_last)
            s->ptr_last += b->source_len;  // omit source code hex dump
        /* b->source is a UTF-8 encoded null terminated C string */
        b->source = js_mallocz(ctx, b->source_len + 1);
        if (!b->source)
            goto fail;
        if (bc_get_buf(s, b->source, b->source_len))
            goto fail;
    }
    bc_read_trace(s, "}\n");

 nodebug:
    b->realm = JS_DupContext(ctx);
    return obj;

 fail:
    JS_FreeAtom(ctx, bc.func_name);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue JS_ReadModule(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    JSValue obj;
    JSModuleDef *m = NULL;
    JSAtom module_name;
    int i;
    uint8_t v8;

    if (bc_get_atom(s, &module_name))
        goto fail;
#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
    if (check_dump_flag(s->ctx->rt, JS_DUMP_READ_OBJECT)) {
        bc_read_trace(s, "name: ");
        print_atom(s->ctx, module_name);
        printf("\n");
    }
#endif
    m = js_new_module_def(ctx, module_name);
    if (!m)
        goto fail;
    obj = js_dup(JS_MKPTR(JS_TAG_MODULE, m));
    if (bc_get_leb128_int(s, &m->req_module_entries_count))
        goto fail;
    obj = JS_NewModuleValue(ctx, m);
    if (m->req_module_entries_count != 0) {
        m->req_module_entries_size = m->req_module_entries_count;
        m->req_module_entries = js_mallocz(ctx, sizeof(m->req_module_entries[0]) * m->req_module_entries_size);
        if (!m->req_module_entries)
            goto fail;
        for(i = 0; i < m->req_module_entries_count; i++) {
            JSReqModuleEntry *rme = &m->req_module_entries[i];
            JSModuleDef **pm = &rme->module;
            if (bc_get_atom(s, &rme->module_name))
                goto fail;
            // Resolves a module either from the cache or by requesting
            // it from the module loader. From cache is not ideal because
            // the module may not be the one it was a time of serialization
            // but directly petitioning the module loader is not correct
            // either because then the same module can get loaded twice.
            // JS_WriteModule() does not serialize modules transitively
            // because that doesn't work for C modules and is also prone
            // to loading the same JS module twice.
            *pm = js_host_resolve_imported_module_atom(s->ctx, m->module_name,
                                                       rme->module_name,
                                                       rme->attributes);
            if (!*pm)
                goto fail;
        }
    }

    if (bc_get_leb128_int(s, &m->export_entries_count))
        goto fail;
    if (m->export_entries_count != 0) {
        m->export_entries_size = m->export_entries_count;
        m->export_entries = js_mallocz(ctx, sizeof(m->export_entries[0]) * m->export_entries_size);
        if (!m->export_entries)
            goto fail;
        for(i = 0; i < m->export_entries_count; i++) {
            JSExportEntry *me = &m->export_entries[i];
            if (bc_get_u8(s, &v8))
                goto fail;
            me->export_type = v8;
            if (me->export_type == JS_EXPORT_TYPE_LOCAL) {
                if (bc_get_leb128_int(s, &me->u.local.var_idx))
                    goto fail;
            } else {
                if (bc_get_leb128_int(s, &me->u.req_module_idx))
                    goto fail;
                if (bc_get_atom(s, &me->local_name))
                    goto fail;
            }
            if (bc_get_atom(s, &me->export_name))
                goto fail;
        }
    }

    if (bc_get_leb128_int(s, &m->star_export_entries_count))
        goto fail;
    if (m->star_export_entries_count != 0) {
        m->star_export_entries_size = m->star_export_entries_count;
        m->star_export_entries = js_mallocz(ctx, sizeof(m->star_export_entries[0]) * m->star_export_entries_size);
        if (!m->star_export_entries)
            goto fail;
        for(i = 0; i < m->star_export_entries_count; i++) {
            JSStarExportEntry *se = &m->star_export_entries[i];
            if (bc_get_leb128_int(s, &se->req_module_idx))
                goto fail;
        }
    }

    if (bc_get_leb128_int(s, &m->import_entries_count))
        goto fail;
    if (m->import_entries_count != 0) {
        m->import_entries_size = m->import_entries_count;
        m->import_entries = js_mallocz(ctx, sizeof(m->import_entries[0]) * m->import_entries_size);
        if (!m->import_entries)
            goto fail;
        for(i = 0; i < m->import_entries_count; i++) {
            JSImportEntry *mi = &m->import_entries[i];
            if (bc_get_leb128_int(s, &mi->var_idx))
                goto fail;
            if (bc_get_atom(s, &mi->import_name))
                goto fail;
            if (bc_get_leb128_int(s, &mi->req_module_idx))
                goto fail;
        }
    }

    if (bc_get_u8(s, &v8))
        goto fail;
    m->has_tla = (v8 != 0);

    m->func_obj = JS_ReadObjectRec(s);
    if (JS_IsException(m->func_obj))
        goto fail;
    return obj;
 fail:
    if (m) {
        js_free_module_def(ctx, m);
    }
    return JS_EXCEPTION;
}

static JSValue JS_ReadObjectTag(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    JSValue obj;
    uint32_t prop_count, i;
    JSAtom atom;
    JSValue val;
    int ret;

    obj = JS_NewObject(ctx);
    if (BC_add_object_ref(s, obj))
        goto fail;
    if (bc_get_leb128(s, &prop_count))
        goto fail;
    for(i = 0; i < prop_count; i++) {
        if (bc_get_atom(s, &atom))
            goto fail;
#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
        if (check_dump_flag(s->ctx->rt, JS_DUMP_READ_OBJECT)) {
            bc_read_trace(s, "propname: ");
            print_atom(s->ctx, atom);
            printf("\n");
        }
#endif
        val = JS_ReadObjectRec(s);
        if (JS_IsException(val)) {
            JS_FreeAtom(ctx, atom);
            goto fail;
        }
        ret = JS_DefinePropertyValue(ctx, obj, atom, val, JS_PROP_C_W_E);
        JS_FreeAtom(ctx, atom);
        if (ret < 0)
            goto fail;
    }
    return obj;
 fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue JS_ReadArray(BCReaderState *s, int tag)
{
    JSContext *ctx = s->ctx;
    JSValue obj;
    uint32_t len, i;
    JSValue val;
    int ret, prop_flags;
    bool is_template;

    obj = JS_NewArray(ctx);
    if (BC_add_object_ref(s, obj))
        goto fail;
    is_template = (tag == BC_TAG_TEMPLATE_OBJECT);
    if (bc_get_leb128(s, &len))
        goto fail;
    for(i = 0; i < len; i++) {
        val = JS_ReadObjectRec(s);
        if (JS_IsException(val))
            goto fail;
        if (is_template)
            prop_flags = JS_PROP_ENUMERABLE;
        else
            prop_flags = JS_PROP_C_W_E;
        ret = JS_DefinePropertyValueUint32(ctx, obj, i, val,
                                           prop_flags);
        if (ret < 0)
            goto fail;
    }
    if (is_template) {
        val = JS_ReadObjectRec(s);
        if (JS_IsException(val))
            goto fail;
        if (!JS_IsUndefined(val)) {
            ret = JS_DefinePropertyValue(ctx, obj, JS_ATOM_raw, val, 0);
            if (ret < 0)
                goto fail;
        }
        JS_PreventExtensions(ctx, obj);
    }
    return obj;
 fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue JS_ReadTypedArray(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    JSValue obj = JS_UNDEFINED, array_buffer = JS_UNDEFINED;
    uint8_t array_tag;
    JSValueConst args[3];
    uint32_t offset, len, idx;

    if (bc_get_u8(s, &array_tag))
        return JS_EXCEPTION;
    if (array_tag >= JS_TYPED_ARRAY_COUNT)
        return JS_ThrowTypeError(ctx, "invalid typed array");
    if (bc_get_leb128(s, &len))
        return JS_EXCEPTION;
    if (bc_get_leb128(s, &offset))
        return JS_EXCEPTION;
    /* XXX: this hack could be avoided if the typed array could be
       created before the array buffer */
    idx = s->objects_count;
    if (BC_add_object_ref1(s, NULL))
        goto fail;
    array_buffer = JS_ReadObjectRec(s);
    if (JS_IsException(array_buffer))
        return JS_EXCEPTION;
    if (!js_get_array_buffer(ctx, array_buffer)) {
        JS_FreeValue(ctx, array_buffer);
        return JS_EXCEPTION;
    }
    args[0] = array_buffer;
    args[1] = js_int64(offset);
    args[2] = js_int64(len);
    obj = js_typed_array_constructor(ctx, JS_UNDEFINED,
                                     3, args,
                                     JS_CLASS_UINT8C_ARRAY + array_tag);
    if (JS_IsException(obj))
        goto fail;
    if (s->allow_reference) {
        s->objects[idx] = JS_VALUE_GET_OBJ(obj);
    }
    JS_FreeValue(ctx, array_buffer);
    return obj;
 fail:
    JS_FreeValue(ctx, array_buffer);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue JS_ReadArrayBuffer(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    uint32_t byte_length, max_byte_length;
    uint64_t max_byte_length_u64, *pmax_byte_length = NULL;
    JSValue obj;

    if (bc_get_leb128(s, &byte_length))
        return JS_EXCEPTION;
    if (bc_get_leb128(s, &max_byte_length))
        return JS_EXCEPTION;
    if (max_byte_length < byte_length)
        return JS_ThrowTypeError(ctx, "invalid array buffer");
    if (max_byte_length != UINT32_MAX) {
        max_byte_length_u64 = max_byte_length;
        pmax_byte_length = &max_byte_length_u64;
    }
    if (unlikely(s->buf_end - s->ptr < byte_length)) {
        bc_read_error_end(s);
        return JS_EXCEPTION;
    }
    // makes a copy of the input
    obj = js_array_buffer_constructor3(ctx, JS_UNDEFINED,
                                       byte_length, pmax_byte_length,
                                       JS_CLASS_ARRAY_BUFFER,
                                       (uint8_t*)s->ptr,
                                       js_array_buffer_free, NULL,
                                       /*alloc_flag*/true);
    if (JS_IsException(obj))
        goto fail;
    if (BC_add_object_ref(s, obj))
        goto fail;
    s->ptr += byte_length;
    return obj;
 fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue JS_ReadSharedArrayBuffer(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    uint32_t byte_length, max_byte_length;
    uint64_t max_byte_length_u64, *pmax_byte_length = NULL;
    uint8_t *data_ptr;
    JSValue obj;
    uint64_t u64;

    if (bc_get_leb128(s, &byte_length))
        return JS_EXCEPTION;
    if (bc_get_leb128(s, &max_byte_length))
        return JS_EXCEPTION;
    if (max_byte_length < byte_length)
        return JS_ThrowTypeError(ctx, "invalid array buffer");
    if (max_byte_length != UINT32_MAX) {
        max_byte_length_u64 = max_byte_length;
        pmax_byte_length = &max_byte_length_u64;
    }
    if (bc_get_u64(s, &u64))
        return JS_EXCEPTION;
    data_ptr = (uint8_t *)(uintptr_t)u64;
    if (js_resize_array(s->ctx, (void **)&s->sab_tab, sizeof(s->sab_tab[0]),
                        &s->sab_tab_size, s->sab_tab_len + 1))
        return JS_EXCEPTION;
    /* keep the SAB pointer so that the user can clone it or free it */
    s->sab_tab[s->sab_tab_len++] = data_ptr;
    /* the SharedArrayBuffer is cloned */
    obj = js_array_buffer_constructor3(ctx, JS_UNDEFINED,
                                       byte_length, pmax_byte_length,
                                       JS_CLASS_SHARED_ARRAY_BUFFER,
                                       data_ptr,
                                       NULL, NULL, false);
    if (JS_IsException(obj))
        goto fail;
    if (BC_add_object_ref(s, obj))
        goto fail;
    return obj;
 fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue JS_ReadRegExp(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    JSString *pattern;
    JSString *bc;

    pattern = JS_ReadString(s);
    if (!pattern)
        return JS_EXCEPTION;

    bc = JS_ReadString(s);
    if (!bc) {
        js_free_string(ctx->rt, pattern);
        return JS_EXCEPTION;
    }

    if (bc->is_wide_char ||
        lre_check_bytecode(str8(bc), bc->len) != 0) {
        js_free_string(ctx->rt, pattern);
        js_free_string(ctx->rt, bc);
        return JS_ThrowInternalError(ctx, "bad regexp bytecode");
    }

    return js_regexp_constructor_internal(ctx, JS_UNDEFINED,
                                          JS_MKPTR(JS_TAG_STRING, pattern),
                                          JS_MKPTR(JS_TAG_STRING, bc));
}

static JSValue JS_ReadDate(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    JSValue val, obj = JS_UNDEFINED;

    val = JS_ReadObjectRec(s);
    if (JS_IsException(val))
        goto fail;
    if (!JS_IsNumber(val)) {
        JS_ThrowTypeError(ctx, "Number tag expected for date");
        goto fail;
    }
    obj = JS_NewObjectProtoClass(ctx, ctx->class_proto[JS_CLASS_DATE],
                                 JS_CLASS_DATE);
    if (JS_IsException(obj))
        goto fail;
    if (BC_add_object_ref(s, obj))
        goto fail;
    JS_SetObjectData(ctx, obj, val);
    return obj;
 fail:
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue JS_ReadObjectValue(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    JSValue val, obj = JS_UNDEFINED;

    val = JS_ReadObjectRec(s);
    if (JS_IsException(val))
        goto fail;
    obj = JS_ToObject(ctx, val);
    if (JS_IsException(obj))
        goto fail;
    if (BC_add_object_ref(s, obj))
        goto fail;
    JS_FreeValue(ctx, val);
    return obj;
 fail:
    JS_FreeValue(ctx, val);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue JS_ReadMap(BCReaderState *s);
static JSValue JS_ReadSet(BCReaderState *s);

static JSValue JS_ReadObjectRec(BCReaderState *s)
{
    JSContext *ctx = s->ctx;
    uint8_t tag;
    JSValue obj = JS_UNDEFINED;

    if (js_check_stack_overflow(ctx->rt, 0))
        return JS_ThrowStackOverflow(ctx);

    if (bc_get_u8(s, &tag))
        return JS_EXCEPTION;

    bc_read_trace(s, "%s {\n", bc_tag_name(tag));

    switch(tag) {
    case BC_TAG_NULL:
        obj = JS_NULL;
        break;
    case BC_TAG_UNDEFINED:
        obj = JS_UNDEFINED;
        break;
    case BC_TAG_BOOL_FALSE:
    case BC_TAG_BOOL_TRUE:
        obj = js_bool(tag - BC_TAG_BOOL_FALSE);
        break;
    case BC_TAG_INT32:
        {
            int32_t val;
            if (bc_get_sleb128(s, &val))
                return JS_EXCEPTION;
            bc_read_trace(s, "%d\n", val);
            obj = js_int32(val);
        }
        break;
    case BC_TAG_FLOAT64:
        {
            JSFloat64Union u;
            if (bc_get_u64(s, &u.u64))
                return JS_EXCEPTION;
            bc_read_trace(s, "%g\n", u.d);
            obj = js_float64(u.d);
        }
        break;
    case BC_TAG_STRING:
        {
            JSString *p;
            p = JS_ReadString(s);
            if (!p)
                return JS_EXCEPTION;
            obj = JS_MKPTR(JS_TAG_STRING, p);
        }
        break;
    case BC_TAG_FUNCTION_BYTECODE:
        if (!s->allow_bytecode)
            goto no_allow_bytecode;
        obj = JS_ReadFunctionTag(s);
        break;
    case BC_TAG_MODULE:
        if (!s->allow_bytecode) {
        no_allow_bytecode:
            return JS_ThrowSyntaxError(ctx, "no bytecode allowed");
        }
        obj = JS_ReadModule(s);
        break;
    case BC_TAG_OBJECT:
        obj = JS_ReadObjectTag(s);
        break;
    case BC_TAG_ARRAY:
    case BC_TAG_TEMPLATE_OBJECT:
        obj = JS_ReadArray(s, tag);
        break;
    case BC_TAG_TYPED_ARRAY:
        obj = JS_ReadTypedArray(s);
        break;
    case BC_TAG_ARRAY_BUFFER:
        obj = JS_ReadArrayBuffer(s);
        break;
    case BC_TAG_SHARED_ARRAY_BUFFER:
        if (!s->allow_sab || !ctx->rt->sab_funcs.sab_dup)
            goto invalid_tag;
        obj = JS_ReadSharedArrayBuffer(s);
        break;
    case BC_TAG_REGEXP:
        obj = JS_ReadRegExp(s);
        break;
    case BC_TAG_DATE:
        obj = JS_ReadDate(s);
        break;
    case BC_TAG_OBJECT_VALUE:
        obj = JS_ReadObjectValue(s);
        break;
    case BC_TAG_BIG_INT:
        obj = JS_ReadBigInt(s);
        break;
    case BC_TAG_OBJECT_REFERENCE:
        {
            uint32_t val;
            if (!s->allow_reference)
                return JS_ThrowSyntaxError(ctx, "object references are not allowed");
            if (bc_get_leb128(s, &val))
                return JS_EXCEPTION;
            bc_read_trace(s, "%u\n", val);
            if (val >= s->objects_count || !s->objects[val]) {
                return JS_ThrowSyntaxError(ctx, "invalid object reference (%u >= %u)",
                                           val, s->objects_count);
            }
            obj = js_dup(JS_MKPTR(JS_TAG_OBJECT, s->objects[val]));
        }
        break;
    case BC_TAG_MAP:
        obj = JS_ReadMap(s);
        break;
    case BC_TAG_SET:
        obj = JS_ReadSet(s);
        break;
    case BC_TAG_SYMBOL:
        {
            JSAtom atom;
            if (bc_get_atom(s, &atom))
                return JS_EXCEPTION;
            if (__JS_AtomIsConst(atom)) {
                obj = JS_AtomToValue(s->ctx, atom);
            } else {
                JSAtomStruct *p = s->ctx->rt->atom_array[atom];
                obj = JS_NewSymbolFromAtom(s->ctx, atom, p->atom_type);
            }
        }
        break;
    default:
    invalid_tag:
        return JS_ThrowSyntaxError(ctx, "invalid tag (tag=%d pos=%u)",
                                   tag, (unsigned int)(s->ptr - s->buf_start));
    }
    bc_read_trace(s, "}\n");
    return obj;
}

static int JS_ReadObjectAtoms(BCReaderState *s)
{
    uint8_t v8, type;
    JSString *p;
    uint32_t h;
    int i;
    JSAtom atom;

    if (bc_get_u8(s, &v8))
        return -1;
    if (v8 != BC_VERSION) {
        JS_ThrowSyntaxError(s->ctx, "invalid version (%d expected=%d)",
                            v8, BC_VERSION);
        return -1;
    }
    if (bc_get_u32(s, &h))
        return -1;
    // allow UINT32_MAX as an escape hatch, otherwise updating
    // the test corpus in tests/test_bjson.js gets too tedious
    if (h != UINT32_MAX && h != bc_csum(s->ptr, s->buf_end - s->ptr)) {
        JS_ThrowSyntaxError(s->ctx, "checksum error");
        return -1;
    }
    if (bc_get_leb128(s, &s->idx_to_atom_count))
        return -1;
    if (s->idx_to_atom_count > 1000*1000) {
        JS_ThrowInternalError(s->ctx, "unreasonable atom count: %u",
                              s->idx_to_atom_count);
        return -1;
    }

    bc_read_trace(s, "%u atom indexes {\n", s->idx_to_atom_count);

    if (s->idx_to_atom_count != 0) {
        s->idx_to_atom = js_mallocz(s->ctx, s->idx_to_atom_count *
                                    sizeof(s->idx_to_atom[0]));
        if (!s->idx_to_atom)
            return s->error_state = -1;
    }
    for(i = 0; i < s->idx_to_atom_count; i++) {
        if (bc_get_u8(s, &type)) {
            return -1;
        }
        if (type == 0) {
            if (bc_get_u32(s, &atom))
                return -1;
            if (!__JS_AtomIsConst(atom)) {
                JS_ThrowInternalError(s->ctx, "out of range atom");
                return -1;
            }
        } else {
            if (type < JS_ATOM_TYPE_STRING || type >= JS_ATOM_TYPE_PRIVATE) {
                JS_ThrowInternalError(s->ctx, "invalid symbol type %d", type);
                return -1;
            }
            p = JS_ReadString(s);
            if (!p)
                return -1;
            atom = __JS_NewAtom(s->ctx->rt, p, type);
        }
        if (atom == JS_ATOM_NULL)
            return s->error_state = -1;
        s->idx_to_atom[i] = atom;
    }
    bc_read_trace(s, "}\n");
    return 0;
}

static void bc_reader_free(BCReaderState *s)
{
    int i;
    if (s->idx_to_atom) {
        for(i = 0; i < s->idx_to_atom_count; i++) {
            JS_FreeAtom(s->ctx, s->idx_to_atom[i]);
        }
        js_free(s->ctx, s->idx_to_atom);
    }
    js_free(s->ctx, s->objects);
}

JSValue JS_ReadObject2(JSContext *ctx, const uint8_t *buf, size_t buf_len,
                       int flags, JSSABTab *psab_tab)
{
    BCReaderState ss, *s = &ss;
    JSValue obj;

    ctx->binary_object_count += 1;
    ctx->binary_object_size += buf_len;

    memset(s, 0, sizeof(*s));
    s->ctx = ctx;
    s->buf_start = buf;
    s->buf_end = buf + buf_len;
    s->ptr = buf;
    s->allow_bytecode = ((flags & JS_READ_OBJ_BYTECODE) != 0);
    s->allow_sab = ((flags & JS_READ_OBJ_SAB) != 0);
    s->allow_reference = ((flags & JS_READ_OBJ_REFERENCE) != 0);
    if (s->allow_bytecode)
        s->first_atom = JS_ATOM_END;
    else
        s->first_atom = 1;
    if (JS_ReadObjectAtoms(s)) {
        obj = JS_EXCEPTION;
    } else {
        obj = JS_ReadObjectRec(s);
    }
    if (psab_tab) {
        psab_tab->tab = s->sab_tab;
        psab_tab->len = s->sab_tab_len;
    } else {
        js_free(ctx, s->sab_tab);
    }
    bc_reader_free(s);
    return obj;
}

JSValue JS_ReadObject(JSContext *ctx, const uint8_t *buf, size_t buf_len,
                      int flags)
{
    return JS_ReadObject2(ctx, buf, buf_len, flags, NULL);
}

/*******************************************************************/
