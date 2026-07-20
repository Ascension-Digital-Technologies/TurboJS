/* Engine domain source: objects/shapes_objects.inc -> shape_core.
 * Ownership: objects subsystem. Assembled by tools/generate_engine_unit.py; not compiled independently yet.
 */

/* Shape support */

static inline size_t get_shape_size(size_t hash_size, size_t prop_size)
{
    return hash_size * sizeof(uint32_t) + sizeof(JSShape) +
        prop_size * sizeof(JSShapeProperty);
}

static inline JSShape *get_shape_from_alloc(void *sh_alloc, size_t hash_size)
{
    (void)hash_size;
    return (JSShape *)sh_alloc; /* shape sits at the allocation start */
}

/* one-past-the-end of the hash bucket array; buckets are addressed as
   prop_hash_end(sh)[-h - 1] for h in [0, prop_hash_mask], in BOTH layouts. */
static inline uint32_t *prop_hash_end(JSShape *sh)
{
    return sh->hash_table + sh->prop_hash_mask + 1;
}

/* the JSShapeProperty array */
static inline JSShapeProperty *get_shape_prop(JSShape *sh)
{
    return (JSShapeProperty *)(void *)(sh->hash_table + sh->prop_hash_mask + 1);
}

static inline void *get_alloc_from_shape(JSShape *sh)
{
    return sh; /* shape sits at the allocation start */
}

static int init_shape_hash(JSRuntime *rt)
{
    rt->shape_hash_bits = 6;   /* 64 shapes */
    rt->shape_hash_size = 1 << rt->shape_hash_bits;
    rt->shape_hash_count = 0;
    rt->shape_hash = js_mallocz_rt(rt, sizeof(rt->shape_hash[0]) *
                                   rt->shape_hash_size);
    if (!rt->shape_hash)
        return -1;
    return 0;
}

/* same magic hash multiplier as the Linux kernel */
static uint32_t shape_hash(uint32_t h, uint32_t val)
{
    return hash32(h + val);
}

/* truncate the shape hash to 'hash_bits' bits */
static uint32_t get_shape_hash(uint32_t h, int hash_bits)
{
    return h >> (32 - hash_bits);
}

static uint32_t shape_initial_hash(JSObject *proto)
{
    uint32_t h;
    h = shape_hash(1, (uintptr_t)proto);
    if (sizeof(proto) > 4)
        h = shape_hash(h, (uint64_t)(uintptr_t)proto >> 32);
    return h;
}

static int resize_shape_hash(JSRuntime *rt, int new_shape_hash_bits)
{
    int new_shape_hash_size, i;
    uint32_t h;
    JSShape **new_shape_hash, *sh, *sh_next;

    new_shape_hash_size = 1 << new_shape_hash_bits;
    new_shape_hash = js_mallocz_rt(rt, sizeof(rt->shape_hash[0]) *
                                   new_shape_hash_size);
    if (!new_shape_hash)
        return -1;
    for(i = 0; i < rt->shape_hash_size; i++) {
        for(sh = rt->shape_hash[i]; sh != NULL; sh = sh_next) {
            sh_next = sh->shape_hash_next;
            h = get_shape_hash(sh->hash, new_shape_hash_bits);
            sh->shape_hash_next = new_shape_hash[h];
            new_shape_hash[h] = sh;
        }
    }
    js_free_rt(rt, rt->shape_hash);
    rt->shape_hash_bits = new_shape_hash_bits;
    rt->shape_hash_size = new_shape_hash_size;
    rt->shape_hash = new_shape_hash;
    return 0;
}

static void js_shape_hash_link(JSRuntime *rt, JSShape *sh)
{
    uint32_t h;
    h = get_shape_hash(sh->hash, rt->shape_hash_bits);
    sh->shape_hash_next = rt->shape_hash[h];
    rt->shape_hash[h] = sh;
    rt->shape_hash_count++;
}

static void js_shape_hash_unlink(JSRuntime *rt, JSShape *sh)
{
    uint32_t h;
    JSShape **psh;

    h = get_shape_hash(sh->hash, rt->shape_hash_bits);
    psh = &rt->shape_hash[h];
    while (*psh != sh)
        psh = &(*psh)->shape_hash_next;
    *psh = sh->shape_hash_next;
    rt->shape_hash_count--;
}

/* create a new empty shape with prototype 'proto'. It is not hashed */
static inline JSShape *js_new_shape_nohash(JSContext *ctx, JSObject *proto,
                                           int hash_size, int prop_size)
{
    JSRuntime *rt = ctx->rt;
    void *sh_alloc;
    JSShape *sh;

    sh_alloc = js_malloc(ctx, get_shape_size(hash_size, prop_size));
    if (!sh_alloc)
        return NULL;
    sh = get_shape_from_alloc(sh_alloc, hash_size);
    JS_REF_COUNT(sh) = 1;
    add_gc_object(rt, &sh->header, JS_GC_OBJ_TYPE_SHAPE);
    if (proto)
        js_dup(JS_MKPTR(JS_TAG_OBJECT, proto));
    sh->proto = proto;
    /* prop_hash_mask must be set before prop_hash_end(sh) is used, as the hash
       location depends on it in the merged-header layout. */
    sh->prop_hash_mask = hash_size - 1;
    memset(prop_hash_end(sh) - hash_size, 0, sizeof(prop_hash_end(sh)[0]) *
           hash_size);
    sh->prop_size = prop_size;
    sh->prop_count = 0;
    sh->deleted_prop_count = 0;
    sh->is_hashed = false;
    return sh;
}

/* create a new empty shape with prototype 'proto' */
static no_inline JSShape *js_new_shape2(JSContext *ctx, JSObject *proto,
                                        int hash_size, int prop_size)
{
    JSRuntime *rt = ctx->rt;
    JSShape *sh;

    /* resize the shape hash table if necessary */
    if (2 * (rt->shape_hash_count + 1) > rt->shape_hash_size) {
        resize_shape_hash(rt, rt->shape_hash_bits + 1);
    }

    sh = js_new_shape_nohash(ctx, proto, hash_size, prop_size);
    if (!sh)
        return NULL;

    /* insert in the hash table */
    sh->hash = shape_initial_hash(proto);
    sh->is_hashed = true;
    js_shape_hash_link(ctx->rt, sh);
    return sh;
}

static JSShape *js_new_shape(JSContext *ctx, JSObject *proto)
{
    return js_new_shape2(ctx, proto, JS_PROP_INITIAL_HASH_SIZE,
                         JS_PROP_INITIAL_SIZE);
}

static JSObject *object_or_null(JSValueConst val)
{
    if (JS_TAG_OBJECT == JS_VALUE_GET_TAG(val))
        return JS_VALUE_GET_OBJ(val);
    return NULL;
}

static int add_shape_property(JSContext *ctx, JSShape **psh,
                              JSObject *p, JSAtom atom, int prop_flags);

static JSShape *js_new_shape_with2(JSContext *ctx, JSObject *proto,
                                   int prop_count, const JSShapeProperty props[]) {
    JSShape *sh;
    int i;

    sh = js_new_shape2(ctx, proto, JS_PROP_INITIAL_HASH_SIZE, prop_count);
    if (sh)
        for (i = 0; i < prop_count; i++)
            if (add_shape_property(ctx, &sh, NULL, props[i].atom, props[i].flags))
                goto fail;
    return sh;
fail:
    js_free_shape(ctx->rt, sh);
    return NULL;
}

static int js_new_shape_with(JSContext *ctx, JSShape **psh, JSValueConst proto,
                              int prop_count, const JSShapeProperty props[]) {
    *psh = js_new_shape_with2(ctx, object_or_null(proto), prop_count, props);
    if (*psh)
        return 0;
    return -1;
}

/* The shape is cloned. The new shape is not inserted in the shape
   hash table */
static JSShape *js_clone_shape(JSContext *ctx, JSShape *sh1)
{
    JSShape *sh;
    void *sh_alloc, *sh_alloc1;
    size_t size;
    JSShapeProperty *pr;
    uint32_t i, hash_size;

    hash_size = sh1->prop_hash_mask + 1;
    size = get_shape_size(hash_size, sh1->prop_size);
    sh_alloc = js_malloc(ctx, size);
    if (!sh_alloc)
        return NULL;
    sh_alloc1 = get_alloc_from_shape(sh1);
    memcpy(sh_alloc, sh_alloc1, size);
    sh = get_shape_from_alloc(sh_alloc, hash_size);
    JS_REF_COUNT(sh) = 1;
    add_gc_object(ctx->rt, &sh->header, JS_GC_OBJ_TYPE_SHAPE);
    sh->is_hashed = false;
    if (sh->proto) {
        js_dup(JS_MKPTR(JS_TAG_OBJECT, sh->proto));
    }
    for(i = 0, pr = get_shape_prop(sh); i < sh->prop_count; i++, pr++) {
        JS_DupAtom(ctx, pr->atom);
    }
    return sh;
}

static JSShape *js_dup_shape(JSShape *sh)
{
    JS_REF_COUNT(sh)++;
    return sh;
}

static void js_free_shape0(JSRuntime *rt, JSShape *sh)
{
    uint32_t i;
    JSShapeProperty *pr;

    assert(JS_REF_COUNT(sh) == 0);
    for (i = 0; i < countof(rt->shape_transition_source); i++) {
        if (rt->shape_transition_source[i] == sh ||
            rt->shape_transition_target[i] == sh) {
            rt->shape_transition_source[i] = NULL;
            rt->shape_transition_target[i] = NULL;
            rt->shape_transition_atom[i] = JS_ATOM_NULL;
            rt->shape_transition_flags[i] = 0;
        }
    }
    if (sh->is_hashed)
        js_shape_hash_unlink(rt, sh);
    if (sh->proto != NULL) {
        JS_FreeValueRT(rt, JS_MKPTR(JS_TAG_OBJECT, sh->proto));
    }
    pr = get_shape_prop(sh);
    for(i = 0; i < sh->prop_count; i++) {
        JS_FreeAtomRT(rt, pr->atom);
        pr++;
    }
    remove_gc_object(&sh->header);
    js_free_rt(rt, get_alloc_from_shape(sh));
}

static void js_free_shape(JSRuntime *rt, JSShape *sh)
{
    if (unlikely(--JS_REF_COUNT(sh) <= 0)) {
        js_free_shape0(rt, sh);
    }
}

static void js_free_shape_null(JSRuntime *rt, JSShape *sh)
{
    if (sh)
        js_free_shape(rt, sh);
}

/* make space to hold at least 'count' properties */
static no_inline int resize_properties(JSContext *ctx, JSShape **psh,
                                       JSObject *p, uint32_t count)
{
    JSShape *sh;
    uint32_t new_size, new_hash_size, new_hash_mask, i;
    JSShapeProperty *pr;
    void *sh_alloc;
    intptr_t h;

    sh = *psh;
    /* Avoid the common 4 -> 6 -> 9 double growth for medium object
       literals while keeping less slack than an eight-slot jump. */
    if (sh->prop_size < 7)
        new_size = max_int(count, 7);
    else
        new_size = max_int(count, sh->prop_size * 3 / 2);
    /* Reallocate prop array first to avoid crash or size inconsistency
       in case of memory allocation failure */
    if (p) {
        JSProperty *new_prop;
        new_prop = js_realloc(ctx, p->prop, sizeof(new_prop[0]) * new_size);
        if (unlikely(!new_prop))
            return -1;
        p->prop = new_prop;
    }
    new_hash_size = sh->prop_hash_mask + 1;
    while (new_hash_size < new_size)
        new_hash_size = 2 * new_hash_size;
    if (new_hash_size != (sh->prop_hash_mask + 1)) {
        JSShape *old_sh;
        /* resize the hash table and the properties */
        old_sh = sh;
        sh_alloc = js_malloc(ctx, get_shape_size(new_hash_size, new_size));
        if (!sh_alloc)
            return -1;
        sh = get_shape_from_alloc(sh_alloc, new_hash_size);
        list_del(&old_sh->header.link);
        /* copy the shape header, then the properties. Their location relative
           to the struct differs by layout, so copy via get_shape_prop(). */
        memcpy(sh, old_sh, sizeof(JSShape));
        list_add_tail(&sh->header.link, &ctx->rt->gc_obj_list);
        /* the GC/refcount fields live in the block header, not the struct, so
           the memcpy above did not carry them: transfer them explicitly */
        JS_REF_COUNT(sh) = JS_REF_COUNT(old_sh);
        JS_GC_TYPE(sh) = JS_GC_TYPE(old_sh);
        JS_GC_MARK(sh) = JS_GC_MARK(old_sh);
        new_hash_mask = new_hash_size - 1;
        sh->prop_hash_mask = new_hash_mask;
        memcpy(get_shape_prop(sh), get_shape_prop(old_sh),
               sizeof(JSShapeProperty) * old_sh->prop_count);
        memset(prop_hash_end(sh) - new_hash_size, 0,
               sizeof(prop_hash_end(sh)[0]) * new_hash_size);
        for(i = 0, pr = get_shape_prop(sh); i < sh->prop_count; i++, pr++) {
            if (pr->atom != JS_ATOM_NULL) {
                h = ((uintptr_t)pr->atom & new_hash_mask);
                pr->hash_next = prop_hash_end(sh)[-h - 1];
                prop_hash_end(sh)[-h - 1] = i + 1;
            }
        }
        js_free(ctx, get_alloc_from_shape(old_sh));
    } else {
        /* only resize the properties */
        list_del(&sh->header.link);
        sh_alloc = js_realloc(ctx, get_alloc_from_shape(sh),
                              get_shape_size(new_hash_size, new_size));
        if (unlikely(!sh_alloc)) {
            /* insert again in the GC list */
            list_add_tail(&sh->header.link, &ctx->rt->gc_obj_list);
            return -1;
        }
        sh = get_shape_from_alloc(sh_alloc, new_hash_size);
        list_add_tail(&sh->header.link, &ctx->rt->gc_obj_list);
    }
    *psh = sh;
    sh->prop_size = new_size;
    return 0;
}

/* remove the deleted properties. */
static int compact_properties(JSContext *ctx, JSObject *p)
{
    JSShape *sh, *old_sh;
    void *sh_alloc;
    intptr_t h;
    uint32_t new_hash_size, i, j, new_hash_mask, new_size;
    JSShapeProperty *old_pr, *pr;
    JSProperty *prop, *new_prop;

    sh = p->shape;
    assert(!sh->is_hashed);

    new_size = max_int(JS_PROP_INITIAL_SIZE,
                       sh->prop_count - sh->deleted_prop_count);
    assert(new_size <= sh->prop_size);

    new_hash_size = sh->prop_hash_mask + 1;
    while ((new_hash_size / 2) >= new_size)
        new_hash_size = new_hash_size / 2;
    new_hash_mask = new_hash_size - 1;

    /* resize the hash table and the properties */
    old_sh = sh;
    sh_alloc = js_malloc(ctx, get_shape_size(new_hash_size, new_size));
    if (!sh_alloc)
        return -1;
    sh = get_shape_from_alloc(sh_alloc, new_hash_size);
    list_del(&old_sh->header.link);
    memcpy(sh, old_sh, sizeof(JSShape));
    list_add_tail(&sh->header.link, &ctx->rt->gc_obj_list);
    /* the GC/refcount fields live in the block header, not the struct, so the
       memcpy above did not carry them: transfer them explicitly */
    JS_REF_COUNT(sh) = JS_REF_COUNT(old_sh);
    JS_GC_TYPE(sh) = JS_GC_TYPE(old_sh);
    JS_GC_MARK(sh) = JS_GC_MARK(old_sh);

    /* set the new hash mask before prop_hash_end()/get_shape_prop() are used,
       as their locations depend on it in the merged-header layout */
    sh->prop_hash_mask = new_hash_mask;
    memset(prop_hash_end(sh) - new_hash_size, 0,
           sizeof(prop_hash_end(sh)[0]) * new_hash_size);

    j = 0;
    old_pr = get_shape_prop(old_sh);
    pr = get_shape_prop(sh);
    prop = p->prop;
    for(i = 0; i < sh->prop_count; i++) {
        if (old_pr->atom != JS_ATOM_NULL) {
            pr->atom = old_pr->atom;
            pr->flags = old_pr->flags;
            h = ((uintptr_t)old_pr->atom & new_hash_mask);
            pr->hash_next = prop_hash_end(sh)[-h - 1];
            prop_hash_end(sh)[-h - 1] = j + 1;
            prop[j] = prop[i];
            j++;
            pr++;
        }
        old_pr++;
    }
    assert(j == (sh->prop_count - sh->deleted_prop_count));
    sh->prop_hash_mask = new_hash_mask;
    sh->prop_size = new_size;
    sh->deleted_prop_count = 0;
    sh->prop_count = j;

    p->shape = sh;
    js_free(ctx, get_alloc_from_shape(old_sh));

    /* reduce the size of the object properties */
    new_prop = js_realloc(ctx, p->prop, sizeof(new_prop[0]) * new_size);
    if (new_prop)
        p->prop = new_prop;
    return 0;
}

static int add_shape_property(JSContext *ctx, JSShape **psh,
                              JSObject *p, JSAtom atom, int prop_flags)
{
    JSRuntime *rt = ctx->rt;
    JSShape *sh = *psh;
    JSShapeProperty *pr, *prop;
    uint32_t hash_mask, new_shape_hash = 0;
    intptr_t h;

    /* update the shape hash */
    if (sh->is_hashed) {
        js_shape_hash_unlink(rt, sh);
        new_shape_hash = shape_hash(shape_hash(sh->hash, atom), prop_flags);
    }

    if (unlikely(sh->prop_count >= sh->prop_size)) {
        if (resize_properties(ctx, psh, p, sh->prop_count + 1)) {
            /* in case of error, reinsert in the hash table.
               sh is still valid if resize_properties() failed */
            if (sh->is_hashed)
                js_shape_hash_link(rt, sh);
            return -1;
        }
        sh = *psh;
    }
    if (sh->is_hashed) {
        sh->hash = new_shape_hash;
        js_shape_hash_link(rt, sh);
    }
    /* Initialize the new shape property.
       The object property at p->prop[sh->prop_count] is uninitialized */
    prop = get_shape_prop(sh);
    pr = &prop[sh->prop_count++];
    pr->atom = JS_DupAtom(ctx, atom);
    pr->flags = prop_flags;
    /* add in hash table */
    hash_mask = sh->prop_hash_mask;
    h = atom & hash_mask;
    pr->hash_next = prop_hash_end(sh)[-h - 1];
    prop_hash_end(sh)[-h - 1] = sh->prop_count;
    return 0;
}

/* find a hashed empty shape matching the prototype. Return NULL if
   not found */
static JSShape *find_hashed_shape_proto(JSRuntime *rt, JSObject *proto)
{
    JSShape *sh1;
    uint32_t h, h1;

    h = shape_initial_hash(proto);
    h1 = get_shape_hash(h, rt->shape_hash_bits);
    for(sh1 = rt->shape_hash[h1]; sh1 != NULL; sh1 = sh1->shape_hash_next) {
        if (sh1->hash == h &&
            sh1->proto == proto &&
            sh1->prop_count == 0) {
            return sh1;
        }
    }
    return NULL;
}

/* find a hashed shape matching sh + (prop, prop_flags). Return NULL if
   not found */
static JSShape *find_hashed_shape_prop(JSRuntime *rt, JSShape *sh,
                                       JSAtom atom, int prop_flags)
{
    JSShape *sh1;
    uint32_t h, h1, i, n, cache_index;

    cache_index = ((((uintptr_t)sh >> 4) ^ (uintptr_t)atom ^
                    (uint32_t)prop_flags * 0x9e3779b9u) &
                   (countof(rt->shape_transition_source) - 1));
    if (likely(rt->shape_transition_source[cache_index] == sh &&
               rt->shape_transition_atom[cache_index] == atom &&
               rt->shape_transition_flags[cache_index] == (uint8_t)prop_flags)) {
        sh1 = rt->shape_transition_target[cache_index];
        if (likely(sh1 != NULL)) {
            rt->shape_transition_hits++;
            return sh1;
        }
    }
    rt->shape_transition_misses++;

    h = sh->hash;
    h = shape_hash(h, atom);
    h = shape_hash(h, prop_flags);
    h1 = get_shape_hash(h, rt->shape_hash_bits);
    for(sh1 = rt->shape_hash[h1]; sh1 != NULL; sh1 = sh1->shape_hash_next) {
        /* we test the hash first so that the rest is done only if the
           shapes really match */
        if (sh1->hash == h &&
            sh1->proto == sh->proto &&
            sh1->prop_count == ((n = sh->prop_count) + 1)) {
            for(i = 0; i < n; i++) {
                if (unlikely(get_shape_prop(sh1)[i].atom != get_shape_prop(sh)[i].atom) ||
                    unlikely(get_shape_prop(sh1)[i].flags != get_shape_prop(sh)[i].flags))
                    goto next;
            }
            if (unlikely(get_shape_prop(sh1)[n].atom != atom) ||
                unlikely(get_shape_prop(sh1)[n].flags != prop_flags))
                goto next;
            rt->shape_transition_source[cache_index] = sh;
            rt->shape_transition_target[cache_index] = sh1;
            rt->shape_transition_atom[cache_index] = atom;
            rt->shape_transition_flags[cache_index] = (uint8_t)prop_flags;
            rt->shape_transition_fills++;
            return sh1;
        }
    next: ;
    }
    return NULL;
}

static __maybe_unused void JS_DumpShape(JSRuntime *rt, int i, JSShape *sh)
{
    char atom_buf[ATOM_GET_STR_BUF_SIZE];
    int j;

    /* XXX: should output readable class prototype */
    printf("%5d %3d%c %14p %5d %5d", i,
           JS_REF_COUNT(sh), " *"[sh->is_hashed],
           (void *)sh->proto, sh->prop_size, sh->prop_count);
    for(j = 0; j < sh->prop_count; j++) {
        printf(" %s", JS_AtomGetStrRT(rt, atom_buf, sizeof(atom_buf),
                                      get_shape_prop(sh)[j].atom));
    }
    printf("\n");
}


/* Four-way polymorphic VM property inline cache for ordinary own data
   properties. Each bytecode site can retain several stable immutable shapes,
   avoiding cache thrashing when a call site observes a small object family. */
static inline TurboJSVMPropertyICEntry *turbojs_vm_property_ic_slot(
    JSFunctionBytecode *b, uint32_t bytecode_offset)
{
    uint32_t h;
    if (unlikely(!b->property_ic)) {
        b->property_ic = js_mallocz_rt(b->realm->rt,
            sizeof(*b->property_ic) * TURBOJS_VM_PROPERTY_IC_SIZE);
        if (unlikely(!b->property_ic))
            return NULL;
    }
    h = bytecode_offset * 2654435761u;
    return &b->property_ic[(h >> 28) & (TURBOJS_VM_PROPERTY_IC_SIZE - 1u)];
}

static inline int turbojs_vm_property_ic_lookup(JSRuntime *rt,
    JSFunctionBytecode *b, uint32_t bytecode_offset, JSObject *object,
    JSAtom atom, JSProperty **out_property)
{
    TurboJSVMPropertyICEntry *entry;
    uint32_t i;
    entry = turbojs_vm_property_ic_slot(b, bytecode_offset);
    if (unlikely(!entry))
        return 0;
    if (unlikely(entry->bytecode_offset != bytecode_offset || entry->atom != atom)) {
        rt->property_ic_misses++;
        return 0;
    }
    /* Same-object temporal locality is extremely common in hot application
       loops. The cached object pointer is only a hint: validate the current
       immutable shape before using the slot index, and always derive the
       property address from the current object so allocator address reuse
       cannot leave a stale JSProperty pointer in the cache. */
    if (likely(entry->last_object == object &&
               entry->last_shape == object->shape)) {
        rt->property_ic_hits++;
        *out_property = &object->prop[entry->last_property_index];
        return 1;
    }
    if (likely(entry->used_ways != 0)) {
        TurboJSVMPropertyICWay *hot = &entry->ways[entry->hot_way];
        if (likely(hot->shape == object->shape)) {
            entry->last_object = object;
            entry->last_shape = object->shape;
            entry->last_property_index = hot->property_index;
            rt->property_ic_hits++;
            *out_property = &object->prop[hot->property_index];
            return 1;
        }
    }
    for (i = 0; i < entry->used_ways; ++i) {
        TurboJSVMPropertyICWay *way = &entry->ways[i];
        /* Shapes are immutable while retained by the cache. A matching shape
           therefore guarantees that the property index and descriptor class
           validated at fill time are still valid. Avoid repeating atom, flag,
           and bounds checks on every hot property load. */
        if (likely(way->shape == object->shape)) {
            entry->hot_way = (uint8_t)i;
            entry->last_object = object;
            entry->last_shape = object->shape;
            entry->last_property_index = way->property_index;
            rt->property_ic_hits++;
            *out_property = &object->prop[way->property_index];
            return 1;
        }
    }
    rt->property_ic_misses++;
    return 0;
}

static inline int turbojs_vm_property_ic_lookup_writable(JSRuntime *rt,
    JSFunctionBytecode *b, uint32_t bytecode_offset, JSObject *object,
    JSAtom atom, JSProperty **out_property)
{
    TurboJSVMPropertyICEntry *entry;
    uint32_t i;
    entry = turbojs_vm_property_ic_slot(b, bytecode_offset);
    if (unlikely(!entry))
        return 0;
    if (unlikely(entry->bytecode_offset != bytecode_offset || entry->atom != atom)) {
        rt->property_ic_misses++;
        return 0;
    }
    /* Same-object temporal locality is extremely common in hot application
       loops. The cached object pointer is only a hint: validate the current
       immutable shape before using the slot index, and always derive the
       property address from the current object so allocator address reuse
       cannot leave a stale JSProperty pointer in the cache. */
    if (likely(entry->last_object == object &&
               entry->last_shape == object->shape)) {
        rt->property_ic_hits++;
        *out_property = &object->prop[entry->last_property_index];
        return 1;
    }
    if (likely(entry->used_ways != 0)) {
        TurboJSVMPropertyICWay *hot = &entry->ways[entry->hot_way];
        if (likely(hot->shape == object->shape)) {
            entry->last_object = object;
            entry->last_shape = object->shape;
            entry->last_property_index = hot->property_index;
            rt->property_ic_hits++;
            *out_property = &object->prop[hot->property_index];
            return 1;
        }
    }
    for (i = 0; i < entry->used_ways; ++i) {
        TurboJSVMPropertyICWay *way = &entry->ways[i];
        /* Writable sites only fill after validating an ordinary writable data
           descriptor. Descriptor mutations transition the object to another
           immutable shape, so a shape hit can use the cached slot directly. */
        if (likely(way->shape == object->shape)) {
            entry->hot_way = (uint8_t)i;
            entry->last_object = object;
            entry->last_shape = object->shape;
            entry->last_property_index = way->property_index;
            rt->property_ic_hits++;
            *out_property = &object->prop[way->property_index];
            return 1;
        }
    }
    rt->property_ic_misses++;
    return 0;
}

static inline void turbojs_vm_property_ic_fill(JSRuntime *rt,
    JSFunctionBytecode *b, uint32_t bytecode_offset, JSObject *object,
    JSAtom atom, JSProperty *property)
{
    TurboJSVMPropertyICEntry *entry;
    TurboJSVMPropertyICWay *way;
    ptrdiff_t property_index;
    uint32_t i, index;
    entry = turbojs_vm_property_ic_slot(b, bytecode_offset);
    if (unlikely(!entry))
        return;
    property_index = property - object->prop;
    if (unlikely(property_index < 0 || property_index >= object->shape->prop_count))
        return;
    if (entry->bytecode_offset != bytecode_offset || entry->atom != atom) {
        for (i = 0; i < entry->used_ways; ++i)
            if (entry->ways[i].shape)
                js_free_shape(rt, entry->ways[i].shape);
        memset(entry, 0, sizeof(*entry));
        entry->bytecode_offset = bytecode_offset;
        entry->atom = atom;
    }
    for (i = 0; i < entry->used_ways; ++i) {
        if (entry->ways[i].shape == object->shape) {
            entry->ways[i].property_index = (uint32_t)property_index;
            entry->hot_way = (uint8_t)i;
            entry->last_object = object;
            entry->last_shape = object->shape;
            entry->last_property_index = (uint32_t)property_index;
            rt->property_ic_fills++;
            return;
        }
    }
    if (entry->used_ways < TURBOJS_VM_PROPERTY_IC_WAYS)
        index = entry->used_ways++;
    else {
        index = entry->replacement_way++ & (TURBOJS_VM_PROPERTY_IC_WAYS - 1u);
        if (entry->ways[index].shape)
            js_free_shape(rt, entry->ways[index].shape);
    }
    way = &entry->ways[index];
    way->shape = js_dup_shape(object->shape);
    way->property_index = (uint32_t)property_index;
    entry->hot_way = (uint8_t)index;
    entry->last_object = object;
    entry->last_shape = object->shape;
    entry->last_property_index = (uint32_t)property_index;
    rt->property_ic_fills++;
}

static void turbojs_vm_property_ic_destroy(JSRuntime *rt,
                                            JSFunctionBytecode *b)
{
    uint32_t i, j;
    if (!b || !b->property_ic)
        return;
    for (i = 0; i < TURBOJS_VM_PROPERTY_IC_SIZE; ++i)
        for (j = 0; j < b->property_ic[i].used_ways; ++j)
            if (b->property_ic[i].ways[j].shape)
                js_free_shape(rt, b->property_ic[i].ways[j].shape);
    js_free_rt(rt, b->property_ic);
    b->property_ic = NULL;
}
