#include <assert.h>
#include "psp2/optimization.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int PVRSRV_ERROR, IMG_BOOL;
#define IMG_TRUE 1
#define IMG_FALSE 0
typedef unsigned IMG_UINT32;
typedef void IMG_VOID;
typedef void *IMG_PVOID;
#define __inline static inline
#define IMG_NULL NULL
#define PVRSRV_OK 0
#define PVRSRV_ERROR_OUT_OF_MEMORY 1
#define PVRSRV_ERROR_SYNC_INFO_LIMIT_REACHED 185
#define PVRSRV_MAP_GC_MMU 1
#define PVRSRV_MEM_NO_SYNCOBJ 2
typedef struct { unsigned size, alignment; } SceHeapAllocOptParam;
#include "heap_failure.inc"
typedef struct { int value; } Sync;
typedef struct Mem {
    Sync *psClientSyncInfo;
    void *pvLinAddr, *hKernelMemInfo;
    struct Mem *psNext;
    struct { void *uiAddr; } sDevVAddr;
    unsigned uAllocSize, ui32Flags;
} PVRSRV_CLIENT_MEM_INFO;
typedef struct { PVRSRV_CLIENT_MEM_INFO info; void *heap; int texture; } GLES2HeapMemInfo;
typedef struct { void *pvCDRAMHeap, *pvUNCHeap, *ps3DDevData; } GLES2Context;
enum { SUCCESS, BACKING, DESCRIPTOR, SYNC, SYNC_LIMIT };
static int failure, buffers, descriptors, syncs;
static unsigned backing_calls, descriptor_calls, sync_calls;
static void *expected_heap;
static void *sceHeapAllocHeapMemoryWithReport(void *heap, unsigned bytes,
                                            SceHeapAllocOptParam *opt, SceHeapAllocFailure *report) {
    ++backing_calls;
    assert(heap == expected_heap && opt->alignment == 64 && opt->size == sizeof(*opt));
    *report = (SceHeapAllocFailure){ failure == BACKING ? "gpu-map" : "ok", failure == BACKING ? -42 : 0, 4096 };
    if(failure == BACKING) return NULL;
    ++buffers; return malloc(bytes);
}
static int sceHeapFreeHeapMemory(void *heap, void *buffer) {
    assert(heap == expected_heap && buffers == 1); --buffers; free(buffer); return 0;
}
static void *GLES2Calloc(GLES2Context *gc, size_t bytes) {
    ++descriptor_calls;
    if(failure == DESCRIPTOR) return NULL;
    ++descriptors; return calloc(1, bytes);
}
static void GLES2Free(GLES2Context *gc, void *ptr) {
    assert(!gc && descriptors == 1); --descriptors; free(ptr);
}
static int PVRSRVAllocSyncInfo(void *dev, Sync **out) {
    ++sync_calls;
    if(failure == SYNC) return -77;
    if(failure == SYNC_LIMIT) {
        /* A failed allocation must not leave a usable sync pointer. */
        *out = (Sync *)(uintptr_t)1;
        return PVRSRV_ERROR_SYNC_INFO_LIMIT_REACHED;
    }
    ++syncs; *out = malloc(sizeof(**out)); return 0;
}
static int PVRSRVFreeSyncInfo(void *dev, Sync *ptr) {
    assert(syncs == 1); --syncs; free(ptr); return 0;
}
static void SWTextureReleaseSync(GLES2Context *gc, Sync *sync) { PVRSRVFreeSyncInfo(gc->ps3DDevData, sync); }
#include "heap_device_mem_functions.inc"
int main(void) {
    GLES2Context gc = { (void *)1, (void *)2, NULL };
    for(unsigned pool = 0; pool < 2; ++pool) {
        unsigned flags = pool ? PVRSRV_MAP_GC_MMU : 0;
        expected_heap = pool ? gc.pvCDRAMHeap : gc.pvUNCHeap;
        for(int mode = BACKING; mode <= SYNC; ++mode) {
            failure = mode;
            PVRSRV_CLIENT_MEM_INFO *info = (void *)1;
            SceHeapAllocFailure report;
            int code = GLES2AllocDeviceMemHeapWithReport(&gc, flags, 4096, 64, &info, &report);
            assert(code == (mode == SYNC ? -77 : PVRSRV_ERROR_OUT_OF_MEMORY));
            assert(!info && !buffers && !descriptors && !syncs);
            assert(!strcmp(report.stage, mode == BACKING ? "gpu-map" : mode == DESCRIPTOR ? "descriptor" : "sync-object"));
            assert(report.error == (mode == BACKING ? -42 : mode == DESCRIPTOR ? PVRSRV_ERROR_OUT_OF_MEMORY : -77));
            assert(report.blockSize == 4096);
        }
        /* Texture-only quota fallback; strict allocations must still fail. */
        failure = SYNC_LIMIT;
        PVRSRV_CLIENT_MEM_INFO *texture = NULL;
        SceHeapAllocFailure report;
        assert(GLES2ALLOCDEVICEMEM_HEAP(&gc, flags, 128, 64, &texture) == PVRSRV_ERROR_SYNC_INFO_LIMIT_REACHED);
        assert(!texture && !buffers && !descriptors && !syncs);
        backing_calls = descriptor_calls = sync_calls = 0;
        assert(!GLES2AllocTextureMemWithReport(&gc, flags, 128, 64, &texture, &report));
        assert(backing_calls == 1 && descriptor_calls == 1 && sync_calls == (PVR_OPT(7) ? 0U : 1U));
        assert(!strcmp(report.stage, "ok") && !report.error);
        assert(texture && !texture->psClientSyncInfo && !syncs);
        assert(texture->ui32Flags & PVRSRV_MEM_NO_SYNCOBJ);
        assert(!GLES2FREEDEVICEMEM_HEAP(&gc, texture));
        assert(!buffers && !descriptors);
        for(int mode = BACKING; mode <= (PVR_OPT(7) ? DESCRIPTOR : SYNC); ++mode) {
            failure = mode;
            assert(GLES2AllocTextureMemWithReport(&gc, flags, 128, 64, &texture, &report) != PVRSRV_OK);
            assert(!texture && !buffers && !descriptors && !syncs);
        }
        for(unsigned no_sync = 0; no_sync < 2; ++no_sync) {
            /* NO_SYNCOBJ allocations must also succeed when no sync slots remain. */
            failure = no_sync ? SYNC : SUCCESS;
            PVRSRV_CLIENT_MEM_INFO *info = NULL;
            unsigned attributes = flags | (no_sync ? PVRSRV_MEM_NO_SYNCOBJ : 0);
            assert(!GLES2ALLOCDEVICEMEM_HEAP(&gc, attributes, 4096, 64, &info));
            assert(buffers == 1 && descriptors == 1 && syncs == !no_sync);
            assert(info->uAllocSize == 4096 && info->ui32Flags == attributes);
            assert(info->sDevVAddr.uiAddr == info->pvLinAddr && !info->hKernelMemInfo && !info->psNext);
            assert(!GLES2FREEDEVICEMEM_HEAP(&gc, info));
            assert(!buffers && !descriptors && !syncs);
        }
    }
    puts("device heap: backing, descriptor, sync failures preserve origin and roll back ownership");
}
