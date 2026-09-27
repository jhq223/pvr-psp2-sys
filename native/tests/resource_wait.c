#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned IMG_UINT32;
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
    if(gpu_failure) return -1;
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
    needed = 0;
    ReclaimUnneededResourcesInList(&manager, &manager.psGhostList, destroy, NULL, 1);
    assert(destroyed == 1 && !manager.psGhostList);
    puts("resource waits: unlock, waiter pinning, timeout and deferred retirement passed");
}
