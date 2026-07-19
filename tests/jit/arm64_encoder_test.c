#include "src/jit/backend/arm64/arm64_encoder.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
int main(void)
{
    TurboJSArm64Buffer b; size_t branch_at;
    TurboJS_Arm64BufferInit(&b);
    CHECK(TurboJS_Arm64EmitMovZ(&b, 0, 42, 0));
    CHECK(TurboJS_Arm64EmitMovK(&b, 0, 1, 16));
    CHECK(TurboJS_Arm64EmitMovReg(&b, 4, 0));
    CHECK(TurboJS_Arm64EmitAddReg(&b, 2, 0, 1));
    CHECK(TurboJS_Arm64EmitSubReg(&b, 3, 2, 1));
    CHECK(TurboJS_Arm64EmitMulReg(&b, 5, 2, 3));
    CHECK(TurboJS_Arm64EmitCmpReg(&b, 2, 3));
    CHECK(TurboJS_Arm64EmitCset(&b, 6, TURBOJS_ARM64_LT));
    CHECK(TurboJS_Arm64EmitStore64(&b, 6, 31, 16));
    CHECK(TurboJS_Arm64EmitLoad64(&b, 7, 31, 16));
    branch_at=b.count; CHECK(TurboJS_Arm64EmitBranchCond(&b,0,TURBOJS_ARM64_NE));
    CHECK(TurboJS_Arm64EmitAddImm(&b,8,8,1));
    CHECK(TurboJS_Arm64PatchBranch(&b,branch_at,b.count,1));
    CHECK(TurboJS_Arm64EmitBranch(&b, -4));
    CHECK(TurboJS_Arm64EmitRet(&b));
    CHECK(b.words[0] == 0xD2800540u);
    CHECK((b.words[branch_at]&0xFF00001Fu)==0x54000001u);
    CHECK(!TurboJS_Arm64EmitMovZ(&b, 32, 0, 0));
    CHECK(!TurboJS_Arm64EmitBranch(&b, 2));
    CHECK(!TurboJS_Arm64EmitLoad64(&b,0,0,3));
    TurboJS_Arm64BufferDestroy(&b);
    puts("ARM64 production encoder surface passed");
    return 0;
}
