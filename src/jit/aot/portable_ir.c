#include <stdlib.h>
#include <string.h>

#include "jit.h"

#define HEADER_SIZE 32u
#define INSTRUCTION_SIZE 32u

static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { unsigned i; for(i=0;i<4;i++) p[i]=(uint8_t)(v>>(i*8)); }
static void put64(uint8_t *p, uint64_t v) { unsigned i; for(i=0;i<8;i++) p[i]=(uint8_t)(v>>(i*8)); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t get64(const uint8_t *p) { uint64_t v=0; unsigned i; for(i=0;i<8;i++) v|=(uint64_t)p[i]<<(i*8); return v; }
static uint32_t checksum(const uint8_t *p,size_t n){uint32_t h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}
static TurboJSIRStatus fail(TurboJSIRDiagnostic*d,TurboJSIRStatus s,size_t i,const char*m){if(d){d->status=s;d->instruction_index=i;d->message=m;}return s;}

void TurboJS_AOTBufferDestroy(TurboJSAOTBuffer *b){if(!b)return;free(b->data);b->data=NULL;b->size=0;}

TurboJSIRStatus TurboJS_AOTSerializeIR(const TurboJSIRFunction *f,TurboJSAOTBuffer *out,TurboJSIRDiagnostic *d){
    size_t i,total;uint8_t*p;TurboJSIRStatus st;
    if(!f||!out)return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid AOT serialization arguments");
    out->data=NULL;out->size=0;st=TurboJS_IRVerify(f,d);if(st!=TURBOJS_IR_OK)return st;
    if(f->instruction_count>(SIZE_MAX-HEADER_SIZE)/INSTRUCTION_SIZE)return fail(d,TURBOJS_IR_OUT_OF_MEMORY,0,"AOT image too large");
    total=HEADER_SIZE+f->instruction_count*INSTRUCTION_SIZE;p=(uint8_t*)calloc(1,total);if(!p)return fail(d,TURBOJS_IR_OUT_OF_MEMORY,0,"AOT allocation failed");
    memcpy(p,"TJIR",4);put16(p+4,TURBOJS_AOT_FORMAT_VERSION);put16(p+6,HEADER_SIZE);put32(p+8,(uint32_t)f->instruction_count);
    put16(p+12,f->register_count);put16(p+14,f->argument_count);put16(p+16,f->local_count);put16(p+18,INSTRUCTION_SIZE);
    for(i=0;i<f->instruction_count;i++){const TurboJSIRInstruction*in=&f->instructions[i];uint8_t*q=p+HEADER_SIZE+i*INSTRUCTION_SIZE;
        put16(q,(uint16_t)in->opcode);put16(q+2,in->destination);put16(q+4,in->left);put16(q+6,in->right);put64(q+8,(uint64_t)in->immediate);put32(q+16,in->target);put32(q+20,in->bytecode_offset);
    }
    put32(p+20,checksum(p+HEADER_SIZE,total-HEADER_SIZE));put64(p+24,(uint64_t)total);out->data=p;out->size=total;
    if(d){d->status=TURBOJS_IR_OK;d->instruction_index=0;d->message="ok";}return TURBOJS_IR_OK;
}

TurboJSIRStatus TurboJS_AOTDeserializeIR(const uint8_t *data,size_t size,TurboJSIRFunction*out,TurboJSIRDiagnostic*d){
    uint32_t count,stored;uint16_t hs,is;uint64_t declared;size_t i,expected;TurboJSIRStatus st;
    if(!data||!out)return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid AOT deserialization arguments");
    if(size<HEADER_SIZE||memcmp(data,"TJIR",4)!=0)return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid AOT magic");
    if(get16(data+4)!=TURBOJS_AOT_FORMAT_VERSION)return fail(d,TURBOJS_IR_UNSUPPORTED,0,"unsupported AOT version");
    hs=get16(data+6);count=get32(data+8);is=get16(data+18);declared=get64(data+24);
    if(hs!=HEADER_SIZE||is!=INSTRUCTION_SIZE||declared!=size)return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"invalid AOT layout");
#if SIZE_MAX < UINT32_MAX
    if ((size_t)count > (SIZE_MAX - HEADER_SIZE) / INSTRUCTION_SIZE) return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"AOT instruction table overflow");
#endif
    expected=HEADER_SIZE+(size_t)count*INSTRUCTION_SIZE;if(expected!=size)return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"truncated AOT image");
    stored=get32(data+20);if(stored!=checksum(data+HEADER_SIZE,size-HEADER_SIZE))return fail(d,TURBOJS_IR_INVALID_ARGUMENT,0,"AOT checksum mismatch");
    TurboJS_IRFunctionInit(out,get16(data+14));out->register_count=get16(data+12);out->local_count=get16(data+16);
    if(out->register_count>TURBOJS_IR_MAX_REGISTERS||out->local_count>TURBOJS_IR_MAX_REGISTERS){TurboJS_IRFunctionDestroy(out);return fail(d,TURBOJS_IR_INVALID_REGISTER,0,"AOT register/local limit exceeded");}
    for(i=0;i<count;i++){const uint8_t*q=data+HEADER_SIZE+i*INSTRUCTION_SIZE;TurboJSIRInstruction in;
        memset(&in,0,sizeof(in));in.opcode=(TurboJSIROpcode)get16(q);in.destination=get16(q+2);in.left=get16(q+4);in.right=get16(q+6);in.immediate=(int64_t)get64(q+8);in.target=get32(q+16);in.bytecode_offset=get32(q+20);
        st=TurboJS_IREmit(out,in);if(st!=TURBOJS_IR_OK){TurboJS_IRFunctionDestroy(out);return fail(d,st,i,"AOT instruction allocation failed");}
    }
    st=TurboJS_IRVerify(out,d);if(st!=TURBOJS_IR_OK)TurboJS_IRFunctionDestroy(out);return st;
}
