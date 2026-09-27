#include <assert.h>
#define PVRSRV_MAP_GC_MMU 4
#define PVR_OPT(n) 1
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef unsigned IMG_UINT32;
typedef unsigned char IMG_UINT8;
typedef uintptr_t IMG_UINTPTR_T;
typedef int IMG_BOOL;
#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_NULL NULL
#define GLES_ASSERT(x) assert(x)
#define PVR_DPF(x) ((void)0)
#define GLES2_MAX_TEXTURE_UNITS 4
#define GLES2_MAX_TEXTURE_MIPMAP_LEVELS 1
#define GLES2_TEXTURE_CEM_FACE_MAX 6
#define GLES2_TEXTURE_TARGET_CEM 1
#define GLES2_TEXTURE_TARGET_STREAM 2
#define GLES2_TEXTURE_TARGET_MAX 3
#define GLES2_TEX_CONSISTENT 1
#define GLES2_IS_PERM_TEXTURE_UNIT(x) 0
#define GLES2_IS_GRAD_TEXTURE_UNIT(x) 0
#define GLES2_IS_MIPMAP(x) 0
#define GLES2_MIPMAP 1
#define GLES2_SCHEDULE_HW_LAST_IN_SCENE 1
#define GLES2_SCHEDULE_HW_WAIT_FOR_3D 2
#define GL_OUT_OF_MEMORY 0x505
#define GLES2_DIRTYFLAG_TEXTURE_STATE 1
#define PDS_NUM_TEXTURE_IMAGE_CHUNKS 1
#define EURASIA_TAG_TEXTURE_STATE_SIZE 3
#define SGX_FEATURE_TAG_SWIZZLE 1
#define SGX_FEATURE_TAG_LUMINANCE_ALPHA 1
#define EURASIA_PDS_DOUTT_DADJUST_ZERO_UINT 0
#define EURASIA_PDS_DOUTT0_MIPMAPCLAMP_CLRMSK (~0U)
#define EURASIA_PDS_DOUTT0_MAGFILTER_CLRMSK (~0U)
#define EURASIA_PDS_DOUTT0_MINFILTER_CLRMSK (~0U)
#define EURASIA_PDS_DOUTT0_CHANREPLICATE 0
#define EURASIA_PDS_DOUTT0_MIPFILTER 0
#define EURASIA_PDS_DOUTT0_NOTMIPMAP 0
#define EURASIA_PDS_DOUTT0_MINFILTER_POINT 0
#define EURASIA_PDS_DOUTT0_MAGFILTER_POINT 0
#define EURASIA_PDS_DOUTT0_DADJUST_SHIFT 0
#define EURASIA_PDS_DOUTT0_DADJUST_CLRMSK (~0U)
#define EURASIA_PDS_DOUTT0_MIPMAPCLAMP_MIN 0
#define EURASIA_PDS_DOUTT1_TEXFORMAT_U8888 0
#define EURASIA_PDS_DOUTT2_TEXADDR_ALIGNSHIFT 0
#define EURASIA_PDS_DOUTT2_TEXADDR_SHIFT 0
#define GLES2_LOADED_LEVEL ((unsigned char *)(uintptr_t)1)
typedef struct { unsigned ui32FirstAttachment, ui32Waiters; } KRMResource;
typedef struct { struct { unsigned uiAddr; } sDevVAddr; unsigned uAllocSize, ui32Flags; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { unsigned ui32TotalBytesPerTexel, ui32NumChunks; } GLES2TextureFormat;
typedef struct { void *psRenderSurface; } GLES2FrameBufferAttachable;
typedef struct {
    GLES2FrameBufferAttachable sFBAttachable;
    unsigned ui32Width, ui32Height;
    const GLES2TextureFormat *psTexFormat;
    unsigned char *pui8Buffer;
} GLES2MipMapLevel;
typedef struct {
    unsigned ui32StateWord0, ui32MinFilter, ui32MagFilter;
    unsigned aui32StateWord1[1], aui32StateWord2[1];
} GLES2TextureParamState;
typedef struct {
    KRMResource sResource;
    unsigned ui32ValidationPins;
    PVRSRV_CLIENT_MEM_INFO *psMemInfo;
    GLES2MipMapLevel *psMipLevel;
    const GLES2TextureFormat *psFormat;
    GLES2TextureParamState sState;
    unsigned ui32NumRenderTargets, ui32TextureTarget, ui32HWFlags;
    int bResidence, bHasEverBeenGhosted;
} GLES2Texture;
typedef struct { unsigned char ui8ImageUnit, ui8SamplerTypeIndex; } GLES2TextureSampler;
typedef struct { unsigned ui32SamplersActive; GLES2TextureSampler asTextureSamplers[4]; } GLES2ProgramShader;
typedef struct { GLES2ProgramShader sVertex, sFragment; } Program;
typedef struct {
    int bSomeTexturesWereGhosted;
    unsigned ui32ImageUnitEnables, aui32ChunkCount[4], aui32TAGControlWord[4][3];
    const GLES2TextureFormat *apsTexFormat[4];
} GLES2CompiledTextureState;
typedef struct { int sKRM; PVRSRV_CLIENT_MEM_INFO *psBlackDummyTexture, *psWhiteDummyTexture; } GLES2TextureManager;
typedef struct { GLES2TextureManager *psTextureManager; int hSecondaryLock; } Shared;
typedef struct {
    Shared *psSharedState;
    unsigned ui32DirtyState;
    struct { Program *psCurrentProgram; } sProgram;
    struct { GLES2CompiledTextureState sVertexTextureState, sFragmentTextureState; } sPrim;
    struct { GLES2Texture *apsBoundTexture[4][3]; } sTexture;
    struct { unsigned ui32OverloadTexLayout; } sAppHints;
    unsigned ui32TextureReclaimBudget, ui32TextureReclaimed;
    int bTextureReclaimLimited;
} GLES2Context;
static GLES2TextureFormat TexFormatABGR8888 = {4,1};
static GLES2Texture *victim;
static int pressure, freed, lock_depth, error, fail_allocation, reset_resource, waits;
static void PVRSRVLockMutex(int lock) { (void)lock; assert(!lock_depth++); }
static void PVRSRVUnlockMutex(int lock) { (void)lock; assert(lock_depth-- == 1); }
static int SWTextureBusy(GLES2Context *gc, GLES2Texture *t) { (void)gc; (void)t; return 0; }
static void SWTextureWait(GLES2Context *gc, GLES2Texture *t) { (void)gc; assert(t); ++waits; }
static void FlushUnflushedTextureRenders(GLES2Context *gc,GLES2Texture *t) { (void)gc;(void)t; }
static int FlushAttachableIfNeeded(GLES2Context *gc,GLES2FrameBufferAttachable *a,unsigned flags) { (void)gc;(void)a;(void)flags; return 1; }
static void *GLES2MallocHeapUNC(GLES2Context *gc,size_t n) { (void)gc; return malloc(n); }
static void ReadBackTextureData(GLES2Context *gc,GLES2Texture *t,unsigned f,unsigned l,void *p) { (void)gc;(void)t;(void)f;(void)l; memset(p,0,4); }
static void GLES2FREEDEVICEMEM_HEAP(GLES2Context *gc,PVRSRV_CLIENT_MEM_INFO *m) { (void)gc; ++freed; free(m); }
static void SetError(GLES2Context *gc,int e) { (void)gc; error=e; }
static unsigned IsTextureConsistent(GLES2Context *gc,GLES2Texture *t,unsigned layout,int loop) {
    (void)gc;(void)layout;(void)loop;
    /* UnloadInconsistentTexture can unlink and reset its KRM resource. */
    if(reset_resource) memset(&t->sResource,0,sizeof(t->sResource));
    return GLES2_TEX_CONSISTENT;
}
static int TextureMakeResident(GLES2Context *gc,GLES2Texture *t);
#include "texture_validation_functions.inc"
static int TextureMakeResident(GLES2Context *gc,GLES2Texture *t) {
    if(t->bResidence) return 1;
    if(pressure && t != victim && !victim->sResource.ui32Waiters)
        ReclaimTextureMemKRM(gc, &victim->sResource);
    if(fail_allocation) return 0;
    t->psMemInfo=calloc(1,sizeof(*t->psMemInfo)); assert(t->psMemInfo);
    t->psMemInfo->sDevVAddr.uiAddr=0x64000000;
    t->sState.aui32StateWord2[0]=t->psMemInfo->sDevVAddr.uiAddr;
    t->bResidence=1;
    return 1;
}
static void dispose(GLES2Texture *t) {
    free(t->psMemInfo);
    if(t->psMipLevel->pui8Buffer != GLES2_LOADED_LEVEL) free(t->psMipLevel->pui8Buffer);
}
static void check_validation(unsigned first_stage, unsigned second_stage, int duplicate, int fail, int reset) {
    GLES2TextureManager manager={0}; Shared shared={&manager,0}; Program program={0}; GLES2Context gc={0};
    PVRSRV_CLIENT_MEM_INFO dummy={0};
    GLES2MipMapLevel level[2]={0}; GLES2Texture tex[2]={0};
    manager.psWhiteDummyTexture=manager.psBlackDummyTexture=&dummy;
    gc.psSharedState=&shared; gc.sProgram.psCurrentProgram=&program;
    GLES2ProgramShader *stages[2]={&program.sVertex,&program.sFragment};
    for(unsigned i=0;i<2;++i) {
        level[i].ui32Width=level[i].ui32Height=1; level[i].psTexFormat=&TexFormatABGR8888; level[i].pui8Buffer=GLES2_LOADED_LEVEL;
        tex[i].psMipLevel=&level[i]; tex[i].psFormat=&TexFormatABGR8888;
        gc.sTexture.apsBoundTexture[i][0]=&tex[i];
    }
    stages[first_stage]->ui32SamplersActive|=1;
    stages[second_stage]->ui32SamplersActive|=2;
    stages[second_stage]->asTextureSamplers[1].ui8ImageUnit=1;
    if(duplicate) {
        stages[1-first_stage]->ui32SamplersActive|=4;
        stages[1-first_stage]->asTextureSamplers[2].ui8ImageUnit=0;
    }
    tex[0].psMemInfo=calloc(1,sizeof(*tex[0].psMemInfo));assert(tex[0].psMemInfo);
    tex[0].psMemInfo->sDevVAddr.uiAddr=0x634c0000; tex[0].sState.aui32StateWord2[0]=0x634c0000; tex[0].bResidence=1;
    victim=&tex[0]; pressure=1; freed=error=0; fail_allocation=fail; reset_resource=reset;
    SetupTextureState(&gc);
    /* Allocating sampler 1 must not evict the address just emitted for sampler 0. */
    assert(tex[0].psMemInfo && tex[0].bResidence && !freed);
    GLES2CompiledTextureState *state=first_stage ? &gc.sPrim.sFragmentTextureState : &gc.sPrim.sVertexTextureState;
    assert(state->aui32TAGControlWord[0][2] == tex[0].psMemInfo->sDevVAddr.uiAddr);
    assert(!tex[0].sResource.ui32Waiters && !tex[1].sResource.ui32Waiters && !lock_depth);
    assert(!tex[0].ui32ValidationPins && !tex[1].ui32ValidationPins);
    assert(error == (fail ? GL_OUT_OF_MEMORY : 0));
    /* After validation, unused storage remains reclaimable and invalidates cached state. */
    gc.ui32DirtyState=0;
    gc.bTextureReclaimLimited=1; gc.ui32TextureReclaimBudget=16;
    tex[0].psMemInfo->uAllocSize=16;
    ReclaimTextureMemKRM(&gc,&tex[0].sResource); /* USER -> USER cannot help. */
    assert(!freed && tex[0].psMemInfo);
    tex[0].psMemInfo->ui32Flags=PVRSRV_MAP_GC_MMU;
    gc.ui32TextureReclaimed=16;
    ReclaimTextureMemKRM(&gc,&tex[0].sResource); /* Reached the byte budget. */
    assert(!freed && tex[0].psMemInfo);
    gc.ui32TextureReclaimed=0;
    ReclaimTextureMemKRM(&gc,&tex[0].sResource);
    assert(gc.ui32TextureReclaimed==16);
    gc.bTextureReclaimLimited=0;
    assert(freed==1 && !tex[0].psMemInfo && !tex[0].bResidence);
    assert(gc.ui32DirtyState & GLES2_DIRTYFLAG_TEXTURE_STATE);
    /* Eviction by a shared context may leave this context's own dirty bits clear. */
    gc.ui32DirtyState=0; waits=0;
    PrepareTextureDependencies(&gc);
    assert(waits==2 && (gc.ui32DirtyState & GLES2_DIRTYFLAG_TEXTURE_STATE));
    tex[0].bResidence=tex[1].bResidence=1;
    gc.ui32DirtyState=0; waits=0;
    PrepareTextureDependencies(&gc);
    assert(waits==2 && !gc.ui32DirtyState);
    gc.sProgram.psCurrentProgram=NULL; waits=0;
    PrepareTextureDependencies(&gc);
    assert(!waits);
    dispose(&tex[0]);dispose(&tex[1]);
}
int main(void) {
    for(unsigned a=0;a<2;++a)
        for(unsigned b=0;b<2;++b)
            for(int duplicate=0;duplicate<2;++duplicate)
                for(int fail=0;fail<2;++fail)
                    for(int reset=0;reset<2;++reset) check_validation(a,b,duplicate,fail,reset);
    puts("texture validation: 32 cases passed (both stages, repeated inputs, OOM, resource reset, reclaim invalidation)");
}
