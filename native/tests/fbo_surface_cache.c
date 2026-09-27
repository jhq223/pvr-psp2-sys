#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_BOOL int
#define IMG_UINT32 unsigned
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_NULL NULL
#define PVRSRV_OK 0
#define GL_TEXTURE 1
#define GL_RENDERBUFFER 2
#define GL_FRAMEBUFFER_COMPLETE 3
#define GLES2_FRAMEBUFFER_STATUS_UNKNOWN 4
#define GLES2_COLOR_ATTACHMENT 0
#define GLES2_DEPTH_ATTACHMENT 1
#define GLES2_STENCIL_ATTACHMENT 2
#define GLES2_MAX_ATTACHMENTS 3
#define GLES2_NAMETYPE_FRAMEBUFFER 0
#define GLES2_DEFAULT_NAMES_ARRAY_SIZE 1
#define GLES2_EXTENSION_EGL_IMAGE
#define GLES2_EXTENSION_TEXTURE_STREAM
#define GLES2_SCHEDULE_HW_LAST_IN_SCENE 1
#define GLES2_SCHEDULE_HW_WAIT_FOR_3D 2
#define PVR_DPF(x) ((void)0)
typedef struct { int busy; } PVRSRV_CLIENT_SYNC_INFO;
typedef struct { PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo; unsigned pixels[8]; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { unsigned *pui32ReadOffset, ui32CommittedHWOffsetInBytes; } CircularBuffer;
typedef struct EGLRenderSurfaceTAG {
    void *hEGLSurface;
    int bInFrame, bInExternalFrame, bDepthStencilBits;
    unsigned ui32FBOAttachmentCount;
    PVRSRV_CLIENT_SYNC_INFO *psSyncInfo, *psRenderSurfaceSyncInfo;
    CircularBuffer sPDSBuffer, sUSSEBuffer;
    unsigned pds, usse;
} EGLRenderSurface;
typedef struct GLES2NamedItemTAG { int bGeneratedButUnused; struct GLES2NamedItemTAG *psNext; } GLES2NamedItem;
typedef struct GLES2FrameBufferAttachableRec {
    EGLRenderSurface *psRenderSurface;
    struct GLES2FrameBufferAttachableRec *psSurfacePrev, *psSurfaceNext;
    int bSurfaceCached, bGhosted, eAttachmentType;
} GLES2FrameBufferAttachable;
typedef struct { int needed; } KRMResource;
typedef struct {
    PVRSRV_CLIENT_MEM_INFO *psMemInfo;
    unsigned ui32ValidationPins, ui32NumRenderTargets;
    int cpu_busy;
    void *psEGLImageSource, *psEGLImageTarget, *psBufferDevice;
    KRMResource sResource;
} GLES2Texture;
typedef struct { GLES2FrameBufferAttachable a; GLES2Texture *psTex; } GLES2MipMapLevel;
typedef struct {
    GLES2FrameBufferAttachable a;
    PVRSRV_CLIENT_MEM_INFO *psMemInfo;
    void *psEGLImageSource, *psEGLImageTarget;
} GLES2RenderBuffer;
typedef struct { EGLRenderSurface *psRenderSurface; PVRSRV_CLIENT_SYNC_INFO *psSyncInfo; } Params;
typedef struct {
    GLES2NamedItem sNamedItem;
    int eStatus;
    Params sDrawParams, sReadParams;
    GLES2FrameBufferAttachable *apsAttachment[3];
} GLES2FrameBuffer;
typedef struct { GLES2NamedItem *apsEntry[1]; } GLES2NamesArray;
typedef struct { int sKRM; } Manager;
typedef struct {
    unsigned ui32RefCount, ui32SurfaceCacheCount;
    int hPrimaryLock, sUSEShaderVariantKRM;
    GLES2FrameBufferAttachable *psSurfaceCacheHead, *psSurfaceCacheTail;
    Manager *psTextureManager;
    GLES2NamesArray *apsNamesArray[1];
} GLES2ContextSharedState;
typedef struct {
    GLES2ContextSharedState *psSharedState;
    EGLRenderSurface *psRenderSurface;
    void *psSysContext, *ps3DDevData;
    struct { GLES2FrameBuffer *psActiveFrameBuffer; } sFrameBuffer;
} GLES2Context;
static int locked, destroyed, queries, live;
static void PVRSRVLockMutex(int lock) { assert(!locked); locked=1; }
static void PVRSRVUnlockMutex(int lock) { assert(locked); locked=0; }
static int SWTextureBusy(GLES2Context *gc, GLES2Texture *t) { return t->cpu_busy; }
static int KRM_IsResourceNeeded(int *manager, KRMResource *r) { assert(locked); return r->needed; }
static int SGX2DQueryBlitsComplete(void *device, PVRSRV_CLIENT_SYNC_INFO *sync, int wait) {
    assert(sync && !wait); ++queries; return sync->busy;
}
static int FlushAttachableIfNeeded(GLES2Context *gc, GLES2FrameBufferAttachable *a, unsigned flags) {
    assert(!a->psRenderSurface->bInFrame && !a->psRenderSurface->psSyncInfo->busy); return 1;
}
static int KEGLDestroyRenderSurface(void *context, EGLRenderSurface *s) {
    assert(!s->psRenderSurfaceSyncInfo->busy); free(s->psRenderSurfaceSyncInfo); ++destroyed; return 1;
}
static void KRM_RemoveAttachmentPointReferences(int *manager, EGLRenderSurface *s) {}
static void GLES2Free(void *context, void *p) { free(p); --live; }
static void DestroyFBOAttachableRenderSurface(GLES2Context *gc, GLES2FrameBufferAttachable *a);
#include "fbo_surface_cache.h"
#include "fbo_surface_functions.inc"

typedef struct { GLES2MipMapLevel level; GLES2Texture texture; PVRSRV_CLIENT_MEM_INFO memory; GLES2FrameBuffer fb; } Fixture;
static void make(GLES2Context *gc, Fixture *f, unsigned value) {
    memset(f,0,sizeof(*f));
    for(unsigned i=0;i<8;++i) f->memory.pixels[i]=value;
    f->level.psTex=&f->texture; f->level.a.eAttachmentType=GL_TEXTURE;
    f->texture.psMemInfo=&f->memory; f->texture.ui32NumRenderTargets=1;
    EGLRenderSurface *s=calloc(1,sizeof(*s)); assert(s); ++live;
    /* StartFrame sets this even for offscreen surfaces. Finishing their GPU
     * work clears bInFrame, but does not clear bInExternalFrame. */
    s->bInExternalFrame=1;
    s->ui32FBOAttachmentCount=1;
    s->psRenderSurfaceSyncInfo=calloc(1,sizeof(*s->psRenderSurfaceSyncInfo)); assert(s->psRenderSurfaceSyncInfo);
    s->psSyncInfo=s->psRenderSurfaceSyncInfo;
    s->sPDSBuffer.pui32ReadOffset=&s->pds; s->sUSSEBuffer.pui32ReadOffset=&s->usse;
    f->level.a.psRenderSurface=s;
    f->fb.eStatus=GL_FRAMEBUFFER_COMPLETE; f->fb.apsAttachment[0]=&f->level.a;
    f->fb.sDrawParams=(Params){s,s->psSyncInfo}; f->fb.sReadParams=f->fb.sDrawParams;
    GLES2NamesArray *names=gc->psSharedState->apsNamesArray[0];
    f->fb.sNamedItem.psNext=names->apsEntry[0]; names->apsEntry[0]=&f->fb.sNamedItem;
    TouchFBOSurface(gc,&f->fb);
}
static void list(GLES2Context *gc, unsigned count) {
    unsigned n=0; GLES2FrameBufferAttachable *prev=NULL;
    for(GLES2FrameBufferAttachable *a=gc->psSharedState->psSurfaceCacheHead;a;a=a->psSurfaceNext) {
        assert(a->bSurfaceCached && a->psSurfacePrev==prev && a->psRenderSurface);
        prev=a; assert(++n<=count);
    }
    assert(n==count && gc->psSharedState->ui32SurfaceCacheCount==count);
    assert(prev==gc->psSharedState->psSurfaceCacheTail);
}
int main(void) {
    Manager manager={0}; GLES2NamesArray names={0}; GLES2FrameBuffer active={0};
    GLES2ContextSharedState shared={.ui32RefCount=1,.psTextureManager=&manager,.apsNamesArray={&names}};
    GLES2Context gc={.psSharedState=&shared,.sFrameBuffer={&active}};
    Fixture f[32];
    for(unsigned i=0;i<16;++i) make(&gc,&f[i],i+1);
    list(&gc,16);
    TouchFBOSurface(&gc,&f[0].fb); TouchFBOSurface(&gc,&f[0].fb); list(&gc,16);
    /* Two different FBO names cache the same attachable/surface. Both invalidate. */
    GLES2FrameBuffer alias=f[1].fb;
    alias.sNamedItem.psNext=names.apsEntry[0]; names.apsEntry[0]=&alias.sNamedItem;
    /* An unused generated name has no full framebuffer allocation behind it. */
    GLES2NamedItem unused={1,names.apsEntry[0]}; names.apsEntry[0]=&unused;
    TrimIdleFBOSurfaces(&gc); list(&gc,15);
    assert(!f[1].level.a.psRenderSurface && f[0].level.a.psRenderSurface);
    assert(f[1].fb.eStatus==GLES2_FRAMEBUFFER_STATUS_UNKNOWN && alias.eStatus==GLES2_FRAMEBUFFER_STATUS_UNKNOWN);
    assert(!alias.sReadParams.psSyncInfo && !alias.sDrawParams.psRenderSurface);
    assert(!f[1].texture.ui32NumRenderTargets && f[1].texture.psMemInfo==&f[1].memory);
    for(unsigned i=0;i<8;++i) assert(f[1].memory.pixels[i]==2);
    /* Continuous rotations retain all GL objects and pixels while bounding surfaces. */
    for(unsigned i=16;i<32;++i) { make(&gc,&f[i],i+1); TrimIdleFBOSurfaces(&gc); list(&gc,15); }
    assert(live==15 && destroyed==17);
    for(unsigned i=0;i<32;++i) for(unsigned p=0;p<8;++p) assert(f[i].memory.pixels[p]==i+1);
    for(unsigned i=0;i<32;++i) DestroyFBOAttachableRenderSurface(&gc,&f[i].level.a);
    list(&gc,0); assert(live==0); names.apsEntry[0]=NULL;
    /* Every exclusion must preserve both the surface and all cached aliases. */
    for(unsigned i=0;i<16;++i) make(&gc,&f[i],i+1);
    shared.ui32RefCount=2;
    int before=queries; TrimIdleFBOSurfaces(&gc); assert(queries==before); list(&gc,16);
    shared.ui32RefCount=1;
    for(unsigned i=1;i<16;++i) f[i].level.a.psRenderSurface->bInFrame=1;
    GLES2FrameBufferAttachable *a=&f[0].level.a; EGLRenderSurface *s=a->psRenderSurface;
#define BLOCK(set,clear) do { set; TrimIdleFBOSurfaces(&gc); list(&gc,16); assert(a->psRenderSurface==s && f[0].fb.eStatus==GL_FRAMEBUFFER_COMPLETE); clear; } while(0)
    BLOCK(gc.psRenderSurface=s,gc.psRenderSurface=NULL);
    BLOCK(active.apsAttachment[2]=a,active.apsAttachment[2]=NULL);
    BLOCK(a->bGhosted=1,a->bGhosted=0);
    BLOCK(s->bInFrame=1,s->bInFrame=0);
    BLOCK(s->hEGLSurface=s,s->hEGLSurface=NULL);
    BLOCK(s->ui32FBOAttachmentCount=2,s->ui32FBOAttachmentCount=1);
    BLOCK(s->bDepthStencilBits=1,s->bDepthStencilBits=0);
    BLOCK(f[0].texture.ui32ValidationPins=1,f[0].texture.ui32ValidationPins=0);
    BLOCK(f[0].texture.cpu_busy=1,f[0].texture.cpu_busy=0);
    BLOCK(f[0].texture.sResource.needed=1,f[0].texture.sResource.needed=0);
    BLOCK(f[0].texture.psEGLImageSource=s,f[0].texture.psEGLImageSource=NULL);
    BLOCK(f[0].texture.psEGLImageTarget=s,f[0].texture.psEGLImageTarget=NULL);
    BLOCK(f[0].texture.psBufferDevice=s,f[0].texture.psBufferDevice=NULL);
    BLOCK(s->psSyncInfo->busy=1,s->psSyncInfo->busy=0);
    BLOCK(s->sPDSBuffer.ui32CommittedHWOffsetInBytes=4,s->sPDSBuffer.ui32CommittedHWOffsetInBytes=0);
    BLOCK(s->sUSSEBuffer.ui32CommittedHWOffsetInBytes=4,s->sUSSEBuffer.ui32CommittedHWOffsetInBytes=0);
    PVRSRV_CLIENT_SYNC_INFO transfer={1};
    BLOCK(f[0].memory.psClientSyncInfo=&transfer,f[0].memory.psClientSyncInfo=NULL);
    s->psSyncInfo=&transfer; transfer.busy=0;
    BLOCK(s->psRenderSurfaceSyncInfo->busy=1,s->psRenderSurfaceSyncInfo->busy=0);
    s->psSyncInfo=s->psRenderSurfaceSyncInfo;
    TrimIdleFBOSurfaces(&gc); list(&gc,15); assert(!a->psRenderSurface);
    for(unsigned i=1;i<16;++i) { f[i].level.a.psRenderSurface->bInFrame=0; DestroyFBOAttachableRenderSurface(&gc,&f[i].level.a); }
    list(&gc,0); assert(live==0 && !locked); names.apsEntry[0]=NULL;
    /* Work surfaces use renderbuffers. The cache must retire their driver
     * surface without deleting the renderbuffer storage. */
    for(unsigned i=0;i<16;++i) make(&gc,&f[i],i+1);
    GLES2RenderBuffer rb={0};
    rb.a.psRenderSurface=f[0].level.a.psRenderSurface;
    rb.a.eAttachmentType=GL_RENDERBUFFER; rb.psMemInfo=&f[0].memory;
    ForgetFBOSurface(&gc,&f[0].level.a);
    f[0].level.a.psRenderSurface=NULL;
    f[0].fb.apsAttachment[0]=&rb.a;
    TouchFBOSurface(&gc,&f[0].fb);
    for(unsigned i=1;i<16;++i) f[i].level.a.psRenderSurface->bInFrame=1;
    rb.psEGLImageSource=&gc; TrimIdleFBOSurfaces(&gc); list(&gc,16);
    rb.psEGLImageSource=NULL;
    rb.psEGLImageTarget=&gc; TrimIdleFBOSurfaces(&gc); list(&gc,16);
    rb.psEGLImageTarget=NULL;
    TrimIdleFBOSurfaces(&gc); list(&gc,15);
    assert(!rb.a.psRenderSurface && rb.psMemInfo==&f[0].memory && rb.psMemInfo->pixels[0]==1);
    for(unsigned i=1;i<16;++i) { f[i].level.a.psRenderSurface->bInFrame=0; DestroyFBOAttachableRenderSurface(&gc,&f[i].level.a); }
    list(&gc,0); assert(live==0 && !locked);
    puts("FBO surfaces: bounded rotation, LRU, shared aliases, retained pixels, busy/pinned/external exclusions and deletion passed");
}
