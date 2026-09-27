#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define IMG_VOID void
#define IMG_TRUE 1
#define IMG_FALSE 0
#define GLES2_EXTENSION_EGL_IMAGE 1
#define GLES2_MAX_TEXTURE_UNITS 4
#define GLES2_TEXTURE_TARGET_MAX 1
#define SGX_MAX_SRC_SYNCS 2
#define EGLIMAGE_FLAGS_COMPOSITION_SYNC 1
#define GL_OUT_OF_MEMORY 0x505
#define GLES2_IS_PERM_TEXTURE_UNIT(x) 0
#define GLES2_IS_GRAD_TEXTURE_UNIT(x) 0
#define PVR_DPF(x) ((void)0)
#define GLES_ASSERT(x) assert(x)
typedef unsigned IMG_UINT32;
typedef uint8_t IMG_UINT8;
typedef int IMG_BOOL;
typedef struct { void *psClientSyncInfo; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { PVRSRV_CLIENT_MEM_INFO *psMemInfo; unsigned ui32Flags; } EGLImage;
typedef struct { EGLImage *psEGLImageTarget; int bResidence, sResource; } GLES2Texture;
typedef struct { unsigned char ui8ImageUnit,ui8SamplerTypeIndex; } GLES2TextureSampler;
typedef struct { unsigned ui32SamplersActive; GLES2TextureSampler asTextureSamplers[4]; } GLES2ProgramShader;
typedef struct { GLES2ProgramShader sVertex, sFragment; } Program;
typedef struct { unsigned ui32NumSrcSyncs; void *apsSrcSurfSyncInfo[2]; int sRenderStatusUpdate; } Surface;
typedef struct { int sKRM; } Manager;
typedef struct { Manager *psTextureManager; int sUSEShaderVariantKRM; } Shared;
typedef struct { int sResource; } Variant;
typedef struct { Surface *psRenderSurface; Shared *psSharedState; struct { int bEnableAppTextureDependency; } sAppHints; struct { GLES2Texture *apsBoundTexture[4][1]; } sTexture; struct { Program *psCurrentProgram; Variant *psCurrentFragmentVariant; } sProgram; } GLES2Context;
static int attachments,fail_at,error;
static void SetError(GLES2Context *g,int e) { error=e; }
static int KRM_Attach(int *m,void *s,int *status,int *resource) { return ++attachments!=fail_at; }
static void SWTextureWait(GLES2Context *g,GLES2Texture *t) {}
#include "texture_dependencies_functions.inc"
int main(void) {
    Manager manager={0}; Shared shared={&manager,0}; Surface surface={0}; Program program={0}; Variant variant={0}; GLES2Context gc={0};
    gc.psSharedState=&shared; gc.psRenderSurface=&surface; gc.sProgram.psCurrentProgram=&program; gc.sProgram.psCurrentFragmentVariant=&variant;
    assert(AttachAllUsedResourcesToCurrentSurface(&gc) && attachments==1);
    fail_at=2; assert(!AttachAllUsedResourcesToCurrentSurface(&gc) && error==GL_OUT_OF_MEMORY);
    PVRSRV_CLIENT_MEM_INFO mem[3]={{(void *)1},{(void *)2},{(void *)3}};
    EGLImage image[3]={{&mem[0],1},{&mem[1],1},{&mem[2],1}};
    GLES2Texture tex[3]={{&image[0],1,0},{&image[1],1,0},{&image[2],1,0}};
    for(unsigned i=0;i<3;++i) { gc.sTexture.apsBoundTexture[i][0]=&tex[i]; program.sFragment.asTextureSamplers[i].ui8ImageUnit=i; }
    program.sFragment.ui32SamplersActive=3; program.sVertex.ui32SamplersActive=1;
    attachments=0; fail_at=0; assert(AttachAllUsedResourcesToCurrentSurface(&gc));
    assert(attachments==3 && surface.ui32NumSrcSyncs==2);
    program.sFragment.ui32SamplersActive=7; attachments=0; error=0;
    assert(!AttachAllUsedResourcesToCurrentSurface(&gc) && error==GL_OUT_OF_MEMORY);
    assert(surface.ui32NumSrcSyncs==2 && attachments==2);
    program.sFragment.ui32SamplersActive=1; attachments=0; fail_at=1;
    assert(!AttachAllUsedResourcesToCurrentSurface(&gc));
    puts("texture dependencies: empty shaders, duplicate resources, full sync slots and attach failure passed");
}
