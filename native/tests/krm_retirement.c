/* The production KRM loops must stop before their post-callback metadata frees. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef unsigned IMG_UINT32;
typedef unsigned char IMG_UINT8;
typedef int IMG_BOOL;
typedef void IMG_VOID;
#define IMG_INTERNAL
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define PVR_ASSERT assert
#define PVR_DPF(...) ((void)0)
#define PVRSRVMemSet memset
#define KRM_DEFAULT_WAIT_RETRIES 100
#include "../../vendor/PVR_PSP2/eurasiacon/include/device_fault_state.h"
typedef struct KRMResourceRec { unsigned ui32Waiters, ui32FirstAttachment; struct KRMResourceRec *psPrev, *psNext; } KRMResource;
typedef struct { unsigned ui32Next; } KRMAttachment;
typedef struct KRMKickResourceManagerRec {
    int bInitialized;
    KRMResource *psResourceList, *psGhostList;
    KRMAttachment *asAttachment;
    unsigned ui32MaxAttachments;
    const unsigned *pui32TerminalState;
    void (*pfnDestroyGhost)(void *, KRMResource *);
} KRMKickResourceManager;
typedef struct { KRMResource resource; unsigned id; int backing_live; } Ghost;
static unsigned terminal, callbacks, free_metadata, called[4];
static int lock_depth;
#define KRM_ENTER_CRITICAL_SECTION(manager) ((void)(manager), assert(lock_depth++ == 0))
#define KRM_EXIT_CRITICAL_SECTION(manager) ((void)(manager), assert(--lock_depth == 0))
static int IsResourceNeeded(KRMKickResourceManager *manager, KRMResource *resource) { (void)manager; (void)resource; return 0; }
static int WaitUntilResourceIsNotNeeded(KRMKickResourceManager *manager, KRMResource *resource, unsigned retries) { (void)manager; (void)resource; assert(retries == 100); return 1; }
static void FreeAttachment(KRMKickResourceManager *manager, unsigned attachment) { (void)manager; (void)attachment; assert(0); }
static void PVRSRVFreeUserModeMem(void *memory) { assert(memory); ++free_metadata; }
#include "krm_retirement_functions.inc"
static void release_ghost(void *context, KRMResource *resource) {
    Ghost *ghost = (Ghost *)((char *)resource - offsetof(Ghost, resource));
    (void)context;
    assert(!terminal && ghost->backing_live);
    ++callbacks; ++called[ghost->id];
    if(ghost->id == 2) { __atomic_store_n(&terminal, 1, __ATOMIC_RELEASE); return; }
    ghost->backing_live = 0;
}
static void seed(KRMKickResourceManager *manager, Ghost ghosts[3], int remove) {
    static KRMAttachment attachments[1];
    unsigned i;
    memset(manager, 0, sizeof(*manager)); memset(ghosts, 0, 3 * sizeof(*ghosts));
    terminal = callbacks = free_metadata = 0; memset(called, 0, sizeof(called));
    manager->bInitialized = 1; manager->asAttachment = attachments;
    manager->ui32MaxAttachments = 1; manager->pui32TerminalState = &terminal;
    manager->pfnDestroyGhost = release_ghost;
    for(i = 0; i < 3; ++i) {
        ghosts[i].id = i + 1; ghosts[i].backing_live = 1;
        if(i) ghosts[i].resource.psPrev = &ghosts[i-1].resource;
        if(i < 2) ghosts[i].resource.psNext = &ghosts[i+1].resource;
    }
    if(remove) manager->psGhostList = &ghosts[0].resource;
    else manager->psResourceList = &ghosts[0].resource;
}
int main(void) {
    KRMKickResourceManager manager; Ghost ghosts[3];
    seed(&manager, ghosts, 1);
    KRM_Destroy(NULL, &manager);
    assert(terminal && callbacks == 2 && called[1] == 1 && called[2] == 1 && !called[3]);
    assert(!ghosts[0].backing_live && ghosts[1].backing_live && ghosts[2].backing_live);
    assert(manager.bInitialized && manager.asAttachment && !free_metadata);
    assert(manager.psGhostList == &ghosts[2].resource);
    KRM_Destroy(NULL, &manager); assert(callbacks == 2 && !free_metadata);
    seed(&manager, ghosts, 1);
    ReclaimUnneededResourcesInList(&manager, &manager.psGhostList, release_ghost, NULL, 1);
    assert(terminal && callbacks == 2 && called[3] == 1 && called[2] == 1 && !called[1]);
    assert(ghosts[0].backing_live && ghosts[1].backing_live && !ghosts[2].backing_live);
    assert(manager.bInitialized && manager.asAttachment && !free_metadata);
    ReclaimUnneededResourcesInList(&manager, &manager.psGhostList, release_ghost, NULL, 1);
    assert(callbacks == 2 && lock_depth == 0);
    seed(&manager, ghosts, 0);
    ReclaimUnneededResourcesInList(&manager, &manager.psResourceList, release_ghost, NULL, 0);
    assert(terminal && callbacks == 2 && lock_depth == 0 && manager.psResourceList);
    assert(ghosts[1].backing_live && ghosts[2].backing_live && !free_metadata);
    puts("KRM: failed callback retains backing, detached descriptors, attachment storage and manager state");
    return 0;
}
