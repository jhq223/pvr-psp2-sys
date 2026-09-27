#define main storage_cases
#include "texture_storage_bounds.c"
#undef main
typedef unsigned GLenum;
#define IMG_NULL NULL
#define GLES2_FLOAT 8U
#define GLES2_MULTICHUNK 16U
#define GLES2_MAX_TEXTURE_MIPMAP_LEVELS 12
#define GLES2_IS_MIPMAP(x) ((x) != 0)
#define GLES2_TEX_UNKNOWN 0
#define GLES2_TEX_CONSISTENT 1
#define GLES2_DIRTYFLAG_TEXTURE_STATE 1
#define GLES2_LOADED_LEVEL ((unsigned char *)(uintptr_t)1)
#define EURASIA_PDS_DOUTT2_TEXADDR_ALIGNSHIFT 6
#define EURASIA_PDS_DOUTT2_TEXADDR_SHIFT 0
static unsigned FloorLog2(unsigned v) { unsigned n=0; while(v>>=1) ++n; return n; }
static unsigned IsTextureConsistent(GLES2Context *gc, GLES2Texture *tex, unsigned layout, int loop) {
    tex->sState.aui32StateWord1[0]=layout | (tex->psMipLevel->ui32Width-1) | ((tex->psMipLevel->ui32Height-1)<<12);
    tex->ui32NumLevels=1; return GLES2_TEX_CONSISTENT;
}
static void SetupTwiddleFns(GLES2Texture *t) {}
#include "texture_initialize_functions.inc"
typedef void GLES2FrameBufferAttachable;
typedef void (*PFNCopyTextureData)(void *,const void *,unsigned,unsigned,unsigned,GLES2MipMapLevel *,int);
#define GLES2_SCHEDULE_HW_LAST_IN_SCENE 1
#define GLES2_SCHEDULE_HW_WAIT_FOR_3D 2
static int busy, flush_ok=1, sync_ok=1;
static int KRM_IsResourceNeeded(int *krm,int *resource) { return busy; }
static int FlushAttachableIfNeeded(GLES2Context *gc,void *level,unsigned flags) { return flush_ok; }
static int SGX2DQueryBlitsComplete(void *dev,void *sync,int wait) { return sync_ok ? 0 : 1; }
static void *GLES2Malloc(GLES2Context *gc,unsigned bytes) { assert(!"stride update must not allocate"); return NULL; }
static void GLES2Free(GLES2Context *gc,void *p) { free(p); }
static void PVRTextureWriteRegion(void *dst,void *src,unsigned layout,unsigned w,unsigned h,unsigned x,unsigned y,unsigned rw,unsigned rh,unsigned bytes,unsigned stride) { assert(!"not stride"); }
#include "texture_partial_functions.inc"
static void copy(void *dst,const void *src,unsigned w,unsigned h,unsigned stride,GLES2MipMapLevel *l,int unused) {
    unsigned bytes=l->psTexFormat->ui32TotalBytesPerTexel;
    for(unsigned y=0; y<h; ++y) memcpy((unsigned char *)dst+y*w*bytes,(const unsigned char *)src+y*stride,w*bytes);
}
static void initialize_case(unsigned w,unsigned h,unsigned bpp,unsigned layout,int with_data) {
    GLES2TextureManager manager={0}; Shared shared={&manager};
    GLES2Context gc={.psSharedState=&shared,.sAppHints={layout},.sState={{8}}};
    GLES2MipMapLevel levels[GLES2_MAX_TEXTURE_MIPMAP_LEVELS]={0}; GLES2Texture tex={.psMipLevel=levels};
    GLES2TextureFormat format={bpp,1,0};
    unsigned char *pixels=malloc(w*h*bpp); assert(pixels);
    for(unsigned i=0; i<w*h*bpp; ++i) pixels[i]=(unsigned char)(i*23+7);
    unsigned before=allocation_calls;
    assert(TextureInitializeStorage(&gc,&tex,0,&format,w,h,with_data ? pixels : NULL,w*bpp));
    assert(tex.bResidence && levels[0].pui8Buffer==GLES2_LOADED_LEVEL && allocation_calls==before+1);
    for(unsigned y=0; y<h; ++y) for(unsigned x=0; x<w; ++x) {
        unsigned offset=layout==EURASIA_PDS_DOUTT1_TEXTYPE_STRIDE ? y*ALIGNCOUNT(w,8)+x :
            ((y/32)*((w+31)/32)+x/32)*1024+(y%32)*32+x%32;
        for(unsigned b=0;b<bpp;++b)
            assert(((unsigned char *)storage.pvLinAddr)[offset*bpp+b] == (with_data ? pixels[(y*w+x)*bpp+b] : 0xa5));
    }
    /* API returns with no client pointer retained; overwriting it changes no storage. */
    memset(pixels,0xdd,w*h*bpp);
    assert(!TextureInitializeStorage(&gc,&tex,0,&format,w,h,pixels,w*bpp));
    if(layout==EURASIA_PDS_DOUTT1_TEXTYPE_STRIDE && with_data) {
        unsigned rw=w>1?w-1:1, rh=h>1?h-1:1, x=w-rw, y=h-rh;
        unsigned pitch=ALIGNCOUNT(w,8)*bpp, src_pitch=ALIGNCOUNT(rw*bpp,8);
        unsigned char *region=malloc(src_pitch*rh), *old=malloc(storage.uAllocSize);
        assert(region && old); memset(region,0x67,src_pitch*rh); memcpy(old,storage.pvLinAddr,storage.uAllocSize);
        busy=1; assert(!UploadIdleTextureRegion(&gc,&tex,levels,x,y,rw,rh,bpp,region,copy)); busy=0;
        flush_ok=0; assert(!UploadIdleTextureRegion(&gc,&tex,levels,x,y,rw,rh,bpp,region,copy)); flush_ok=1;
        storage.psClientSyncInfo=&tex; sync_ok=0;
        assert(!UploadIdleTextureRegion(&gc,&tex,levels,x,y,rw,rh,bpp,region,copy)); sync_ok=1;
        assert(UploadIdleTextureRegion(&gc,&tex,levels,x,y,rw,rh,bpp,region,copy));
        for(unsigned i=0;i<storage.uAllocSize;++i) {
            unsigned iy=i/pitch, ix=(i%pitch)/bpp;
            int changed=iy>=y && iy<y+rh && ix>=x && ix<x+rw;
            assert(((unsigned char *)storage.pvLinAddr)[i] == (changed ? 0x67 : old[i]));
        }
        storage.psClientSyncInfo=NULL; free(old); free(region);
    }
    free(storage.pvLinAddr); free(pixels);
}
int main(void) {
    const unsigned sizes[]={1,3,4,7,8,31,32,33,64,127};
    for(unsigned i=0;i<10;++i) for(unsigned b=1;b<=4;b*=2)
        for(unsigned layout=1;layout<=2;++layout) for(int data=0;data<2;++data)
            initialize_case(sizes[i],sizes[9-i],b,layout<<28,data);
    GLES2TextureManager manager={0}; Shared shared={&manager}; GLES2Context gc={.psSharedState=&shared};
    GLES2MipMapLevel levels[12]={0}; GLES2Texture tex={.psMipLevel=levels}; GLES2TextureFormat format={4,1,0};
    unsigned before=allocation_calls; unsigned char pixels[64]={0};
    assert(!TextureInitializeStorage(&gc,&tex,0,&format,0,1,pixels,0));
    assert(!TextureInitializeStorage(&gc,&tex,0,&format,3,1,pixels,16));
    tex.sState.ui32MinFilter=1; assert(!TextureInitializeStorage(&gc,&tex,0,&format,4,4,pixels,16));
    tex.sState.ui32MinFilter=0; levels[4].ui32Width=1;
    assert(!TextureInitializeStorage(&gc,&tex,0,&format,4,4,pixels,16));
    assert(allocation_calls==before);
    puts("texture fast paths: 240 direct/NULL layout cases, source independence, partial updates and busy/flush/sync fallback passed");
}
