#ifndef TURBOJS_JIT_ARM64_ENCODER_H
#define TURBOJS_JIT_ARM64_ENCODER_H

#include <stddef.h>
#include <stdint.h>

typedef struct TurboJSArm64Buffer {
    uint32_t *words;
    size_t count;
    size_t capacity;
} TurboJSArm64Buffer;

typedef enum TurboJSArm64Condition {
    TURBOJS_ARM64_EQ = 0,
    TURBOJS_ARM64_NE = 1,
    TURBOJS_ARM64_HS = 2,
    TURBOJS_ARM64_LO = 3,
    TURBOJS_ARM64_MI = 4,
    TURBOJS_ARM64_PL = 5,
    TURBOJS_ARM64_VS = 6,
    TURBOJS_ARM64_VC = 7,
    TURBOJS_ARM64_HI = 8,
    TURBOJS_ARM64_LS = 9,
    TURBOJS_ARM64_GE = 10,
    TURBOJS_ARM64_LT = 11,
    TURBOJS_ARM64_GT = 12,
    TURBOJS_ARM64_LE = 13
} TurboJSArm64Condition;

void TurboJS_Arm64BufferInit(TurboJSArm64Buffer *buffer);
void TurboJS_Arm64BufferDestroy(TurboJSArm64Buffer *buffer);
int TurboJS_Arm64EmitWord(TurboJSArm64Buffer *buffer, uint32_t word);
int TurboJS_Arm64PatchBranch(TurboJSArm64Buffer *buffer, size_t word_index,
                             size_t target_word, int conditional);
int TurboJS_Arm64EmitMovZ(TurboJSArm64Buffer *buffer, unsigned rd,
                          uint16_t immediate, unsigned shift);
int TurboJS_Arm64EmitMovK(TurboJSArm64Buffer *buffer, unsigned rd,
                          uint16_t immediate, unsigned shift);
int TurboJS_Arm64EmitMovReg(TurboJSArm64Buffer *buffer, unsigned rd, unsigned rn);
int TurboJS_Arm64EmitAddReg(TurboJSArm64Buffer *buffer, unsigned rd,
                            unsigned rn, unsigned rm);
int TurboJS_Arm64EmitSubReg(TurboJSArm64Buffer *buffer, unsigned rd,
                            unsigned rn, unsigned rm);
int TurboJS_Arm64EmitAdds32(TurboJSArm64Buffer *buffer, unsigned rd,
                            unsigned rn, unsigned rm);
int TurboJS_Arm64EmitSubs32(TurboJSArm64Buffer *buffer, unsigned rd,
                            unsigned rn, unsigned rm);
int TurboJS_Arm64EmitMulReg(TurboJSArm64Buffer *buffer, unsigned rd,
                            unsigned rn, unsigned rm);
int TurboJS_Arm64EmitAddImm(TurboJSArm64Buffer *buffer, unsigned rd,
                            unsigned rn, uint16_t immediate);
int TurboJS_Arm64EmitSubImm(TurboJSArm64Buffer *buffer, unsigned rd,
                            unsigned rn, uint16_t immediate);
int TurboJS_Arm64EmitCmpReg(TurboJSArm64Buffer *buffer, unsigned rn, unsigned rm);
int TurboJS_Arm64EmitCset(TurboJSArm64Buffer *buffer, unsigned rd,
                          TurboJSArm64Condition condition);
int TurboJS_Arm64EmitLoad64(TurboJSArm64Buffer *buffer, unsigned rt,
                            unsigned rn, uint16_t byte_offset);
int TurboJS_Arm64EmitStore64(TurboJSArm64Buffer *buffer, unsigned rt,
                             unsigned rn, uint16_t byte_offset);
int TurboJS_Arm64EmitBranch(TurboJSArm64Buffer *buffer, int32_t byte_offset);
int TurboJS_Arm64EmitBranchCond(TurboJSArm64Buffer *buffer, int32_t byte_offset,
                                TurboJSArm64Condition condition);
int TurboJS_Arm64EmitFmovDFromX(TurboJSArm64Buffer *buffer, unsigned dd, unsigned xn);
int TurboJS_Arm64EmitFmovXFromD(TurboJSArm64Buffer *buffer, unsigned xd, unsigned dn);
int TurboJS_Arm64EmitFaddD(TurboJSArm64Buffer *buffer, unsigned dd, unsigned dn, unsigned dm);
int TurboJS_Arm64EmitFsubD(TurboJSArm64Buffer *buffer, unsigned dd, unsigned dn, unsigned dm);
int TurboJS_Arm64EmitFmulD(TurboJSArm64Buffer *buffer, unsigned dd, unsigned dn, unsigned dm);
int TurboJS_Arm64EmitFdivD(TurboJSArm64Buffer *buffer, unsigned dd, unsigned dn, unsigned dm);
int TurboJS_Arm64EmitFcmpD(TurboJSArm64Buffer *buffer, unsigned dn, unsigned dm);
int TurboJS_Arm64EmitScvtfDFromX(TurboJSArm64Buffer *buffer, unsigned dd, unsigned xn);
int TurboJS_Arm64EmitFcvtzsXFromD(TurboJSArm64Buffer *buffer, unsigned xd, unsigned dn);
int TurboJS_Arm64EmitBlr(TurboJSArm64Buffer *buffer, unsigned rn);
int TurboJS_Arm64EmitSxtw(TurboJSArm64Buffer *buffer, unsigned rd, unsigned rn);
int TurboJS_Arm64EmitRet(TurboJSArm64Buffer *buffer);
int TurboJS_Arm64EmitMovImm64(TurboJSArm64Buffer *buffer, unsigned rd, uint64_t value);

#endif
