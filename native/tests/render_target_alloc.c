/* Exercise the production helper with failures at every ownership boundary. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

typedef unsigned IMG_UINT32;
typedef unsigned IMG_SID;
typedef int SceUID;
typedef int IMG_BOOL;
typedef void *IMG_HANDLE;
typedef struct { int s3D; } SrvSysContext;
typedef struct { int i32DataMemblockUID; unsigned hMemBlockProcRef; } SGX_ADDRENDTARG;
#define IMG_NULL NULL
#define SCE_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define PVRSRV_OK 0
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_NC_RW 1

static int failure, allocations, registrations, targets;
static int stage;
static int SGXGetRenderTargetMemSize(SGX_ADDRENDTARG *info, unsigned *bytes)
{
    (void)info;
    stage = 1;
    *bytes = failure == 5 ? 0 : 8192;
    return failure == 1 ? -1 : 0;
}
static int sceKernelAllocMemBlock(const char *name, int type, unsigned bytes, void *option)
{
    (void)name; (void)type; (void)option;
    assert(stage == 1 && bytes == 8192);
    stage = 2;
    if (failure == 2) return -1;
    allocations++;
    return 42;
}
static int PVRSRVRegisterMemBlock(int *dev, int uid, unsigned *reference, int enabled)
{
    (void)dev; (void)enabled;
    assert(stage == 2 && uid == 42 && allocations == 1);
    stage = 3;
    if (failure == 3) return -1;
    registrations++;
    *reference = 73;
    return 0;
}
static int SGXAddRenderTarget(int *dev, SGX_ADDRENDTARG *info, void **target)
{
    (void)dev;
    assert(stage == 3 && allocations == 1 && registrations == 1);
    assert(info->i32DataMemblockUID == 42 && info->hMemBlockProcRef == 73);
    stage = 4;
    if (failure == 4) return -1;
    targets++;
    *target = &targets;
    return 0;
}
static int PVRSRVUnregisterMemBlock(int *dev, int uid)
{
    (void)dev;
    assert(uid == 42 && allocations == 1 && registrations == 1);
    registrations--;
    return 0;
}
static int sceKernelFreeMemBlock(int uid)
{
    assert(uid == 42 && allocations == 1 && registrations == 0);
    allocations--;
    return 0;
}

#include "../../vendor/PVR_PSP2/eurasiacon/imgegl/imgegl/render_target_alloc.h"

int main(void)
{
    SrvSysContext context = {0};
    SGX_ADDRENDTARG info;
    IMG_HANDLE target;
    int iteration;
    for (iteration = 0; iteration < 64; iteration++) {
        for (failure = 1; failure <= 5; failure++) {
            target = &info;
            assert(!KrkrAddRenderTarget(&context, &info, &target, "test"));
            assert(!target && !allocations && !registrations && !targets);
            assert(info.i32DataMemblockUID == -1 && info.hMemBlockProcRef == 0);
        }
        failure = 0;
        assert(KrkrAddRenderTarget(&context, &info, &target, "test"));
        assert(target == &targets && allocations == 1 && registrations == 1 && targets == 1);
        targets--;
        PVRSRVUnregisterMemBlock(&context.s3D, info.i32DataMemblockUID);
        sceKernelFreeMemBlock(info.i32DataMemblockUID);
    }
    puts("render-target allocation: failure unwind and retry passed (384 cases)");
    return 0;
}
