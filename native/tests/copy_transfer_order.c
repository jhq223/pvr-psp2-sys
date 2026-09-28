#include <assert.h>
#include <stdio.h>

#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_NULL NULL
#define PVR_ASSERT assert
#define PVR_DPF(x) ((void)0)
#define KRM_TYPE_3D 1
#define KRM_TYPE_TA 2
#define PVRSRV_OK 0
typedef unsigned IMG_UINT32;
typedef int IMG_BOOL, PVRSRV_ERROR;
typedef struct { unsigned ui32StatusValue; } Status;
typedef struct {
    unsigned ui32Value, ui32Next;
    Status *psStatusUpdate;
    void *pvAttachmentPoint;
} KRMAttachment;
typedef struct { unsigned ui32FirstAttachment; } KRMResource;
typedef struct {
    unsigned bInitialized, eType;
    KRMAttachment *asAttachment;
} KRMKickResourceManager;
static unsigned lock_depth;
#define KRM_ENTER_CRITICAL_SECTION(m) do { assert(lock_depth++ == 0); } while(0)
#define KRM_EXIT_CRITICAL_SECTION(m) do { assert(lock_depth-- == 1); } while(0)
#include "resource_consumers_functions.inc"

typedef struct { unsigned waits; } PVRSRV_CLIENT_SYNC_INFO;
typedef struct { PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo; } Memory;
typedef struct { KRMResource sResource; Memory *psMemInfo; } GLES2Texture;
typedef struct { PVRSRV_CLIENT_SYNC_INFO *psSyncInfo; } EGLDrawableParams;
typedef struct { KRMKickResourceManager sKRM; } TextureManager;
typedef struct { TextureManager *psTextureManager; } Shared;
typedef struct { int s3D; void *hTransferContext; } System;
typedef struct { Shared *psSharedState; System *psSysContext; } GLES2Context;
typedef struct { unsigned *destination; unsigned value; } SGX_QUEUETRANSFER;
typedef struct {
    Status status;
    unsigned *sampled;
    unsigned observed, kicks;
    int queued;
} Reader;
static Reader readers[4];
static unsigned submissions, epochs;
static int fail_transfer;
static GLES2Context *context;

static void KickUnFlushed_ScheduleTA(void *ctx, void *surface) {
    Reader *reader = surface;
    assert(ctx == context && lock_depth == 1 && !reader->queued);
    reader->queued = 1;
    ++reader->kicks;
    ++reader->status.ui32StatusValue;
}
static void execute_readers(void) {
    for(unsigned i=0;i<4;++i) {
        if(readers[i].queued) {
            readers[i].observed = *readers[i].sampled;
            readers[i].queued = 0;
        }
    }
}
static int SGXQueueTransfer(int *device, void *transfer, SGX_QUEUETRANSFER *copy) {
    assert(!lock_depth);
    ++submissions;
    /* 3DTQ_SYNC orders submitted 3D work, not readers still held by a surface. */
    execute_readers();
    if(fail_transfer) return 1;
    *copy->destination = copy->value;
    return PVRSRV_OK;
}
static void SWTextureTransferSubmitted(GLES2Context *gc) { ++epochs; }
static int SGX2DQueryBlitsComplete(int *device, PVRSRV_CLIENT_SYNC_INFO *sync, int wait) {
    assert(wait);
    ++sync->waits;
    return PVRSRV_OK;
}
#include "copy_transfer_order_functions.inc"

int main(void) {
    enum { RED = 1, YELLOW = 4 };
    unsigned destination = RED, unrelated = 9;
    KRMAttachment attachments[5] = {0};
    TextureManager manager = {{1, KRM_TYPE_3D, attachments}};
    Shared shared = {&manager}; System system = {0};
    GLES2Context gc = {&shared, &system}; context = &gc;
    PVRSRV_CLIENT_SYNC_INFO src_sync = {0}, dst_sync = {0};
    Memory memory = {&dst_sync};
    GLES2Texture texture = {{1}, &memory};
    EGLDrawableParams source = {&src_sync};
    SGX_QUEUETRANSFER copy = {&destination, YELLOW};
    for(unsigned i=0;i<4;++i) {
        readers[i] = (Reader){{10}, i==3 ? &unrelated : &destination, 0, 0, 0};
        attachments[i+1] = (KRMAttachment){10, i<2 ? i+2 : 0, &readers[i].status, &readers[i]};
    }
    /* Two unsubmitted readers, one already submitted reader, one unrelated. */
    readers[2].status.ui32StatusValue = 11;
    readers[2].queued = 1;
    assert(HWTQTextureNormalBlit(&gc, &texture, &source, &copy));
    /* Finish the readers left pending by an incorrect transfer implementation. */
    for(unsigned i=0;i<2;++i) {
        if(readers[i].observed == 0) readers[i].observed = destination;
        if(readers[i].observed != RED) {
            fprintf(stderr, "FAIL: reader %u observed later copy value %u, expected %u\n", i, readers[i].observed, RED);
            return 1;
        }
    }
    assert(readers[2].observed == RED && !readers[2].kicks);
    assert(readers[0].kicks == 1 && readers[1].kicks == 1);
    assert(!readers[3].kicks && !readers[3].observed);
    assert(destination == YELLOW && submissions == 1 && epochs == 1);
    assert(!src_sync.waits && !dst_sync.waits);

    /* Rewriting after completion adds no reader submission or CPU wait. */
    copy.value = 7;
    assert(HWTQTextureNormalBlit(&gc, &texture, &source, &copy));
    assert(destination == 7 && submissions == 2 && epochs == 2);
    assert(readers[0].kicks == 1 && readers[1].kicks == 1 && !readers[3].kicks);
    assert(!src_sync.waits && !dst_sync.waits);

    /* Preserve the existing failed-transfer fallback and both completion waits. */
    fail_transfer = 1; copy.value = 8;
    assert(!HWTQTextureNormalBlit(&gc, &texture, &source, &copy));
    assert(destination == 7 && submissions == 3 && epochs == 3);
    assert(src_sync.waits == 1 && dst_sync.waits == 1 && !lock_depth);
    puts("copy transfer: pending readers precede overwrite, unrelated/completed readers untouched, fallback preserved");
}
