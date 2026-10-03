#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t IMG_UINT32;
typedef uint64_t IMG_UINT64;
typedef void *IMG_HANDLE;
typedef int IMG_BOOL, PVRSRV_ERROR, IMG_EGLERROR;
typedef struct { int id; } PVRSRV_CONNECTION;
typedef struct { int id; } PVRSRV_CLIENT_SYNC_INFO;
#define IMG_INTERNAL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define PVRSRV_OK 0
#define PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE 7
#define SCE_OK 0
#define PVR_DPF(x) ((void)0)
#define GLES_ASSERT(x) assert(x)
#define GLES2_TIME_START(x) (++timer_depth)
#define GLES2_TIME_STOP(x) (assert(timer_depth), --timer_depth)
#define GLES2_DEFAULT_WAIT_RETRIES 3
#define CBUF_TYPE_VDM_CTRL_BUFFER 0
#define GLES2_SCHEDULE_HW_LAST_IN_SCENE 1U
#define GLES2_SCHEDULE_HW_DISCARD_SCENE 2U
#define GLES2_SCHEDULE_HW_WAIT_FOR_TA 4U
#define GLES2_SCHEDULE_HW_WAIT_FOR_3D 8U
#define IMG_EGL_NO_ERROR 0
#define IMG_EGL_MEMORY_INVALID_ERROR 2
#define IMG_EGL_GENERIC_ERROR 3

static unsigned waits, clock_queries, timer_depth, complete_at;
static int wait_result, query_result;
static uint64_t process_time, wait_step;
static uint32_t last_interval, completion_value;
static volatile uint32_t *completion;
static PVRSRV_CLIENT_SYNC_INFO *last_sync;
static uint64_t sceKernelGetProcessTimeWide(void) {
    ++clock_queries;
    return process_time;
}
static void *sceKernelGetTLSAddr(unsigned slot) {
    assert(slot == 0x44);
    return NULL;
}
static int sceGpuSignalWait(void *signal, unsigned interval) {
    ++waits;
    last_interval = interval;
    process_time += wait_step ? wait_step : interval;
    if(complete_at && waits == complete_at) {
        if(completion) *completion = completion_value;
        query_result = PVRSRV_OK;
    }
    return wait_result;
}
#include "gpu_wait_functions.inc"

typedef struct { void *pvLinAddr; } Mem;
typedef struct { Mem *psStatusUpdateMemInfo; unsigned ui32CommittedHWOffsetInBytes; } Ring;
typedef struct EGLRenderSurface {
    int bInFrame, bPrimitivesSinceLastTA;
    void *hEGLSurface;
    PVRSRV_CLIENT_SYNC_INFO *psSyncInfo;
} EGLRenderSurface;
typedef struct { EGLRenderSurface *psRenderSurface; PVRSRV_CLIENT_SYNC_INFO *psSyncInfo; } Params;
typedef struct {
    PVRSRV_CONNECTION *psConnection;
    struct { struct { void *hOSGlobalEvent; } sMiscInfo; } sHWInfo;
    int s3D;
} Sys;
typedef struct {
    Ring *apsBuffers[1];
    Sys *psSysContext;
    struct { struct { Params sReadParams, sDrawParams; } sDefaultFrameBuffer; } sFrameBuffer;
} GLES2Context;
static int valid, kick_result;
static unsigned kicks;
static int ValidateMemory(GLES2Context *gc) { return valid; }
static int DoKickTA(GLES2Context *gc, EGLRenderSurface *surface, unsigned flags) {
    ++kicks;
    return kick_result;
}
static int SGX2DQueryBlitsComplete(int *device, PVRSRV_CLIENT_SYNC_INFO *sync, int wait) {
    assert(sync && !wait);
    last_sync = sync;
    return query_result;
}
#include "gpu_completion_functions.inc"

static void reset(void) {
    waits = clock_queries = timer_depth = complete_at = kicks = 0;
    process_time = wait_step = 0;
    wait_result = query_result = PVRSRV_OK;
    last_interval = completion_value = 0;
    completion = NULL;
    last_sync = NULL;
    valid = 1;
    kick_result = IMG_EGL_NO_ERROR;
}
static int poll(volatile uint32_t *value, unsigned wanted, unsigned mask, unsigned interval, unsigned tries) {
    return PVRSRVPollForValue(NULL, NULL, value, wanted, mask, interval, tries);
}
int main(void) {
    volatile uint32_t value = 0x1234;
    reset();
    assert(poll(&value, 0x34, 0xff, 100, 0) == PVRSRV_OK);
    assert(!waits && !clock_queries);
    assert(poll(&value, 1, ~0U, 100, 0) == PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE);
    assert(!waits);

    reset();
    assert(poll(&value, 1, ~0U, 100, 3) == PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE);
    assert(waits == 3 && process_time == 300); /* unrelated successful signals */
    reset(); wait_step = 10;
    assert(poll(&value, 1, ~0U, 100, 3) == PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE);
    assert(waits == 30 && process_time == 300); /* early wake does not consume a full interval */
    reset(); wait_result = -1; wait_step = 1;
    assert(poll(&value, 1, ~0U, 100, 3) == PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE);
    assert(waits == 3);
    reset(); wait_step = 125;
    assert(poll(&value, 1, ~0U, 100, 3) == PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE);
    assert(waits == 3 && last_interval == 50);
    reset(); completion = &value; completion_value = 1; complete_at = 3;
    assert(poll(&value, 1, ~0U, 100, 3) == PVRSRV_OK && waits == 3);
    reset(); value = 0;
    assert(poll(&value, 1, ~0U, 0, 3) == PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE && waits == 3);
    reset(); completion = &value; completion_value = 1; complete_at = 2;
    assert(poll(&value, 1, ~0U, 0x80000000U, 3) == PVRSRV_OK);
    assert(waits == 2 && process_time == UINT64_C(0x100000000));
    reset(); value = 0; process_time = UINT64_MAX - 150;
    assert(poll(&value, 1, ~0U, 100, 3) == PVRSRV_ERROR_TIMEOUT_POLLING_FOR_VALUE);
    assert(waits == 3); /* elapsed subtraction remains valid across wrap */

    Mem mem = {.pvLinAddr = (void *)&value};
    Ring ring = {.psStatusUpdateMemInfo = &mem, .ui32CommittedHWOffsetInBytes = 1};
    Sys sys = {0};
    GLES2Context gc = {.apsBuffers = {&ring}, .psSysContext = &sys};
    PVRSRV_CLIENT_SYNC_INFO offscreen_sync = {1}, read_sync = {2}, draw_sync = {3};
    EGLRenderSurface surface = {.bInFrame = 1, .bPrimitivesSinceLastTA = 1, .psSyncInfo = &offscreen_sync};
    reset(); value = 0;
    assert(!WaitForTA(&gc) && waits == 3 && !timer_depth);
    reset(); value = 1;
    assert(WaitForTA(&gc) && !waits && !timer_depth);
    reset(); value = 0;
    assert(ScheduleTA(&gc, &surface, GLES2_SCHEDULE_HW_WAIT_FOR_TA) == IMG_EGL_GENERIC_ERROR);
    assert(kicks == 1 && waits == 3 && !timer_depth);
    reset(); query_result = 1;
    assert(ScheduleTA(&gc, &surface, GLES2_SCHEDULE_HW_WAIT_FOR_3D) == IMG_EGL_GENERIC_ERROR);
    assert(last_sync == &offscreen_sync && waits == 3 && !timer_depth);
    reset(); query_result = 1; wait_step = 10000; complete_at = 25;
    assert(WaitForRender(&gc, &offscreen_sync) && waits == 25 && !timer_depth);
    reset(); query_result = 1; complete_at = 3;
    assert(WaitForRender(&gc, &offscreen_sync) && waits == 3 && !timer_depth);

    gc.sFrameBuffer.sDefaultFrameBuffer.sReadParams = (Params){&surface, &read_sync};
    gc.sFrameBuffer.sDefaultFrameBuffer.sDrawParams = (Params){NULL, &draw_sync};
    surface.hEGLSurface = &surface;
    reset();
    assert(ScheduleTA(&gc, &surface, GLES2_SCHEDULE_HW_WAIT_FOR_3D) == IMG_EGL_NO_ERROR);
    assert(last_sync == &read_sync && !waits && !clock_queries);
    gc.sFrameBuffer.sDefaultFrameBuffer.sReadParams.psRenderSurface = NULL;
    reset();
    assert(ScheduleTA(&gc, &surface, GLES2_SCHEDULE_HW_WAIT_FOR_3D) == IMG_EGL_NO_ERROR);
    assert(last_sync == &draw_sync && !waits && !clock_queries);
    reset(); kick_result = IMG_EGL_GENERIC_ERROR;
    assert(ScheduleTA(&gc, &surface, GLES2_SCHEDULE_HW_WAIT_FOR_TA) == IMG_EGL_GENERIC_ERROR);
    assert(kicks == 1 && !waits && !clock_queries);
    reset(); valid = 0;
    assert(ScheduleTA(&gc, &surface, 0) == IMG_EGL_MEMORY_INVALID_ERROR && !kicks);
    reset();
    assert(ScheduleTA(&gc, &surface, 0) == IMG_EGL_NO_ERROR && kicks == 1 && !clock_queries);
    puts("GPU waits: unrelated wakes, deadlines, final completion, counter wrap and TA/3D failure propagation passed");
}
