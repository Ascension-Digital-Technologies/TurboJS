#include "jit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed:%d: %s\n",__LINE__,#x); return 1; } } while (0)

int main(void) {
    TurboJSSSAGraph g; TurboJSSSAOptimizationStats st;
    memset(&g,0,sizeof(g));
    g.entry_block=0; g.block_count=g.block_capacity=4;
    g.blocks=calloc(4,sizeof(*g.blocks)); CHECK(g.blocks);
    g.blocks[0]=(TurboJSSSABlock){.id=0,.first_value=0,.value_count=3,.successors={1},.successor_count=1,.reachable=1};
    g.blocks[1]=(TurboJSSSABlock){.id=1,.first_value=3,.value_count=4,.predecessors={0,2},.predecessor_count=2,.successors={2,3},.successor_count=2,.loop_depth=1,.loop_header=1,.reachable=1};
    g.blocks[2]=(TurboJSSSABlock){.id=2,.first_value=7,.value_count=4,.predecessors={1},.predecessor_count=1,.successors={1},.successor_count=1,.loop_depth=1,.loop_header=1,.reachable=1};
    g.blocks[3]=(TurboJSSSABlock){.id=3,.first_value=11,.value_count=1,.predecessors={1},.predecessor_count=1,.reachable=1};
    g.value_count=g.value_capacity=12; g.values=calloc(12,sizeof(*g.values)); CHECK(g.values);
    g.values[0]=(TurboJSSSAValue){.id=0,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_REFERENCE,.immediate=0};
    g.values[1]=(TurboJSSSAValue){.id=1,.block=0,.opcode=TURBOJS_SSA_ARGUMENT,.type=TURBOJS_SSA_TYPE_INT32,.immediate=1};
    g.values[2]=(TurboJSSSAValue){.id=2,.block=0,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=0};
    g.values[3]=(TurboJSSSAValue){.id=3,.block=1,.opcode=TURBOJS_SSA_PHI,.type=TURBOJS_SSA_TYPE_INT32,.left=2,.right=10};
    g.values[4]=(TurboJSSSAValue){.id=4,.block=1,.opcode=TURBOJS_SSA_LESS_THAN_I64,.type=TURBOJS_SSA_TYPE_BOOLEAN,.left=3,.right=1};
    g.values[5]=(TurboJSSSAValue){.id=5,.block=1,.opcode=TURBOJS_SSA_BRANCH_FALSE,.type=TURBOJS_SSA_TYPE_BOOLEAN,.left=4,.metadata=3};
    g.values[6]=(TurboJSSSAValue){.id=6,.block=1,.opcode=TURBOJS_SSA_NOP};
    g.values[7]=(TurboJSSSAValue){.id=7,.block=2,.opcode=TURBOJS_SSA_ELEMENT_LOAD,.type=TURBOJS_SSA_TYPE_REFERENCE,.left=0,.right=3,.element_kind=TURBOJS_ELEMENT_KIND_PACKED_I64,.element_generation=7,.element_length_value=1};
    g.values[8]=(TurboJSSSAValue){.id=8,.block=2,.opcode=TURBOJS_SSA_CONSTANT_I64,.type=TURBOJS_SSA_TYPE_INT32,.immediate=1};
    g.values[9]=(TurboJSSSAValue){.id=9,.block=2,.opcode=TURBOJS_SSA_NOP};
    g.values[10]=(TurboJSSSAValue){.id=10,.block=2,.opcode=TURBOJS_SSA_ADD_I64,.type=TURBOJS_SSA_TYPE_INT32,.left=3,.right=8};
    g.values[11]=(TurboJSSSAValue){.id=11,.block=3,.opcode=TURBOJS_SSA_RETURN,.type=TURBOJS_SSA_TYPE_INT32,.left=3};
    st=TurboJS_SSAOptimize(&g);
    CHECK(st.element_canonical_inductions==1);
    CHECK(st.element_length_hoists==1);
    CHECK(st.element_base_pointer_hoists==1);
    CHECK(st.element_loop_bounds_checks_eliminated==1);
    CHECK(g.values[7].element_bounds_proven==1);
    CHECK(g.values[7].element_length_hoisted==1);
    CHECK(g.values[7].element_base_hoisted==1);
    CHECK(g.values[7].element_induction_step==1);
    CHECK(g.values[7].element_range_min==0);
    free(g.values); free(g.blocks);
    puts("element loop range proof test passed");
    return 0;
}
