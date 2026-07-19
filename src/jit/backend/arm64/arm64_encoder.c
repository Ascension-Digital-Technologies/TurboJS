#include "src/jit/backend/arm64/arm64_encoder.h"
#include <stdlib.h>

static int valid_reg(unsigned reg) { return reg < 32u; }

int TurboJS_Arm64EmitWord(TurboJSArm64Buffer *buffer, uint32_t word)
{
    uint32_t *next; size_t capacity;
    if (!buffer) return 0;
    if (buffer->count == buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity * 2u : 32u;
        if (capacity < buffer->capacity || capacity > SIZE_MAX / sizeof(*next)) return 0;
        next = (uint32_t *)realloc(buffer->words, capacity * sizeof(*next));
        if (!next) return 0;
        buffer->words = next; buffer->capacity = capacity;
    }
    buffer->words[buffer->count++] = word; return 1;
}

void TurboJS_Arm64BufferInit(TurboJSArm64Buffer *buffer)
{ if (buffer) { buffer->words = NULL; buffer->count = 0; buffer->capacity = 0; } }
void TurboJS_Arm64BufferDestroy(TurboJSArm64Buffer *buffer)
{ if (!buffer) return; free(buffer->words); TurboJS_Arm64BufferInit(buffer); }

int TurboJS_Arm64EmitMovZ(TurboJSArm64Buffer *b,unsigned rd,uint16_t imm,unsigned shift)
{ unsigned hw;if(!valid_reg(rd)||shift>48u||(shift&15u))return 0;hw=shift/16u;return TurboJS_Arm64EmitWord(b,0xD2800000u|(hw<<21)|((uint32_t)imm<<5)|rd); }
int TurboJS_Arm64EmitMovK(TurboJSArm64Buffer *b,unsigned rd,uint16_t imm,unsigned shift)
{ unsigned hw;if(!valid_reg(rd)||shift>48u||(shift&15u))return 0;hw=shift/16u;return TurboJS_Arm64EmitWord(b,0xF2800000u|(hw<<21)|((uint32_t)imm<<5)|rd); }
int TurboJS_Arm64EmitMovReg(TurboJSArm64Buffer*b,unsigned rd,unsigned rn)
{ if(!valid_reg(rd)||!valid_reg(rn))return 0;return TurboJS_Arm64EmitWord(b,0xAA0003E0u|(rn<<16)|rd); }
int TurboJS_Arm64EmitAddReg(TurboJSArm64Buffer*b,unsigned rd,unsigned rn,unsigned rm)
{if(!valid_reg(rd)||!valid_reg(rn)||!valid_reg(rm))return 0;return TurboJS_Arm64EmitWord(b,0x8B000000u|(rm<<16)|(rn<<5)|rd);}
int TurboJS_Arm64EmitSubReg(TurboJSArm64Buffer*b,unsigned rd,unsigned rn,unsigned rm)
{if(!valid_reg(rd)||!valid_reg(rn)||!valid_reg(rm))return 0;return TurboJS_Arm64EmitWord(b,0xCB000000u|(rm<<16)|(rn<<5)|rd);}
int TurboJS_Arm64EmitAdds32(TurboJSArm64Buffer*b,unsigned rd,unsigned rn,unsigned rm)
{if(!valid_reg(rd)||!valid_reg(rn)||!valid_reg(rm))return 0;return TurboJS_Arm64EmitWord(b,0x2B000000u|(rm<<16)|(rn<<5)|rd);}
int TurboJS_Arm64EmitSubs32(TurboJSArm64Buffer*b,unsigned rd,unsigned rn,unsigned rm)
{if(!valid_reg(rd)||!valid_reg(rn)||!valid_reg(rm))return 0;return TurboJS_Arm64EmitWord(b,0x6B000000u|(rm<<16)|(rn<<5)|rd);}
int TurboJS_Arm64EmitMulReg(TurboJSArm64Buffer*b,unsigned rd,unsigned rn,unsigned rm)
{if(!valid_reg(rd)||!valid_reg(rn)||!valid_reg(rm))return 0;return TurboJS_Arm64EmitWord(b,0x9B007C00u|(rm<<16)|(rn<<5)|rd);}
int TurboJS_Arm64EmitAddImm(TurboJSArm64Buffer*b,unsigned rd,unsigned rn,uint16_t imm)
{if(!valid_reg(rd)||!valid_reg(rn)||imm>4095u)return 0;return TurboJS_Arm64EmitWord(b,0x91000000u|((uint32_t)imm<<10)|(rn<<5)|rd);}
int TurboJS_Arm64EmitSubImm(TurboJSArm64Buffer*b,unsigned rd,unsigned rn,uint16_t imm)
{if(!valid_reg(rd)||!valid_reg(rn)||imm>4095u)return 0;return TurboJS_Arm64EmitWord(b,0xD1000000u|((uint32_t)imm<<10)|(rn<<5)|rd);}
int TurboJS_Arm64EmitCmpReg(TurboJSArm64Buffer*b,unsigned rn,unsigned rm)
{if(!valid_reg(rn)||!valid_reg(rm))return 0;return TurboJS_Arm64EmitWord(b,0xEB00001Fu|(rm<<16)|(rn<<5));}
int TurboJS_Arm64EmitCset(TurboJSArm64Buffer*b,unsigned rd,TurboJSArm64Condition c)
{if(!valid_reg(rd)||(unsigned)c>13u)return 0;return TurboJS_Arm64EmitWord(b,0x9A9F07E0u|(((uint32_t)c^1u)<<12)|rd);}
int TurboJS_Arm64EmitLoad64(TurboJSArm64Buffer*b,unsigned rt,unsigned rn,uint16_t off)
{if(!valid_reg(rt)||!valid_reg(rn)||(off&7u)||off>32760u)return 0;return TurboJS_Arm64EmitWord(b,0xF9400000u|(((uint32_t)off/8u)<<10)|(rn<<5)|rt);}
int TurboJS_Arm64EmitStore64(TurboJSArm64Buffer*b,unsigned rt,unsigned rn,uint16_t off)
{if(!valid_reg(rt)||!valid_reg(rn)||(off&7u)||off>32760u)return 0;return TurboJS_Arm64EmitWord(b,0xF9000000u|(((uint32_t)off/8u)<<10)|(rn<<5)|rt);}
int TurboJS_Arm64EmitBranch(TurboJSArm64Buffer*b,int32_t off)
{int32_t w;if(off&3)return 0;w=off/4;if(w<-(1<<25)||w>=(1<<25))return 0;return TurboJS_Arm64EmitWord(b,0x14000000u|((uint32_t)w&0x03FFFFFFu));}
int TurboJS_Arm64EmitBranchCond(TurboJSArm64Buffer*b,int32_t off,TurboJSArm64Condition c)
{int32_t w;if((off&3)||(unsigned)c>13u)return 0;w=off/4;if(w<-(1<<18)||w>=(1<<18))return 0;return TurboJS_Arm64EmitWord(b,0x54000000u|(((uint32_t)w&0x7FFFFu)<<5)|(uint32_t)c);}
int TurboJS_Arm64PatchBranch(TurboJSArm64Buffer*b,size_t at,size_t target,int conditional)
{int64_t delta;uint32_t old;if(!b||at>=b->count||target>b->count)return 0;delta=(int64_t)target-(int64_t)at;if(conditional){if(delta<-(1<<18)||delta>=(1<<18))return 0;old=b->words[at]&0xFF00001Fu;b->words[at]=old|(((uint32_t)delta&0x7FFFFu)<<5);}else{if(delta<-(1<<25)||delta>=(1<<25))return 0;old=b->words[at]&0xFC000000u;b->words[at]=old|((uint32_t)delta&0x03FFFFFFu);}return 1;}

int TurboJS_Arm64EmitFmovDFromX(TurboJSArm64Buffer*b,unsigned dd,unsigned xn)
{if(!valid_reg(dd)||!valid_reg(xn))return 0;return TurboJS_Arm64EmitWord(b,0x9E670000u|(xn<<5)|dd);}
int TurboJS_Arm64EmitFmovXFromD(TurboJSArm64Buffer*b,unsigned xd,unsigned dn)
{if(!valid_reg(xd)||!valid_reg(dn))return 0;return TurboJS_Arm64EmitWord(b,0x9E660000u|(dn<<5)|xd);}
static int fp3(TurboJSArm64Buffer*b,uint32_t base,unsigned dd,unsigned dn,unsigned dm)
{if(!valid_reg(dd)||!valid_reg(dn)||!valid_reg(dm))return 0;return TurboJS_Arm64EmitWord(b,base|(dm<<16)|(dn<<5)|dd);}
int TurboJS_Arm64EmitFaddD(TurboJSArm64Buffer*b,unsigned dd,unsigned dn,unsigned dm){return fp3(b,0x1E602800u,dd,dn,dm);}
int TurboJS_Arm64EmitFsubD(TurboJSArm64Buffer*b,unsigned dd,unsigned dn,unsigned dm){return fp3(b,0x1E603800u,dd,dn,dm);}
int TurboJS_Arm64EmitFmulD(TurboJSArm64Buffer*b,unsigned dd,unsigned dn,unsigned dm){return fp3(b,0x1E600800u,dd,dn,dm);}
int TurboJS_Arm64EmitFdivD(TurboJSArm64Buffer*b,unsigned dd,unsigned dn,unsigned dm){return fp3(b,0x1E601800u,dd,dn,dm);}
int TurboJS_Arm64EmitFcmpD(TurboJSArm64Buffer*b,unsigned dn,unsigned dm)
{if(!valid_reg(dn)||!valid_reg(dm))return 0;return TurboJS_Arm64EmitWord(b,0x1E602000u|(dm<<16)|(dn<<5));}
int TurboJS_Arm64EmitScvtfDFromX(TurboJSArm64Buffer*b,unsigned dd,unsigned xn)
{if(!valid_reg(dd)||!valid_reg(xn))return 0;return TurboJS_Arm64EmitWord(b,0x9E620000u|(xn<<5)|dd);}
int TurboJS_Arm64EmitFcvtzsXFromD(TurboJSArm64Buffer*b,unsigned xd,unsigned dn)
{if(!valid_reg(xd)||!valid_reg(dn))return 0;return TurboJS_Arm64EmitWord(b,0x9E780000u|(dn<<5)|xd);}
int TurboJS_Arm64EmitBlr(TurboJSArm64Buffer*b,unsigned rn)
{if(!valid_reg(rn))return 0;return TurboJS_Arm64EmitWord(b,0xD63F0000u|(rn<<5));}
int TurboJS_Arm64EmitSxtw(TurboJSArm64Buffer*b,unsigned rd,unsigned rn)
{if(!valid_reg(rd)||!valid_reg(rn))return 0;return TurboJS_Arm64EmitWord(b,0x93407C00u|(rn<<5)|rd);}
int TurboJS_Arm64EmitRet(TurboJSArm64Buffer*b){return TurboJS_Arm64EmitWord(b,0xD65F03C0u);}
int TurboJS_Arm64EmitMovImm64(TurboJSArm64Buffer*b,unsigned rd,uint64_t v)
{unsigned s;if(!TurboJS_Arm64EmitMovZ(b,rd,(uint16_t)v,0))return 0;for(s=16;s<=48;s+=16)if(!TurboJS_Arm64EmitMovK(b,rd,(uint16_t)(v>>s),s))return 0;return 1;}
