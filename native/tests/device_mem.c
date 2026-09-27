#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
typedef int PVRSRV_ERROR;
typedef int IMG_BOOL;
typedef unsigned IMG_UINT32;
typedef void *IMG_HANDLE;
typedef struct { int value; } PVRSRV_DEV_DATA;
typedef struct { int value; } PVRSRV_CLIENT_SYNC_INFO;
typedef struct { PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { void *hPerProcRef; } SrvSysContext;
#define IMG_EXPORT
#define IMG_CALLCONV
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define PVRSRV_OK 0
#define PVRSRV_MEM_READ 4
#define PVRSRV_MEM_NO_SYNCOBJ 1
#define sceClibPrintf(...) ((void)0)
#define PVRSRV_HAP_NO_GPU_VIRTUAL_ON_ALLOC 2

static int fail_memory, fail_sync, fail_free, memory_count, sync_count;
static int PVRSRVAllocDeviceMem(PVRSRV_DEV_DATA *dev, void *heap, unsigned flags,
                               unsigned bytes, unsigned alignment, void *reference,
                               PVRSRV_CLIENT_MEM_INFO **out)
{
    (void)dev; (void)heap; (void)bytes; (void)alignment; (void)reference;
    assert(flags & PVRSRV_MEM_NO_SYNCOBJ);
    if (fail_memory) return -10;
    *out = malloc(sizeof(**out));
    assert(*out);
    memory_count++;
    return 0;
}
static int PVRSRVAllocSyncInfo(PVRSRV_DEV_DATA *dev, PVRSRV_CLIENT_SYNC_INFO **out)
{
    (void)dev;
    if (fail_sync) return -20;
    *out = malloc(sizeof(**out));
    assert(*out);
    sync_count++;
    return 0;
}
static int PVRSRVFreeDeviceMem(PVRSRV_DEV_DATA *dev, PVRSRV_CLIENT_MEM_INFO *info)
{
    (void)dev;
    if (fail_free) return -30;
    assert(memory_count == 1);
    free(info); /* ASan detects any subsequent descriptor access. */
    memory_count--;
    return 0;
}
static int PVRSRVFreeSyncInfo(PVRSRV_DEV_DATA *dev, PVRSRV_CLIENT_SYNC_INFO *info)
{
    (void)dev;
    assert(sync_count == 1);
    free(info);
    sync_count--;
    return 0;
}

/* Extracted unchanged from the driver's srv.c by the Rust host checker. */
#include "device_mem_functions.inc"
#define IMGEGLALLOCDEVICEMEM(d,h,f,b,a,o) KEGLAllocDeviceMemPsp2(psSysContext,d,h,f,b,a,o)
#include "../../vendor/PVR_PSP2/eurasiacon/imgegl/imgegl/render_command_alloc.h"

int main(void)
{
    SrvSysContext context = {0};
    PVRSRV_DEV_DATA dev = {0};
    PVRSRV_CLIENT_MEM_INFO *info;
    int iteration;
    for (iteration = 0; iteration < 64; iteration++) {
        info = (void *)1;
        fail_memory = 1;
        assert(KEGLAllocDeviceMemPsp2(&context, &dev, NULL, 0, 1024, 4, &info) == -10);
        assert(!info && !memory_count && !sync_count);
        fail_memory = 0;
        fail_sync = 1;
        assert(KEGLAllocDeviceMemPsp2(&context, &dev, NULL, 0, 1024, 4, &info) == -20);
        assert(!info && !memory_count && !sync_count);
        fail_sync = 0;
        assert(!KEGLAllocDeviceMemPsp2(&context, &dev, NULL, 0, 1024, 4, &info));
        assert(memory_count == 1 && sync_count == 1);
        fail_free = 1;
        assert(KEGLFreeDeviceMemPsp2(&context, &dev, info) == -30);
        assert(memory_count == 1 && sync_count == 1);
        fail_free = 0;
        assert(!KEGLFreeDeviceMemPsp2(&context, &dev, info));
        assert(!memory_count && !sync_count);
        assert(!KEGLAllocDeviceMemPsp2(&context, &dev, NULL, PVRSRV_MEM_NO_SYNCOBJ, 1024, 4, &info));
        assert(!KEGLFreeDeviceMemPsp2(&context, &dev, info));
        assert(!memory_count && !sync_count);
    }
    /* Even with sync quota exhausted, both command stores remain allocatable;
     * a required surface sync must still fail and roll back its allocation. */
    fail_sync = 1;
    for(iteration = 0; iteration < 64; ++iteration) {
        unsigned sizes[] = {32768,1024};
        for(unsigned j=0;j<2;++j) {
            assert(KrkrAllocRenderCommandBuffer(&context,&dev,NULL,sizes[j],16,&info,"command") == 0);
            assert(info && !info->psClientSyncInfo && !sync_count);
            assert(KEGLFreeDeviceMemPsp2(&context,&dev,info) == 0);
        }
        assert(KEGLAllocDeviceMemPsp2(&context,&dev,NULL,PVRSRV_MEM_READ,4,4,&info) == -20);
        assert(!info && !memory_count && !sync_count);
    }
    puts("device memory: allocation rollback and descriptor lifetime passed");
    return 0;
}
