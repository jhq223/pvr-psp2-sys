/* The production query/counter implementation runs against deterministic kernel calls. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t IMG_UINT32;
typedef uint64_t IMG_UINT64;
typedef int32_t IMG_INT32;
typedef void IMG_VOID;
typedef char IMG_CHAR;
typedef int IMG_BOOL;
typedef int SceUID;
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define SCE_NULL NULL
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_NC_RW 1
#include "../../vendor/PVR_PSP2/eurasiacon/include/memory_diagnostics.h"
typedef struct { void *hMemoryStatsLock; PvrMemoryCounters memoryCounters; int hDriverMemUID, h3DParamBufMemUID; } SrvSysContext;
typedef struct { unsigned size; void *mappedBase; unsigned mappedSize; } SceKernelMemBlockInfo;
static unsigned sizes[32];
static int fail_alloc, fail_free, fail_query, locks;
static int sceKernelAllocMemBlock(const char *name, int type, unsigned bytes, void *option)
{
    (void)name; (void)type; (void)option;
    assert(locks == 0);
    if(fail_alloc) return -1;
    /* The counter must use the queried actual mapping, rather than the request. */
    sizes[3] = bytes + 4096;
    return 3;
}
static int sceKernelGetMemBlockBase(int uid, void **base)
{
    assert(locks == 0);
    if(fail_query || uid <= 0 || uid >= 32 || sizes[uid] == 0) return -2;
    *base = (void *)(uintptr_t)uid;
    return 0;
}
static int sceKernelGetMemBlockInfoByAddr(void *base, SceKernelMemBlockInfo *info)
{
    int uid = (int)(uintptr_t)base;
    assert(info->size == sizeof(*info) && locks == 0);
    info->mappedSize = sizes[uid];
    return 0;
}
static int sceKernelFreeMemBlock(int uid)
{
    assert(locks == 0 && sizes[uid] != 0);
    if(fail_free) return -3;
    sizes[uid] = 0;
    return 0;
}
static void PVRSRVLockMutex(void *lock) { (void)lock; assert(locks++ == 0); }
static void PVRSRVUnlockMutex(void *lock) { (void)lock; assert(--locks == 0); }
#include "memory_diagnostics_functions.inc"

typedef float IMG_FLOAT;
typedef struct { int unused; } IMGEGLAppHints;
#define IMG_INTERNAL
#define SUPPORT_SGX
#define PVRSRV_OK 0
#define PVRSRVMemSet memset
static int init_failure, deinit_failure, mutexes;
static int PVRSRVCreateMutex(void **mutex) { assert(!*mutex); ++mutexes; *mutex = &mutexes; return 0; }
static void PVRSRVDestroyMutex(void *mutex) { assert(mutex && mutexes == 1); --mutexes; }
static int SRV_ServicesDeInit(SrvSysContext *context);
static int SRV_SGXServicesDeInit(SrvSysContext *context) { (void)context; return !deinit_failure; }
static int SRV_SGXServicesInit(SrvSysContext *context, IMGEGLAppHints *hints) {
    (void)hints;
    if(init_failure) { SRV_ServicesDeInit(context); return 0; }
    return 1;
}
#include "memory_services_functions.inc"

int main(void)
{
    SrvSysContext context = {0};
    PvrDriverMemorySnapshot snapshot;
    int uid;
    context.hMemoryStatsLock = &context;
    context.memoryCounters.valid = 1;
    context.hDriverMemUID = 1; context.h3DParamBufMemUID = 2;
    sizes[1] = 4U << 20; sizes[2] = 16U << 20;
    fail_alloc = 1;
    assert(KrkrAllocRenderTargetBlock(&context, "rt", 8192) < 0);
    assert(context.memoryCounters.renderTargetLive == 0);
    fail_alloc = 0;
    uid = KrkrAllocRenderTargetBlock(&context, "rt", 8192);
    assert(uid == 3 && context.memoryCounters.renderTargetLive == 12288);
    fail_free = 1;
    assert(KrkrFreeRenderTargetBlock(&context, uid) < 0);
    assert(context.memoryCounters.renderTargetLive == 12288);
    fail_free = 0;
    assert(KrkrFreeRenderTargetBlock(&context, uid) == 0);
    assert(context.memoryCounters.renderTargetLive == 0 && context.memoryCounters.renderTargetPeak == 12288);
    KrkrDirectDeviceAllocated(&context, 51200); KrkrDirectDeviceAllocated(&context, 1024);
    KrkrDirectDeviceFreed(&context, 1024);
    KrkrSurfaceCreated(&context); KrkrSurfaceCreated(&context); KrkrSurfaceRetired(&context);
    assert(KrkrQueryDriverMemory(&context, &snapshot) == 0);
    assert(snapshot.registeredUNC == (4U << 20) && snapshot.parameterCDRAM == (16U << 20));
    assert(snapshot.directLive == 51200 && snapshot.directPeak == 52224);
    assert(snapshot.surfaceCreates == 2 && snapshot.surfaceRetires == 1);
    assert(snapshot.renderTargetLive == 0 && snapshot.renderTargetPeak == 12288);
    /* A failed size observation remains visibly unavailable, never guessed. */
    fail_query = 1;
    assert(KrkrAllocRenderTargetBlock(&context, "rt", 8192) == 3);
    fail_query = 0;
    assert(KrkrQueryDriverMemory(&context, &snapshot) < 0);
    assert(locks == 0);
    context.hMemoryStatsLock = NULL;
    init_failure = 1;
    assert(!SRV_ServicesInit(&context, NULL, 0));
    assert(!context.hMemoryStatsLock && mutexes == 0);
    init_failure = 0;
    assert(SRV_ServicesInit(&context, NULL, 0));
    assert(context.memoryCounters.valid && context.memoryCounters.renderTargetPeak == 0);
    deinit_failure = 1;
    assert(!SRV_ServicesDeInit(&context));
    assert(context.hMemoryStatsLock && mutexes == 1);
    deinit_failure = 0;
    assert(SRV_ServicesDeInit(&context) && !context.hMemoryStatsLock && mutexes == 0);
    puts("native memory counters preserve mapped sizes, failed-free ownership and separate domains");
    return 0;
}
