#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned IMG_UINT32;
typedef uint64_t IMG_UINT64;
typedef int IMG_BOOL;
#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_NULL NULL
#define PVRSRV_OK 0
#define PVR_DPF(x) ((void)0)
#define PVR_ASSERT(x) assert(x)
#define PVRSRVMemSet memset
typedef struct KRMResource { unsigned ui32FirstAttachment, ui32Waiters; struct KRMResource *psNext, *psPrev; } KRMResource;
typedef struct { KRMResource *psResourceList, *psGhostList; unsigned bInitialized; } KRMKickResourceManager;
static unsigned lock_depth, needed, kicked = 1, gpu_failure, destroyed;
static unsigned wait_count, complete_at;
static uint64_t process_time, wait_step;
static int unrelated_wake;
static uint64_t sceKernelGetProcessTimeWide(void) { return process_time; }
static KRMKickResourceManager manager;
static KRMResource resource;
#define KRM_ENTER_CRITICAL_SECTION(m) do { assert(lock_depth++ == 0); } while(0)
#define KRM_EXIT_CRITICAL_SECTION(m) do { assert(lock_depth-- == 1); } while(0)
static int IsResourceNeeded(const KRMKickResourceManager *m, const KRMResource *r) { assert(lock_depth == 1); return needed; }
static int IsResourceKicked(const KRMKickResourceManager *m, const KRMResource *r) { return kicked; }
static void RemoveResourceFromAllLists(KRMKickResourceManager *m, KRMResource *r) {
    if(r->psPrev) r->psPrev->psNext = r->psNext;
    else if(m->psResourceList == r) m->psResourceList = r->psNext;
    else if(m->psGhostList == r) m->psGhostList = r->psNext;
    if(r->psNext) r->psNext->psPrev = r->psPrev;
}
static void destroy(void *context, KRMResource *r) { assert(!r->ui32Waiters && !needed); ++destroyed; }
static int sceGpuSignalWait(void *signal, unsigned timeout);
static void *sceKernelGetTLSAddr(unsigned index) { return NULL; }
#include "resource_wait_functions.inc"
static int sceGpuSignalWait(void *signal, unsigned timeout) {
    assert(!lock_depth && resource.ui32Waiters == 1);
    ++wait_count;
    process_time += wait_step ? wait_step : timeout;
    if(gpu_failure) return -1;
    if(unrelated_wake && wait_count != complete_at) return 0;
    needed = 0;
    /* Another context reclaims while this thread has dropped the manager lock. */
    ReclaimUnneededResourcesInList(&manager, &manager.psGhostList, destroy, NULL, 1);
    assert(!destroyed && manager.psGhostList == &resource);
    return 0;
}
int main(void) {
    manager.psResourceList = &resource; manager.bInitialized = 1;
    KRM_RetireResource(&manager, &resource); assert(!manager.psResourceList && manager.psGhostList == &resource);
    needed = 1; assert(KRM_WaitUntilResourceIsNotNeeded(&manager, &resource, 2));
    assert(!lock_depth && !resource.ui32Waiters);
    needed = gpu_failure = 1;
    assert(!KRM_WaitUntilResourceIsNotNeeded(&manager, &resource, 2));
    assert(!lock_depth && !resource.ui32Waiters);
    gpu_failure = 0; unrelated_wake = 1; wait_count = 0;
    assert(!KRM_WaitUntilResourceIsNotNeeded(&manager, &resource, 2));
    assert(wait_count == 2 && !lock_depth && !resource.ui32Waiters && !destroyed);
    wait_count = 0; wait_step = 10000; complete_at = 15;
    assert(KRM_WaitUntilResourceIsNotNeeded(&manager, &resource, 2));
    assert(wait_count == 15 && !needed && !lock_depth && !resource.ui32Waiters);
    ReclaimUnneededResourcesInList(&manager, &manager.psGhostList, destroy, NULL, 1);
    assert(destroyed == 1 && !manager.psGhostList);
    puts("resource waits: unrelated wakes, elapsed deadline, unlock, waiter pinning and deferred retirement passed");
}
