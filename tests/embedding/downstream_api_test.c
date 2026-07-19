#include <turbojs_embed.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
int main(void){const TurboJSEmbedAPI*a=TurboJS_GetEmbedAPI(TURBOJS_EMBED_ABI_VERSION);TurboJSEmbedConfig c={sizeof(c),TURBOJS_EMBED_API_VERSION,32u*1024u*1024u,2u*1024u*1024u};TurboJSEngine*e;int64_t r=0;CHECK(a&&a->struct_size>=sizeof(*a));CHECK(!TurboJS_GetEmbedAPI(999));e=a->create(&c);CHECK(e);CHECK(a->eval_i64(e,"let s=0; for(let i=0;i<10000;i++) s+=i; s",strlen("let s=0; for(let i=0;i<10000;i++) s+=i; s"),&r)==TURBOJS_EMBED_OK);CHECK(r==49995000);CHECK(a->eval_i64(e,"throw new Error('embed-contract')",strlen("throw new Error('embed-contract')"),&r)==TURBOJS_EMBED_EXCEPTION);CHECK(strstr(a->last_error(e),"embed-contract")!=NULL);a->collect_garbage(e);a->destroy(e);puts("downstream stable embed API passed");return 0;}
