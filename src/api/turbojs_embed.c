#include "turbojs_embed.h"
#include "turbojs.h"
#include <stdlib.h>
#include <string.h>

struct TurboJSEngine {
    JSRuntime *runtime;
    JSContext *context;
    char error[512];
};

static void set_error(TurboJSEngine *e,const char *s){size_t n;if(!e)return;if(!s)s="unknown error";n=strlen(s);if(n>=sizeof(e->error))n=sizeof(e->error)-1;memcpy(e->error,s,n);e->error[n]='\0';}
static TurboJSEngine *embed_create(const TurboJSEmbedConfig *c)
{
    TurboJSEngine *e;
    if(c && (c->struct_size<sizeof(*c)||c->api_version!=TURBOJS_EMBED_API_VERSION))return NULL;
    e=(TurboJSEngine*)calloc(1,sizeof(*e));if(!e)return NULL;
    e->runtime=JS_NewRuntime();if(!e->runtime){free(e);return NULL;}
    if(c&&c->memory_limit_bytes)JS_SetMemoryLimit(e->runtime,c->memory_limit_bytes);
    if(c&&c->max_stack_bytes)JS_SetMaxStackSize(e->runtime,c->max_stack_bytes);
    e->context=JS_NewContext(e->runtime);if(!e->context){JS_FreeRuntime(e->runtime);free(e);return NULL;}
    return e;
}
static void embed_destroy(TurboJSEngine *e){if(!e)return;if(e->context)JS_FreeContext(e->context);if(e->runtime)JS_FreeRuntime(e->runtime);free(e);}
static TurboJSEmbedStatus embed_eval_i64(TurboJSEngine *e,const char *src,size_t len,int64_t *out)
{
    JSValue v,exc;const char *msg;int rc;
    if(!e||!src||!out)return TURBOJS_EMBED_INVALID_ARGUMENT;
    e->error[0]='\0';
    v=JS_Eval(e->context,src,len,"<embed>",JS_EVAL_TYPE_GLOBAL);
    if(JS_IsException(v)){
        exc=JS_GetException(e->context);msg=JS_ToCString(e->context,exc);set_error(e,msg?msg:"JavaScript exception");if(msg)JS_FreeCString(e->context,msg);JS_FreeValue(e->context,exc);return TURBOJS_EMBED_EXCEPTION;
    }
    rc=JS_ToInt64(e->context,out,v);JS_FreeValue(e->context,v);
    if(rc<0){set_error(e,"result is not convertible to int64");return TURBOJS_EMBED_CONVERSION_ERROR;}
    return TURBOJS_EMBED_OK;
}
static const char *embed_last_error(const TurboJSEngine *e){return e?e->error:"invalid engine";}
static void embed_gc(TurboJSEngine *e){if(e&&e->runtime)JS_RunGC(e->runtime);}
static const TurboJSEmbedAPI api={sizeof(TurboJSEmbedAPI),TURBOJS_EMBED_API_VERSION,TURBOJS_EMBED_ABI_VERSION,embed_create,embed_destroy,embed_eval_i64,embed_last_error,embed_gc};
const TurboJSEmbedAPI *TurboJS_GetEmbedAPI(uint32_t abi_version){return abi_version==TURBOJS_EMBED_ABI_VERSION?&api:NULL;}
