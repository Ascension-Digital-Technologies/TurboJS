#include <stdlib.h>
#include <string.h>
#include "jit.h"

typedef struct ShapeProperty { char *name; uint16_t offset; } ShapeProperty;
typedef struct ShapeTransition { uint32_t parent_id; uint64_t name_hash; char *name; struct TurboJSShape *child; } ShapeTransition;
struct TurboJSShape { uint32_t id; uint16_t property_count; ShapeProperty properties[TURBOJS_SHAPE_MAX_PROPERTIES]; };
struct TurboJSShapeTable { struct TurboJSShape **shapes; size_t shape_count,shape_capacity; ShapeTransition *transitions; size_t transition_count,transition_capacity; };

static uint64_t hash_name(const char *s){uint64_t h=1469598103934665603ULL;while(*s){h^=(unsigned char)*s++;h*=1099511628211ULL;}return h;}
static char *dup_name(const char*s){size_t n=strlen(s)+1;char*p=(char*)malloc(n);if(p)memcpy(p,s,n);return p;}
static int grow(void **p,size_t *cap,size_t item){size_t n=*cap?*cap*2u:16u;void*q;if(n<*cap||n>SIZE_MAX/item)return 0;q=realloc(*p,n*item);if(!q)return 0;*p=q;*cap=n;return 1;}
TurboJSShapeTable *TurboJS_ShapeTableCreate(void){TurboJSShapeTable*t=(TurboJSShapeTable*)calloc(1,sizeof(*t));struct TurboJSShape*r;if(!t)return NULL;r=(struct TurboJSShape*)calloc(1,sizeof(*r));if(!r){free(t);return NULL;}r->id=1;if(!grow((void**)&t->shapes,&t->shape_capacity,sizeof(*t->shapes))){free(r);free(t);return NULL;}t->shapes[t->shape_count++]=r;return t;}
void TurboJS_ShapeTableDestroy(TurboJSShapeTable*t){size_t i,j;if(!t)return;for(i=0;i<t->shape_count;i++){for(j=0;j<t->shapes[i]->property_count;j++)free(t->shapes[i]->properties[j].name);free(t->shapes[i]);}for(i=0;i<t->transition_count;i++)free(t->transitions[i].name);free(t->transitions);free(t->shapes);free(t);}
const TurboJSShape *TurboJS_ShapeRoot(const TurboJSShapeTable*t){return t&&t->shape_count?t->shapes[0]:NULL;}
uint32_t TurboJS_ShapeId(const TurboJSShape*s){return s?s->id:0;}
uint16_t TurboJS_ShapePropertyCount(const TurboJSShape*s){return s?s->property_count:0;}
int TurboJS_ShapeLookup(const TurboJSShape*s,const char*n,uint16_t*out){uint16_t i;if(!s||!n)return 0;for(i=0;i<s->property_count;i++)if(strcmp(s->properties[i].name,n)==0){if(out)*out=s->properties[i].offset;return 1;}return 0;}
const TurboJSShape *TurboJS_ShapeTransition(TurboJSShapeTable*t,const TurboJSShape*s,const char*n){size_t i;uint64_t h;struct TurboJSShape*c;if(!t||!s||!n||!*n||s->property_count>=TURBOJS_SHAPE_MAX_PROPERTIES)return NULL;h=hash_name(n);for(i=0;i<t->transition_count;i++)if(t->transitions[i].parent_id==s->id&&t->transitions[i].name_hash==h&&strcmp(t->transitions[i].name,n)==0)return t->transitions[i].child;c=(struct TurboJSShape*)calloc(1,sizeof(*c));if(!c)return NULL;c->id=(uint32_t)(t->shape_count+1u);c->property_count=s->property_count;for(i=0;i<s->property_count;i++){c->properties[i].name=dup_name(s->properties[i].name);if(!c->properties[i].name)goto fail;c->properties[i].offset=s->properties[i].offset;}c->properties[c->property_count].name=dup_name(n);if(!c->properties[c->property_count].name)goto fail;c->properties[c->property_count].offset=c->property_count;c->property_count++;if(t->shape_count==t->shape_capacity&&!grow((void**)&t->shapes,&t->shape_capacity,sizeof(*t->shapes)))goto fail;if(t->transition_count==t->transition_capacity&&!grow((void**)&t->transitions,&t->transition_capacity,sizeof(*t->transitions)))goto fail;t->shapes[t->shape_count++]=c;t->transitions[t->transition_count++]=(ShapeTransition){s->id,h,dup_name(n),c};if(!t->transitions[t->transition_count-1].name){t->transition_count--;t->shape_count--;goto fail;}return c;fail:for(i=0;i<c->property_count;i++)free(c->properties[i].name);free(c);return NULL;}
void TurboJS_PropertyInlineCacheInit(TurboJSPropertyInlineCache*c){if(c)memset(c,0,sizeof(*c));}
int TurboJS_PropertyInlineCacheLookup(TurboJSPropertyInlineCache*c,const TurboJSShape*s,const char*n,uint16_t*out){uint16_t off;if(!c||!s||!n)return 0;if(c->shape_id==s->id&&c->shape_id!=0){if(out)*out=c->property_offset;if(c->hits!=UINT16_MAX)c->hits++;return 1;}c->misses++;if(!TurboJS_ShapeLookup(s,n,&off))return 0;c->shape_id=s->id;c->property_offset=off;if(out)*out=off;return 1;}
