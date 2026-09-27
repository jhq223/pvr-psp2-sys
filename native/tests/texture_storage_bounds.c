#include <assert.h>
#include "psp2/optimization.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned IMG_UINT32;
typedef unsigned char IMG_UINT8;
typedef void *IMG_PVOID;
typedef int IMG_BOOL;
typedef int PVRSRV_ERROR;
#define IMG_INTERNAL
#define IMG_VOID void
#define IMG_FALSE 0
#define IMG_TRUE 1
#define PVRSRV_OK 0
#define PVRSRV_MEM_READ 1
#define PVRSRV_MEM_WRITE 2
#define PVRSRV_MAP_GC_MMU 4
#define PVRSRV_PIXEL_FORMAT_B16G16R16F 1
#define PVRSRV_PIXEL_FORMAT_PVRTC2 2
#define PVRSRV_PIXEL_FORMAT_PVRTCII2 3
#define PVRSRV_PIXEL_FORMAT_BC1 4
#define PVRSRV_PIXEL_FORMAT_BC3 5
#define PVRSRV_PIXEL_FORMAT_PVRTCIII 6
#define GLES2_NONPOW2 1
#define GLES2_COMPRESSED 2
#define GLES2_MIPMAP 4
#define GLES2_TEXTURE_TARGET_2D 0
#define GLES2_TEXTURE_TARGET_CEM 1
#define GLES2_MAX_TEXTURE_SIZE 4096
#define EURASIA_CACHE_LINE_SIZE 64
#define EURASIA_PDS_DOUTT1_TEXTYPE_CLRMSK (~0x30000000U)
#define EURASIA_PDS_DOUTT1_TEXTYPE_STRIDE 0x10000000U
#define EURASIA_PDS_DOUTT1_TEXTYPE_TILED 0x20000000U
#define EURASIA_PDS_DOUTT1_TEXTYPE_2D 0U
#define EURASIA_PDS_DOUTT1_TEXTYPE_CEM 0x30000000U
#define EURASIA_PDS_DOUTT1_WIDTH_CLRMSK (~0xfffU)
#define EURASIA_PDS_DOUTT1_WIDTH_SHIFT 0
#define EURASIA_PDS_DOUTT1_HEIGHT_CLRMSK (~0xfff000U)
#define EURASIA_PDS_DOUTT1_HEIGHT_SHIFT 12
#define EURASIA_PDS_DOUTT1_USIZE_CLRMSK EURASIA_PDS_DOUTT1_WIDTH_CLRMSK
#define EURASIA_PDS_DOUTT1_USIZE_SHIFT EURASIA_PDS_DOUTT1_WIDTH_SHIFT
#define EURASIA_PDS_DOUTT1_VSIZE_CLRMSK EURASIA_PDS_DOUTT1_HEIGHT_CLRMSK
#define EURASIA_PDS_DOUTT1_VSIZE_SHIFT EURASIA_PDS_DOUTT1_HEIGHT_SHIFT
#define SGX_FEATURE_TAG_POT_TWIDDLE 1
/* SGX543 layout granularity, from sgxfeaturedefs.h / sgxdefs.h. */
#define EURASIA_TAG_STRIDE_ALIGN0 8U
#define EURASIA_TAG_STRIDE_ALIGN1 8U
#define EURASIA_TAG_STRIDE_THRESHOLD 0U
#define EURASIA_TAG_TILE_SIZEX 32U
#define EURASIA_TAG_TILE_SIZEY 32U
#define EURASIA_TAG_TILE_SHIFTX 5U
#define EURASIA_TAG_TILE_SHIFTY 5U
#define EURASIA_TAG_CUBEMAP_NO_ALIGN_SIZE_8BPP 16
#define EURASIA_TAG_CUBEMAP_NO_ALIGN_SIZE_16_32BPP 8
#define EURASIA_TAG_CUBEMAP_FACE_ALIGN 4096
#define MAX_NUMBER_OF_BITS 12
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define ALIGNCOUNT(x,a) (((x)+(a)-1)&~((a)-1))
#define GLES2_TIME_START(x) ((void)0)
#define GLES2_TIME_STOP(x) ((void)0)
#define PVR_DPF(x) ((void)0)
#define GLES_ASSERT(x) assert(x)
#define GLES2MemSet memset
#define GLES2MemCopy memcpy
#include "heap_failure.inc"
typedef struct { unsigned ui32TotalBytesPerTexel, ui32NumChunks, ePixelFormat; } GLES2TextureFormat;
typedef struct {
    unsigned ui32Width, ui32Height, ui32ImageSize;
    unsigned char *pui8Buffer;
    const GLES2TextureFormat *psTexFormat;
    void *psTex;
    unsigned ui32WidthLog2, ui32HeightLog2, eRequestedFormat, ui32Level;
} GLES2MipMapLevel;
typedef struct { unsigned uAllocSize; void *pvLinAddr; void *psClientSyncInfo;
    struct { uintptr_t uiAddr; } sDevVAddr; } PVRSRV_CLIENT_MEM_INFO;
typedef struct {
    const GLES2TextureFormat *psFormat;
    unsigned ui32HWFlags, ui32NumLevels, ui32ChunkSize, ui32TextureTarget;
    struct { unsigned aui32StateWord1[1], aui32StateWord2[1], ui32MinFilter; } sState;
    struct { unsigned ui32Name; } sNamedItem;
    GLES2MipMapLevel *psMipLevel;
    PVRSRV_CLIENT_MEM_INFO *psMemInfo;
    unsigned ui32LevelsConsistent; int bResidence, sResource;
} GLES2Texture;
typedef struct { int sKRM; } GLES2TextureManager;
typedef struct { GLES2TextureManager *psTextureManager; } Shared;
typedef struct { Shared *psSharedState; void *pvUNCHeap, *pvCDRAMHeap; unsigned ui32TextureReclaimBudget, ui32TextureReclaimed; int bTextureReclaimLimited;
    struct { unsigned ui32OverloadTexLayout; } sAppHints;
    struct { struct { unsigned ui32UnpackAlignment; } sClientPixel; } sState;
    unsigned ui32DirtyState; void *ps3DDevData;
} GLES2Context;
struct malloc_managed_size { unsigned current_inuse_size, current_system_size; };
static PVRSRV_CLIENT_MEM_INFO storage;
static unsigned allocation_calls;
static int allocation_failure;
static int GLES2AllocTextureMemWithReport(GLES2Context *gc, unsigned flags,
    unsigned bytes, unsigned alignment, PVRSRV_CLIENT_MEM_INFO **out, SceHeapAllocFailure *report) {
    ++allocation_calls;
    if(allocation_failure) { *out=NULL; *report=(SceHeapAllocFailure){"test",1,bytes}; return 1; }
    storage.uAllocSize=bytes; storage.pvLinAddr=malloc(bytes); assert(storage.pvLinAddr);
    storage.sDevVAddr.uiAddr=(uintptr_t)storage.pvLinAddr;
    memset(storage.pvLinAddr,0xa5,bytes); *out=&storage; return PVRSRV_OK;
}
static void KRM_DestroyUnneededGhosts(GLES2Context *gc, int *krm) { assert(0); }
static void KRM_ReclaimUnneededResources(GLES2Context *gc, int *krm) { assert(0); }
static void SWTextureTrimStaging(GLES2Context *gc) {}
static unsigned sceHeapTrimEmpty(void *heap) { return 0; }
static int sceHeapGetTotalFreeSize(void *heap) { return 0; }
static int malloc_stats_fast(struct malloc_managed_size *out) { return -1; }
static int sceClibPrintf(const char *format, ...) { assert(0); return 0; }
/* The roundtrip cases exercise stride/tiled uploads only. Twiddled/compressed
 * allocations are checked separately; no replacement upload is simulated. */
static void unsupported_upload(void *d, const void *s, unsigned w, unsigned h, unsigned stride) { assert(0); }
#define DeTwiddleAddressETC1 unsupported_upload
#define DeTwiddleAddress8bpp unsupported_upload
#define DeTwiddleAddress16bpp unsupported_upload
#define DeTwiddleAddress32bpp unsupported_upload
#include "texture_storage_functions.inc"
#include "texture_allocation_functions.inc"

static void check(GLES2Context *gc, unsigned w, unsigned h, unsigned bytes, unsigned layout) {
    GLES2TextureFormat format={bytes,1,0};
    unsigned input_bytes=w*h*bytes;
    unsigned char *source=malloc(input_bytes); assert(source);
    for(unsigned i=0; i<input_bytes; ++i) source[i]=(unsigned char)(i*17+53);
    GLES2MipMapLevel level={.ui32Width=w,.ui32Height=h,.ui32ImageSize=input_bytes,.pui8Buffer=source,.psTexFormat=&format};
    GLES2Texture tex={0}; tex.psFormat=&format; tex.psMipLevel=&level; tex.ui32NumLevels=1;
    tex.ui32HWFlags=((w&(w-1)) || (h&(h-1))) ? GLES2_NONPOW2 : 0;
    tex.sState.aui32StateWord1[0]=layout | (w-1) | ((h-1)<<12);
    assert(CreateTextureMemory(gc,&tex));
    /* Exact malloc size turns an undersized allocation into an ASan failure
     * in the production upload, without adjacent heap slack hiding it. */
    TextureUpload(&tex,&level,0,&format,0,0,w,h);
    unsigned expected=layout==EURASIA_PDS_DOUTT1_TEXTYPE_STRIDE ?
        ((w+7)/8)*8*h*bytes : ((w+31)/32)*32*((h+31)/32)*32*bytes;
    assert(storage.uAllocSize==expected && tex.ui32ChunkSize==expected);
    for(unsigned y=0; y<h; ++y) for(unsigned x=0; x<w; ++x) {
        unsigned offset=layout==EURASIA_PDS_DOUTT1_TEXTYPE_STRIDE ?
            y*((w+7)/8)*8+x : ((y/32)*((w+31)/32)+x/32)*1024+(y%32)*32+x%32;
        assert(offset*bytes+bytes<=storage.uAllocSize);
        assert(!memcmp((unsigned char *)storage.pvLinAddr+offset*bytes,source+(y*w+x)*bytes,bytes));
    }
    free(storage.pvLinAddr); free(source);
}

int main(void) {
    GLES2TextureManager manager={0}; Shared shared={&manager}; GLES2Context gc={.psSharedState=&shared};
    check(&gc,4,4,4,EURASIA_PDS_DOUTT1_TEXTYPE_STRIDE); /* Vita buffer-orphan input. */
    const unsigned sizes[]={1,2,3,4,7,8,16,31,32,33,64};
    for(unsigned i=0; i<sizeof(sizes)/sizeof(*sizes); ++i)
        for(unsigned j=0; j<sizeof(sizes)/sizeof(*sizes); ++j)
            for(unsigned bytes=1; bytes<=4; bytes*=2) {
                check(&gc,sizes[i],sizes[j],bytes,EURASIA_PDS_DOUTT1_TEXTYPE_STRIDE);
                check(&gc,sizes[i],sizes[j],bytes,EURASIA_PDS_DOUTT1_TEXTYPE_TILED);
            }
    /* Twiddled mip chains must keep their compact layout. */
    GLES2TextureFormat format={4,1,0}; GLES2MipMapLevel level={.ui32Width=4,.ui32Height=4,.ui32ImageSize=64,.psTexFormat=&format};
    GLES2Texture tex={0}; tex.psFormat=&format; tex.psMipLevel=&level;
    tex.sState.aui32StateWord1[0]=2 | (2<<12); tex.ui32NumLevels=3; tex.ui32HWFlags=GLES2_MIPMAP;
    assert(CreateTextureMemory(&gc,&tex)); assert(storage.uAllocSize==84); free(storage.pvLinAddr);
    assert(allocation_calls == 728);
    puts("texture storage bounds: 727 stride/tiled upload roundtrips and twiddled mip allocation passed");
    return 0;
}
