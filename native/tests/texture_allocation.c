#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef unsigned IMG_UINT32;
typedef int IMG_BOOL;
typedef int PVRSRV_ERROR;
#define IMG_INTERNAL
#define IMG_FALSE 0
#define IMG_TRUE 1
#define PVRSRV_OK 0
#define PVRSRV_MEM_READ 1
#define PVRSRV_MEM_WRITE 2
#define PVRSRV_MAP_GC_MMU 4
#define PVRSRV_PIXEL_FORMAT_B16G16R16F 1
#define PVRSRV_PIXEL_FORMAT_PVRTC2 2
#define PVRSRV_PIXEL_FORMAT_PVRTCII2 3
#define GLES2_NONPOW2 1
#define GLES2_COMPRESSED 2
#define GLES2_MIPMAP 4
#define GLES2_TEXTURE_TARGET_CEM 1
#define GLES2_MAX_TEXTURE_SIZE 4096
#define EURASIA_CACHE_LINE_SIZE 64
#define EURASIA_PDS_DOUTT1_TEXTYPE_CLRMSK (~0x30000000U)
#define EURASIA_PDS_DOUTT1_TEXTYPE_STRIDE 0x10000000U
#define EURASIA_PDS_DOUTT1_TEXTYPE_TILED 0x20000000U
#define EURASIA_PDS_DOUTT1_WIDTH_CLRMSK (~0xfffU)
#define EURASIA_PDS_DOUTT1_WIDTH_SHIFT 0
#define EURASIA_PDS_DOUTT1_HEIGHT_CLRMSK (~0xfff000U)
#define EURASIA_PDS_DOUTT1_HEIGHT_SHIFT 12
#define EURASIA_TAG_CUBEMAP_NO_ALIGN_SIZE_8BPP 16
#define EURASIA_TAG_CUBEMAP_NO_ALIGN_SIZE_16_32BPP 8
#define EURASIA_TAG_CUBEMAP_FACE_ALIGN 4096
#define ALIGNCOUNT(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define GLES2_TIME_START(x) ((void)0)
#define GLES2_TIME_STOP(x) ((void)0)
#define PVR_DPF(x) ((void)0)
#define GLES_ASSERT(x) assert(x)
#define GLES2MemSet memset
#include "heap_failure.inc"
typedef struct { unsigned ui32TotalBytesPerTexel, ui32NumChunks, ePixelFormat; } GLES2TextureFormat;
typedef struct { unsigned ui32Width, ui32Height; } GLES2MipMapLevel;
typedef struct { unsigned uAllocSize; } PVRSRV_CLIENT_MEM_INFO;
typedef struct {
    const GLES2TextureFormat *psFormat;
    unsigned ui32HWFlags, ui32NumLevels, ui32ChunkSize, ui32TextureTarget;
    struct { unsigned aui32StateWord1[1]; } sState;
    struct { unsigned ui32Name; } sNamedItem;
    GLES2MipMapLevel *psMipLevel;
    PVRSRV_CLIENT_MEM_INFO *psMemInfo;
} GLES2Texture;
typedef struct { int sKRM; } GLES2TextureManager;
typedef struct { GLES2TextureManager *psTextureManager; } Shared;
typedef struct { Shared *psSharedState; void *pvUNCHeap, *pvCDRAMHeap; unsigned ui32TextureReclaimBudget, ui32TextureReclaimed; int bTextureReclaimLimited; } GLES2Context;
struct malloc_managed_size { unsigned current_inuse_size, current_system_size; };
static int attempts, succeed_on, ghosts, reclaims, reports, stats_fail;
static unsigned wanted;
static char log_data[2048];
static PVRSRV_CLIENT_MEM_INFO storage;
static const char *stages[] = {"kernel-block", "gpu-map", "descriptor", "sync-object", "descriptor", "sync-object"};
/* Layout arithmetic is not under test: the real allocation/retry/reporting
 * function below receives a deterministic layout from these stubs. */
static unsigned GetNPOTMipMapOffset(unsigned levels, GLES2Texture *t) {
    assert(levels == 1); return t->psMipLevel->ui32Width * t->psMipLevel->ui32Height;
}
static unsigned GetMipMapOffset(unsigned levels, unsigned w, unsigned h) {
    assert(levels == 1); return w * h;
}
static unsigned GetCompressedMipMapOffset(unsigned levels, unsigned w, unsigned h, int two) {
    assert(!"compressed layout not exercised by this retry test"); return 0;
}
static int GLES2AllocTextureMemWithReport(GLES2Context *gc, unsigned flags, unsigned bytes,
                                           unsigned alignment, PVRSRV_CLIENT_MEM_INFO **out,
                                           SceHeapAllocFailure *report) {
    assert(attempts < 6 && bytes == wanted && alignment == 64);
    assert(!!(flags & PVRSRV_MAP_GC_MMU) == !(attempts & 1));
    *out = NULL;
    *report = (SceHeapAllocFailure){ stages[attempts], -10 - attempts, bytes + 4096 };
    ++attempts;
    if(attempts != succeed_on) return 1;
    storage.uAllocSize = bytes; *out = &storage; return 0;
}
static void KRM_DestroyUnneededGhosts(GLES2Context *gc, int *krm) {
    assert(attempts == 2 && ghosts == 0); ++ghosts;
}
static void KRM_ReclaimUnneededResources(GLES2Context *gc, int *krm) {
    assert(attempts == 4 && ghosts == 1 && reclaims == 0); ++reclaims;
}
static void SWTextureTrimStaging(GLES2Context *gc) {}
static unsigned sceHeapTrimEmpty(void *heap) { return 0; }
static int sceHeapGetTotalFreeSize(void *heap) { return 256; }
static int malloc_stats_fast(struct malloc_managed_size *out) {
    if(stats_fail) return -99;
    out->current_system_size = 1024; out->current_inuse_size = 512; return 0;
}
static int sceClibPrintf(const char *format, ...) {
    va_list args; va_start(args, format);
    size_t offset = strlen(log_data);
    int n = vsnprintf(log_data + offset, sizeof(log_data) - offset, format, args);
    va_end(args); assert(n >= 0 && (size_t)n < sizeof(log_data) - offset); ++reports; return n;
}
#include "texture_allocation_functions.inc"
int main(void) {
    GLES2TextureManager manager = {0}; Shared shared = {&manager};
    GLES2Context gc = {.psSharedState=&shared, .pvUNCHeap=(void *)1, .pvCDRAMHeap=(void *)2};
    GLES2TextureFormat format = {4, 1, 0};
    GLES2MipMapLevel level = {1024, 512};
    wanted = 1024 * 512 * 4;
    for(int attempt = 1; attempt <= 7; ++attempt) {
      for(int unavailable_stats = 0; unavailable_stats < 2; ++unavailable_stats) {
        attempts = ghosts = reclaims = reports = 0; log_data[0] = 0;
        succeed_on = attempt; stats_fail = unavailable_stats;
        GLES2Texture tex = {0}; tex.psFormat = &format; tex.psMipLevel = &level;
        tex.ui32NumLevels = 1; tex.ui32HWFlags = GLES2_NONPOW2; tex.sNamedItem.ui32Name = 42;
        assert(CreateTextureMemory(&gc, &tex) == (attempt < 7));
        assert(attempts == (attempt < 7 ? attempt : 6));
        assert(ghosts == (attempt > 2) && reclaims == (attempt > 4));
        if(attempt < 7) {
            assert(tex.psMemInfo == &storage && reports == 0 && !log_data[0]);
        } else {
            assert(!tex.psMemInfo && reports == 6);
            assert(strstr(log_data, "size=1024x512 levels=1 bytes=2097152"));
            assert(strstr(log_data, "stage=descriptor") && strstr(log_data, "stage=sync-object"));
            assert(strstr(log_data, unavailable_stats ? "libc_result=-99 libc_used=0 libc_arena=0" : "libc_result=0 libc_used=512 libc_arena=1024"));
        }
      }
    }
    puts("texture allocation: both pools, reclaim/retry, silent recovery and terminal failure origins passed");
}
