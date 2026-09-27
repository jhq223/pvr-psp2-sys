#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define IMG_VOID void
#define IMG_NULL NULL
#define IMG_FALSE 0
#define PVRSRV_OK 0
#define PVRSRV_MEM_NO_SYNCOBJ 8U
#define GLES2_EXTENSION_EGL_IMAGE
#define GLES2_EXTENSION_TEXTURE_STREAM
#define GLES2_NAMETYPE_TEXOBJ 0
#define PVR_UNREFERENCED_PARAMETER(x) (void)(x)
typedef struct { int busy, fail_free; } PVRSRV_CLIENT_SYNC_INFO;
typedef struct { PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo; unsigned ui32Flags; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { int needed; } KRMResource;
typedef struct { int name; } GLES2NamedItem;
typedef struct {
    GLES2NamedItem name;
    PVRSRV_CLIENT_MEM_INFO *psMemInfo;
    unsigned ui32ValidationPins, ui32NumRenderTargets;
    void *psEGLImageSource, *psEGLImageTarget, *psBufferDevice;
    KRMResource sResource;
    int cpu_busy;
} GLES2Texture;
typedef struct { int sKRM; } Manager;
typedef struct { GLES2Texture *textures; unsigned count; } Names;
typedef struct { unsigned ui32RefCount; Manager *psTextureManager; Names *apsNamesArray[1]; } Shared;
typedef struct { Shared *psSharedState; void *ps3DDevData; } GLES2Context;
static int frees, queries, trims, locked;
static int SWTextureBusy(GLES2Context *gc, GLES2Texture *texture) { return texture->cpu_busy; }
static int KRM_IsResourceNeeded(int *mgr, KRMResource *resource) { assert(locked); return resource->needed; }
static int SGX2DQueryBlitsComplete(void *device, PVRSRV_CLIENT_SYNC_INFO *sync, int wait) {
    assert(!wait); ++queries; return sync->busy;
}
static int PVRSRVFreeSyncInfo(void *device, PVRSRV_CLIENT_SYNC_INFO *sync) {
    if(sync->fail_free) return -1;
    ++frees; free(sync); return 0;
}
static void SWTextureTrimSyncs(GLES2Context *gc) { assert(!locked); ++trims; }
static void NamesArrayMapFunction(GLES2Context *gc, Names *names,
    void (*callback)(GLES2Context *, const void *, GLES2NamedItem *), const void *context) {
    assert(!locked); locked=1;
    for(unsigned i=0;i<names->count;++i) callback(gc,context,&names->textures[i].name);
    locked=0;
}
#include "texture_sync_reclaim.h"
int main(void) {
    Manager manager={0}; GLES2Texture textures[11]={0}; PVRSRV_CLIENT_MEM_INFO memory[11]={0};
    Names names={textures,11}; Shared shared={1,&manager,{&names}}; GLES2Context gc={&shared,NULL};
    for(unsigned i=0;i<11;++i) {
        memory[i].psClientSyncInfo=calloc(1,sizeof(PVRSRV_CLIENT_SYNC_INFO));
        assert(memory[i].psClientSyncInfo); memory[i].ui32Flags=2;
        textures[i].psMemInfo=&memory[i];
    }
    textures[1].ui32ValidationPins=1;
    textures[2].ui32NumRenderTargets=1;
    textures[3].psEGLImageSource=&gc;
    textures[4].psEGLImageTarget=&gc;
    textures[5].cpu_busy=1;
    textures[6].sResource.needed=1; /* Includes work not submitted yet. */
    memory[7].psClientSyncInfo->busy=1;
    memory[8].psClientSyncInfo->fail_free=1;
    textures[9].psBufferDevice=&gc;
    textures[10].psMemInfo=NULL;
    ReclaimIdleTextureSyncs(&gc);
    assert(trims==1 && queries==3 && frees==1);
    assert(!memory[0].psClientSyncInfo && memory[0].ui32Flags==(2|PVRSRV_MEM_NO_SYNCOBJ));
    for(unsigned i=1;i<11;++i) assert(memory[i].psClientSyncInfo && memory[i].ui32Flags==2);
    /* Safe to call again after a texture has already lost its idle fence. */
    ReclaimIdleTextureSyncs(&gc); assert(frees==1);
    shared.ui32RefCount=2;
    int before=queries;
    ReclaimIdleTextureSyncs(&gc); assert(queries==before && frees==1);
    shared.ui32RefCount=1;
    for(unsigned i=1;i<11;++i) {
        textures[i]=(GLES2Texture){.psMemInfo=&memory[i]};
        memory[i].psClientSyncInfo->busy=memory[i].psClientSyncInfo->fail_free=0;
    }
    ReclaimIdleTextureSyncs(&gc); assert(frees==11);
    for(unsigned i=0;i<11;++i) assert(!memory[i].psClientSyncInfo);
    /* Texture pixels/storage stay resident; a later transfer can attach a new fence. */
    memory[0].psClientSyncInfo=calloc(1,sizeof(PVRSRV_CLIENT_SYNC_INFO));
    memory[0].ui32Flags &= ~PVRSRV_MEM_NO_SYNCOBJ;
    ReclaimIdleTextureSyncs(&gc); assert(frees==12 && textures[0].psMemInfo==&memory[0]);
    puts("texture sync reclamation: idle, busy, submitted, shared, surface, pinned and failed-free cases passed");
}
