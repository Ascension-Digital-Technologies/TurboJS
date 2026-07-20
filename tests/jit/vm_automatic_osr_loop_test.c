#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "turbojs.h"
#include "optimization.h"

static int run_source(const char *source, int64_t expected) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value;
    int64_t result = 0;
    TurboJSRuntimeJITStats stats;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    TurboJS_SetRuntimeJITThreshold(rt, 100);
    value = JS_Eval(ctx, source, strlen(source), "automatic-osr.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value) || JS_ToInt64(ctx, &result, value) || result != expected) {
        fprintf(stderr, "automatic OSR result mismatch: got=%lld expected=%lld\n",
                (long long)result, (long long)expected);
        JS_FreeValue(ctx, value); JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeValue(ctx, value);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.osr_compile_requests < 1 || stats.osr_frame_captures < 1 ||
        stats.osr_entries < 1 || stats.osr_bailouts != 0) {
        fprintf(stderr, "automatic native OSR did not activate: req=%llu cap=%llu entry=%llu bailout=%llu\n",
                (unsigned long long)stats.osr_compile_requests,
                (unsigned long long)stats.osr_frame_captures,
                (unsigned long long)stats.osr_entries,
                (unsigned long long)stats.osr_bailouts);
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    return 0;
}

static int run_source_f64(const char *source, double expected) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value;
    double result = 0.0;
    TurboJSRuntimeJITStats stats;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    TurboJS_SetRuntimeJITThreshold(rt, 100);
    value = JS_Eval(ctx, source, strlen(source), "automatic-osr-f64.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value) || JS_ToFloat64(ctx, &result, value) || result != expected) {
        fprintf(stderr, "automatic Float64 OSR mismatch: got=%.17g expected=%.17g\n", result, expected);
        JS_FreeValue(ctx, value); JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeValue(ctx, value);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.osr_entries < 1 || stats.osr_bailouts != 0) {
        fprintf(stderr, "automatic Float64 OSR did not activate: entry=%llu bailout=%llu\n",
                (unsigned long long)stats.osr_entries,
                (unsigned long long)stats.osr_bailouts);
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    return 0;
}


static int run_dense_array_source(const char *source, int64_t expected) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value;
    int64_t result = 0;
    TurboJSRuntimeJITStats stats;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    TurboJS_SetRuntimeJITThreshold(rt, 100);
    value = JS_Eval(ctx, source, strlen(source), "automatic-dense-array-osr.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value) || JS_ToInt64(ctx, &result, value) || result != expected) {
        fprintf(stderr, "dense array OSR mismatch: got=%lld expected=%lld\n",
                (long long)result, (long long)expected);
        JS_FreeValue(ctx, value); JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeValue(ctx, value);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.dense_array_osr_entries < 1 || stats.dense_array_osr_elements < 900000 ||
        stats.dense_array_osr_unrolled_blocks < 200000 ||
        stats.dense_array_osr_multi_lane_blocks < 200000 || stats.osr_entries < 1 || stats.osr_bailouts != 0) {
        fprintf(stderr, "dense array OSR inactive: entries=%llu elements=%llu blocks=%llu lanes=%llu osr=%llu bailout=%llu\n",
                (unsigned long long)stats.dense_array_osr_entries,
                (unsigned long long)stats.dense_array_osr_elements,
                (unsigned long long)stats.dense_array_osr_unrolled_blocks,
                (unsigned long long)stats.dense_array_osr_multi_lane_blocks,
                (unsigned long long)stats.osr_entries,
                (unsigned long long)stats.osr_bailouts);
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    return 0;
}


static int run_dense_array_bailout_source(void) {
    const char *source =
        "(function(){let a=[];for(let i=0;i<2000;i++)a[i]=i;"
        "delete a[1000];Array.prototype[1000]=7;"
        "function f(a){let sum=0;for(let i=0;i<a.length;i++){sum+=a[i];}return sum;}"
        "let r=f(a);delete Array.prototype[1000];return r;})()";
    const int64_t expected = 1998007;
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value;
    int64_t result = 0;
    TurboJSRuntimeJITStats stats;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    TurboJS_SetRuntimeJITThreshold(rt, 100);
    value = JS_Eval(ctx, source, strlen(source), "dense-array-osr-bailout.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value) || JS_ToInt64(ctx, &result, value) || result != expected) {
        fprintf(stderr, "dense array bailout semantic mismatch: got=%lld expected=%lld\n",
                (long long)result, (long long)expected);
        JS_FreeValue(ctx, value); JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeValue(ctx, value);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.osr_bailouts < 1) {
        fprintf(stderr, "dense array hole did not trigger transactional bailout\n");
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    return 0;
}


static int run_dense_array_transform_source(void) {
    const char *source =
        "(function(){let a=[],b=[];for(let i=0;i<200000;i++){a[i]=i;b[i]=0;}"
        "function f(input,output,scale,offset){for(let i=0;i<input.length;i++){output[i]=input[i]*scale+offset;}"
        "return output[0]+output[input.length-1];}return f(a,b,2,3);})()";
    const int64_t expected = 400004;
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value;
    int64_t result = 0;
    TurboJSRuntimeJITStats stats;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    TurboJS_SetRuntimeJITThreshold(rt, 100);
    value = JS_Eval(ctx, source, strlen(source), "dense-array-transform-osr.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value) || JS_ToInt64(ctx, &result, value) || result != expected) {
        fprintf(stderr, "dense transform OSR mismatch: got=%lld expected=%lld\n",
                (long long)result, (long long)expected);
        JS_FreeValue(ctx, value); JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeValue(ctx, value);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.dense_array_transform_osr_entries < 1 ||
        stats.dense_array_transform_osr_elements < 190000 || stats.osr_bailouts != 0) {
        fprintf(stderr, "dense transform OSR inactive: entries=%llu elements=%llu bailout=%llu\n",
                (unsigned long long)stats.dense_array_transform_osr_entries,
                (unsigned long long)stats.dense_array_transform_osr_elements,
                (unsigned long long)stats.osr_bailouts);
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    return 0;
}

static int run_dense_array_transform_bailout_source(void) {
    const char *source =
        "(function(){let a=[],b=[];for(let i=0;i<2000;i++){a[i]=i;b[i]=0;}"
        "delete a[1000];Array.prototype[1000]=9;"
        "function f(input,output,scale,offset){for(let i=0;i<input.length;i++){output[i]=input[i]*scale+offset;}"
        "return output[1000];}let r=f(a,b,2,3);delete Array.prototype[1000];return r;})()";
    const int64_t expected = 21;
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value;
    int64_t result = 0;
    TurboJSRuntimeJITStats stats;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    TurboJS_SetRuntimeJITThreshold(rt, 100);
    value = JS_Eval(ctx, source, strlen(source), "dense-array-transform-bailout.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value) || JS_ToInt64(ctx, &result, value) || result != expected) {
        fprintf(stderr, "dense transform bailout mismatch: got=%lld expected=%lld\n",
                (long long)result, (long long)expected);
        JS_FreeValue(ctx, value); JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeValue(ctx, value);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.osr_bailouts < 1) {
        fprintf(stderr, "dense transform hole did not trigger bailout\n");
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt);
    return 0;
}

static int run_dense_array_extended_kernels(void) {
    const char *source =
        "(function(){let a=[],b=[],out=[];for(let i=0;i<200000;i++){a[i]=i+0.5;b[i]=i*2+0.25;out[i]=0;}"
        "function inplace(x,s,o){for(let i=0;i<x.length;i++){x[i]=x[i]*s+o;}}"
        "function add(x,y,z){for(let i=0;i<x.length;i++){z[i]=x[i]+y[i];}}"
        "function copy(x,z){for(let i=0;i<x.length;i++){z[i]=x[i];}}"
        "function fill(z,v){for(let i=0;i<z.length;i++){z[i]=v;}}"
        "inplace(a,2,3);add(a,b,out);let r1=out[199999];copy(b,out);let r2=out[199999];fill(out,7.5);"
        "return r1+r2+out[0]+out[199999];})()";
    const double expected = (399998.0 + 4.0 + 399998.0 + 0.25) + (399998.0 + 0.25) + 15.0;
    JSRuntime *rt=JS_NewRuntime(); JSContext *ctx; JSValue value; double result=0.0; TurboJSRuntimeJITStats stats;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    TurboJS_SetRuntimeJITThreshold(rt,100); value=JS_Eval(ctx,source,strlen(source),"dense-extended-osr.js",JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(value)||JS_ToFloat64(ctx,&result,value)||fabs(result-expected)>1e-9){
        fprintf(stderr,"dense extended mismatch: got=%.17g expected=%.17g\
",result,expected);
        JS_FreeValue(ctx,value);JS_FreeContext(ctx);JS_FreeRuntime(rt);return 1;}
    JS_FreeValue(ctx,value); stats=TurboJS_GetRuntimeJITStats(rt);
    if(stats.dense_array_inplace_osr_entries<1||stats.dense_array_binary_osr_entries<1||
       stats.dense_array_copy_osr_entries<1||stats.dense_array_fill_osr_entries<1||stats.osr_bailouts!=0){
        fprintf(stderr,"dense extended kernels inactive: in=%llu bin=%llu copy=%llu fill=%llu bailout=%llu\
",
          (unsigned long long)stats.dense_array_inplace_osr_entries,(unsigned long long)stats.dense_array_binary_osr_entries,
          (unsigned long long)stats.dense_array_copy_osr_entries,(unsigned long long)stats.dense_array_fill_osr_entries,
          (unsigned long long)stats.osr_bailouts);
        JS_FreeContext(ctx);JS_FreeRuntime(rt);return 1;}
    JS_FreeContext(ctx);JS_FreeRuntime(rt);return 0;
}


static int run_float64_typed_array_kernels(void) {
    const char *source =
        "(function(){let n=200000,a=new Float64Array(n),b=new Float64Array(n),out=new Float64Array(n);"
        "for(let i=0;i<n;i++){a[i]=i+0.5;b[i]=i*2+0.25;}"
        "function add(x,y,z){for(let i=0;i<x.length;i++){z[i]=x[i]+y[i];}}"
        "function affine(x,z,s,o){for(let i=0;i<x.length;i++){z[i]=x[i]*s+o;}}"
        "add(a,b,out);let r1=out[n-1];affine(a,out,2,3);return r1+out[n-1];})()";
    const double expected = (199999.5 + 399998.25) + (199999.5 * 2.0 + 3.0);
    JSRuntime *rt=JS_NewRuntime(); JSContext *ctx; JSValue value; double result=0.0; TurboJSRuntimeJITStats stats;
    if (!rt) return 1;
    ctx=JS_NewContext(rt); if(!ctx){JS_FreeRuntime(rt);return 1;}
    TurboJS_SetRuntimeJITThreshold(rt,100);
    value=JS_Eval(ctx,source,strlen(source),"float64array-osr.js",JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(value)||JS_ToFloat64(ctx,&result,value)||fabs(result-expected)>1e-9){
        fprintf(stderr,"Float64Array OSR mismatch: got=%.17g expected=%.17g\n",result,expected);
        JS_FreeValue(ctx,value);JS_FreeContext(ctx);JS_FreeRuntime(rt);return 1;
    }
    JS_FreeValue(ctx,value);stats=TurboJS_GetRuntimeJITStats(rt);
    if(stats.typed_array_osr_entries<2||stats.typed_array_osr_elements<390000||
       stats.typed_array_simd_elements<390000||
       (stats.typed_array_simd_sse2_entries+stats.typed_array_simd_avx2_entries)<2||stats.osr_bailouts!=0){
        fprintf(stderr,"Float64Array OSR inactive: entries=%llu elements=%llu simd=%llu sse2=%llu avx2=%llu bailouts=%llu\n",
          (unsigned long long)stats.typed_array_osr_entries,
          (unsigned long long)stats.typed_array_osr_elements,
          (unsigned long long)stats.typed_array_simd_elements,
          (unsigned long long)stats.typed_array_simd_sse2_entries,
          (unsigned long long)stats.typed_array_simd_avx2_entries,
          (unsigned long long)stats.osr_bailouts);
        JS_FreeContext(ctx);JS_FreeRuntime(rt);return 1;
    }
    JS_FreeContext(ctx);JS_FreeRuntime(rt);return 0;
}


static int run_float64_simulation_region(void) {
    const char *source =
        "(function(){const n=4096,a=new Float64Array(n),b=new Float64Array(n);"
        "for(let i=0;i<n;i++){a[i]=(i%997)*0.0001+0.25;b[i]=(i%113)*0.00001+0.95;}"
        "let total=0;for(let r=0;r<8;r++)for(let i=0;i<n;i++){"
        "const v=(a[i]*b[i]+(i&15)*0.0003)/(1.00001+(r&3)*0.000001);"
        "a[i]=v;total+=v;}return total+a[123];})()";
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx;
    JSValue value;
    TurboJSRuntimeJITStats stats;
    double result = 0.0, expected = 0.0;
    double a[4096], b[4096];
    int r, i;
    if (!rt) return 1;
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); return 1; }
    for (i = 0; i < 4096; ++i) {
        a[i] = (double)(i % 997) * 0.0001 + 0.25;
        b[i] = (double)(i % 113) * 0.00001 + 0.95;
    }
    for (r = 0; r < 8; ++r) {
        const double divisor = 1.00001 + (double)(r & 3) * 0.000001;
        for (i = 0; i < 4096; ++i) {
            const double v = (a[i] * b[i] + (double)(i & 15) * 0.0003) / divisor;
            a[i] = v;
            expected += v;
        }
    }
    expected += a[123];
    TurboJS_SetRuntimeJITThreshold(rt, 100);
    value = JS_Eval(ctx, source, strlen(source), "float64-simulation-region.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value) || JS_ToFloat64(ctx, &result, value) ||
        fabs(result - expected) > 1e-9 * (1.0 + fabs(expected))) {
        fprintf(stderr, "Float64 simulation region mismatch: got=%.17g expected=%.17g\n",
                result, expected);
        JS_FreeValue(ctx, value); JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeValue(ctx, value);
    stats = TurboJS_GetRuntimeJITStats(rt);
    if (stats.typed_array_affine_sum_osr_entries < 8 ||
        stats.typed_array_affine_sum_osr_elements < 32000 || stats.osr_bailouts != 0) {
        fprintf(stderr, "Float64 simulation region inactive: entries=%llu elements=%llu bailouts=%llu\n",
                (unsigned long long)stats.typed_array_affine_sum_osr_entries,
                (unsigned long long)stats.typed_array_affine_sum_osr_elements,
                (unsigned long long)stats.osr_bailouts);
        JS_FreeContext(ctx); JS_FreeRuntime(rt); return 1;
    }
    JS_FreeContext(ctx); JS_FreeRuntime(rt); return 0;
}

static int run_dynamic_scalar_limit_sources(void) {
    if (run_source("(function kernel(n){let s=0;for(let i=0;i<n;i++)s=(s+i)|0;return s;})(1000000)",
                   1783293664LL)) return 1;
    if (run_source_f64("(function kernel(n){let x=1.25;for(let i=0;i<n;i++)x=(x*1.000001+0.25)/1.0000005;return x;})(10000)",
                       2507.5148109479655)) return 1;
    if (run_source_f64("(function(){function leaf(a,b){return(a+b)*3-7;}function kernel(n){let s=0;for(let i=0;i<n;i++)s+=leaf(i,2);return s;}return kernel(1000000);})()",
                       1499997500000.0)) return 1;
    return 0;
}

int main(void) {
    if (run_dynamic_scalar_limit_sources()) return 1;
    if (run_source("(function f(n){let sum=0;for(let i=0;i<n;i++){sum+=i;}return sum;})(1000000)", 499999500000LL)) return 1;
    if (run_source("(function f(n){let sum=10;for(let i=5;i<=n;i++){sum+=i;}return sum;})(1000000)", 500000500000LL)) return 1;
    if (run_source("(function f(limit,start){let sum=0;for(let i=start;i>limit;i--){sum+=i;}return sum;})(0,1000000)", 500000500000LL)) return 1;
    if (run_source("(function f(limit,start){let sum=1;for(let i=start;i>=limit;i--){sum+=i;}return sum;})(1,1000000)", 500000500001LL)) return 1;
    if (run_source("(function f(n){let sum=7;for(let i=3;i<n;i+=2){sum+=i;}return sum;})(1000001)", 250000000006LL)) return 1;
    if (run_source("(function f(limit,start){let sum=5;for(let i=start;i>limit;i-=3){sum+=i;}return sum;})(0,1000000)", 166667166672LL)) return 1;
    if (run_source_f64("(function f(n){let sum=0.5;for(let i=0;i<n;i++){sum+=i;}return sum;})(1000000)", 499999500000.5)) return 1;
    if (run_dense_array_source("(function(){let a=[];for(let i=0;i<1000000;i++)a[i]=i;function f(a){let sum=0;for(let i=0;i<a.length;i++){sum+=a[i];}return sum;}return f(a);})()", 499999500000LL)) return 1;
    if (run_dense_array_bailout_source()) return 1;
    if (run_dense_array_transform_source()) return 1;
    if (run_dense_array_transform_bailout_source()) return 1;
    if (run_dense_array_extended_kernels()) return 1;
    if (run_float64_typed_array_kernels()) return 1;
    if (run_float64_simulation_region()) return 1;
    puts("Automatic integer, Float64, packed-array, and Float64Array JavaScript OSR passed");
    return 0;
}
