/* Execute production retirement phases against failing ownership operations. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned IMG_UINT32;
typedef int IMG_INT32;
typedef int IMG_BOOL;
typedef char IMG_CHAR;
typedef void *IMG_HANDLE;
typedef int SceUID;
typedef int PVRSRV_ERROR;
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define PVRSRV_OK 0
#define EGL_RENDER_TARGET_NOAA_INDEX 0
#define EGL_RENDER_TARGET_AA_INDEX 1

typedef struct { void *pvLinAddr; unsigned id; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { unsigned value; } PVRSRV_CLIENT_SYNC_INFO;
typedef struct {
    int s3D; void *hRenderContext, *psConnection, *hMemoryStatsLock;
    struct { struct { void *hOSGlobalEvent; } sMiscInfo; } sHWInfo;
    unsigned terminalState; const char *terminalOperation; int terminalCode;
} SrvSysContext;
static int lock_depth;
static void PVRSRVLockMutex(void *lock) { (void)lock; assert(lock_depth++ == 0); }
static void PVRSRVUnlockMutex(void *lock) { (void)lock; assert(--lock_depth == 0); }
#include "../../vendor/PVR_PSP2/eurasiacon/include/device_fault.h"

typedef struct {
    PVRSRV_CLIENT_MEM_INFO *psMemInfo, *psStatusUpdateMemInfo;
    unsigned *pui32ReadOffset, ui32CommittedHWOffsetInBytes;
} Buffer;
typedef struct {
    struct { PVRSRV_CLIENT_MEM_INFO *psTerminatePDSMemInfo, *psSAUpdatePDSMemInfo, *psTerminateUSEMemInfo; } sTerm;
    Buffer sPDSBuffer, sUSSEBuffer;
    struct { PVRSRV_CLIENT_MEM_INFO *psMemInfo; } sRenderStatusUpdate;
    PVRSRV_CLIENT_SYNC_INFO *psRenderSurfaceSyncInfo, *psSyncInfo;
    void *ahRenderTarget[2];
    PVRSRV_CLIENT_MEM_INFO *psZSBufferMemInfo;
    int hZSBufferMemBlockUID;
    void *hMutex;
} EGLRenderSurface;
static unsigned step, fail_at, operations[32], device_frees, rt_frees, descriptor_frees;
static int phase(unsigned operation) {
    assert(step < 32 && lock_depth == 0);
    operations[step++] = operation;
    return step == fail_at ? -17 : 0;
}
static int SGXGetRenderTargetDriverMemBlock(int *device, void *target, int *uid, int *internal) {
    int error; (void)device; assert(target); error = phase(2);
    if(!error) { *uid = 100 + (int)(uintptr_t)target; *internal = 0; }
    return error;
}
static int SGXRemoveRenderTarget(int *device, void *context, void *target) {
    (void)device; (void)context; assert(target); return phase(3);
}
static int PVRSRVUnregisterMemBlock(int *device, int uid) {
    (void)device; assert(uid == 101 || uid == 102); return phase(4);
}
static int KrkrFreeRenderTargetBlock(SrvSysContext *context, int uid) {
    int error; (void)context; assert(uid == 101 || uid == 102); error = phase(5);
    if(!error) ++rt_frees;
    return error;
}
static int PVRSRVUnmapMemoryFromGpu(int *device, void *address, int unused, int flag) {
    (void)device; (void)unused; (void)flag; assert(address); return phase(6);
}
static int sceKernelFreeMemBlock(int uid) { assert(uid == 200); return phase(7); }
static void PVRSRVFreeUserModeMem(void *memory) { assert(memory); ++descriptor_frees; }
static int PVRSRVPollForValue(void *connection, void *event, unsigned *read, unsigned committed,
    unsigned mask, unsigned wait, unsigned retries) {
    (void)connection; (void)event; assert(read && committed == 3 && mask == ~0U && wait == 1000 && retries == 1000);
    return phase(1);
}
static int FreeMemory(int *device, PVRSRV_CLIENT_MEM_INFO *memory) {
    int error; (void)device; assert(memory); error = phase(8);
    if(!error) ++device_frees;
    return error;
}
#define IMGEGLFREEDEVICEMEM(device, memory) FreeMemory(device, memory)
static int PVRSRVFreeSyncInfo(int *device, PVRSRV_CLIENT_SYNC_INFO *sync) {
    (void)device; assert(sync); return phase(9);
}
static int PVRSRVDestroyMutex(void *mutex) { assert(mutex); return phase(10); }
#include "../../vendor/PVR_PSP2/eurasiacon/imgegl/imgegl/render_target_retire.h"

static void seed(EGLRenderSurface *surface) {
    static PVRSRV_CLIENT_MEM_INFO memory[9];
    static PVRSRV_CLIENT_SYNC_INFO sync;
    static unsigned read;
    unsigned i;
    memset(surface, 0, sizeof(*surface));
    for(i = 0; i < 9; ++i) { memory[i].id = i; memory[i].pvLinAddr = &memory[i]; }
    surface->sTerm.psTerminatePDSMemInfo = &memory[0];
    surface->sTerm.psSAUpdatePDSMemInfo = &memory[1];
    surface->sTerm.psTerminateUSEMemInfo = &memory[2];
    surface->sPDSBuffer.psMemInfo = &memory[3];
    surface->sUSSEBuffer.psMemInfo = &memory[4];
    surface->sRenderStatusUpdate.psMemInfo = &memory[5];
    surface->sUSSEBuffer.psStatusUpdateMemInfo = &memory[6];
    surface->sPDSBuffer.psStatusUpdateMemInfo = &memory[7];
    surface->psZSBufferMemInfo = &memory[8]; surface->hZSBufferMemBlockUID = 200;
    surface->psRenderSurfaceSyncInfo = surface->psSyncInfo = &sync;
    surface->ahRenderTarget[0] = (void *)1; surface->ahRenderTarget[1] = (void *)2;
    surface->hMutex = &read; surface->sPDSBuffer.pui32ReadOffset = &read;
    surface->sPDSBuffer.ui32CommittedHWOffsetInBytes = 3;
}
int main(void) {
    unsigned failure;
    for(failure = 0; failure <= 21; ++failure) {
        SrvSysContext context = {0}; EGLRenderSurface surface;
        PvrDeviceFaultSnapshot fault;
        context.hMemoryStatsLock = &context;
        seed(&surface); step = device_frees = rt_frees = descriptor_frees = 0; fail_at = failure;
        if(!failure) {
            assert(KrkrRetireSurfaceStorage(&context, &surface, IMG_TRUE));
            assert(step == 21 && device_frees == 8 && rt_frees == 2 && descriptor_frees == 1);
            assert(!surface.psRenderSurfaceSyncInfo && !surface.psSyncInfo);
            assert(!surface.ahRenderTarget[0] && !surface.ahRenderTarget[1] && !surface.psZSBufferMemInfo && !surface.hMutex);
            assert(!KrkrQueryDeviceFault(&context, &fault));
        } else {
            unsigned stopped;
            assert(!KrkrRetireSurfaceStorage(&context, &surface, IMG_TRUE));
            assert(step == failure && KrkrQueryDeviceFault(&context, &fault) == 1 && fault.code == -17);
            stopped = step;
            assert(!KrkrRetireSurfaceStorage(&context, &surface, IMG_TRUE) && step == stopped);
            KrkrDeviceFault(&context, "later-failure", -99);
            assert(KrkrQueryDeviceFault(&context, &fault) == 1 && fault.code == -17);
            if(failure == 1) assert(!device_frees && !rt_frees && !descriptor_frees && surface.sPDSBuffer.psMemInfo);
            if(failure <= 17) assert(!descriptor_frees && surface.psZSBufferMemInfo);
        }
        assert(lock_depth == 0);
    }
    puts("retirement: every production ownership phase stops at the first fault and preserves later owners");
    return 0;
}
