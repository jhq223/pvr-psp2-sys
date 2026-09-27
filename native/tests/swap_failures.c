#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define IMG_CALLCONV
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define PVR_DPF(x) ((void)0)
#define PVRSRV_OK 0
#define PVRSRV_ERROR_INVALID_PARAMS 1
#define PVRSRV_ERROR_INVALID_SWAPINTERVAL 2
#define PSP2_SWAPCHAIN_MAX_PENDING_COUNT 3
#define PSP2_SWAPCHAIN_MIN_INTERVAL 1
#define PSP2_SWAPCHAIN_MAX_INTERVAL 4
#define PVRSRV_MAX_DC_CLIP_RECTS 4
#define PVRSRV_MODIFYSYNCOPS_FLAGS_RO_INC 1
#define SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 0
#define SCE_DISPLAY_UPDATETIMING_NEXTVSYNC 0
#define SCE_KERNEL_EVF_WAITMODE_OR 1
#define SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT 2
typedef int IMG_INT32, IMG_BOOL, PVRSRV_ERROR, SceUID, PVRSRV_CONNECTION, PVRSRV_CLIENT_SYNC_INFO, IMG_RECT;
typedef unsigned IMG_UINT32, SceSize;
typedef void *IMG_PVOID, *IMG_HANDLE;
typedef uintptr_t IMG_SID;
typedef struct { void *pvLinAddr; PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { PVRSRV_CLIENT_SYNC_INFO *psInfoOld, *psInfoNew; } PVRSRV_OP_CLIENT_SYNC_INFO;
typedef struct { unsigned size,pitch,pixelformat,width,height; void *base; } SceDisplayFrameBuf;
typedef struct { PVRSRV_CONNECTION *psConnection; struct { unsigned ui32ByteStride,ui32Width,ui32Height; } sDims; } PSP2_SWAPCHAIN;
static PVRSRV_CLIENT_SYNC_INFO *s_psOldBufSyncInfo;
static PVRSRV_ERROR s_eSwapError;
static IMG_BOOL s_flipChainExists;
static unsigned s_ui32CurrentSwapChainIdx, s_ui32CurrentSwapInterval;
static void *s_pvCurrentNewBuf[3];
static IMG_SID s_hKernelSwapChainSync[3]={10,11,12};
static SceUID s_hSwapChainReadyEvf=1, s_hSwapChainPendingEvf=2;
static int ready=1,pending,pending_error,wait_error,displayed,completed,worker_waits,worker;
static int sceKernelWaitEventFlag(int id,int bits,int flags,void *out,void *timeout) {
    if(id==1) { assert(ready); ready=0; }
    else { assert(worker); if(worker_waits++) s_flipChainExists=0; else { assert(pending); pending=0; } }
    return 0;
}
static int sceKernelSetEventFlag(int id,int bits) { if(id==1) ready=1; else pending=1; return 0; }
static int PVRSRVWaitSyncOp(IMG_SID id,void *timeout) { assert(id==11); return wait_error; }
static int PVRSRVModifyPendingSyncOps(PVRSRV_CONNECTION *c,IMG_SID id,PVRSRV_OP_CLIENT_SYNC_INFO *s,unsigned n,int f,void *a,void *b) { assert(!ready && !pending && id==11); return pending_error; }
static int PVRSRVModifyCompleteSyncOps(PVRSRV_CONNECTION *c,IMG_SID id) { ++completed; return 0; }
static int sceDisplaySetFrameBuf(SceDisplayFrameBuf *fb,int mode) { ++displayed; return 0; }
static int sceDisplayWaitVblankStartMulti(unsigned n) { return 0; }
static int sceKernelExitDeleteThread(int x) { return x; }
#include "swap_failures_functions.inc"
int main(void) {
    PVRSRV_CONNECTION conn=1; PVRSRV_CLIENT_SYNC_INFO old=1,next=2;
    PVRSRV_CLIENT_MEM_INFO mem={(void *)0x1000,&next}; s_psOldBufSyncInfo=&old;
    pending_error=9;
    assert(PVRSRVSwapToDCBuffer(&conn,(uintptr_t)&mem,0,NULL,1,NULL)==9);
    assert(ready && !pending && s_ui32CurrentSwapChainIdx==0 && s_psOldBufSyncInfo==&old);
    pending_error=0; assert(PVRSRVSwapToDCBuffer(&conn,(uintptr_t)&mem,0,NULL,1,NULL)==0);
    assert(!ready && pending && s_ui32CurrentSwapChainIdx==1 && s_psOldBufSyncInfo==&next);
    PSP2_SWAPCHAIN chain={&conn,{3840,960,544}}, *p=&chain;
    worker=1; s_flipChainExists=1; wait_error=7; _dcSwapChainThread(sizeof(p),&p);
    assert(ready && !pending && !displayed && !completed && s_eSwapError==7);
    assert(PVRSRVSwapToDCBuffer(&conn,(uintptr_t)&mem,0,NULL,1,NULL)==7);
    assert(ready && !pending && s_ui32CurrentSwapChainIdx==1);
    worker_waits=0; s_flipChainExists=1; pending=1; wait_error=0; _dcSwapChainThread(sizeof(p),&p);
    assert(displayed==1 && completed==1 && ready && !pending);
    puts("swap failures: pending rollback, failed wait, error propagation and normal presentation passed");
}
