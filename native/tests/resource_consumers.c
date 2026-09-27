#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned IMG_UINT32;
typedef int IMG_BOOL;
#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_NULL NULL
#define PVR_ASSERT assert
#define PVR_DPF(x) ((void)0)
#define KRM_TYPE_3D 1
#define KRM_TYPE_TA 2
typedef struct { unsigned ui32StatusValue; } Status;
typedef struct { unsigned ui32Value, ui32Next; Status *psStatusUpdate; void *pvAttachmentPoint; } KRMAttachment;
typedef struct { unsigned ui32FirstAttachment; } KRMResource;
typedef struct { unsigned bInitialized, eType; KRMAttachment *asAttachment; } KRMKickResourceManager;
static unsigned lock_depth, called[8], calls;
#define KRM_ENTER_CRITICAL_SECTION(m) do { assert(lock_depth++ == 0); } while(0)
#define KRM_EXIT_CRITICAL_SECTION(m) do { assert(lock_depth-- == 1); } while(0)
static unsigned targets[8];
static void *context;
static void schedule(void *ctx, void *target) {
    assert(lock_depth == 1);
    if(target) assert(ctx == context);
    else target = ctx;
    unsigned id = *(unsigned *)target;
    assert(id < 8); ++called[id]; ++calls;
}
#include "resource_consumers_functions.inc"
int main(void) {
    Status status = {10};
    KRMAttachment attachments[8] = {0};
    KRMKickResourceManager manager = {.bInitialized = 1, .eType = KRM_TYPE_3D, .asAttachment = attachments};
    KRMResource deleting = {1}, unrelated = {4};
    context = &manager;
    for(unsigned i=0;i<8;++i) targets[i] = i;
    /* Deleted texture: two unsubmitted readers and one already kicked reader. */
    manager.asAttachment[1] = (KRMAttachment){10,2,&status,&targets[1]};
    manager.asAttachment[2] = (KRMAttachment){9,3,&status,&targets[2]};
    manager.asAttachment[3] = (KRMAttachment){10,0,&status,&targets[3]};
    /* An independent active FBO must remain unsubmitted. */
    manager.asAttachment[4] = (KRMAttachment){10,0,&status,&targets[4]};
    assert(KRM_FlushUnKickedResource(&manager,&deleting,context,schedule));
    assert(calls == 2 && called[1] == 1 && called[3] == 1 && !called[2] && !called[4] && !lock_depth);
    assert(KRM_FlushUnKickedResource(&manager,&unrelated,context,schedule));
    assert(calls == 3 && called[4] == 1);
    status.ui32StatusValue = 11;
    assert(KRM_FlushUnKickedResource(&manager,&deleting,context,schedule));
    assert(calls == 3);
    manager.eType = KRM_TYPE_TA; status.ui32StatusValue = 10;
    assert(KRM_FlushUnKickedResource(&manager,&deleting,context,schedule));
    assert(calls == 5 && !lock_depth);
    manager.eType = 99;
    assert(!KRM_FlushUnKickedResource(&manager,&deleting,context,schedule) && !lock_depth);
    puts("resource consumers: only unsubmitted readers kicked; unrelated/completed readers preserved");
}
