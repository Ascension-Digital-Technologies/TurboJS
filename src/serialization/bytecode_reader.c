/* Engine domain source: serialization/binary_io.inc -> bytecode_reader.
 * Ownership: serialization subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

/* object list */

typedef struct {
    JSObject *obj;
    uint32_t hash_next; /* -1 if no next entry */
} JSObjectListEntry;

/* XXX: reuse it to optimize weak references */
typedef struct {
    JSObjectListEntry *object_tab;
    int object_count;
    int object_size;
    uint32_t *hash_table;
    uint32_t hash_size;
} JSObjectList;

static void js_object_list_init(JSObjectList *s)
{
    memset(s, 0, sizeof(*s));
}

static uint32_t js_object_list_get_hash(JSObject *p, uint32_t hash_size)
{
    return ((uintptr_t)p * 3163) & (hash_size - 1);
}

static int js_object_list_resize_hash(JSContext *ctx, JSObjectList *s,
                                 uint32_t new_hash_size)
{
    JSObjectListEntry *e;
    uint32_t i, h, *new_hash_table;

    new_hash_table = js_malloc(ctx, sizeof(new_hash_table[0]) * new_hash_size);
    if (!new_hash_table)
        return -1;
    js_free(ctx, s->hash_table);
    s->hash_table = new_hash_table;
    s->hash_size = new_hash_size;

    for(i = 0; i < s->hash_size; i++) {
        s->hash_table[i] = -1;
    }
    for(i = 0; i < s->object_count; i++) {
        e = &s->object_tab[i];
        h = js_object_list_get_hash(e->obj, s->hash_size);
        e->hash_next = s->hash_table[h];
        s->hash_table[h] = i;
    }
    return 0;
}

/* the reference count of 'obj' is not modified. Return 0 if OK, -1 if
   memory error */
static int js_object_list_add(JSContext *ctx, JSObjectList *s, JSObject *obj)
{
    JSObjectListEntry *e;
    uint32_t h, new_hash_size;

    if (js_resize_array(ctx, (void *)&s->object_tab,
                        sizeof(s->object_tab[0]),
                        &s->object_size, s->object_count + 1))
        return -1;
    if (unlikely((s->object_count + 1) >= s->hash_size)) {
        new_hash_size = max_uint32(s->hash_size, 4);
        while (new_hash_size <= s->object_count)
            new_hash_size *= 2;
        if (js_object_list_resize_hash(ctx, s, new_hash_size))
            return -1;
    }
    e = &s->object_tab[s->object_count++];
    h = js_object_list_get_hash(obj, s->hash_size);
    e->obj = obj;
    e->hash_next = s->hash_table[h];
    s->hash_table[h] = s->object_count - 1;
    return 0;
}

/* return -1 if not present or the object index */
static int js_object_list_find(JSContext *ctx, JSObjectList *s, JSObject *obj)
{
    JSObjectListEntry *e;
    uint32_t h, p;

    /* must test empty size because there is no hash table */
    if (s->object_count == 0)
        return -1;
    h = js_object_list_get_hash(obj, s->hash_size);
    p = s->hash_table[h];
    while (p != -1) {
        e = &s->object_tab[p];
        if (e->obj == obj)
            return p;
        p = e->hash_next;
    }
    return -1;
}

static void js_object_list_end(JSContext *ctx, JSObjectList *s)
{
    js_free(ctx, s->object_tab);
    js_free(ctx, s->hash_table);
}

/*******************************************************************/
/* binary object writer & reader */

typedef enum BCTagEnum {
    BC_TAG_NULL = 1,
    BC_TAG_UNDEFINED,
    BC_TAG_BOOL_FALSE,
    BC_TAG_BOOL_TRUE,
    BC_TAG_INT32,
    BC_TAG_FLOAT64,
    BC_TAG_STRING,
    BC_TAG_OBJECT,
    BC_TAG_ARRAY,
    BC_TAG_BIG_INT,
    BC_TAG_TEMPLATE_OBJECT,
    BC_TAG_FUNCTION_BYTECODE,
    BC_TAG_MODULE,
    BC_TAG_TYPED_ARRAY,
    BC_TAG_ARRAY_BUFFER,
    BC_TAG_SHARED_ARRAY_BUFFER,
    BC_TAG_REGEXP,
    BC_TAG_DATE,
    BC_TAG_OBJECT_VALUE,
    BC_TAG_OBJECT_REFERENCE,
    BC_TAG_MAP,
    BC_TAG_SET,
    BC_TAG_SYMBOL,
} BCTagEnum;

#define BC_VERSION 27

typedef struct BCWriterState {
    JSContext *ctx;
    DynBuf dbuf;
    bool allow_bytecode;
    bool allow_sab;
    bool allow_reference;
    bool allow_source;
    bool allow_debug;
    uint32_t first_atom;
    uint32_t *atom_to_idx;
    int atom_to_idx_size;
    JSAtom *idx_to_atom;
    int idx_to_atom_count;
    int idx_to_atom_size;
    uint8_t **sab_tab;
    int sab_tab_len;
    int sab_tab_size;
    /* list of referenced objects (used if allow_reference = true) */
    JSObjectList object_list;
} BCWriterState;

#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
static const char * const bc_tag_str[] = {
    "invalid",
    "null",
    "undefined",
    "false",
    "true",
    "int32",
    "float64",
    "string",
    "object",
    "array",
    "BigInt",
    "template",
    "function",
    "module",
    "TypedArray",
    "ArrayBuffer",
    "SharedArrayBuffer",
    "RegExp",
    "Date",
    "ObjectValue",
    "ObjectReference",
    "Map",
    "Set",
    "Symbol",
};

static const char *bc_tag_name(uint8_t tag)
{
    if (tag >= countof(bc_tag_str))
        return "<bad tag>";
    return bc_tag_str[tag];
}
#endif

static void bc_put_u8(BCWriterState *s, uint8_t v)
{
    dbuf_putc(&s->dbuf, v);
}

static void bc_put_u16(BCWriterState *s, uint16_t v)
{
    dbuf_put_u16(&s->dbuf, v);
}

static __maybe_unused void bc_put_u32(BCWriterState *s, uint32_t v)
{
    dbuf_put_u32(&s->dbuf, v);
}

static void bc_put_u64(BCWriterState *s, uint64_t v)
{
    dbuf_put(&s->dbuf, (uint8_t *)&v, sizeof(v));
}

static void bc_put_leb128(BCWriterState *s, uint32_t v)
{
    dbuf_put_leb128(&s->dbuf, v);
}

static void bc_put_sleb128(BCWriterState *s, int32_t v)
{
    dbuf_put_sleb128(&s->dbuf, v);
}

static void bc_set_flags(uint32_t *pflags, int *pidx, uint32_t val, int n)
{
    *pflags = *pflags | (val << *pidx);
    *pidx += n;
}

static int bc_atom_to_idx(BCWriterState *s, uint32_t *pres, JSAtom atom)
{
    uint32_t v;

    if (atom < s->first_atom || __JS_AtomIsTaggedInt(atom)) {
        *pres = atom;
        return 0;
    }
    atom -= s->first_atom;
    if (atom < s->atom_to_idx_size && s->atom_to_idx[atom] != 0) {
        *pres = s->atom_to_idx[atom];
        return 0;
    }
    if (atom >= s->atom_to_idx_size) {
        int old_size, i;
        old_size = s->atom_to_idx_size;
        if (js_resize_array(s->ctx, (void **)&s->atom_to_idx,
                            sizeof(s->atom_to_idx[0]), &s->atom_to_idx_size,
                            atom + 1))
            return -1;
        /* XXX: could add a specific js_resize_array() function to do it */
        for(i = old_size; i < s->atom_to_idx_size; i++)
            s->atom_to_idx[i] = 0;
    }
    if (js_resize_array(s->ctx, (void **)&s->idx_to_atom,
                        sizeof(s->idx_to_atom[0]),
                        &s->idx_to_atom_size, s->idx_to_atom_count + 1))
        goto fail;

    v = s->idx_to_atom_count++;
    s->idx_to_atom[v] = atom + s->first_atom;
    v += s->first_atom;
    s->atom_to_idx[atom] = v;
    *pres = v;
    return 0;
 fail:
    *pres = 0;
    return -1;
}

static int bc_put_atom(BCWriterState *s, JSAtom atom)
{
    uint32_t v;

    if (__JS_AtomIsTaggedInt(atom)) {
        v = (__JS_AtomToUInt32(atom) << 1) | 1;
    } else {
        if (bc_atom_to_idx(s, &v, atom))
            return -1;
        v <<= 1;
    }
    bc_put_leb128(s, v);
    return 0;
}

static uint32_t bc_csum(const uint8_t *p, size_t n)
{
    uint32_t a, b, c, h;
    size_t i;

    h = 0;
    for (i = 0; i+4 < n; i += 4) {
        h += get_u32(p+i);
        h *= 0x9e370001;
    }
    a = b = c = 0;
    switch (n-i) {
    case 3:
        c = (uint32_t)p[i+2];
    case 2:
        b = (uint32_t)p[i+1];
    case 1:
        a = (uint32_t)p[i+0];
    case 0:
        break;
    }
    h += a | b<<8 | c<<16;
    h *= 0x9e370001;
    return h;
}

static int JS_WriteFunctionBytecode(BCWriterState *s,
                                    const JSFunctionBytecode *b)
{
    int pos, len, bc_len, op;
    JSAtom atom;
    uint8_t *bc_buf;
    uint32_t val;

    bc_len = b->byte_code_len;
    bc_buf = js_malloc(s->ctx, bc_len);
    if (!bc_buf)
        return -1;
    memcpy(bc_buf, b->byte_code_buf, bc_len);

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
            atom = get_u32(bc_buf + pos + 1);
            if (bc_atom_to_idx(s, &val, atom))
                goto fail;
            put_u32(bc_buf + pos + 1, val);
            break;
        default:
            break;
        }
        pos += len;
    }

    dbuf_put(&s->dbuf, bc_buf, bc_len);

    js_free(s->ctx, bc_buf);
    return 0;
 fail:
    js_free(s->ctx, bc_buf);
    return -1;
}

static void JS_WriteString(BCWriterState *s, JSString *p)
{
    int i;
    bc_put_leb128(s, ((uint32_t)p->len << 1) | p->is_wide_char);
    if (p->is_wide_char) {
        for(i = 0; i < p->len; i++)
            bc_put_u16(s, str16(p)[i]);
    } else {
        dbuf_put(&s->dbuf, str8(p), p->len);
    }
}

static int JS_WriteBigInt(BCWriterState *s, JSValueConst obj)
{
    JSBigIntBuf buf;
    JSBigInt *p;
    uint32_t len, i;
    js_limb_t v, b;
    int shift;

    bc_put_u8(s, BC_TAG_BIG_INT);

    if (JS_VALUE_GET_TAG(obj) == JS_TAG_SHORT_BIG_INT)
        p = js_bigint_set_short(&buf, obj);
    else
        p = JS_VALUE_GET_PTR(obj);
    if (p->len == 1 && p->tab[0] == 0) {
        /* zero case */
        len = 0;
    } else {
        /* compute the length of the two's complement representation
           in bytes */
        len = p->len * (JS_LIMB_BITS / 8);
        v = p->tab[p->len - 1];
        shift = JS_LIMB_BITS - 8;
        while (shift > 0) {
            b = (v >> shift) & 0xff;
            if (b != 0x00 && b != 0xff)
                break;
            if ((b & 1) != ((v >> (shift - 1)) & 1))
                break;
            shift -= 8;
            len--;
        }
    }
    bc_put_leb128(s, len);
    if (len > 0) {
        for(i = 0; i < (len / (JS_LIMB_BITS / 8)); i++) {
            bc_put_u32(s, p->tab[i]);
        }
        for(i = 0; i < len % (JS_LIMB_BITS / 8); i++) {
            bc_put_u8(s, (p->tab[p->len - 1] >> (i * 8)) & 0xff);
        }
    }
    return 0;
}

static int JS_WriteObjectRec(BCWriterState *s, JSValueConst obj);

static int JS_WriteFunctionTag(BCWriterState *s, JSValueConst obj)
{
    JSFunctionBytecode *b = JS_VALUE_GET_PTR(obj);
    uint32_t flags;
    int idx, i;

    bc_put_u8(s, BC_TAG_FUNCTION_BYTECODE);
    flags = idx = 0;
    bc_set_flags(&flags, &idx, b->has_prototype, 1);
    bc_set_flags(&flags, &idx, b->has_simple_parameter_list, 1);
    bc_set_flags(&flags, &idx, b->is_derived_class_constructor, 1);
    bc_set_flags(&flags, &idx, b->need_home_object, 1);
    bc_set_flags(&flags, &idx, b->func_kind, 2);
    bc_set_flags(&flags, &idx, b->new_target_allowed, 1);
    bc_set_flags(&flags, &idx, b->super_call_allowed, 1);
    bc_set_flags(&flags, &idx, b->super_allowed, 1);
    bc_set_flags(&flags, &idx, b->arguments_allowed, 1);
    bc_set_flags(&flags, &idx, b->backtrace_barrier, 1);
    bc_set_flags(&flags, &idx, s->allow_debug, 1);
    assert(idx <= 16);
    bc_put_u16(s, flags);
    bc_put_u8(s, b->is_strict_mode);
    bc_put_atom(s, b->func_name);

    bc_put_leb128(s, b->arg_count);
    bc_put_leb128(s, b->var_count);
    bc_put_leb128(s, b->defined_arg_count);
    bc_put_leb128(s, b->stack_size);
    bc_put_leb128(s, b->var_ref_count);
    bc_put_leb128(s, b->closure_var_count);
    bc_put_leb128(s, b->cpool_count);
    bc_put_leb128(s, b->byte_code_len);
    if (b->vardefs) {
        /* XXX: this field is redundant */
        bc_put_leb128(s, b->arg_count + b->var_count);
        for(i = 0; i < b->arg_count + b->var_count; i++) {
            JSVarDef *vd = &b->vardefs[i];
            bc_put_atom(s, vd->var_name);
            bc_put_leb128(s, vd->scope_level);
            bc_put_leb128(s, vd->scope_next + 1);
            flags = idx = 0;
            bc_set_flags(&flags, &idx, vd->var_kind, 4);
            bc_set_flags(&flags, &idx, vd->is_const, 1);
            bc_set_flags(&flags, &idx, vd->is_lexical, 1);
            bc_set_flags(&flags, &idx, vd->is_captured, 1);
            assert(idx <= 8);
            bc_put_u8(s, flags);
            if (vd->is_captured)
                bc_put_leb128(s, vd->var_ref_idx);
        }
    } else {
        bc_put_leb128(s, 0);
    }

    for(i = 0; i < b->closure_var_count; i++) {
        JSClosureVar *cv = &b->closure_var[i];
        bc_put_atom(s, cv->var_name);
        bc_put_leb128(s, cv->var_idx);
        flags = idx = 0;
        bc_set_flags(&flags, &idx, cv->closure_type, 3);
        bc_set_flags(&flags, &idx, cv->is_const, 1);
        bc_set_flags(&flags, &idx, cv->is_lexical, 1);
        bc_set_flags(&flags, &idx, cv->var_kind, 4);
        assert(idx <= 16);
        bc_put_leb128(s, flags);
    }

    // write constant pool before code so code can be disassembled
    // on the fly at read time
    for(i = 0; i < b->cpool_count; i++) {
        if (JS_WriteObjectRec(s, b->cpool[i]))
            goto fail;
    }

    if (JS_WriteFunctionBytecode(s, b))
        goto fail;

    if (s->allow_debug) {
        bc_put_atom(s, b->filename);
        bc_put_leb128(s, b->line_num);
        bc_put_leb128(s, b->col_num);
        bc_put_leb128(s, b->pc2line_len);
        dbuf_put(&s->dbuf, b->pc2line_buf, b->pc2line_len);
        if (s->allow_source && b->source) {
            bc_put_leb128(s, b->source_len);
            dbuf_put(&s->dbuf, b->source, b->source_len);
        } else {
            bc_put_leb128(s, 0);
        }
    }
    return 0;
 fail:
    return -1;
}

static int JS_WriteModule(BCWriterState *s, JSValueConst obj)
{
    JSModuleDef *m = JS_VALUE_GET_PTR(obj);
    int i;

    bc_put_u8(s, BC_TAG_MODULE);
    bc_put_atom(s, m->module_name);

    bc_put_leb128(s, m->req_module_entries_count);
    for(i = 0; i < m->req_module_entries_count; i++) {
        JSReqModuleEntry *rme = &m->req_module_entries[i];
        bc_put_atom(s, rme->module_name);
    }

    bc_put_leb128(s, m->export_entries_count);
    for(i = 0; i < m->export_entries_count; i++) {
        JSExportEntry *me = &m->export_entries[i];
        bc_put_u8(s, me->export_type);
        if (me->export_type == JS_EXPORT_TYPE_LOCAL) {
            bc_put_leb128(s, me->u.local.var_idx);
        } else {
            bc_put_leb128(s, me->u.req_module_idx);
            bc_put_atom(s, me->local_name);
        }
        bc_put_atom(s, me->export_name);
    }

    bc_put_leb128(s, m->star_export_entries_count);
    for(i = 0; i < m->star_export_entries_count; i++) {
        JSStarExportEntry *se = &m->star_export_entries[i];
        bc_put_leb128(s, se->req_module_idx);
    }

    bc_put_leb128(s, m->import_entries_count);
    for(i = 0; i < m->import_entries_count; i++) {
        JSImportEntry *mi = &m->import_entries[i];
        bc_put_leb128(s, mi->var_idx);
        bc_put_atom(s, mi->import_name);
        bc_put_leb128(s, mi->req_module_idx);
    }

    bc_put_u8(s, m->has_tla);

    if (JS_WriteObjectRec(s, m->func_obj))
        goto fail;
    return 0;
 fail:
    return -1;
}

static int JS_WriteArray(BCWriterState *s, JSValueConst obj)
{
    JSObject *p = JS_VALUE_GET_OBJ(obj);
    uint32_t i, len;
    JSValue val;
    int ret;
    bool is_template;

    if (s->allow_bytecode && !p->extensible) {
        /* not extensible array: we consider it is a
           template when we are saving bytecode */
        bc_put_u8(s, BC_TAG_TEMPLATE_OBJECT);
        is_template = true;
    } else {
        bc_put_u8(s, BC_TAG_ARRAY);
        is_template = false;
    }
    if (js_get_length32(s->ctx, &len, obj))
        goto fail1;
    bc_put_leb128(s, len);
    for(i = 0; i < len; i++) {
        val = JS_GetPropertyUint32(s->ctx, obj, i);
        if (JS_IsException(val))
            goto fail1;
        ret = JS_WriteObjectRec(s, val);
        JS_FreeValue(s->ctx, val);
        if (ret)
            goto fail1;
    }
    if (is_template) {
        val = JS_GetProperty(s->ctx, obj, JS_ATOM_raw);
        if (JS_IsException(val))
            goto fail1;
        ret = JS_WriteObjectRec(s, val);
        JS_FreeValue(s->ctx, val);
        if (ret)
            goto fail1;
    }
    return 0;
 fail1:
    return -1;
}

static int JS_WriteObjectTag(BCWriterState *s, JSValueConst obj)
{
    JSObject *p = JS_VALUE_GET_OBJ(obj);
    uint32_t i, prop_count;
    JSShape *sh;
    JSShapeProperty *pr;
    int pass;
    JSAtom atom;

    bc_put_u8(s, BC_TAG_OBJECT);
    prop_count = 0;
    sh = p->shape;
    for(pass = 0; pass < 2; pass++) {
        if (pass == 1)
            bc_put_leb128(s, prop_count);
        for(i = 0, pr = get_shape_prop(sh); i < sh->prop_count; i++, pr++) {
            atom = pr->atom;
            if (atom != JS_ATOM_NULL && (pr->flags & JS_PROP_ENUMERABLE)) {
                if (pr->flags & JS_PROP_TMASK) {
                    JS_ThrowTypeError(s->ctx, "only value properties are supported");
                    goto fail;
                }
                if (pass == 0) {
                    prop_count++;
                } else {
                    bc_put_atom(s, atom);
                    if (JS_WriteObjectRec(s, p->prop[i].u.value))
                        goto fail;
                }
            }
        }
    }
    return 0;
 fail:
    return -1;
}

static int JS_WriteTypedArray(BCWriterState *s, JSValueConst obj)
{
    JSObject *p = JS_VALUE_GET_OBJ(obj);
    JSTypedArray *ta = p->u.typed_array;

    bc_put_u8(s, BC_TAG_TYPED_ARRAY);
    bc_put_u8(s, p->class_id - JS_CLASS_UINT8C_ARRAY);
    bc_put_leb128(s, p->u.array.count);
    bc_put_leb128(s, ta->offset);
    if (JS_WriteObjectRec(s, JS_MKPTR(JS_TAG_OBJECT, ta->buffer)))
        return -1;
    return 0;
}

static int JS_WriteArrayBuffer(BCWriterState *s, JSValueConst obj)
{
    JSObject *p = JS_VALUE_GET_OBJ(obj);
    JSArrayBuffer *abuf = p->u.array_buffer;
    if (abuf->detached) {
        JS_ThrowTypeErrorDetachedArrayBuffer(s->ctx);
        return -1;
    }
    bc_put_u8(s, BC_TAG_ARRAY_BUFFER);
    bc_put_leb128(s, abuf->byte_length);
    bc_put_leb128(s, abuf->max_byte_length);
    dbuf_put(&s->dbuf, abuf->data, abuf->byte_length);
    return 0;
}

static int JS_WriteSharedArrayBuffer(BCWriterState *s, JSValueConst obj)
{
    JSObject *p = JS_VALUE_GET_OBJ(obj);
    JSArrayBuffer *abuf = p->u.array_buffer;
    assert(!abuf->detached); /* SharedArrayBuffer are never detached */
    bc_put_u8(s, BC_TAG_SHARED_ARRAY_BUFFER);
    bc_put_leb128(s, abuf->byte_length);
    bc_put_leb128(s, abuf->max_byte_length);
    bc_put_u64(s, (uintptr_t)abuf->data);
    if (js_resize_array(s->ctx, (void **)&s->sab_tab, sizeof(s->sab_tab[0]),
                        &s->sab_tab_size, s->sab_tab_len + 1))
        return -1;
    /* keep the SAB pointer so that the user can clone it or free it */
    s->sab_tab[s->sab_tab_len++] = abuf->data;
    return 0;
}

static int JS_WriteRegExp(BCWriterState *s, JSRegExp regexp)
{
    JSString *bc = regexp.bytecode;
    assert(!bc->is_wide_char);
    JS_WriteString(s, regexp.pattern);
    JS_WriteString(s, bc);
    return 0;
}

static int JS_WriteMap(BCWriterState *s, struct JSMapState *map_state);
static int JS_WriteSet(BCWriterState *s, struct JSMapState *map_state);

static int JS_WriteObjectRec(BCWriterState *s, JSValueConst obj)
{
    uint32_t tag;

    if (js_check_stack_overflow(s->ctx->rt, 0)) {
        JS_ThrowStackOverflow(s->ctx);
        return -1;
    }

    tag = JS_VALUE_GET_NORM_TAG(obj);
    switch(tag) {
    case JS_TAG_NULL:
        bc_put_u8(s, BC_TAG_NULL);
        break;
    case JS_TAG_UNDEFINED:
        bc_put_u8(s, BC_TAG_UNDEFINED);
        break;
    case JS_TAG_BOOL:
        bc_put_u8(s, BC_TAG_BOOL_FALSE + JS_VALUE_GET_INT(obj));
        break;
    case JS_TAG_INT:
        bc_put_u8(s, BC_TAG_INT32);
        bc_put_sleb128(s, JS_VALUE_GET_INT(obj));
        break;
    case JS_TAG_FLOAT64:
        {
            JSFloat64Union u;
            bc_put_u8(s, BC_TAG_FLOAT64);
            u.d = JS_VALUE_GET_FLOAT64(obj);
            bc_put_u64(s, u.u64);
        }
        break;
    case JS_TAG_STRING:
        {
            JSString *p = JS_VALUE_GET_STRING(obj);
            bc_put_u8(s, BC_TAG_STRING);
            JS_WriteString(s, p);
        }
        break;
    case JS_TAG_STRING_ROPE:
        {
            JSValue str;
            int ret;
            str = JS_ToString(s->ctx, obj);
            if (JS_IsException(str))
                goto fail;
            ret = JS_WriteObjectRec(s, str);
            JS_FreeValue(s->ctx, str);
            if (ret)
                goto fail;
        }
        break;
    case JS_TAG_FUNCTION_BYTECODE:
        if (!s->allow_bytecode)
            goto invalid_tag;
        if (JS_WriteFunctionTag(s, obj))
            goto fail;
        break;
    case JS_TAG_MODULE:
        if (!s->allow_bytecode)
            goto invalid_tag;
        if (JS_WriteModule(s, obj))
            goto fail;
        break;
    case JS_TAG_OBJECT:
        {
            JSObject *p = JS_VALUE_GET_OBJ(obj);
            int ret, idx;

            if (s->allow_reference) {
                idx = js_object_list_find(s->ctx, &s->object_list, p);
                if (idx >= 0) {
                    bc_put_u8(s, BC_TAG_OBJECT_REFERENCE);
                    bc_put_leb128(s, idx);
                    break;
                } else {
                    if (js_object_list_add(s->ctx, &s->object_list, p))
                        goto fail;
                }
            } else {
                if (p->tmp_mark) {
                    JS_ThrowTypeError(s->ctx, "circular reference");
                    goto fail;
                }
                p->tmp_mark = 1;
            }
            switch(p->class_id) {
            case JS_CLASS_ARRAY:
                ret = JS_WriteArray(s, obj);
                break;
            case JS_CLASS_OBJECT:
                ret = JS_WriteObjectTag(s, obj);
                break;
            case JS_CLASS_ARRAY_BUFFER:
                ret = JS_WriteArrayBuffer(s, obj);
                break;
            case JS_CLASS_SHARED_ARRAY_BUFFER:
                if (!s->allow_sab)
                    goto invalid_tag;
                ret = JS_WriteSharedArrayBuffer(s, obj);
                break;
            case JS_CLASS_REGEXP:
                bc_put_u8(s, BC_TAG_REGEXP);
                ret = JS_WriteRegExp(s, p->u.regexp);
                break;
            case JS_CLASS_DATE:
                bc_put_u8(s, BC_TAG_DATE);
                ret = JS_WriteObjectRec(s, p->u.object_data);
                break;
            case JS_CLASS_NUMBER:
            case JS_CLASS_STRING:
            case JS_CLASS_BOOLEAN:
            case JS_CLASS_BIG_INT:
                bc_put_u8(s, BC_TAG_OBJECT_VALUE);
                ret = JS_WriteObjectRec(s, p->u.object_data);
                break;
            case JS_CLASS_MAP:
                bc_put_u8(s, BC_TAG_MAP);
                ret = JS_WriteMap(s, p->u.map_state);
                break;
            case JS_CLASS_SET:
                bc_put_u8(s, BC_TAG_SET);
                ret = JS_WriteSet(s, p->u.map_state);
                break;
            default:
                if (is_typed_array(p->class_id)) {
                    ret = JS_WriteTypedArray(s, obj);
                } else {
                    JS_ThrowTypeError(s->ctx, "unsupported object class");
                    ret = -1;
                }
                break;
            }
            p->tmp_mark = 0;
            if (ret)
                goto fail;
        }
        break;
    case JS_TAG_SHORT_BIG_INT:
    case JS_TAG_BIG_INT:
        if (JS_WriteBigInt(s, obj))
            goto fail;
        break;
    case JS_TAG_SYMBOL:
        {
            JSAtomStruct *p = JS_VALUE_GET_PTR(obj);
            if (p->atom_type != JS_ATOM_TYPE_GLOBAL_SYMBOL && p->atom_type != JS_ATOM_TYPE_SYMBOL) {
                JS_ThrowTypeError(s->ctx, "unsupported symbol type");
                goto fail;
            }
            JSAtom atom = js_get_atom_index(s->ctx->rt, p);
            bc_put_u8(s, BC_TAG_SYMBOL);
            bc_put_atom(s, atom);
        }
        break;
    default:
    invalid_tag:
        JS_ThrowInternalError(s->ctx, "unsupported tag (%d)", tag);
        goto fail;
    }
    return 0;

 fail:
    return -1;
}

/* create the atom table */
static int JS_WriteObjectAtoms(BCWriterState *s)
{
    JSRuntime *rt = s->ctx->rt;
    DynBuf dbuf1;
    int i, atoms_size;

    dbuf1 = s->dbuf;
    js_dbuf_init(s->ctx, &s->dbuf);
    bc_put_u8(s, BC_VERSION);
    bc_put_u32(s, 0); // checksum, filled in after serialization

    bc_put_leb128(s, s->idx_to_atom_count);
    for(i = 0; i < s->idx_to_atom_count; i++) {
        JSAtom atom = s->idx_to_atom[i];
        if (__JS_AtomIsConst(atom)) {
            bc_put_u8(s, 0 /* the type */);
            /* TODO(saghul): encoding for tagged integers and keyword-ish atoms could be
               more efficient. */
            bc_put_u32(s, atom);
        } else {
            JSAtomStruct *p = rt->atom_array[atom];
            uint8_t type = p->atom_type;
            assert(type != JS_ATOM_TYPE_PRIVATE);
            bc_put_u8(s, type);
            JS_WriteString(s, p);
        }
    }
    /* XXX: should check for OOM in above phase */

    /* move the atoms at the start */
    /* XXX: could just append dbuf1 data, but it uses more memory if
       dbuf1 is larger than dbuf */
    atoms_size = s->dbuf.size;
    if (dbuf_claim(&dbuf1, atoms_size))
        goto fail;
    memmove(dbuf1.buf + atoms_size, dbuf1.buf, dbuf1.size);
    memcpy(dbuf1.buf, s->dbuf.buf, atoms_size);
    dbuf1.size += atoms_size;
    dbuf_free(&s->dbuf);
    s->dbuf = dbuf1;
    return 0;
 fail:
    dbuf_free(&dbuf1);
    return -1;
}

uint8_t *JS_WriteObject2(JSContext *ctx, size_t *psize, JSValueConst obj,
                         int flags, JSSABTab *psab_tab)
{
    BCWriterState ss, *s = &ss;
    uint32_t h;
    DynBuf *d;

    memset(s, 0, sizeof(*s));
    s->ctx = ctx;
    s->allow_bytecode = ((flags & JS_WRITE_OBJ_BYTECODE) != 0);
    s->allow_sab = ((flags & JS_WRITE_OBJ_SAB) != 0);
    s->allow_reference = ((flags & JS_WRITE_OBJ_REFERENCE) != 0);
    s->allow_source = ((flags & JS_WRITE_OBJ_STRIP_SOURCE) == 0);
    s->allow_debug = ((flags & JS_WRITE_OBJ_STRIP_DEBUG) == 0);
    /* XXX: could use a different version when bytecode is included */
    if (s->allow_bytecode)
        s->first_atom = JS_ATOM_END;
    else
        s->first_atom = 1;
    js_dbuf_init(ctx, &s->dbuf);
    js_object_list_init(&s->object_list);

    if (JS_WriteObjectRec(s, obj))
        goto fail;
    if (JS_WriteObjectAtoms(s))
        goto fail;
    js_object_list_end(ctx, &s->object_list);
    js_free(ctx, s->atom_to_idx);
    js_free(ctx, s->idx_to_atom);
    *psize = s->dbuf.size;
    if (psab_tab) {
        psab_tab->tab = s->sab_tab;
        psab_tab->len = s->sab_tab_len;
    } else {
        js_free(ctx, s->sab_tab);
    }
    // don't include version and checksum fields in checksum
    d = &s->dbuf;
    h = bc_csum(&d->buf[5], d->size - 5);
    put_u32(&d->buf[1], h);
    return d->buf;
 fail:
    js_object_list_end(ctx, &s->object_list);
    js_free(ctx, s->atom_to_idx);
    js_free(ctx, s->idx_to_atom);
    dbuf_free(&s->dbuf);
    *psize = 0;
    if (psab_tab) {
        psab_tab->tab = NULL;
        psab_tab->len = 0;
    }
    return NULL;
}

uint8_t *JS_WriteObject(JSContext *ctx, size_t *psize, JSValueConst obj,
                        int flags)
{
    return JS_WriteObject2(ctx, psize, obj, flags, NULL);
}

typedef struct BCReaderState {
    JSContext *ctx;
    const uint8_t *buf_start, *ptr, *buf_end;
    uint32_t first_atom;
    uint32_t idx_to_atom_count;
    JSAtom *idx_to_atom;
    int error_state;
    bool allow_sab;
    bool allow_bytecode;
    bool allow_reference;
    /* object references */
    JSObject **objects;
    int objects_count;
    int objects_size;
    /* SAB references */
    uint8_t **sab_tab;
    int sab_tab_len;
    int sab_tab_size;
    /* used for JS_DUMP_READ_OBJECT */
    const uint8_t *ptr_last;
    int level;
} BCReaderState;

#ifdef ENABLE_DUMPS // JS_DUMP_READ_OBJECT
static void JS_PRINTF_FORMAT_ATTR(2, 3) bc_read_trace(BCReaderState *s, JS_PRINTF_FORMAT const char *fmt, ...) {
    va_list ap;
    int i, n, n0;

    if (!check_dump_flag(s->ctx->rt, JS_DUMP_READ_OBJECT))
        return;

    if (!s->ptr_last)
        s->ptr_last = s->buf_start;

    n = n0 = 0;
    if (s->ptr > s->ptr_last || s->ptr == s->buf_start) {
        n0 = printf("%04x: ", (int)(s->ptr_last - s->buf_start));
        n += n0;
    }
    for (i = 0; s->ptr_last < s->ptr; i++) {
        if ((i & 7) == 0 && i > 0) {
            printf("\n%*s", n0, "");
            n = n0;
        }
        n += printf(" %02x", *s->ptr_last++);
    }
    if (*fmt == '}')
        s->level--;
    if (n < 32 + s->level * 2) {
        printf("%*s", 32 + s->level * 2 - n, "");
    }
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    if (strchr(fmt, '{'))
        s->level++;
}
#else
#define bc_read_trace(...)
#endif

static int bc_read_error_end(BCReaderState *s)
{
    if (!s->error_state) {
        JS_ThrowSyntaxError(s->ctx, "read after the end of the buffer");
    }
    return s->error_state = -1;
}

static int bc_get_u8(BCReaderState *s, uint8_t *pval)
{
    if (unlikely(s->buf_end - s->ptr < 1)) {
        *pval = 0; /* avoid warning */
        return bc_read_error_end(s);
    }
    *pval = *s->ptr++;
    return 0;
}

static int bc_get_u16(BCReaderState *s, uint16_t *pval)
{
    uint16_t v;
    if (unlikely(s->buf_end - s->ptr < 2)) {
        *pval = 0; /* avoid warning */
        return bc_read_error_end(s);
    }
    v = get_u16(s->ptr);
    *pval = v;
    s->ptr += 2;
    return 0;
}

static __maybe_unused int bc_get_u32(BCReaderState *s, uint32_t *pval)
{
    uint32_t v;
    if (unlikely(s->buf_end - s->ptr < 4)) {
        *pval = 0; /* avoid warning */
        return bc_read_error_end(s);
    }
    v = get_u32(s->ptr);
    *pval = v;
    s->ptr += 4;
    return 0;
}

static int bc_get_u64(BCReaderState *s, uint64_t *pval)
{
    uint64_t v;
    if (unlikely(s->buf_end - s->ptr < 8)) {
        *pval = 0; /* avoid warning */
        return bc_read_error_end(s);
    }
    v = get_u64(s->ptr);
    *pval = v;
    s->ptr += 8;
    return 0;
}

static int bc_get_leb128(BCReaderState *s, uint32_t *pval)
{
    int ret;
    ret = get_leb128(pval, s->ptr, s->buf_end);
    if (unlikely(ret < 0))
        return bc_read_error_end(s);
    s->ptr += ret;
    return 0;
}

static int bc_get_sleb128(BCReaderState *s, int32_t *pval)
{
    int ret;
    ret = get_sleb128(pval, s->ptr, s->buf_end);
    if (unlikely(ret < 0))
        return bc_read_error_end(s);
    s->ptr += ret;
    return 0;
}

/* XXX: used to read an `int` with a positive value */
static int bc_get_leb128_int(BCReaderState *s, int *pval)
{
    return bc_get_leb128(s, (uint32_t *)pval);
}

static int bc_get_leb128_u16(BCReaderState *s, uint16_t *pval)
{
    uint32_t val;
    if (bc_get_leb128(s, &val)) {
        *pval = 0;
        return -1;
    }
    *pval = val;
    return 0;
}

