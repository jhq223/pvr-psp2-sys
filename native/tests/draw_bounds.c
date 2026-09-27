#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#define IMG_VOID void
#define GLvoid void
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_NULL NULL
#define GL_API_EXT
#define GL_APIENTRY
#define GL_POINTS 0
#define GL_LINES 1
#define GL_LINE_LOOP 2
#define GL_LINE_STRIP 3
#define GL_TRIANGLES 4
#define GL_TRIANGLE_FAN 6
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FRONT_AND_BACK 0x408
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_INVALID_ENUM 0x500
#define GL_INVALID_VALUE 0x501
#define GL_INVALID_OPERATION 0x502
#define GL_OUT_OF_MEMORY 0x505
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x506
#define GLES2_CULLFACE_ENABLE 1
#define EURASIA_MTE_SIZE 1
#define GLES2_NO_ERROR 0
#define ATTRIBARRAY_BAD_BUFOBJ 1
#define ATTRIBARRAY_MAP_BUFOBJ 2
#define ATTRIBARRAY_SOURCE_VARRAY 4
#define GLES_ASSERT(x) assert(x)
#define PVR_DPF(x) ((void)0)
#define GLES2_TIME_START(x) ((void)0)
#define GLES2_TIME_STOP(x) ((void)0)
#define GLES2_PROFILE_INCREMENT_DRAWARRAYS_CALLCOUNT(x) ((void)0)
#define GLES2_PROFILE_INCREMENT_DRAWARRAYS_VERTEXCOUNT(x,y) ((void)0)
#define GLES2_PROFILE_INCREMENT_DRAWELEMENTS_CALLCOUNT(x) ((void)0)
#define GLES2_PROFILE_INCREMENT_DRAWELEMENTS_VERTEXCOUNT(x,y) ((void)0)
#define GLES2_PROFILE_ADD_STATE_METRIC ((void)0)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef uint8_t IMG_UINT8;
typedef uint16_t IMG_UINT16;
typedef uint32_t IMG_UINT32;
typedef int IMG_BOOL, IMG_INT32, GLint, GLsizei;
typedef unsigned GLenum;
typedef uintptr_t IMG_UINTPTR_T;
typedef intptr_t GLintptr;
typedef struct { void *pvLinAddr; unsigned uAllocSize; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { PVRSRV_CLIENT_MEM_INFO *psMemInfo; unsigned ui32BufferSize, ui32RangeCount, ui32RangeType, ui32RangeMin, ui32RangeMax; uintptr_t uiRangeOffset; int bMapped, bRangeCached; } GLES2BufferObject;
typedef struct { unsigned ui32DirtyState; } VAOState;
typedef struct { GLES2BufferObject *psBoundElementBuffer; VAOState *psActiveVAO; unsigned ui32ControlWord; } GLES2VertexArrayObjectMachine;
typedef struct { int bSuccessfulLink; unsigned ui32OutputSelects; } Program;
typedef struct { int hMutex, bPrimitivesSinceLastTA; } Surface;
typedef struct { GLES2VertexArrayObjectMachine sVAOMachine; struct { Program *psCurrentProgram; } sProgram; struct { struct { GLenum eCullMode; } sPolygon; } sState; unsigned ui32Enables, ui32DirtyState, ui32IndexScratchCapacity; IMG_UINT16 *pui16IndexScratch; Surface *psRenderSurface; void *apsBuffers; } GLES2Context;
typedef void (*PFNMultiDrawVArray)(GLES2Context *, GLenum, unsigned *, unsigned *, unsigned, GLenum, const void **, unsigned, unsigned, unsigned);
static GLES2Context ctx;
#define __GLES2_GET_CONTEXT() GLES2Context *gc = &ctx
#define VAO_IS_ZERO(gc) 1
#define INDEX_BUFFER_OBJECT(gc) ((gc)->sVAOMachine.psBoundElementBuffer)
#define GLES2_BUFFER_OFFSET(p) ((GLintptr)(p))
static const int primDirectIndex[7]={1,1,0,0,1,1,1};
static unsigned live, attempts, fail_at, draws, locks, unlocks;
static int error, prepare_ok=1, validation_ok=1, attach_ok=1;
static void SetError(GLES2Context *g,int e) { error=e; }
static void *GLES2Malloc(GLES2Context *g,size_t n) { if(++attempts==fail_at) return NULL; void *p=malloc(n); assert(p); ++live; return p; }
static void *GLES2Realloc(GLES2Context *g,void *p,size_t n) { assert(!p); return GLES2Malloc(g,n); }
static void GLES2Free(void *g,void *p) { if(p) { assert(live); --live; free(p); } }
static unsigned GetNumIndices(GLenum m,unsigned c) { if(m==GL_TRIANGLES) return c-c%3; if(m==GL_LINES) return c-c%2; if(m==GL_LINE_STRIP) return c>1?(c-1)*2:0; if(m==GL_LINE_LOOP) return c>1?c*2:0; return c; }
static int GetFrameBufferCompleteness(GLES2Context *g) { return GL_FRAMEBUFFER_COMPLETE; }
static int PrepareToDraw(GLES2Context *g,unsigned *c,int b) { if(prepare_ok) ++locks; return prepare_ok; }
static int ValidateState(GLES2Context *g) { return validation_ok?0:1; }
static int AttachAllUsedResourcesToCurrentSurface(GLES2Context *g) { if(!attach_ok) SetError(g,GL_OUT_OF_MEMORY); return attach_ok; }
static void PVRSRVUnlockMutex(int h) { assert(unlocks<locks); ++unlocks; }
static void draw(GLES2Context *g,GLenum m,unsigned *first,unsigned *counts,unsigned total,GLenum type,const void **elems,unsigned start,unsigned count,unsigned prims) {
    ++draws; assert(locks==unlocks+1);
    if(elems) for(unsigned i=0;i<prims;++i) { assert(counts[i]==3); if(type==GL_UNSIGNED_INT) assert(((const uint32_t *)elems[i])[2]==3); else assert(((const uint16_t *)elems[i])[2]==3); }
}
static void MultiDrawElementsIndexBO(GLES2Context *g,GLenum m,unsigned *f,unsigned *c,unsigned t,GLenum ty,const void **e,unsigned s,unsigned n,unsigned p) { assert(0); }
static PFNMultiDrawVArray PickMultiDrawArraysProc(GLES2Context *g,GLenum m,unsigned n) { return draw; }
static PFNMultiDrawVArray PickMultiDrawElementsProc(GLES2Context *g,GLenum m,GLenum t,unsigned n,unsigned c,unsigned max) { return draw; }
#define CBUF_UpdateVIBufferCommittedPrimOffsets(a,b,c,d) ((void)0)
#include "draw_bounds_functions.inc"
static void reset(void) { assert(!live && locks==unlocks); error=0; attempts=fail_at=draws=0; }
int main(void) {
    Program program={1,1}; Surface surface={0}; VAOState vao={0};
    ctx.sProgram.psCurrentProgram=&program; ctx.psRenderSurface=&surface; ctx.sVAOMachine.psActiveVAO=&vao;
    uint8_t b[]={1,2,3}; uint16_t s[]={1,2,3}; uint32_t u[]={1,2,3};
    GLsizei counts[]={3,3,3}; const void *indices[]={s,s,s};
    glMultiDrawElementsEXT(GL_TRIANGLES,counts,GL_UNSIGNED_SHORT,indices,3); assert(draws==1); reset();
    for(unsigned i=0;i<3;++i) indices[i]=u;
    glMultiDrawElementsEXT(GL_TRIANGLES,counts,GL_UNSIGNED_INT,indices,3); assert(draws==1); reset();
    for(unsigned i=0;i<3;++i) indices[i]=b;
    /* Three list allocations, then one promotion for each nonempty primitive. */
    for(unsigned n=1;n<=6;++n) { fail_at=n; glMultiDrawElementsEXT(GL_TRIANGLES,counts,GL_UNSIGNED_BYTE,indices,3); assert(!draws && error==GL_OUT_OF_MEMORY); reset(); }
    glMultiDrawElementsEXT(GL_TRIANGLES,counts,GL_UNSIGNED_BYTE,indices,3); assert(draws==1); reset();
    counts[1]=-1; glMultiDrawElementsEXT(GL_TRIANGLES,counts,GL_UNSIGNED_BYTE,indices,3); assert(error==GL_INVALID_VALUE); reset(); counts[1]=3;
    attach_ok=0; glMultiDrawElementsEXT(GL_TRIANGLES,counts,GL_UNSIGNED_BYTE,indices,3); assert(!draws && error==GL_OUT_OF_MEMORY); reset(); attach_ok=1;
    GLint first[]={0,0,0}; ctx.ui32DirtyState=1; validation_ok=0;
    glMultiDrawArraysEXT(GL_TRIANGLES,first,counts,3); assert(!draws); reset(); validation_ok=1;
    first[0]=INT_MAX; glMultiDrawArraysEXT(GL_TRIANGLES,first,counts,3); assert(error==GL_INVALID_VALUE); reset(); first[0]=0;
    glMultiDrawArraysEXT(UINT_MAX,first,counts,3); assert(error==GL_INVALID_ENUM); reset();
    unsigned total=0x3fffffff; assert(!AddMultiDrawCount(GL_POINTS,1,&total));
    total=0; assert(!AddMultiDrawCount(GL_LINE_LOOP,0x3fffffff,&total));
    PVRSRV_CLIENT_MEM_INFO mem={s,sizeof(s)}; GLES2BufferObject buffer={0}; buffer.psMemInfo=&mem; buffer.ui32BufferSize=sizeof(s); ctx.sVAOMachine.psBoundElementBuffer=&buffer;
    unsigned min,max; assert(DetermineMinAndMaxIndices(&ctx,3,GL_UNSIGNED_SHORT,NULL,&min,&max) && min==1 && max==3);
    assert(!DetermineMinAndMaxIndices(&ctx,3,GL_UNSIGNED_SHORT,(void *)2,&min,&max));
    assert(!ValidateIndexBufferRange(&ctx,1,GL_UNSIGNED_BYTE,(void *)(uintptr_t)UINT_MAX));
    buffer.bMapped=1; assert(!ValidateIndexBufferRange(&ctx,1,GL_UNSIGNED_BYTE,NULL));
    buffer.bMapped=0; buffer.psMemInfo=NULL; assert(!ValidateIndexBufferRange(&ctx,1,GL_UNSIGNED_BYTE,NULL));
    assert(!live && locks==unlocks);
    puts("draw bounds: client pointers, six allocation failures, lock cleanup and index ranges passed");
}
