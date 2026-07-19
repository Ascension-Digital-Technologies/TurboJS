#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "jit.h"
#include "executable_memory.h"

struct TurboJSOSRLoopProgram {
    TurboJSOSRCountedLoopSpec spec;
    void *code;
    void *float_code;
    size_t code_size;
    size_t float_code_size;
    size_t allocation_size;
    size_t float_allocation_size;
};

typedef int (*TurboJSRawOSRLoopKernel)(TurboJSOSRFrame *frame);

typedef struct CodeBuffer {
    uint8_t bytes[256];
    size_t count;
} CodeBuffer;

static int emit8(CodeBuffer *b, uint8_t v) {
    if (!b || b->count >= sizeof(b->bytes)) return 0;
    b->bytes[b->count++] = v;
    return 1;
}
static int emit32(CodeBuffer *b, uint32_t v) {
    unsigned i;
    for (i = 0; i < 4; ++i) if (!emit8(b, (uint8_t)(v >> (i * 8u)))) return 0;
    return 1;
}
static void patch32(CodeBuffer *b, size_t off, int32_t value) {
    unsigned i;
    for (i = 0; i < 4; ++i) b->bytes[off + i] = (uint8_t)((uint32_t)value >> (i * 8u));
}
static int rex(CodeBuffer *b, int w, unsigned r, unsigned x, unsigned m) {
    return emit8(b, (uint8_t)(0x40u | (w ? 8u : 0u) | ((r & 8u) ? 4u : 0u) |
                              ((x & 8u) ? 2u : 0u) | ((m & 8u) ? 1u : 0u)));
}
static int mov_rr(CodeBuffer *b, unsigned dst, unsigned src) {
    return rex(b, 1, src, 0, dst) && emit8(b, 0x89) &&
           emit8(b, (uint8_t)(0xC0u | ((src & 7u) << 3u) | (dst & 7u)));
}
static int load_base_disp32(CodeBuffer *b, unsigned dst, unsigned base, uint32_t disp) {
    return rex(b, 1, dst, 0, base) && emit8(b, 0x8B) &&
           emit8(b, (uint8_t)(0x80u | ((dst & 7u) << 3u) | (base & 7u))) && emit32(b, disp);
}
static int store_base_disp32(CodeBuffer *b, unsigned base, unsigned src, uint32_t disp) {
    return rex(b, 1, src, 0, base) && emit8(b, 0x89) &&
           emit8(b, (uint8_t)(0x80u | ((src & 7u) << 3u) | (base & 7u))) && emit32(b, disp);
}
static int add_rr(CodeBuffer *b, unsigned dst, unsigned src) {
    return rex(b, 1, src, 0, dst) && emit8(b, 0x01) &&
           emit8(b, (uint8_t)(0xC0u | ((src & 7u) << 3u) | (dst & 7u)));
}
static int add_imm32(CodeBuffer *b, unsigned reg, int32_t imm) {
    return rex(b, 1, 0, 0, reg) && emit8(b, 0x81) &&
           emit8(b, (uint8_t)(0xC0u | (reg & 7u))) && emit32(b, (uint32_t)imm);
}

static int movq_xmm_mem(CodeBuffer *b, unsigned xmm, unsigned base, uint32_t disp) {
    return emit8(b, 0xF3) && rex(b, 0, xmm, 0, base) && emit8(b, 0x0F) && emit8(b, 0x7E) &&
           emit8(b, (uint8_t)(0x80u | ((xmm & 7u) << 3u) | (base & 7u))) && emit32(b, disp);
}
static int movq_mem_xmm(CodeBuffer *b, unsigned base, unsigned xmm, uint32_t disp) {
    return emit8(b, 0x66) && rex(b, 0, xmm, 0, base) && emit8(b, 0x0F) && emit8(b, 0xD6) &&
           emit8(b, (uint8_t)(0x80u | ((xmm & 7u) << 3u) | (base & 7u))) && emit32(b, disp);
}
static int cvtsi2sd_xmm_reg(CodeBuffer *b, unsigned xmm, unsigned reg) {
    return emit8(b, 0xF2) && rex(b, 1, xmm, 0, reg) && emit8(b, 0x0F) && emit8(b, 0x2A) &&
           emit8(b, (uint8_t)(0xC0u | ((xmm & 7u) << 3u) | (reg & 7u)));
}
static int addsd_xmm_xmm(CodeBuffer *b, unsigned dst, unsigned src) {
    return emit8(b, 0xF2) && rex(b, 0, dst, 0, src) && emit8(b, 0x0F) && emit8(b, 0x58) &&
           emit8(b, (uint8_t)(0xC0u | ((dst & 7u) << 3u) | (src & 7u)));
}

static int cmp_rr(CodeBuffer *b, unsigned left, unsigned right) {
    return rex(b, 1, right, 0, left) && emit8(b, 0x39) &&
           emit8(b, (uint8_t)(0xC0u | ((right & 7u) << 3u) | (left & 7u)));
}

static TurboJSOSRExitKind run_native_loop(TurboJSOSRFrame *frame, void *opaque,
                                           uint32_t *resume_bytecode_offset) {
    TurboJSOSRLoopProgram *program = (TurboJSOSRLoopProgram *)opaque;
    const TurboJSOSRCountedLoopSpec *s;
    TurboJSOSRValue *locals;
    int64_t induction, limit;
    uint64_t iterations;
    TurboJSRawOSRLoopKernel kernel;

    if (!program || !frame || !resume_bytecode_offset || !program->code)
        return TURBOJS_OSR_EXIT_ERROR;
    s = &program->spec;
    if (s->induction_local >= frame->local_count || s->limit_local >= frame->local_count ||
        s->accumulator_local >= frame->local_count)
        return TURBOJS_OSR_EXIT_BAILOUT;
    locals = frame->locals;
    if (locals[s->induction_local].kind != TURBOJS_OSR_VALUE_INT64 ||
        locals[s->limit_local].kind != TURBOJS_OSR_VALUE_INT64 ||
        (locals[s->accumulator_local].kind != TURBOJS_OSR_VALUE_INT64 &&
         locals[s->accumulator_local].kind != TURBOJS_OSR_VALUE_FLOAT64) || s->step == 0 ||
        s->comparison < TURBOJS_OSR_LOOP_LT || s->comparison > TURBOJS_OSR_LOOP_GTE)
        return TURBOJS_OSR_EXIT_BAILOUT;
    induction = (int64_t)locals[s->induction_local].bits;
    limit = (int64_t)locals[s->limit_local].bits;
    if (s->step > 0) {
        uint64_t distance, step = (uint32_t)s->step;
        if (s->comparison == TURBOJS_OSR_LOOP_LT) {
            if (induction >= limit) iterations = 0;
            else { distance = (uint64_t)(limit - induction); iterations = (distance + step - 1u) / step; }
        } else if (s->comparison == TURBOJS_OSR_LOOP_LTE) {
            if (induction > limit) iterations = 0;
            else { distance = (uint64_t)(limit - induction); iterations = distance / step + 1u; }
        } else return TURBOJS_OSR_EXIT_BAILOUT;
    } else {
        uint64_t distance, step = (uint32_t)(-(int64_t)s->step);
        if (s->comparison == TURBOJS_OSR_LOOP_GT) {
            if (induction <= limit) iterations = 0;
            else { distance = (uint64_t)(induction - limit); iterations = (distance + step - 1u) / step; }
        } else if (s->comparison == TURBOJS_OSR_LOOP_GTE) {
            if (induction < limit) iterations = 0;
            else { distance = (uint64_t)(induction - limit); iterations = distance / step + 1u; }
        } else return TURBOJS_OSR_EXIT_BAILOUT;
    }
    if (iterations > s->maximum_iterations)
        return TURBOJS_OSR_EXIT_BAILOUT;
    if (locals[s->accumulator_local].kind == TURBOJS_OSR_VALUE_FLOAT64) {
        if (!program->float_code) return TURBOJS_OSR_EXIT_BAILOUT;
        kernel = (TurboJSRawOSRLoopKernel)program->float_code;
    } else {
        kernel = (TurboJSRawOSRLoopKernel)program->code;
    }
    if (kernel(frame) != 0)
        return TURBOJS_OSR_EXIT_ERROR;
    *resume_bytecode_offset = s->resume_bytecode_offset;
    return TURBOJS_OSR_EXIT_COMPLETED;
}

TurboJSIRStatus TurboJS_OSRCompileCountedI64Loop(const TurboJSOSRCountedLoopSpec *spec,
                                                  TurboJSOSRLoopProgram **out_program,
                                                  TurboJSIRDiagnostic *diagnostic) {
#if !defined(__x86_64__) && !defined(_M_X64)
    (void)spec; (void)out_program;
    if (diagnostic) { diagnostic->status = TURBOJS_IR_UNSUPPORTED; diagnostic->instruction_index = 0; diagnostic->message = "x64 OSR loop compiler unavailable"; }
    return TURBOJS_IR_UNSUPPORTED;
#else
    CodeBuffer b = {{0}, 0};
    TurboJSOSRLoopProgram *program;
    size_t loop_offset, done_patch, jump_patch, after;
    uint32_t i_disp, limit_disp, acc_disp;
    const uint32_t value_size = (uint32_t)sizeof(TurboJSOSRValue);

    if (!spec || !out_program || spec->step == 0 || spec->maximum_iterations == 0 ||
        spec->comparison < TURBOJS_OSR_LOOP_LT || spec->comparison > TURBOJS_OSR_LOOP_GTE) {
        if (diagnostic) { diagnostic->status = TURBOJS_IR_INVALID_ARGUMENT; diagnostic->instruction_index = 0; diagnostic->message = "invalid counted-loop specification"; }
        return TURBOJS_IR_INVALID_ARGUMENT;
    }
    *out_program = NULL;
    i_disp = (uint32_t)spec->induction_local * value_size;
    limit_disp = (uint32_t)spec->limit_local * value_size;
    acc_disp = (uint32_t)spec->accumulator_local * value_size;

#if defined(_WIN32)
    /* rcx = frame */
    if (!mov_rr(&b, 8, 1)) goto oom;            /* r8 = frame */
#else
    /* rdi = frame */
    if (!mov_rr(&b, 8, 7)) goto oom;            /* r8 = frame */
#endif
    if (!load_base_disp32(&b, 9, 8, (uint32_t)offsetof(TurboJSOSRFrame, locals)) ||
        !load_base_disp32(&b, 0, 9, i_disp) ||
        !load_base_disp32(&b, 1, 9, limit_disp) ||
        !load_base_disp32(&b, 2, 9, acc_disp)) goto oom;

    loop_offset = b.count;
    if (!cmp_rr(&b, 0, 1) || !emit8(&b, 0x0F)) goto oom;
    switch (spec->comparison) {
    case TURBOJS_OSR_LOOP_LT:  if (!emit8(&b, 0x8D)) goto oom; break; /* jge */
    case TURBOJS_OSR_LOOP_LTE: if (!emit8(&b, 0x8F)) goto oom; break; /* jg  */
    case TURBOJS_OSR_LOOP_GT:  if (!emit8(&b, 0x8E)) goto oom; break; /* jle */
    case TURBOJS_OSR_LOOP_GTE: if (!emit8(&b, 0x8C)) goto oom; break; /* jl  */
    default: goto oom;
    }
    done_patch = b.count; if (!emit32(&b, 0)) goto oom;
    if (!add_rr(&b, 2, 0) || !add_imm32(&b, 0, spec->step) || !emit8(&b, 0xE9)) goto oom;
    jump_patch = b.count; if (!emit32(&b, 0)) goto oom;
    after = jump_patch + 4u;
    patch32(&b, jump_patch, (int32_t)((intptr_t)loop_offset - (intptr_t)after));
    after = done_patch + 4u;
    patch32(&b, done_patch, (int32_t)((intptr_t)b.count - (intptr_t)after));
    if (!store_base_disp32(&b, 9, 0, i_disp) || !store_base_disp32(&b, 9, 2, acc_disp) ||
        !emit8(&b, 0x31) || !emit8(&b, 0xC0) || !emit8(&b, 0xC3)) goto oom;

    program = (TurboJSOSRLoopProgram *)calloc(1, sizeof(*program));
    if (!program) goto oom;
    program->allocation_size = (b.count + 4095u) & ~(size_t)4095u;
    if (!program->allocation_size) program->allocation_size = 4096u;
    program->code = turbojs_executable_memory_allocate(program->allocation_size);
    if (!program->code) { free(program); goto oom; }
    memcpy(program->code, b.bytes, b.count);
    if (!turbojs_executable_memory_seal(program->code, program->allocation_size)) {
        turbojs_executable_memory_free(program->code, program->allocation_size);
        free(program);
        goto oom;
    }
    program->spec = *spec;
    program->code_size = b.count;

    {
        CodeBuffer f = {{0}, 0};
        size_t f_loop, f_done, f_jump, f_after;
#if defined(_WIN32)
        if (!mov_rr(&f, 8, 1)) goto oom_program;
#else
        if (!mov_rr(&f, 8, 7)) goto oom_program;
#endif
        if (!load_base_disp32(&f, 9, 8, (uint32_t)offsetof(TurboJSOSRFrame, locals)) ||
            !load_base_disp32(&f, 0, 9, i_disp) ||
            !load_base_disp32(&f, 1, 9, limit_disp) ||
            !movq_xmm_mem(&f, 0, 9, acc_disp)) goto oom_program;
        f_loop = f.count;
        if (!cmp_rr(&f, 0, 1) || !emit8(&f, 0x0F)) goto oom_program;
        switch (spec->comparison) {
        case TURBOJS_OSR_LOOP_LT:  if (!emit8(&f, 0x8D)) goto oom_program; break;
        case TURBOJS_OSR_LOOP_LTE: if (!emit8(&f, 0x8F)) goto oom_program; break;
        case TURBOJS_OSR_LOOP_GT:  if (!emit8(&f, 0x8E)) goto oom_program; break;
        case TURBOJS_OSR_LOOP_GTE: if (!emit8(&f, 0x8C)) goto oom_program; break;
        default: goto oom_program;
        }
        f_done = f.count; if (!emit32(&f, 0)) goto oom_program;
        if (!cvtsi2sd_xmm_reg(&f, 1, 0) || !addsd_xmm_xmm(&f, 0, 1) ||
            !add_imm32(&f, 0, spec->step) || !emit8(&f, 0xE9)) goto oom_program;
        f_jump = f.count; if (!emit32(&f, 0)) goto oom_program;
        f_after = f_jump + 4u;
        patch32(&f, f_jump, (int32_t)((intptr_t)f_loop - (intptr_t)f_after));
        f_after = f_done + 4u;
        patch32(&f, f_done, (int32_t)((intptr_t)f.count - (intptr_t)f_after));
        if (!store_base_disp32(&f, 9, 0, i_disp) || !movq_mem_xmm(&f, 9, 0, acc_disp) ||
            !emit8(&f, 0x31) || !emit8(&f, 0xC0) || !emit8(&f, 0xC3)) goto oom_program;
        program->float_allocation_size = (f.count + 4095u) & ~(size_t)4095u;
        if (!program->float_allocation_size) program->float_allocation_size = 4096u;
        program->float_code = turbojs_executable_memory_allocate(program->float_allocation_size);
        if (!program->float_code) goto oom_program;
        memcpy(program->float_code, f.bytes, f.count);
        if (!turbojs_executable_memory_seal(program->float_code, program->float_allocation_size))
            goto oom_program;
        program->float_code_size = f.count;
    }
    *out_program = program;
    if (diagnostic) { diagnostic->status = TURBOJS_IR_OK; diagnostic->instruction_index = 0; diagnostic->message = NULL; }
    return TURBOJS_IR_OK;
oom_program:
    if (program) {
        turbojs_executable_memory_free(program->float_code, program->float_allocation_size);
        turbojs_executable_memory_free(program->code, program->allocation_size);
        free(program);
    }
oom:
    if (diagnostic) { diagnostic->status = TURBOJS_IR_OUT_OF_MEMORY; diagnostic->instruction_index = 0; diagnostic->message = "unable to emit native counted-loop OSR code"; }
    return TURBOJS_IR_OUT_OF_MEMORY;
#endif
}

void TurboJS_OSRLoopProgramDestroy(TurboJSOSRLoopProgram *program) {
    if (!program) return;
    turbojs_executable_memory_free(program->float_code, program->float_allocation_size);
    turbojs_executable_memory_free(program->code, program->allocation_size);
    free(program);
}

TurboJSOSREntry TurboJS_OSRLoopProgramEntry(TurboJSOSRLoopProgram *program) {
    TurboJSOSREntry entry;
    memset(&entry, 0, sizeof(entry));
    if (program) {
        entry.callback = run_native_loop;
        entry.opaque = program;
        entry.loop_header = program->spec.loop_header;
        entry.bailout_limit = 3;
    }
    return entry;
}

size_t TurboJS_OSRLoopProgramCodeSize(const TurboJSOSRLoopProgram *program) {
    return program ? program->code_size + program->float_code_size : 0;
}
