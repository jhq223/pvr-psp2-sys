#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef unsigned IMG_UINT32, GLenum, GLuint;
typedef int IMG_BOOL;
#define GL_APICALL
#define GL_APIENTRY
#define IMG_NULL NULL
#define GLES2_EXTENSION_EGL_IMAGE
#define GLES2_NAMETYPE_FRAMEBUFFER 0
#define GLES2_COLOR_ATTACHMENT 0
#define GLES2_MAX_ATTACHMENTS 3
#define GL_FRAMEBUFFER 1
#define GL_FRAMEBUFFER_COMPLETE 2
#define GLES2_FRAMEBUFFER_STATUS_UNKNOWN 3
#define GL_RENDERBUFFER 4
#define GL_TEXTURE 5
#define GL_INVALID_ENUM 6
#define GL_INVALID_OPERATION 7
#define GL_OUT_OF_MEMORY 8
#define GLES2_SCHEDULE_HW_LAST_IN_SCENE 1U
#define IMG_EGL_NO_ERROR 0
#define PVR_DPF(x) ((void)0)
#define GLES_ASSERT(x) assert(x)
#define GLES2_TIME_START(x) (++timer_depth)
#define GLES2_TIME_STOP(x) (assert(timer_depth), --timer_depth)
#define GLES2_INC_COUNT(x,y) ((void)0)

typedef struct { unsigned ui32Name, refs; } GLES2NamedItem;
typedef struct { int eAttachmentType; } GLES2FrameBufferAttachable;
typedef struct { int needed; } Resource;
typedef struct {
    void *psEGLImageSource, *psEGLImageTarget;
    Resource sResource;
} GLES2Texture;
typedef struct { int eAttachmentType; GLES2Texture *psTex; } GLES2MipMapLevel;
typedef struct { int eAttachmentType; void *psEGLImageSource, *psEGLImageTarget; } GLES2RenderBuffer;
typedef struct { int hMutex, bInFrame, bPrimitivesSinceLastTA; } Surface;
typedef struct { Surface *surface; } Params;
typedef struct {
    GLES2NamedItem sNamedItem;
    unsigned eStatus;
    Params sReadParams, sDrawParams;
    GLES2FrameBufferAttachable *apsAttachment[GLES2_MAX_ATTACHMENTS];
} GLES2FrameBuffer;
typedef struct { GLES2FrameBuffer *items[16]; } GLES2NamesArray;
typedef struct { int sKRM; } TextureManager;
typedef struct { GLES2NamesArray *apsNamesArray[1]; TextureManager *psTextureManager; } Shared;
typedef struct {
    Shared *psSharedState;
    Surface *psRenderSurface;
    struct { GLES2FrameBuffer *psActiveFrameBuffer; GLES2FrameBuffer sDefaultFrameBuffer; } sFrameBuffer;
} GLES2Context;
static GLES2Context *current;
#define __GLES2_GET_CONTEXT() GLES2Context *gc = current
static unsigned timer_depth, locks, unlocks, lock_depth, submissions, changes, lookups, frees;
static unsigned error, last_flags;
static int submit_error, allocation_fail, insertion_fail;
static GLES2FrameBuffer allocated;
static void SetError(GLES2Context *gc, unsigned value) { error = value; }
static GLES2NamedItem *NamedItemAddRef(GLES2NamesArray *names, unsigned name) {
    ++lookups;
    assert(name < 16);
    GLES2FrameBuffer *fb = names->items[name];
    if(fb) ++fb->sNamedItem.refs;
    return (GLES2NamedItem *)fb;
}
static void NamedItemDelRef(GLES2Context *gc, GLES2NamesArray *names, GLES2NamedItem *item) {
    assert(item->refs);
    --item->refs;
}
static GLES2FrameBuffer *CreateFrameBufferObject(GLES2Context *gc, unsigned name) {
    if(allocation_fail) return NULL;
    allocated = (GLES2FrameBuffer){.sNamedItem = {name, 1}};
    return &allocated;
}
static int InsertNamedItem(GLES2NamesArray *names, GLES2NamedItem *item) {
    if(insertion_fail) return 0;
    names->items[item->ui32Name] = (GLES2FrameBuffer *)item;
    return 1;
}
static void FreeFrameBuffer(GLES2Context *gc, GLES2FrameBuffer *fb) { ++frees; }
static void PVRSRVLockMutex(int mutex) { assert(!lock_depth++); ++locks; }
static void PVRSRVUnlockMutex(int mutex) { assert(lock_depth-- == 1); ++unlocks; }
static int ScheduleTA(GLES2Context *gc, Surface *surface, unsigned flags) {
    assert(lock_depth == 1);
    ++submissions;
    last_flags = flags;
    return submit_error;
}
static void ChangeDrawableParams(GLES2Context *gc, GLES2FrameBuffer *fb, Params *read, Params *draw) {
    assert(gc->sFrameBuffer.psActiveFrameBuffer == fb && !lock_depth);
    ++changes;
    gc->psRenderSurface = draw->surface;
}
static int KRM_IsResourceNeeded(int *krm, Resource *resource) { return resource->needed; }
#include "fbo_bind_functions.inc"

static void reset_counts(void) {
    timer_depth = locks = unlocks = lock_depth = submissions = changes = lookups = frees = 0;
    error = last_flags = 0;
    submit_error = allocation_fail = insertion_fail = 0;
}
int main(void) {
    GLES2NamesArray names = {0};
    TextureManager manager = {0};
    Shared shared = {.apsNamesArray = {&names}, .psTextureManager = &manager};
    Surface sa = {.hMutex = 1, .bInFrame = 1, .bPrimitivesSinceLastTA = 1};
    Surface sb = {.hMutex = 2, .bInFrame = 1, .bPrimitivesSinceLastTA = 1};
    GLES2FrameBuffer a = {.sNamedItem = {1, 2}, .eStatus = GL_FRAMEBUFFER_COMPLETE, .sDrawParams = {&sa}};
    GLES2FrameBuffer b = {.sNamedItem = {2, 1}, .eStatus = GL_FRAMEBUFFER_COMPLETE, .sDrawParams = {&sb}};
    GLES2Context gc = {.psSharedState = &shared, .psRenderSurface = &sa, .sFrameBuffer.psActiveFrameBuffer = &a};
    names.items[1] = &a; names.items[2] = &b;
    current = &gc;
    reset_counts();
    for(unsigned i = 0; i < 10000; ++i) glBindFramebuffer(GL_FRAMEBUFFER, 1);
    assert(!submissions && !locks && !changes && !timer_depth && !error);
    assert(a.sNamedItem.refs == 2 && lookups == 10000);
    glBindFramebuffer(GL_FRAMEBUFFER, 2);
    assert(submissions == 1 && changes == 1 && locks == 1 && unlocks == 1);
    assert(a.sNamedItem.refs == 1 && b.sNamedItem.refs == 2 && gc.psRenderSurface == &sb);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    assert(submissions == 2 && b.sNamedItem.refs == 1);
    reset_counts();
    for(unsigned i = 0; i < 10000; ++i) glBindFramebuffer(GL_FRAMEBUFFER, 0);
    assert(!submissions && !locks && !lookups && !changes && !timer_depth);

    /* Deleted names can be reused while another context retains the old object. */
    gc.sFrameBuffer.psActiveFrameBuffer = &a; gc.psRenderSurface = &sa;
    a.sNamedItem.refs = 1;
    b.sNamedItem.ui32Name = 1; names.items[1] = &b;
    glBindFramebuffer(GL_FRAMEBUFFER, 1);
    assert(gc.sFrameBuffer.psActiveFrameBuffer == &b && submissions == 1 && changes == 1);
    assert(!a.sNamedItem.refs && b.sNamedItem.refs == 2);

    names.items[1] = &a; a.sNamedItem.refs = 1;
    reset_counts(); submit_error = 1;
    glBindFramebuffer(GL_FRAMEBUFFER, 1);
    assert(error == GL_INVALID_OPERATION && gc.sFrameBuffer.psActiveFrameBuffer == &b);
    assert(gc.psRenderSurface == &sb && b.sNamedItem.refs == 2 && a.sNamedItem.refs == 1);
    assert(submissions == 1 && !changes && locks == unlocks && !lock_depth && !timer_depth);
    reset_counts();
    glBindFramebuffer(GL_FRAMEBUFFER, 1);
    assert(!error && gc.sFrameBuffer.psActiveFrameBuffer == &a);

    reset_counts();
    glBindFramebuffer(999, 1);
    assert(error == GL_INVALID_ENUM && !lookups && !submissions && !timer_depth);
    reset_counts(); allocation_fail = 1;
    glBindFramebuffer(GL_FRAMEBUFFER, 3);
    assert(error == GL_OUT_OF_MEMORY && !submissions && gc.sFrameBuffer.psActiveFrameBuffer == &a);
    reset_counts(); insertion_fail = 1;
    glBindFramebuffer(GL_FRAMEBUFFER, 3);
    assert(error == GL_OUT_OF_MEMORY && frees == 1 && !submissions);

    reset_counts();
    GLES2Texture texture = {.psEGLImageTarget = &texture, .sResource.needed = 1};
    GLES2MipMapLevel mip = {.eAttachmentType = GL_TEXTURE, .psTex = &texture};
    a.apsAttachment[0] = (GLES2FrameBufferAttachable *)&mip;
    b.apsAttachment[0] = (GLES2FrameBufferAttachable *)&mip;
    b.sNamedItem.ui32Name = 2; names.items[2] = &b;
    glBindFramebuffer(GL_FRAMEBUFFER, 2);
    assert(last_flags == GLES2_SCHEDULE_HW_LAST_IN_SCENE && b.eStatus == GLES2_FRAMEBUFFER_STATUS_UNKNOWN);
    b.eStatus = GL_FRAMEBUFFER_COMPLETE;
    GLES2RenderBuffer rb = {.eAttachmentType = GL_RENDERBUFFER, .psEGLImageSource = &rb};
    b.apsAttachment[0] = (GLES2FrameBufferAttachable *)&rb;
    glBindFramebuffer(GL_FRAMEBUFFER, 1);
    assert(last_flags == GLES2_SCHEDULE_HW_LAST_IN_SCENE && a.eStatus == GLES2_FRAMEBUFFER_STATUS_UNKNOWN);
    assert(locks == unlocks && !lock_depth && !timer_depth);
    puts("FBO binding: 20000 redundant binds skip submissions; name reuse, reference counts and failed-switch rollback passed");
}
