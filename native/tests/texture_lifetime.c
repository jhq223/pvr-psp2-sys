#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define IMG_INTERNAL
#define IMG_VOID void
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define GLES_ASSERT(x) assert(x);
#define GLES2_EXTENSION_EGL_IMAGE 1
#define GLES2_EXTENSION_EGL_IMAGE_EXTERNAL 1
#define GLES2_MAX_TEXTURE_MIPMAP_LEVELS 2
#define GLES2_TEXTURE_TARGET_CEM 1
#define GLES2_TEX_UNKNOWN 0
#define GL_OUT_OF_MEMORY 1
#define PVR_DPF(x) ((void)0)
typedef unsigned IMG_UINT, IMG_UINT32;
typedef uintptr_t IMG_UINTPTR_T;
typedef int IMG_BOOL;
typedef struct { unsigned needed, ui32Waiters; } KRMResource;
typedef struct { unsigned uAllocSize; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { void *psRenderSurface; int bGhosted; } GLES2FrameBufferAttachable;
typedef struct { GLES2FrameBufferAttachable sFBAttachable; unsigned char *pui8Buffer; } GLES2MipMapLevel;
#define GLES2_LOADED_LEVEL ((unsigned char *)(uintptr_t)1)
typedef struct { PVRSRV_CLIENT_MEM_INFO *psMemInfo; void *hImage; unsigned ui32Stride, ui32Height; } EGLImage;
typedef struct GLES2GhostRec { struct GLES2TextureRec *psOwner; KRMResource sResource; PVRSRV_CLIENT_MEM_INFO *psMemInfo; unsigned ui32Size; void *hImage; } GLES2Ghost;
typedef struct GLES2TextureRec {
    unsigned name;
    GLES2Ghost sDeletionGhost;
    KRMResource sResource;
    unsigned ui32NumRenderTargets, ui32NumLevels, ui32TextureTarget, ui32LevelsConsistent;
    PVRSRV_CLIENT_MEM_INFO *psMemInfo;
    EGLImage *psEGLImageSource, *psEGLImageTarget;
    GLES2MipMapLevel *psMipLevel;
    void *psExtTexState;
    int bResidence, bHasEverBeenGhosted;
} GLES2Texture;
typedef struct { unsigned ui32GhostMem; int sKRM; } Manager;
typedef Manager GLES2TextureManager;
typedef struct { Manager *psTextureManager; int hSecondaryLock; } Shared;
typedef struct { Shared *psSharedState; } GLES2Context;
static KRMResource *pending;
static int allocation_failed, device_live, images_live, error, probe_reclaim, deferred_reclaims;
static void *GLES2Calloc(GLES2Context *gc, size_t n) { (void)gc; return allocation_failed ? NULL : calloc(1,n); }
static void GLES2Free(GLES2Context *gc, void *p) { (void)gc; free(p); }
static void GLES2FREEDEVICEMEM_HEAP(GLES2Context *gc, PVRSRV_CLIENT_MEM_INFO *p) { (void)gc; assert(p); --device_live; free(p); }
static void SetError(GLES2Context *gc, int e) { (void)gc; error=e; }
static void SWTextureWait(GLES2Context *gc, GLES2Texture *t) { (void)gc;(void)t; }
static int KRM_IsResourceNeeded(int *m, KRMResource *r) { (void)m; return r->needed; }
static void KRM_GhostResource(int *m, KRMResource *original, KRMResource *ghost) { (void)m; assert(!pending); ghost->needed=original->needed;original->needed=0;pending=ghost; }
static void KRM_RemoveResourceFromAllLists(int *m,KRMResource *r) { (void)m;assert(r!=pending); }
static void KEGLUnbindImage(void *h) { assert(h);--images_live; }
static void FlushUnflushedTextureRenders(GLES2Context *gc,GLES2Texture *t) { (void)gc;(void)t; }
static void DestroyFBOAttachableRenderSurface(GLES2Context *gc,GLES2FrameBufferAttachable *a) {
    (void)gc;(void)a;
    if(probe_reclaim && pending) { pending->needed=0; assert(pending->ui32Waiters==1); ++deferred_reclaims; }
}
static void GLES2FreeAsync(GLES2Context *gc,void *p) { GLES2Free(gc,p); }
static void TextureRemoveResident(GLES2Context *gc,GLES2Texture *t) { (void)gc;t->bResidence=0; }
static void PVRSRVLockMutex(int x) { (void)x; }
static void PVRSRVUnlockMutex(int x) { (void)x; }
#include "texture_lifetime_functions.inc"
static GLES2Texture *make_texture(int busy) {
    GLES2Texture *t=calloc(1,sizeof(*t));assert(t);
    t->psMipLevel=calloc(GLES2_MAX_TEXTURE_MIPMAP_LEVELS,sizeof(*t->psMipLevel));assert(t->psMipLevel);
    t->psMipLevel[0].pui8Buffer=malloc(8);assert(t->psMipLevel[0].pui8Buffer);
    t->psExtTexState=malloc(8);assert(t->psExtTexState);
    t->psMemInfo=malloc(sizeof(*t->psMemInfo));assert(t->psMemInfo);t->psMemInfo->uAllocSize=32;++device_live;
    t->sResource.needed=busy;t->bResidence=1;t->ui32NumRenderTargets=1;t->ui32NumLevels=1;
    return t;
}
static void collect(GLES2Context *gc) {
    KRMResource *r=pending;assert(r && !r->ui32Waiters);pending=NULL;r->needed=0;DestroyTextureGhostKRM(gc,r);
}
int main(void) {
    Manager manager={0};Shared shared={&manager,0};GLES2Context gc={&shared};
    GLES2Texture *t=make_texture(1);PVRSRV_CLIENT_MEM_INFO *old=t->psMemInfo;
    allocation_failed=1;assert(!TexMgrGhostTexture(&gc,t));assert(error==GL_OUT_OF_MEMORY);
    assert(t->psMemInfo==old && t->sResource.needed && t->bResidence && !pending);
    /* Deletion with allocation still failing transfers ownership without allocating. */
    probe_reclaim=1;FreeTexture(&gc,t);assert(pending && device_live==1 && deferred_reclaims==2);collect(&gc);assert(!device_live && !manager.ui32GhostMem);
    probe_reclaim=0;allocation_failed=0;t=make_texture(1);
    assert(TexMgrGhostTexture(&gc,t));assert(!t->psMemInfo && !t->sResource.needed && device_live==1);
    FreeTexture(&gc,t);collect(&gc);assert(!device_live);
    t=make_texture(0);FreeTexture(&gc,t);assert(!device_live && !pending);
    t=make_texture(1);GLES2FREEDEVICEMEM_HEAP(&gc,t->psMemInfo);t->psMemInfo=NULL;
    EGLImage image={NULL,(void *)1,8,4};t->psEGLImageTarget=&image;++images_live;
    allocation_failed=1;assert(!TexMgrGhostTexture(&gc,t));assert(t->psEGLImageTarget==&image && images_live==1);
    FreeTexture(&gc,t);assert(images_live==1 && pending);collect(&gc);assert(!images_live && !device_live && !manager.ui32GhostMem);
    puts("texture lifetime: allocation failure, embedded deletion, concurrent reclaim pin and EGL image ownership passed");
}
