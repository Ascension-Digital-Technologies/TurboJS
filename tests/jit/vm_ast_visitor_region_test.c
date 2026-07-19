#include <stdio.h>
#include "turbojs.h"

static int report_exception(JSContext *ctx, const char *where) {
    JSValue ex = JS_GetException(ctx);
    const char *text = JS_ToCString(ctx, ex);
    fprintf(stderr, "%s%s%s\n", where, text ? ": " : "", text ? text : "");
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, ex);
    return 1;
}

int main(void) {
    static const char source[] =
        "const atomNoise={alpha:1,beta:2,gamma:3};"
        "function buildTree(depth,seed){if(depth===0)return{type:'Literal',value:seed};return{type:(depth&1)?'Add':'Mul',left:buildTree(depth-1,seed+1),right:buildTree(depth-1,seed+3)};}"
        "function buildShared(depth,seed){let node={type:'Literal',value:seed};for(let i=0;i<depth;i++)node={type:'Add',left:node,right:node};return node;}"
        "function visitTree(root){const visitors={Literal(n){return n.value|0;},Add(n){return(walk(n.left)+walk(n.right))|0;},Mul(n){return Math.imul(walk(n.left),walk(n.right))|0;}};function walk(node){return visitors[node.type](node);}let s=0;for(let i=0;i<80;i++)s=(s+walk(root))|0;return s|0;}"
        "const normal=visitTree(buildTree(7,3));const shared=visitTree(buildShared(13,3));let hits=0;const accessor={type:'Literal',get value(){hits++;return 3;}};const fallback=visitTree(accessor);globalThis.__ast_region=[normal,shared,fallback,hits];";
    const int32_t expected[] = {-1549271040, 1966080, 240, 80};
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value, global, item;
    int i;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    value = JS_Eval(ctx, source, sizeof(source) - 1, "ast-visitor-region.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value)) { report_exception(ctx, "eval"); goto fail; }
    JS_FreeValue(ctx, value);
    global = JS_GetGlobalObject(ctx);
    value = JS_GetPropertyStr(ctx, global, "__ast_region");
    JS_FreeValue(ctx, global);
    if (JS_IsException(value)) { report_exception(ctx, "result"); goto fail; }
    for (i = 0; i < 4; ++i) {
        int32_t actual = 0;
        item = JS_GetPropertyUint32(ctx, value, (uint32_t)i);
        if (JS_ToInt32(ctx, &actual, item) || actual != expected[i]) {
            fprintf(stderr, "ast visitor mismatch at %d: got %d expected %d\n", i, actual, expected[i]);
            JS_FreeValue(ctx, item); JS_FreeValue(ctx, value); goto fail;
        }
        JS_FreeValue(ctx, item);
    }
    JS_FreeValue(ctx, value);
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    puts("Plain AST and shared DAG visitor regions passed");
    return 0;
fail:
    JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
}
