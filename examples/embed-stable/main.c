#include <turbojs_embed.h>
#include <stdio.h>
#include <string.h>
int main(void){const TurboJSEmbedAPI *api=TurboJS_GetEmbedAPI(TURBOJS_EMBED_ABI_VERSION);TurboJSEmbedConfig config={sizeof(config),TURBOJS_EMBED_API_VERSION,16u*1024u*1024u,1024u*1024u};TurboJSEngine *engine;int64_t value=0;const char *source="({x: 40, y: 2}).x + 2";if(!api||api->struct_size<sizeof(*api))return 1;engine=api->create(&config);if(!engine)return 2;if(api->eval_i64(engine,source,strlen(source),&value)!=TURBOJS_EMBED_OK){fprintf(stderr,"%s\n",api->last_error(engine));api->destroy(engine);return 3;}api->destroy(engine);printf("%lld\n",(long long)value);return value==42?0:4;}
