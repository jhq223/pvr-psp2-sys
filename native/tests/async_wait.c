#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "psp2/optimization.h"
typedef unsigned IMG_UINT32;
typedef uintptr_t IMG_UINTPTR_T;
typedef int IMG_BOOL, SceUID;
#define IMG_VOID void
#define IMG_FALSE 0
#define IMG_TRUE 1
#define SCE_NULL NULL
#define SCE_KERNEL_EVF_WAITMODE_OR 1
#define SW_NONE 0xffffffffU
#define SW_BUCKETS 64U
typedef struct { unsigned id; } GLES2Texture;
typedef struct { unsigned state; SceUID thread; GLES2Texture *texture;
    unsigned activePrev, activeNext, texturePrev, textureNext, bytes; } SWJob;
typedef struct { unsigned jobCount, active; SWJob *jobs; int done;
    unsigned activeHead, buckets[SW_BUCKETS], activeBytes; } SWTextureState;
typedef struct { SWTextureState *psSWTexture; } GLES2Context;
static unsigned locks, unlocks, depth, thread_queries, waits;
static SWTextureState *current;
static GLES2Texture *completed_texture;
static void UnlinkJob(SWTextureState *state, unsigned index);
static void Lock(SWTextureState *state) { assert(state && depth++ == 0); ++locks; }
static void Unlock(SWTextureState *state) { assert(state && depth-- == 1); ++unlocks; }
static int sceKernelGetThreadId(void) { ++thread_queries; return 42; }
static void sceKernelClearEventFlag(int event, unsigned mask) { assert(depth == 1); }
static void sceKernelWaitEventFlag(int event, unsigned bits, int mode, void *result, void *timeout) {
    assert(!depth); ++waits;
    for(unsigned i = 0; i < current->jobCount; ++i)
        if(current->jobs[i].state && current->jobs[i].texture == completed_texture) {
            UnlinkJob(current, i); current->jobs[i].state = 0; --current->active;
        }
}
#include "async_wait_functions.inc"
int main(void) {
    GLES2Texture a={1}, b={2}; GLES2Context gc={0};
    SWTextureWait(&gc,&a); assert(!SWTextureBusy(&gc,&a) && !thread_queries && !locks);
    SWJob *jobs=calloc(4096,sizeof(*jobs)); assert(jobs);
    SWTextureState state={.jobCount=4096,.jobs=jobs,.done=1,.activeHead=SW_NONE};
    for(unsigned i=0; i<SW_BUCKETS; ++i) state.buckets[i]=SW_NONE;
    gc.psSWTexture=current=&state;
    for(unsigned i=0;i<1000;++i) { SWTextureWait(&gc,&a); assert(!SWTextureBusy(&gc,&a)); }
    assert(thread_queries==0 && locks==2000 && unlocks==2000);
    jobs[0]=(SWJob){.state=1,.thread=7,.texture=&b,.bytes=17};
    jobs[4095]=(SWJob){.state=1,.thread=8,.texture=&a,.bytes=23};
    LinkJob(&state,0); LinkJob(&state,4095); state.active=2;
    assert(state.activeBytes==40 && SWTextureBusy(&gc,&a));
    completed_texture=&a; SWTextureWait(&gc,&a);
    assert(waits==1 && state.active==1 && state.activeBytes==17 && !SWTextureBusy(&gc,&a));
    assert(SWTextureBusy(&gc,&b)); jobs[0].thread=42;
    assert(!SWTextureBusy(&gc,&b)); SWTextureWait(&gc,&b); assert(waits==1);
    jobs[0].thread=7; completed_texture=&b; SWTextureWait(&gc,NULL);
    assert(waits==2 && !state.active && !state.activeBytes && state.activeHead==SW_NONE);
    unsigned queries=thread_queries; SWTextureWait(&gc,NULL); assert(!SWTextureBusy(&gc,NULL));
    assert(queries==thread_queries && locks==unlocks && !depth);
    /* Colliding resource buckets and out-of-order unlink must preserve both lists. */
    GLES2Texture textures[1024];
    for(unsigned i=0; i<1024; ++i) {
        jobs[i]=(SWJob){.state=1,.thread=7,.texture=&textures[i],.bytes=i+1}; LinkJob(&state,i); ++state.active;
    }
    for(unsigned i=0; i<1024; i+=2) { UnlinkJob(&state,i); jobs[i].state=0; --state.active; }
    for(unsigned i=0; i<1024; ++i) assert(HasJob(&state,&textures[i],42)==(int)(i%2));
    for(unsigned i=1; i<1024; i+=2) { UnlinkJob(&state,i); jobs[i].state=0; --state.active; }
    assert(!state.activeBytes && state.activeHead==SW_NONE);
    for(unsigned i=0; i<SW_BUCKETS; ++i) assert(state.buckets[i]==SW_NONE);
    free(jobs);
    puts("async wait: 1024 colliding resources, out-of-order retirement, byte accounting and worker reentry passed");
}
