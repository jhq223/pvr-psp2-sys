#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef int IMG_BOOL;
typedef unsigned IMG_UINT32;
typedef int IMG_EGLERROR;
typedef struct { int eDrawableType; } Drawable;
typedef struct { int bInFrame; } Surface;
typedef struct {
    Drawable *psDrawParams;
    Surface *psRenderSurface;
    struct { unsigned ui32FlushBehaviour; } sAppHints;
    unsigned error;
} GLES2Context;
#define GL_APICALL
#define GL_APIENTRY
#define IMG_FALSE 0
#define IMG_TRUE 1
#define IMG_EGL_NO_ERROR 0
#define IMG_EGL_GENERIC_ERROR 1
#define IMG_EGL_MEMORY_INVALID_ERROR 2
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505
#define EGL_DRAWABLETYPE_PIXMAP 1
#define GLES2_HINT_KICK_TA 0
#define GLES2_HINT_KICK_3D 1
#define GLES2_SCHEDULE_HW_WAIT_FOR_3D 1
#define GLES2_SCHEDULE_HW_LAST_IN_SCENE 2
#define GLES2_SCHEDULE_HW_WAIT_FOR_TA 4
#define __GLES2_GET_CONTEXT() GLES2Context *gc = &context
#define PVR_DPF(x) ((void)0)
#define GLES2_TIME_START(x) ((void)0)
#define GLES2_TIME_STOP(x) ((void)0)
#define sceClibPrintf(...) ((void)0)
static GLES2Context context;
static int flush_calls, schedule_calls, flush_result, schedule_result;
static unsigned scheduled_flags;
static int FlushAllUnflushedFBO(GLES2Context *gc, int wait)
{
    assert(gc == &context && wait);
    flush_calls++;
    return flush_result;
}
static int ScheduleTA(GLES2Context *gc, Surface *surface, unsigned flags)
{
    assert(gc == &context && surface == context.psRenderSurface);
    schedule_calls++;
    scheduled_flags = flags;
    return schedule_result;
}
static void SetError(GLES2Context *gc, unsigned error)
{
    if(!gc->error) gc->error = error;
}
/* Production entry point: the same error is consumed by Context::finish. */
#include "finish_function.inc"

static void reset(int pixmap, int hint, int in_frame)
{
    static Drawable drawable;
    static Surface surface;
    drawable.eDrawableType = pixmap ? EGL_DRAWABLETYPE_PIXMAP : 0;
    surface.bInFrame = in_frame;
    memset(&context, 0, sizeof(context));
    context.psDrawParams = &drawable;
    context.psRenderSurface = &surface;
    context.sAppHints.ui32FlushBehaviour = hint;
    flush_calls = schedule_calls = 0;
    flush_result = 1;
    schedule_result = 0;
    scheduled_flags = 0;
}
int main(void)
{
    for(int pixmap = 0; pixmap < 2; pixmap++) {
        for(int hint = 0; hint < 3; hint++) {
            for(int in_frame = 0; in_frame < 2; in_frame++) {
                int drain3d = pixmap || hint == GLES2_HINT_KICK_3D;
                reset(pixmap, hint, in_frame);
                glFinish();
                assert(!context.error && flush_calls == drain3d && schedule_calls == 1);
                assert(scheduled_flags == (unsigned)(drain3d ?
                    GLES2_SCHEDULE_HW_WAIT_FOR_3D | (in_frame ? GLES2_SCHEDULE_HW_LAST_IN_SCENE : 0) :
                    hint == GLES2_HINT_KICK_TA ? GLES2_SCHEDULE_HW_WAIT_FOR_TA : 0));
                if(drain3d) {
                    reset(pixmap, hint, in_frame);
                    flush_result = 0;
                    glFinish();
                    assert(context.error == GL_INVALID_OPERATION && !schedule_calls);
                }
                for(int error = IMG_EGL_GENERIC_ERROR; error <= IMG_EGL_MEMORY_INVALID_ERROR; error++) {
                    reset(pixmap, hint, in_frame);
                    schedule_result = error;
                    glFinish();
                    assert(context.error == GL_INVALID_OPERATION && schedule_calls == 1);
                }
            }
        }
    }
    reset(0, GLES2_HINT_KICK_3D, 0);
    context.error = GL_OUT_OF_MEMORY;
    schedule_result = IMG_EGL_GENERIC_ERROR;
    glFinish();
    assert(context.error == GL_OUT_OF_MEMORY);
    reset(0, GLES2_HINT_KICK_3D, 0);
    context.psRenderSurface = NULL;
    flush_result = 0;
    glFinish();
    assert(context.error == GL_INVALID_OPERATION && !schedule_calls);
    puts("finish: failed submissions and waits reach glGetError; completion flags preserved");
    return 0;
}
