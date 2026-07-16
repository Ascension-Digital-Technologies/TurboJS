#include "jit.h"
#include <stdlib.h>
#include <string.h>

#define TJM_HEADER 32u
#define TJM_ENTRY 24u
static void p16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}static void p32(uint8_t*p,uint32_t v){unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}static void p64(uint8_t*p,uint64_t v){unsigned i;for(i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint16_t g16(const uint8_t*p){return(uint16_t)(p[0]|((uint16_t)p[1]<<8));}static uint32_t g32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}static uint64_t g64(const uint8_t*p){uint64_t v=0;unsigned i;for(i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static uint32_t sum(const uint8_t*p,size_t n){uint32_t h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}
static TurboJSIRStatus bad(TurboJSIRDiagnostic*d,TurboJSIRStatus s,size_t i,const char*m){if(d){d->status=s;d->instruction_index=i;d->message=m;}return s;}

TurboJSIRStatus TurboJS_AOTSerializeModule(const TurboJSAOTModuleFunction *f,size_t n,TurboJSAOTBuffer*out,TurboJSIRDiagnostic*d){
    TurboJSAOTBuffer *images=NULL;size_t i,total,off;uint8_t*p;
    if (!f || !out || !n || n > TURBOJS_AOT_MAX_FUNCTIONS)
        return bad(d, TURBOJS_IR_INVALID_ARGUMENT, 0, "invalid AOT module arguments");
    out->data = NULL;
    out->size = 0;
    images=(TurboJSAOTBuffer*)calloc(n,sizeof(*images));if(!images)return bad(d,TURBOJS_IR_OUT_OF_MEMORY,0,"module image allocation failed");
    total=TJM_HEADER+n*TJM_ENTRY;
    for(i=0;i<n;i++){size_t nl;if(!f[i].name||!f[i].ir){bad(d,TURBOJS_IR_INVALID_ARGUMENT,i,"module function missing name or IR");goto fail;}nl=strlen(f[i].name);if(!nl||nl>UINT32_MAX){bad(d,TURBOJS_IR_INVALID_ARGUMENT,i,"invalid module function name");goto fail;}if(TurboJS_AOTSerializeIR(f[i].ir,&images[i],d)!=TURBOJS_IR_OK)goto fail;if(total>SIZE_MAX-nl-images[i].size){bad(d,TURBOJS_IR_OUT_OF_MEMORY,i,"module too large");goto fail;}total+=nl+images[i].size;}
    p=(uint8_t*)calloc(1,total);if(!p){bad(d,TURBOJS_IR_OUT_OF_MEMORY,0,"module allocation failed");goto fail;}
    memcpy(p,"TJM1",4);p16(p+4,TURBOJS_AOT_MODULE_VERSION);p16(p+6,TJM_HEADER);p32(p+8,(uint32_t)n);p64(p+16,(uint64_t)total);off=TJM_HEADER+n*TJM_ENTRY;
    for(i=0;i<n;i++){uint8_t*e=p+TJM_HEADER+i*TJM_ENTRY;size_t nl=strlen(f[i].name);p32(e,(uint32_t)off);p32(e+4,(uint32_t)nl);memcpy(p+off,f[i].name,nl);off+=nl;p64(e+8,(uint64_t)off);p64(e+16,(uint64_t)images[i].size);memcpy(p+off,images[i].data,images[i].size);off+=images[i].size;}
    p32(p+12,sum(p+TJM_HEADER,total-TJM_HEADER));for(i=0;i<n;i++)TurboJS_AOTBufferDestroy(&images[i]);free(images);out->data=p;out->size=total;if(d){d->status=TURBOJS_IR_OK;d->instruction_index=0;d->message="ok";}return TURBOJS_IR_OK;
fail:for(i=0;i<n;i++)TurboJS_AOTBufferDestroy(&images[i]);free(images);return d?d->status:TURBOJS_IR_INVALID_ARGUMENT;
}

void TurboJS_AOTModuleDestroy(TurboJSAOTModule*m){size_t i;if(!m)return;for(i=0;i<m->function_count;i++){free(m->functions[i].name);TurboJS_IRFunctionDestroy(&m->functions[i].ir);}free(m->functions);m->functions=NULL;m->function_count=0;}
TurboJSIRStatus TurboJS_AOTDeserializeModule(const uint8_t*data,size_t size,TurboJSAOTModule*out,TurboJSIRDiagnostic*d){size_t i;uint32_t n;if(!data||!out)return bad(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid module load arguments");memset(out,0,sizeof(*out));if(size<TJM_HEADER||memcmp(data,"TJM1",4)||g16(data+4)!=TURBOJS_AOT_MODULE_VERSION||g16(data+6)!=TJM_HEADER||g64(data+16)!=size)return bad(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid TJM header");n=g32(data+8);if(!n||n>TURBOJS_AOT_MAX_FUNCTIONS||TJM_HEADER+(size_t)n*TJM_ENTRY>size)return bad(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid TJM function table");if(g32(data+12)!=sum(data+TJM_HEADER,size-TJM_HEADER))return bad(d,TURBOJS_IR_INVALID_ARGUMENT,0,"TJM checksum mismatch");out->functions=(TurboJSAOTLoadedFunction*)calloc(n,sizeof(*out->functions));if(!out->functions)return bad(d,TURBOJS_IR_OUT_OF_MEMORY,0,"module function allocation failed");out->function_count=n;
    for(i=0;i<n;i++){const uint8_t*e=data+TJM_HEADER+i*TJM_ENTRY;uint32_t no=g32(e),nl=g32(e+4);uint64_t io=g64(e+8),is=g64(e+16);if(!nl||no>size||nl>size-no||io>size||is>size-io){bad(d,TURBOJS_IR_INVALID_ARGUMENT,i,"invalid TJM function entry");goto fail;}out->functions[i].name=(char*)malloc((size_t)nl+1);if(!out->functions[i].name){bad(d,TURBOJS_IR_OUT_OF_MEMORY,i,"module name allocation failed");goto fail;}memcpy(out->functions[i].name,data+no,nl);out->functions[i].name[nl]=0;if(TurboJS_AOTDeserializeIR(data+(size_t)io,(size_t)is,&out->functions[i].ir,d)!=TURBOJS_IR_OK)goto fail;}
    return TURBOJS_IR_OK;
fail:TurboJS_AOTModuleDestroy(out);return d?d->status:TURBOJS_IR_INVALID_ARGUMENT;
}
const TurboJSAOTLoadedFunction*TurboJS_AOTFindFunction(const TurboJSAOTModule*m,const char*n){size_t i;if(!m||!n)return NULL;for(i=0;i<m->function_count;i++)if(!strcmp(m->functions[i].name,n))return&m->functions[i];return NULL;}

TurboJSIRStatus TurboJS_AOTInspectModule(const uint8_t *data,
                                         size_t size,
                                         TurboJSAOTModuleInfo *out_info,
                                         TurboJSIRDiagnostic *diagnostic)
{
    uint32_t function_count;
    uint32_t checksum;
    if (!data || !out_info)
        return bad(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, 0,
                   "invalid module inspection arguments");
    memset(out_info, 0, sizeof(*out_info));
    if (size < TJM_HEADER || memcmp(data, "TJM1", 4) != 0)
        return bad(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, 0,
                   "invalid TJM module magic");
    if (g16(data + 4) != TURBOJS_AOT_MODULE_VERSION ||
        g16(data + 6) != TJM_HEADER || g64(data + 16) != size)
        return bad(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, 0,
                   "invalid TJM module header");
    function_count = g32(data + 8);
    if (!function_count || function_count > TURBOJS_AOT_MAX_FUNCTIONS ||
        TJM_HEADER + (size_t)function_count * TJM_ENTRY > size)
        return bad(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, 0,
                   "invalid TJM function table");
    checksum = g32(data + 12);
    if (checksum != sum(data + TJM_HEADER, size - TJM_HEADER))
        return bad(diagnostic, TURBOJS_IR_INVALID_ARGUMENT, 0,
                   "TJM checksum mismatch");
    out_info->version = g16(data + 4);
    out_info->function_count = function_count;
    out_info->image_size = size;
    out_info->checksum = checksum;
    return TURBOJS_IR_OK;
}
