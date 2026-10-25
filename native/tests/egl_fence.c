/* Executes extracted production EGL helpers; the fake services expose only
 * query completion, clock progress and physical release observations. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t IMG_UINT32;
typedef int IMG_BOOL;
typedef int EGLBoolean;
typedef int EGLint;
typedef uint64_t EGLTimeKHR;
typedef int PVRSRV_ERROR;
typedef struct { int unused; } PVRSRV_DEV_DATA;
#define IMG_FALSE 0
#define IMG_TRUE 1
#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_SYNC_FENCE_KHR 1
#define EGL_SYNC_REUSABLE_KHR 2
#define EGL_UNSIGNALED_KHR 3
#define EGL_SIGNALED_KHR 4
#define EGL_CONDITION_SATISFIED_KHR 5
#define EGL_TIMEOUT_EXPIRED_KHR 6
#define EGL_FOREVER_KHR UINT64_MAX
#define EGL_EXTENSION_KHR_REUSABLE_SYNC 1
#define PVRSRV_OK 0
#define PVRSRV_ERROR_CMD_NOT_PROCESSED 10
#define PVRSRV_ERROR_RETRY 11
#define PVRSRV_ERROR_BRIDGE_CALL_FAILED 12
#define PVR_ASSERT assert
#define PVR_DPF(x) ((void)0)
#define PVR_DBG_ERROR 0
#define IMG_NULL NULL
struct SyncMemory { void *psClientSyncInfo; };
typedef struct _KEGL_SYNC_ KEGL_SYNC;
typedef struct { KEGL_SYNC *psHeadSync; IMG_BOOL bSyncsTerminating; } KEGL_DISPLAY;
struct _KEGL_SYNC_ {
    KEGL_SYNC *psNextSync;
    KEGL_DISPLAY *psDpy;
    struct SyncMemory *psSyncMemInfo;
    int eglSyncType, eglSyncStatus, refcount, bIsDeleting;
    void *hReusableSemaphore;
    uint32_t ui32WaitValue;
    uint64_t ui64WaitLevel;
};
typedef struct { PVRSRV_DEV_DATA s3D; } SrvSysContext;
typedef struct { SrvSysContext sSysContext; } Global;
typedef struct { Global *psGlobalData; int lastError; } Tls;
typedef Tls *TLS;
#define IMG_INTERNAL
#define EGL_BAD_ACCESS 0x3002
static int lock_depth;
static void (*wait_hook)(void);
static void EGLThreadLock(TLS tls) { (void)tls; assert(lock_depth==0); ++lock_depth; }
static void EGLThreadUnlock(TLS tls) { (void)tls; assert(lock_depth==1); --lock_depth; }
static void PVRSRVPostSemaphore(void *sem, uint32_t count) { (void)sem; assert(count>0); }

static uint64_t clock_us, slept_us;
static unsigned queries, sleeps, complete_at, memory_frees, sync_frees, sem_frees;
static PVRSRV_ERROR query_error;
static int fail_memory_free;
static PVRSRV_ERROR SGX2DQueryBlitsComplete(PVRSRV_DEV_DATA *device, void *sync, IMG_BOOL wait) {
    (void)device; assert(sync != NULL); assert(wait == IMG_FALSE);
    ++queries;
    return complete_at && queries >= complete_at ? PVRSRV_OK : query_error;
}
static IMG_UINT32 PVRSRVClockus(void) { return (IMG_UINT32)clock_us; }
static void PVRSRVWaitus(IMG_UINT32 delay) { assert(delay > 0 && delay <= 1000); ++sleeps; slept_us += delay; clock_us += delay; if(wait_hook) wait_hook(); }
static PVRSRV_ERROR IMGEGLFREEDEVICEMEM(PVRSRV_DEV_DATA *device, struct SyncMemory *memory) {
    (void)device;
    /* The owned sync was freed, but the allocation remains retryable. */
    if (fail_memory_free) {
        memory->psClientSyncInfo = NULL;
        return PVRSRV_ERROR_BRIDGE_CALL_FAILED;
    }
    ++memory_frees; free(memory); return PVRSRV_OK;
}
static void PVRSRVDestroySemaphore(void *semaphore) { (void)semaphore; ++sem_frees; }
static void EGLFree(void *sync) { ++sync_frees; free(sync); }
#define EGL_FENCE_SYNC_COMPLETE(device,sync) _syncComplete(device,sync)
#include "egl_fence_functions.inc"
static void reset(void) {clock_us=0; slept_us=0; queries=0; sleeps=0; complete_at=0; query_error=PVRSRV_ERROR_CMD_NOT_PROCESSED;}
static KEGL_SYNC *make_sync(KEGL_DISPLAY *display, int type) {
    KEGL_SYNC *sync=calloc(1,sizeof(*sync)); assert(sync);
    sync->psDpy=display; sync->eglSyncType=type; sync->eglSyncStatus=EGL_UNSIGNALED_KHR;
    if(type==EGL_SYNC_FENCE_KHR) {sync->psSyncMemInfo=calloc(1,sizeof(*sync->psSyncMemInfo)); assert(sync->psSyncMemInfo); sync->psSyncMemInfo->psClientSyncInfo=sync;}
    sync->psNextSync=display->psHeadSync; display->psHeadSync=sync; return sync;
}
static KEGL_DISPLAY *waiting_display;
static SrvSysContext *waiting_system;
static void finish_waiter(void) {
    assert(lock_depth==0);
    assert(waiting_display->bSyncsTerminating);
    waiting_display->psHeadSync->refcount=0;
    complete_at=1;
    _cleanupSyncs(waiting_display,waiting_system);
    wait_hook=NULL;
}
int main(void) {
    SrvSysContext context={0}; KEGL_DISPLAY display={0};
    KEGL_SYNC *sync=make_sync(&display,EGL_SYNC_FENCE_KHR);
    reset();
    assert(_waitFence(sync,&context,0)==EGL_TIMEOUT_EXPIRED_KHR);
    assert(queries==1 && sleeps==0);
    reset(); query_error=PVRSRV_ERROR_RETRY;
    assert(_waitFence(sync,&context,100000)==EGL_TIMEOUT_EXPIRED_KHR);
    assert(slept_us==100 && queries==2 && sleeps==1);
    reset(); clock_us=UINT32_MAX-1000; query_error=PVRSRV_ERROR_RETRY;
    assert(_waitFence(sync,&context,2500000)==EGL_TIMEOUT_EXPIRED_KHR);
    assert(slept_us==2500 && sleeps==3);
    reset(); complete_at=4;
    assert(_waitFence(sync,&context,EGL_FOREVER_KHR)==EGL_CONDITION_SATISFIED_KHR);
    assert(queries==4 && sleeps==3 && slept_us==3000);
    reset(); query_error=PVRSRV_ERROR_BRIDGE_CALL_FAILED;
    assert(_waitFence(sync,&context,EGL_FOREVER_KHR)==EGL_FALSE);
    assert(queries==1 && sleeps==0);
    reset(); sync->bIsDeleting=EGL_TRUE; _cleanupSyncs(&display,&context);
    assert(display.psHeadSync==sync && memory_frees==0 && sync_frees==0);
    reset(); complete_at=1; sync->refcount=1; _cleanupSyncs(&display,&context);
    assert(display.psHeadSync==sync && memory_frees==0);
    sync->refcount=0; _cleanupSyncs(&display,&context);
    assert(display.psHeadSync==NULL && memory_frees==1 && sync_frees==1);
    reset(); sync=make_sync(&display,EGL_SYNC_REUSABLE_KHR); sync->bIsDeleting=EGL_TRUE;
    _cleanupSyncs(&display,&context);
    assert(display.psHeadSync==NULL && queries==0 && sem_frees==1 && sync_frees==2);
    reset(); sync=make_sync(&display,EGL_SYNC_FENCE_KHR); sync->refcount=1;
    Global global={0}; Tls tls={&global,0};
    waiting_display=&display; waiting_system=&global.sSysContext; wait_hook=finish_waiter;
    EGLThreadLock(&tls);
    assert(TerminateSyncs(&display,&tls)==EGL_TRUE);
    assert(display.psHeadSync==NULL && lock_depth==1);
    EGLThreadUnlock(&tls);
    reset(); sync=make_sync(&display,EGL_SYNC_FENCE_KHR); query_error=PVRSRV_ERROR_BRIDGE_CALL_FAILED;
    EGLThreadLock(&tls);
    assert(TerminateSyncs(&display,&tls)==EGL_FALSE);
    assert(tls.lastError==EGL_BAD_ACCESS && display.psHeadSync==sync);
    complete_at=1;
    assert(TerminateSyncs(&display,&tls)==EGL_TRUE);
    EGLThreadUnlock(&tls);
    reset(); sync=make_sync(&display,EGL_SYNC_FENCE_KHR);
    sync->bIsDeleting=EGL_TRUE; complete_at=1; fail_memory_free=1;
    unsigned frees_before=memory_frees;
    _cleanupSyncs(&display,&context);
    assert(display.psHeadSync==sync && memory_frees==frees_before);
    assert(sync->eglSyncStatus==EGL_SIGNALED_KHR);
    assert(sync->psSyncMemInfo->psClientSyncInfo==NULL);
    unsigned queries_before=queries;
    fail_memory_free=0;
    _cleanupSyncs(&display,&context);
    assert(display.psHeadSync==NULL && memory_frees==frees_before+1);
    assert(queries==queries_before);
    puts("EGL fence: nonblocking query, deadlines, clock wrap, errors and deferred ownership verified");
    return 0;
}
