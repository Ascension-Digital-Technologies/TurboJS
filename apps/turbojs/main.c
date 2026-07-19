/*
 * TurboJS standalone interpreter
 *
 * Copyright (c) 2017-2021 Fabrice Bellard
 * Copyright (c) 2017-2021 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif
#endif

#include "internal/cutils.h"
#include <turbojs.h>
#include <turbojs-libc.h>

#ifdef TURBOJS_USE_MIMALLOC
#include <mimalloc.h>
#endif

extern const uint8_t turbojsc_repl[];
extern const uint32_t turbojsc_repl_size;
extern const uint8_t turbojsc_standalone[];
extern const uint32_t turbojsc_standalone_size;

#if defined(EMSCRIPTEN) || defined(__wasi__)
// Standalone executables (the --compile option and detecting/running an
// executable with appended bytecode) can't work in these environments.
#define emscripten_or_wasi 1
#else
#define emscripten_or_wasi 0
#endif

#if defined(_WIN32)
static void turbojs_disable_windows_error_dialogs(void)
{
    /* Test262 intentionally executes malformed and adversarial programs.
       Never allow a child process failure to open a modal Windows dialog. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
}
#endif

static int turbojs_argc;
static char **turbojs_argv;

// Must match standalone.js
#define TRAILER_SIZE 12
static const char trailer_magic[] = "turbojs2";
static const int trailer_magic_size = sizeof(trailer_magic) - 1;
static const int trailer_size = TRAILER_SIZE;

static bool is_standalone(const char *exe)
{
    FILE *exe_f = fopen(exe, "rb");
    if (!exe_f)
        return false;
    if (fseek(exe_f, -trailer_size, SEEK_END) < 0)
        goto fail;
    uint8_t buf[TRAILER_SIZE];
    if (fread(buf, 1, trailer_size, exe_f) != trailer_size)
        goto fail;
    fclose(exe_f);
    return !memcmp(buf, trailer_magic, trailer_magic_size);
fail:
    fclose(exe_f);
    return false;
}

static JSValue load_standalone_module(JSContext *ctx)
{
    JSModuleDef *m;
    JSValue obj, val;
    obj = JS_ReadObject(ctx, turbojsc_standalone, turbojsc_standalone_size, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj))
        goto exception;
    assert(JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE);
    if (JS_ResolveModule(ctx, obj) < 0) {
        JS_FreeValue(ctx, obj);
        goto exception;
    }
    if (js_module_set_import_meta(ctx, obj, false, true) < 0) {
        JS_FreeValue(ctx, obj);
        goto exception;
    }
    val = JS_EvalFunction(ctx, JS_DupValue(ctx, obj));
    val = js_std_await(ctx, val);

    if (JS_IsException(val)) {
        JS_FreeValue(ctx, obj);
    exception:
        js_std_dump_error(ctx);
        exit(1);
    }
    JS_FreeValue(ctx, val);

    m = JS_VALUE_GET_PTR(obj);
    JS_FreeValue(ctx, obj);
    return JS_GetModuleNamespace(ctx, m);
}

static int eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags)
{
    bool use_realpath;
    JSValue val;
    int ret;

    if ((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE) {
        /* for the modules, we compile then run to be able to set
           import.meta */
        val = JS_Eval(ctx, buf, buf_len, filename,
                      eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
        if (!JS_IsException(val)) {
            // ex. "<cmdline>" pr "/dev/stdin"
            use_realpath =
                !(*filename == '<' || !strncmp(filename, "/dev/", 5));
            if (js_module_set_import_meta(ctx, val, use_realpath, true) < 0) {
                js_std_dump_error(ctx);
                ret = -1;
                goto end;
            }
            val = JS_EvalFunction(ctx, val);
        }
        val = js_std_await(ctx, val);
    } else {
        val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
    }
    if (JS_IsException(val)) {
        js_std_dump_error(ctx);
        ret = -1;
    } else {
        ret = 0;
    }
end:
    JS_FreeValue(ctx, val);
    return ret;
}

static int eval_file(JSContext *ctx, const char *filename, int module)
{
    uint8_t *buf;
    int ret, eval_flags;
    size_t buf_len;

    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        perror(filename);
        exit(1);
    }

    if (module < 0) {
        module = (js__has_suffix(filename, ".mjs") ||
                  JS_DetectModule((const char *)buf, buf_len));
    }
    if (module)
        eval_flags = JS_EVAL_TYPE_MODULE;
    else
        eval_flags = JS_EVAL_TYPE_GLOBAL;
    ret = eval_buf(ctx, buf, buf_len, filename, eval_flags);
    js_free(ctx, buf);
    return ret;
}

static int64_t parse_limit(const char *arg) {
    char *p;
    unsigned long unit = 1024; /* default to traditional KB */
    double d = strtod(arg, &p);

    if (p == arg) {
        fprintf(stderr, "Invalid limit: %s\n", arg);
        return -1;
    }

    if (*p) {
        switch (*p++) {
        case 'b': case 'B': unit = 1UL <<  0; break;
        case 'k': case 'K': unit = 1UL << 10; break; /* IEC kibibytes */
        case 'm': case 'M': unit = 1UL << 20; break; /* IEC mebibytes */
        case 'g': case 'G': unit = 1UL << 30; break; /* IEC gigibytes */
        default:
            fprintf(stderr, "Invalid limit: %s, unrecognized suffix, only k,m,g are allowed\n", arg);
            return -1;
        }
        if (*p) {
            fprintf(stderr, "Invalid limit: %s, only one suffix allowed\n", arg);
            return -1;
        }
    }

    return (int64_t)(d * unit);
}

static JSValue js_gc(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv)
{
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}

static JSValue js_navigator_get_userAgent(JSContext *ctx, JSValueConst this_val)
{
    char version[32];
    snprintf(version, sizeof(version), "TurboJS/%s", JS_GetVersion());
    return JS_NewString(ctx, version);
}

static const JSCFunctionListEntry navigator_proto_funcs[] = {
    JS_CGETSET_DEF2("userAgent", js_navigator_get_userAgent, NULL, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Navigator", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry global_obj[] = {
    JS_CFUNC_DEF("gc", 0, js_gc),
};

/* also used to initialize the worker context */
static JSContext *JS_NewCustomContext(JSRuntime *rt)
{
    JSContext *ctx;
    ctx = JS_NewContext(rt);
    if (!ctx)
        return NULL;
    /* system modules */
    js_init_module_std(ctx, "turbojs:std");
    js_init_module_os(ctx, "turbojs:os");
    js_init_module_bjson(ctx, "turbojs:bjson");

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyFunctionList(ctx, global, global_obj, countof(global_obj));
    JSValue args = JS_NewArray(ctx);
    int i;
    for(i = 0; i < turbojs_argc; i++) {
        JS_SetPropertyUint32(ctx, args, i, JS_NewString(ctx, turbojs_argv[i]));
    }
    JS_SetPropertyStr(ctx, global, "execArgv", args);
    JS_SetPropertyStr(ctx, global, "argv0", JS_NewString(ctx, turbojs_argv[0]));
    JSValue navigator_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, navigator_proto, navigator_proto_funcs, countof(navigator_proto_funcs));
    JSValue navigator = JS_NewObjectProto(ctx, navigator_proto);
    JS_DefinePropertyValueStr(ctx, global, "navigator", navigator, JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, navigator_proto);

    return ctx;
}

struct trace_malloc_data {
    uint8_t *base;
};

static inline unsigned long long js_trace_malloc_ptr_offset(uint8_t *ptr,
                                                struct trace_malloc_data *dp)
{
    return ptr - dp->base;
}

static void JS_PRINTF_FORMAT_ATTR(2, 3) js_trace_malloc_printf(void *opaque, JS_PRINTF_FORMAT const char *fmt, ...)
{
    va_list ap;
    int c;

    va_start(ap, fmt);
    while ((c = *fmt++) != '\0') {
        if (c == '%') {
            /* only handle %p and %zd */
            if (*fmt == 'p') {
                uint8_t *ptr = va_arg(ap, void *);
                if (ptr == NULL) {
                    printf("NULL");
                } else {
                    printf("H%+06lld.%zd",
                           js_trace_malloc_ptr_offset(ptr, opaque),
                           js__malloc_usable_size(ptr));
                }
                fmt++;
                continue;
            }
            if (fmt[0] == 'z' && fmt[1] == 'd') {
                size_t sz = va_arg(ap, size_t);
                printf("%zd", sz);
                fmt += 2;
                continue;
            }
        }
        putc(c, stdout);
    }
    va_end(ap);
}

static void js_trace_malloc_init(struct trace_malloc_data *s)
{
    free(s->base = malloc(8));
}

static void *js_trace_calloc(void *opaque, size_t count, size_t size)
{
    void *ptr;
    ptr = calloc(count, size);
    js_trace_malloc_printf(opaque, "C %zd %zd -> %p\n", count, size, ptr);
    return ptr;
}

static void *js_trace_malloc(void *opaque, size_t size)
{
    void *ptr;
    ptr = malloc(size);
    js_trace_malloc_printf(opaque, "A %zd -> %p\n", size, ptr);
    return ptr;
}

static void js_trace_free(void *opaque, void *ptr)
{
    if (!ptr)
        return;
    js_trace_malloc_printf(opaque, "F %p\n", ptr);
    free(ptr);
}

static void *js_trace_realloc(void *opaque, void *ptr, size_t size)
{
    js_trace_malloc_printf(opaque, "R %zd %p", size, ptr);
    ptr = realloc(ptr, size);
    js_trace_malloc_printf(opaque, " -> %p\n", ptr);
    return ptr;
}

static const JSMallocFunctions trace_mf = {
    js_trace_calloc,
    js_trace_malloc,
    js_trace_free,
    js_trace_realloc,
    js__malloc_usable_size
};

#ifdef TURBOJS_USE_MIMALLOC
static void *js_mi_calloc(void *opaque, size_t count, size_t size)
{
    return mi_calloc(count, size);
}

static void *js_mi_malloc(void *opaque, size_t size)
{
    return mi_malloc(size);
}

static void js_mi_free(void *opaque, void *ptr)
{
    if (!ptr)
        return;
    mi_free(ptr);
}

static void *js_mi_realloc(void *opaque, void *ptr, size_t size)
{
    return mi_realloc(ptr, size);
}

static const JSMallocFunctions mi_mf = {
    js_mi_calloc,
    js_mi_malloc,
    js_mi_free,
    js_mi_realloc,
    mi_malloc_usable_size
};
#endif

#define PROG_NAME "turbojs"

void help(int exit_status)
{
    printf("TurboJS version %s\n"
           "usage: " PROG_NAME " [options] [file [args]]\n"
           "-h  --help         list options\n"
           "-v  --version      print version string and then exit\n"
           "-e  --eval EXPR    evaluate EXPR\n"
           "-i  --interactive  go to interactive mode\n"
           "-C  --script       load as JS classic script (default=autodetect)\n"
           "-m  --module       load as ES module (default=autodetect)\n"
           "-I  --include file include an additional file\n"
           "    --std          make 'std', 'os' and 'bjson' available to script\n"
           "-T  --trace        trace memory allocation\n"
           "-d  --dump         dump the memory usage stats\n"
           "-D  --dump-flags   flags for dumping debug data (see DUMP_* defines)\n"
           "    --jit-stats    print Redline/Spool/Slipstream execution statistics\n"
           "    --jit-stats-json print JIT statistics as one JSON object\n"
           "    --jit-threshold n set the baseline tier-up threshold\n"
           "    --opt-threshold n set the optimizing tier-up threshold\n"
           "    --osr-threshold n set the loop backedge OSR threshold\n"
           "    --no-jit       disable native execution for this runtime\n",
           JS_GetVersion());
    if (!emscripten_or_wasi)
        printf("-c  --compile FILE compile the given JS file as a standalone executable\n"
               "-o  --out FILE     output file for standalone executables\n"
               "    --exe          select the executable to use as the base, defaults to the current one\n");
    printf("    --memory-limit n       limit the memory usage to 'n' Kbytes\n"
           "    --stack-size n         limit the stack size to 'n' Kbytes\n"
           "-q  --quit         just instantiate the interpreter and quit\n");
    exit(exit_status);
}

int main(int argc, char **argv)
{
#if defined(_WIN32)
    turbojs_disable_windows_error_dialogs();
#endif
    JSRuntime *rt;
    JSContext *ctx;
    struct trace_malloc_data trace_data = { NULL };
    int r = 0;
    int optind = 1;
    JSValue ret = JS_UNDEFINED;
    char exebuf[JS__PATH_MAX];
    size_t exebuf_size = sizeof(exebuf);
    char *compile_file = NULL;
    char *exe = NULL;
    char *out = NULL;
    int standalone = 0;
    char *expr = NULL;
    char *dump_flags_str = NULL;
    int interactive = 0;
    int dump_memory = 0;
    int dump_flags = 0;
    int trace_memory = 0;
    int empty_run = 0;
    int module = -1;
    int load_std = 0;
    char *include_list[32];
    int i, include_count = 0;
    int64_t memory_limit = -1;
    int64_t stack_size = -1;
    uint32_t jit_threshold = 0;
    uint32_t opt_threshold = 0;
    uint32_t osr_threshold = 0;
    int jit_stats = 0;
    int jit_stats_json = 0;
    int disable_jit = 0;

    /* save for later */
    turbojs_argc = argc;
    turbojs_argv = argv;

    /* check if this is a standalone executable */
    if (!emscripten_or_wasi &&
            !js_exepath(exebuf, &exebuf_size) && is_standalone(exebuf)) {
        standalone = 1;
        goto start;
    }

    dump_flags = dump_flags_str ? strtol(dump_flags_str, NULL, 16) : 0;

    /* cannot use getopt because we want to pass the command line to
       the script */
    while (optind < argc && *argv[optind] == '-') {
        char *arg = argv[optind] + 1;
        char *longopt = "";
        char *optarg = NULL;
        /* a single - is not an option, it also stops argument scanning */
        if (!*arg)
            break;
        optind++;
        if (*arg == '-') {
            longopt = arg + 1;
            optarg = strchr(longopt, '=');
            if (optarg)
                *optarg++ = '\0';
            arg += strlen(arg);
            /* -- stops argument scanning */
            if (!*longopt)
                break;
        }
        for (; *arg || *longopt; longopt = "") {
            char opt = *arg;
            if (opt) {
                arg++;
                if (!optarg && *arg)
                    optarg = arg;
            }
            if (opt == 'h' || opt == '?' || !strcmp(longopt, "help")) {
                help(0);
            }
            if (opt == 'v' || !strcmp(longopt, "version")) {
                printf("%s\n",JS_GetVersion());
                return 0;
            }
            if (opt == 'e' || !strcmp(longopt, "eval")) {
                if (!optarg) {
                    if (optind >= argc) {
                        fprintf(stderr, "turbojs: missing expression for -e\n");
                        exit(1);
                    }
                    optarg = argv[optind++];
                }
                expr = optarg;
                break;
            }
            if (opt == 'I' || !strcmp(longopt, "include")) {
                if (optind >= argc) {
                    fprintf(stderr, "expecting filename");
                    exit(1);
                }
                if (include_count >= countof(include_list)) {
                    fprintf(stderr, "too many included files");
                    exit(1);
                }
                include_list[include_count++] = argv[optind++];
                continue;
            }
            if (opt == 'i' || !strcmp(longopt, "interactive")) {
                interactive++;
                continue;
            }
            if (opt == 'm' || !strcmp(longopt, "module")) {
                module = 1;
                continue;
            }
            if (opt == 'C' || !strcmp(longopt, "script")) {
                module = 0;
                continue;
            }
            if (opt == 'd' || !strcmp(longopt, "dump")) {
                dump_memory++;
                continue;
            }
            if (opt == 'D' || !strcmp(longopt, "dump-flags")) {
                dump_flags = optarg ? strtol(optarg, NULL, 16) : 0;
                break;
            }
            if (opt == 'T' || !strcmp(longopt, "trace")) {
                trace_memory++;
                continue;
            }
            if (!strcmp(longopt, "std")) {
                load_std = 1;
                continue;
            }
            if (opt == 'q' || !strcmp(longopt, "quit")) {
                empty_run++;
                continue;
            }
            if (!strcmp(longopt, "jit-stats")) {
                jit_stats = 1;
                continue;
            }
            if (!strcmp(longopt, "jit-stats-json")) {
                jit_stats_json = 1;
                continue;
            }
            if (!strcmp(longopt, "no-jit")) {
                disable_jit = 1;
                continue;
            }
            if (!strcmp(longopt, "jit-threshold") ||
                !strcmp(longopt, "opt-threshold") ||
                !strcmp(longopt, "osr-threshold")) {
                unsigned long parsed;
                char *end = NULL;
                const char *name = longopt;
                if (!optarg) {
                    if (optind >= argc) {
                        fprintf(stderr, "turbojs: expecting value for --%s\n", name);
                        exit(1);
                    }
                    optarg = argv[optind++];
                }
                errno = 0;
                parsed = strtoul(optarg, &end, 10);
                if (errno || end == optarg || *end || parsed == 0 || parsed > UINT32_MAX) {
                    fprintf(stderr, "turbojs: invalid --%s value: %s\n", name, optarg);
                    exit(1);
                }
                if (!strcmp(name, "jit-threshold")) jit_threshold = (uint32_t)parsed;
                else if (!strcmp(name, "opt-threshold")) opt_threshold = (uint32_t)parsed;
                else osr_threshold = (uint32_t)parsed;
                break;
            }
            if (!strcmp(longopt, "memory-limit")) {
                if (!optarg) {
                    if (optind >= argc) {
                        fprintf(stderr, "expecting memory limit");
                        exit(1);
                    }
                    optarg = argv[optind++];
                }
                memory_limit = parse_limit(optarg);
                break;
            }
            if (!strcmp(longopt, "stack-size")) {
                if (!optarg) {
                    if (optind >= argc) {
                        fprintf(stderr, "expecting stack size");
                        exit(1);
                    }
                    optarg = argv[optind++];
                }
                stack_size = parse_limit(optarg);
                break;
            }
            if (!emscripten_or_wasi &&
                    (opt == 'c' || !strcmp(longopt, "compile"))) {
                if (!optarg) {
                    if (optind >= argc) {
                        fprintf(stderr, "turbojs: missing file for -c\n");
                        exit(1);
                    }
                    optarg = argv[optind++];
                }
                compile_file = optarg;
                break;
            }
            if (!emscripten_or_wasi && (opt == 'o' || !strcmp(longopt, "out"))) {
                if (!optarg) {
                    if (optind >= argc) {
                        fprintf(stderr, "turbojs: missing file for -o\n");
                        exit(1);
                    }
                    optarg = argv[optind++];
                }
                out = optarg;
                break;
            }
            if (!emscripten_or_wasi && !strcmp(longopt, "exe")) {
                if (!optarg) {
                    if (optind >= argc) {
                        fprintf(stderr, "turbojs: missing file for --exe\n");
                        exit(1);
                    }
                    optarg = argv[optind++];
                }
                exe = optarg;
                break;
            }
            if (opt) {
                fprintf(stderr, "turbojs: unknown option '-%c'\n", opt);
            } else {
                fprintf(stderr, "turbojs: unknown option '--%s'\n", longopt);
            }
            help(1);
        }
    }

    if (!emscripten_or_wasi && compile_file && !out)
        help(1);

start:

    if (trace_memory) {
        js_trace_malloc_init(&trace_data);
        rt = JS_NewRuntime2(&trace_mf, &trace_data);
    } else {
#ifdef TURBOJS_USE_MIMALLOC
        rt = JS_NewRuntime2(&mi_mf, NULL);
#else
        rt = JS_NewRuntime();
#endif
    }
    if (!rt) {
        fprintf(stderr, "turbojs: cannot allocate JS runtime\n");
        exit(2);
    }
    {
        TurboJSOptimizationConfig optimization = TurboJS_GetRuntimeOptimizationConfig(rt);
        if (jit_threshold) optimization.baseline_threshold = jit_threshold;
        if (opt_threshold) optimization.optimizing_threshold = opt_threshold;
        if (osr_threshold) optimization.osr_threshold = osr_threshold;
        if (disable_jit) {
            optimization.enable_jit = 0;
            optimization.enable_optimizing_jit = 0;
            optimization.enable_osr = 0;
        }
        TurboJS_SetRuntimeOptimizationConfig(rt, &optimization);
    }
    if (memory_limit >= 0)
        JS_SetMemoryLimit(rt, (size_t)memory_limit);
    if (stack_size >= 0)
        JS_SetMaxStackSize(rt, (size_t)stack_size);
    if (dump_flags != 0)
        JS_SetDumpFlags(rt, dump_flags);
    js_std_set_worker_new_context_func(JS_NewCustomContext);
    js_std_init_handlers(rt);
    ctx = JS_NewCustomContext(rt);
    if (!ctx) {
        fprintf(stderr, "turbojs: cannot allocate JS context\n");
        exit(2);
    }

    /* loader for ES6 modules */
    JS_SetModuleLoaderFunc2(rt, NULL, js_module_loader, js_module_check_attributes, NULL);

    /* exit on unhandled promise rejections */
    JS_SetHostPromiseRejectionTracker(rt, js_std_promise_rejection_tracker, NULL);

    if (!empty_run) {
        js_std_add_helpers(ctx, argc - optind, argv + optind);

        /* make 'std' and 'os' visible to non module code */
        if (load_std) {
            const char *str =
                "import * as bjson from 'turbojs:bjson';\n"
                "import * as std from 'turbojs:std';\n"
                "import * as os from 'turbojs:os';\n"
                "globalThis.bjson = bjson;\n"
                "globalThis.std = std;\n"
                "globalThis.os = os;\n";
            eval_buf(ctx, str, strlen(str), "<input>", JS_EVAL_TYPE_MODULE);
        }

        for(i = 0; i < include_count; i++) {
            if (eval_file(ctx, include_list[i], 0))
                goto fail;
        }

        if (standalone) {
            JSValue ns = load_standalone_module(ctx);
            if (JS_IsException(ns))
                goto fail;
            JSValue func = JS_GetPropertyStr(ctx, ns, "runStandalone");
            JS_FreeValue(ctx, ns);
            if (JS_IsException(func))
                goto fail;
            ret = JS_Call(ctx, func, JS_UNDEFINED, 0, NULL);
            JS_FreeValue(ctx, func);
        } else if (compile_file) {
            JSValue ns = load_standalone_module(ctx);
            if (JS_IsException(ns))
                goto fail;
            JSValue func = JS_GetPropertyStr(ctx, ns, "compileStandalone");
            JS_FreeValue(ctx, ns);
            if (JS_IsException(func))
                goto fail;
            JSValue args[3];
            args[0] = JS_NewString(ctx, compile_file);
            args[1] = JS_NewString(ctx, out);
            args[2] = exe != NULL ? JS_NewString(ctx, exe) : JS_UNDEFINED;
            ret = JS_Call(ctx, func, JS_UNDEFINED, 3, (JSValueConst *)args);
            JS_FreeValue(ctx, func);
            JS_FreeValue(ctx, args[0]);
            JS_FreeValue(ctx, args[1]);
            JS_FreeValue(ctx, args[2]);
        } else if (expr) {
            int flags = module ? JS_EVAL_TYPE_MODULE : 0;
            if (eval_buf(ctx, expr, strlen(expr), "<cmdline>", flags))
                goto fail;
        } else if (optind >= argc) {
            /* interactive mode */
            interactive = 1;
        } else {
            const char *filename;
            filename = argv[optind];
            if (eval_file(ctx, filename, module))
                goto fail;
        }
        if (interactive) {
            JS_SetHostPromiseRejectionTracker(rt, NULL, NULL);
            js_std_eval_binary(ctx, turbojsc_repl, turbojsc_repl_size, 0);
        }
        if (standalone || compile_file) {
            if (JS_IsException(ret)) {
                r = 1;
            } else {
                JS_FreeValue(ctx, ret);
                r = js_std_loop(ctx);
            }
        } else {
            r = js_std_loop(ctx);
        }
        if (r) {
            js_std_dump_error(ctx);
            goto fail;
        }
    }

    if (jit_stats || jit_stats_json) {
        TurboJSOptimizationConfig optimization = TurboJS_GetRuntimeOptimizationConfig(rt);
        TurboJSRuntimeJITStats stats = TurboJS_GetRuntimeJITStats(rt);
        double native_ratio = (stats.native_calls + stats.interpreted_calls) ?
            (100.0 * (double)stats.native_calls /
             (double)(stats.native_calls + stats.interpreted_calls)) : 0.0;
        if (jit_stats_json) {
            printf("{\"baseline_threshold\":%u,\"optimizing_threshold\":%u,"
                   "\"osr_threshold\":%u,\"jit_enabled\":%u,"
                   "\"optimizing_enabled\":%u,\"osr_enabled\":%u,"
                   "\"interpreted_calls\":%" PRIu64 ",\"native_calls\":%" PRIu64 ","
                   "\"native_call_percent\":%.3f,\"guard_failures\":%" PRIu64 ","
                   "\"baseline_compile_requests\":%" PRIu64 ","
                   "\"baseline_compilations\":%" PRIu64 ",\"baseline_compile_failures\":%" PRIu64 ","
                   "\"optimizing_compile_requests\":%" PRIu64 ",\"optimizing_compilations\":%" PRIu64 ","
                   "\"optimizing_compile_failures\":%" PRIu64 ","
                   "\"tier_up_requests\":%" PRIu64 ",\"tier_up_successes\":%" PRIu64 ","
                   "\"region_compilations\":%" PRIu64 ",\"region_native_calls\":%" PRIu64 ","
                   "\"region_compile_failures\":%" PRIu64 ",\"osr_backedges\":%" PRIu64 ","
                   "\"osr_compile_requests\":%" PRIu64 ",\"osr_compilations\":%" PRIu64 ","
                   "\"osr_compile_failures\":%" PRIu64 ",\"osr_frame_captures\":%" PRIu64 ","
                   "\"osr_entries\":%" PRIu64 ",\"osr_bailouts\":%" PRIu64 ","
                   "\"osr_negative_cache_hits\":%" PRIu64 ","
                   "\"osr_rejections_unsupported\":%" PRIu64 ","
                   "\"osr_rejections_allocation\":%" PRIu64 ","
                   "\"osr_rejections_backend\":%" PRIu64 ","
                   "\"osr_leaf_call_entries\":%" PRIu64 ","
                   "\"osr_leaf_call_iterations\":%" PRIu64 ","
                   "\"osr_int32_mix_entries\":%" PRIu64 ","
                   "\"osr_int32_mix_iterations\":%" PRIu64 ","
                   "\"holey_array_osr_entries\":%" PRIu64 ","
                   "\"holey_array_osr_elements\":%" PRIu64 ","
                   "\"typed_array_affine_sum_osr_entries\":%" PRIu64 ","
                   "\"typed_array_affine_sum_osr_elements\":%" PRIu64 ","
                   "\"object_array_osr_entries\":%" PRIu64 ","
                   "\"object_array_osr_elements\":%" PRIu64 ","
                   "\"object_array_polymorphic_osr_entries\":%" PRIu64 ","
                   "\"object_array_update_osr_entries\":%" PRIu64 ","
                   "\"object_array_grouped_osr_entries\":%" PRIu64 ","
                   "\"object_array_grouped_osr_elements\":%" PRIu64 ","
                   "\"osr_polymorphic_leaf_entries\":%" PRIu64 ","
                   "\"osr_polymorphic_leaf_iterations\":%" PRIu64 ","
                   "\"osr_closure_call_entries\":%" PRIu64 ","
                   "\"osr_closure_call_iterations\":%" PRIu64 ","
                   "\"osr_recursive_call_entries\":%" PRIu64 ","
                   "\"osr_recursive_call_iterations\":%" PRIu64 ","
                   "\"osr_coupled_float_entries\":%" PRIu64 ","
                   "\"osr_coupled_float_iterations\":%" PRIu64 ","
                   "\"deoptimizations\":%" PRIu64 ",\"cache_hits\":%" PRIu64 ","
                   "\"cache_misses\":%" PRIu64 ",\"cache_compilations\":%" PRIu64 ","
                   "\"cache_evictions\":%" PRIu64 ",\"cache_entries\":%zu,"
                   "\"cache_native_code_bytes\":%zu}\n",
                   optimization.baseline_threshold, optimization.optimizing_threshold,
                   optimization.osr_threshold, optimization.enable_jit,
                   optimization.enable_optimizing_jit, optimization.enable_osr,
                   stats.interpreted_calls, stats.native_calls, native_ratio,
                   stats.guard_failures, stats.baseline_compile_requests,
                   stats.baseline_compilations, stats.baseline_compile_failures,
                   stats.optimizing_compile_requests, stats.optimizing_compilations,
                   stats.optimizing_compile_failures, stats.tier_up_requests,
                   stats.tier_up_successes, stats.region_compilations,
                   stats.region_native_calls, stats.region_compile_failures,
                   stats.osr_backedges, stats.osr_compile_requests,
                   stats.osr_compilations, stats.osr_compile_failures,
                   stats.osr_frame_captures, stats.osr_entries,
                   stats.osr_bailouts, stats.osr_negative_cache_hits,
                   stats.osr_rejections_unsupported, stats.osr_rejections_allocation,
                   stats.osr_rejections_backend, stats.osr_leaf_call_entries,
                   stats.osr_leaf_call_iterations, stats.osr_int32_mix_entries,
                   stats.osr_int32_mix_iterations, stats.holey_array_osr_entries,
                   stats.holey_array_osr_elements,
                   stats.typed_array_affine_sum_osr_entries,
                   stats.typed_array_affine_sum_osr_elements,
                   stats.object_array_osr_entries, stats.object_array_osr_elements,
                   stats.object_array_polymorphic_osr_entries,
                   stats.object_array_update_osr_entries,
                   stats.object_array_grouped_osr_entries,
                   stats.object_array_grouped_osr_elements,
                   stats.osr_polymorphic_leaf_entries,
                   stats.osr_polymorphic_leaf_iterations,
                   stats.osr_closure_call_entries,
                   stats.osr_closure_call_iterations,
                   stats.osr_recursive_call_entries,
                   stats.osr_recursive_call_iterations,
                   stats.osr_coupled_float_entries,
                   stats.osr_coupled_float_iterations, stats.deoptimizations,
                   stats.cache_hits, stats.cache_misses, stats.compilations,
                   stats.evictions, stats.cache_entries, stats.native_code_bytes);
        } else {
            printf("\nTurboJS JIT statistics\n"
                   "  policy: baseline=%u optimizing=%u osr=%u enabled=%u/%u/%u\n"
                   "  calls: interpreted=%" PRIu64 " native=%" PRIu64 " native_ratio=%.2f%% guards=%" PRIu64 "\n"
                   "  baseline: requests=%" PRIu64 " compiled=%" PRIu64 " failed=%" PRIu64 "\n"
                   "  optimizing: requests=%" PRIu64 " compiled=%" PRIu64 " failed=%" PRIu64
                   " tier_up=%" PRIu64 "/%" PRIu64 "\n"
                   "  regions: compiled=%" PRIu64 " calls=%" PRIu64 " failed=%" PRIu64 "\n"
                   "  osr: backedges=%" PRIu64 " requests=%" PRIu64 " compiled=%" PRIu64
                   " failed=%" PRIu64 " captures=%" PRIu64 " entries=%" PRIu64 " bailouts=%" PRIu64 "\n"
                   "  osr specialization: negative_cache=%" PRIu64 " rejected=%" PRIu64 "/%" PRIu64 "/%" PRIu64
                   " leaf_calls=%" PRIu64 " iterations=%" PRIu64 " int32_mix=%" PRIu64 " iterations=%" PRIu64 "\n"
                   "  collection osr: holey=%" PRIu64 "/%" PRIu64
                   " typed_affine=%" PRIu64 "/%" PRIu64
                   " object=%" PRIu64 "/%" PRIu64 " poly=%" PRIu64 " update=%" PRIu64
                   " grouped=%" PRIu64 "/%" PRIu64 "\n"
                   "  call osr: polymorphic=%" PRIu64 "/%" PRIu64
                   " closures=%" PRIu64 "/%" PRIu64
                   " recursive=%" PRIu64 "/%" PRIu64 "\n"
                   "  deoptimizations=%" PRIu64 "\n"
                   "  cache: hits=%" PRIu64 " misses=%" PRIu64 " compilations=%" PRIu64
                   " evictions=%" PRIu64 " entries=%zu native_code=%zu bytes\n",
                   optimization.baseline_threshold, optimization.optimizing_threshold,
                   optimization.osr_threshold, optimization.enable_jit,
                   optimization.enable_optimizing_jit, optimization.enable_osr,
                   stats.interpreted_calls, stats.native_calls, native_ratio,
                   stats.guard_failures, stats.baseline_compile_requests,
                   stats.baseline_compilations, stats.baseline_compile_failures,
                   stats.optimizing_compile_requests, stats.optimizing_compilations,
                   stats.optimizing_compile_failures, stats.tier_up_requests,
                   stats.tier_up_successes, stats.region_compilations,
                   stats.region_native_calls, stats.region_compile_failures,
                   stats.osr_backedges, stats.osr_compile_requests,
                   stats.osr_compilations, stats.osr_compile_failures,
                   stats.osr_frame_captures, stats.osr_entries,
                   stats.osr_bailouts, stats.osr_negative_cache_hits,
                   stats.osr_rejections_unsupported, stats.osr_rejections_allocation,
                   stats.osr_rejections_backend, stats.osr_leaf_call_entries,
                   stats.osr_leaf_call_iterations, stats.osr_int32_mix_entries,
                   stats.osr_int32_mix_iterations, stats.holey_array_osr_entries,
                   stats.holey_array_osr_elements,
                   stats.typed_array_affine_sum_osr_entries,
                   stats.typed_array_affine_sum_osr_elements,
                   stats.object_array_osr_entries, stats.object_array_osr_elements,
                   stats.object_array_polymorphic_osr_entries,
                   stats.object_array_update_osr_entries,
                   stats.object_array_grouped_osr_entries,
                   stats.object_array_grouped_osr_elements,
                   stats.osr_polymorphic_leaf_entries,
                   stats.osr_polymorphic_leaf_iterations,
                   stats.osr_closure_call_entries,
                   stats.osr_closure_call_iterations,
                   stats.osr_recursive_call_entries,
                   stats.osr_recursive_call_iterations, stats.deoptimizations,
                   stats.cache_hits, stats.cache_misses, stats.compilations,
                   stats.evictions, stats.cache_entries, stats.native_code_bytes);
        }
    }
    if (dump_memory) {
        JSMemoryUsage stats;
        JS_ComputeMemoryUsage(rt, &stats);
        JS_DumpMemoryUsage(stdout, &stats, rt);
    }
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    if (empty_run && dump_memory) {
        clock_t t[5];
        double best[5] = {0};
        int i, j;
        for (i = 0; i < 100; i++) {
            t[0] = clock();
            rt = JS_NewRuntime();
            t[1] = clock();
            ctx = JS_NewContext(rt);
            t[2] = clock();
            JS_FreeContext(ctx);
            t[3] = clock();
            JS_FreeRuntime(rt);
            t[4] = clock();
            for (j = 4; j > 0; j--) {
                double ms = 1000.0 * (t[j] - t[j - 1]) / CLOCKS_PER_SEC;
                if (i == 0 || best[j] > ms)
                    best[j] = ms;
            }
        }
        printf("\nInstantiation times (ms): %.3f = %.3f+%.3f+%.3f+%.3f\n",
               best[1] + best[2] + best[3] + best[4],
               best[1], best[2], best[3], best[4]);
    }
    return 0;
 fail:
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 1;
}
