#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int SceUID;
typedef size_t SceSize;
typedef uintptr_t SceUIntPtr;
typedef unsigned SceUInt;
typedef void *SceClibMspace;
typedef struct { int locked; } SceKernelLwMutexWork;
#define SCE_UID_NAMELEN 31
#define SCE_NULL NULL
#define IMG_NULL NULL
#define IMG_FALSE 0
#define PVRSRV_OK 0
#define PVRSRV_MEM_READ 1
#define PVRSRV_MEM_WRITE 2
#define PVRSRV_MEM_USER_SUPPLIED_DEVVADDR 4
#define SCE_HEAP_OPT_MEMBLOCK_TYPE_USER 0
#define SCE_HEAP_ERROR_INVALID_ID (-1)
#define SCE_HEAP_OPT_MEMBLOCK_TYPE_USER_NC 1
#define SCE_HEAP_OPT_MEMBLOCK_TYPE_CDRAM 2
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW 0
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_NC_RW 1
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 2
#define ALIGN(x,a) (((x) + (a) - 1) & ~((a) - 1))
typedef struct { unsigned size, alignment; } SceHeapAllocOptParam;
#include "heap_failure.inc"
#include "psp2/heaplib_internal.h"
static void *st_psDevData, *st_hDevMemContext;
enum { SUCCESS, FAIL_LOCK, FAIL_KERNEL, FAIL_BASE, FAIL_MAP, FAIL_CREATE, FAIL_ALLOC };
static int failure, blocks, mappings, mspaces, allocs, use_primary;
static void *block;
static size_t block_size;
static SceHeapWorkInternal head;
static int sceKernelLockLwMutex(SceKernelLwMutexWork *lock, int count, void *timeout) {
    if(failure == FAIL_LOCK) return -101;
    assert(!lock->locked); lock->locked = 1; return 0;
}
static int sceKernelUnlockLwMutex(SceKernelLwMutexWork *lock, int count) {
    assert(lock->locked); lock->locked = 0; return 0;
}
static int sceKernelAllocMemBlock(const char *name, int type, unsigned bytes, void *opt) {
    if(failure == FAIL_KERNEL) return -102;
    assert(!blocks && bytes >= 4096);
    assert(!(bytes % (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW ? 256 * 1024 : 4096)));
    block = calloc(1, bytes); assert(block); block_size = bytes; blocks++; return 1;
}
static int sceKernelGetMemBlockBase(int id, void **out) {
    assert(id == 1 && blocks == 1);
    if(failure == FAIL_BASE) return -103;
    *out = block; return 0;
}
static int sceKernelFreeMemBlock(int id) {
    assert(id == 1 && blocks == 1 && !mappings && !mspaces);
    free(block); block = NULL; blocks--; return 0;
}
static int PVRSRVMapMemoryToGpu(void *dev, void *context, unsigned unused, unsigned bytes,
                              unsigned unused2, void *ptr, unsigned flags, void *out) {
    assert(blocks == 1 && ptr == block && bytes == block_size);
    if(failure == FAIL_MAP) return -104;
    assert(!mappings); mappings++; return 0;
}
static int PVRSRVUnmapMemoryFromGpu(void *dev, void *ptr, unsigned unused, int sync) {
    assert(ptr == block && mappings == 1 && !mspaces); mappings--; return 0;
}
static void *sceClibMspaceCreate(void *base, size_t size) {
    assert((char *)base + size == (char *)block + block_size);
    if(failure == FAIL_CREATE) return NULL;
    assert(!mspaces); mspaces++; return base;
}
static void sceClibMspaceDestroy(void *msp) { assert(mspaces == 1); mspaces--; }
static void *sceClibMspaceMemalign(void *msp, size_t alignment, size_t bytes) {
    if(msp == &head) return use_primary ? (void *)(&head + 1) : NULL;
    if(failure == FAIL_ALLOC) return NULL;
    assert(mspaces == 1);
    if(bytes + 1024 > block_size) return NULL;
    allocs++; return (void *)ALIGN((uintptr_t)msp + 1024, alignment);
}
static void *sceClibMspaceMalloc(void *msp, size_t bytes) {
    return sceClibMspaceMemalign(msp, 8, bytes);
}
#include "heap_allocation_functions.inc"
static void reset(int mode, unsigned type) {
    assert(!blocks && !mappings && !mspaces);
    memset(&head, 0, sizeof(head)); head.magic = (uintptr_t)(&head + 1);
    head.bsize = 4096; head.memblockType = type;
    head.prim.next = head.prim.prev = &head.prim; head.prim.msp = &head;
    head.info.hblks = 1; head.info.arena = 4096; failure = mode; allocs = 0; use_primary = 0;
}
int main(void) {
    SceHeapAllocOptParam opt = { sizeof(opt), 64 };
    const char *stages[] = { "ok", "heap-lock", "kernel-block", "kernel-base", "gpu-map", "mspace-create", "mspace-alloc" };
    for(unsigned type = 0; type < 3; ++type) {
      for(unsigned aligned = 0; aligned < 2; ++aligned) {
        for(int mode = FAIL_LOCK; mode <= FAIL_ALLOC; ++mode) {
            reset(mode, type);
            SceHeapAllocFailure report;
            assert(!sceHeapAllocHeapMemoryWithReport(&head, 8192, aligned ? &opt : NULL, &report));
            assert(!strcmp(report.stage, stages[mode]));
            assert(report.error == (mode <= FAIL_MAP ? -100 - mode : 0));
            assert((report.blockSize != 0) == (mode != FAIL_LOCK));
            assert(!blocks && !mappings && !mspaces && !head.lwmtx.locked);
            assert(head.prim.next == &head.prim && head.prim.prev == &head.prim);
            assert(head.info.hblks == 1 && head.info.arena == 4096 && head.info.ordblks == 0);
        }
        reset(SUCCESS, type);
        assert(sceHeapAllocHeapMemoryWithOption(&head, 8192, &opt));
        assert(blocks == 1 && mappings == 1 && mspaces == 1 && allocs == 1);
        assert(head.info.hblks == 2 && head.info.arena == (int)(4096 + block_size));
        assert(head.info.ordblks == 1 && !head.lwmtx.locked);
        assert(head.prim.next->next == &head.prim && head.prim.prev->prev == &head.prim);
        sceClibMspaceDestroy(head.prim.next->msp);
        PVRSRVUnmapMemoryFromGpu(NULL, block, 0, 0); sceKernelFreeMemBlock(1);
        reset(SUCCESS, type);
        SceHeapAllocFailure report;
        head.bsize = 0;
        assert(!sceHeapAllocHeapMemoryWithReport(&head, 8192, NULL, &report));
        assert(!strcmp(report.stage, "fixed-heap") && !head.info.ordblks && !head.lwmtx.locked);
        use_primary = 1;
        assert(sceHeapAllocHeapMemoryWithReport(&head, 64, NULL, &report));
        assert(!strcmp(report.stage, "ok") && head.info.ordblks == 1);
        assert(!blocks && !mappings && !mspaces);
      }
    }
    SceHeapAllocFailure report;
    assert(!sceHeapAllocHeapMemoryWithReport(NULL, 1, NULL, &report));
    assert(!strcmp(report.stage, "argument"));
    reset(SUCCESS, 0);
    opt.alignment = 3;
    assert(!sceHeapAllocHeapMemoryWithReport(&head, 1, &opt, &report));
    assert(!strcmp(report.stage, "argument"));
    assert(!sceHeapAllocHeapMemoryWithReport(&head, UINT32_MAX, NULL, &report));
    assert(!strcmp(report.stage, "argument") && !head.lwmtx.locked);
    puts("heap allocation: every extension failure rolls back storage, mapping, links and counts");
    return 0;
}
