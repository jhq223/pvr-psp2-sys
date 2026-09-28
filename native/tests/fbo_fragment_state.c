#include <assert.h>
#include <stdio.h>
#include <string.h>

#define IMG_INTERNAL
#define IMG_VOID void
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
typedef unsigned IMG_UINT32;
typedef int IMG_INT32, IMG_BOOL;
#define PVRSRV_FLIP_Y 1
#define GLES2_FRAMEBUFFER_STATUS_UNKNOWN 0
#define GLES2_DIRTYFLAG_RENDERSTATE 1U
#define GLES2_DIRTYFLAG_FP_STATE 2U
#define GLES2_DIRTYFLAG_FRAGPROG_CONSTANTS 4U
#define GLES2_EMITSTATE_MTE_STATE_ISP 1U
#define GLES2_EMITSTATE_MTE_STATE_REGION_CLIP 2U
#define EURASIA_ISPC_SWMASK_CLRMSK 0xffff00ffU
#define EURASIA_ISPC_SCMPMASK_CLRMSK 0xffffff00U
#define EURASIA_ISPC_SWMASK_SHIFT 8
#define EURASIA_ISPC_SCMPMASK_SHIFT 0
#define EURASIA_ISPA_SREF_SHIFT 0
#define CBUF_TYPE_PDS_FRAG_BUFFER 0
#define CBUF_TYPE_USSE_FRAG_BUFFER 1
typedef struct { unsigned owner, mapped; } Buffer;
typedef struct { Buffer sPDSBuffer, sUSSEBuffer; void *hEGLSurface; } Surface;
typedef struct {
    Surface *psRenderSurface;
    unsigned eRotationAngle, ui32Width, ui32Height;
} EGLDrawableParams;
typedef struct { unsigned ui32StencilBits; } Mode;
typedef struct {
    unsigned eStatus;
    Mode sMode;
    EGLDrawableParams sReadParams, sDrawParams;
} GLES2FrameBuffer;
typedef struct {
    struct { GLES2FrameBuffer *psActiveFrameBuffer, sDefaultFrameBuffer; } sFrameBuffer;
    EGLDrawableParams *psReadParams, *psDrawParams;
    Surface *psRenderSurface;
    Mode *psMode;
    void *hEGLSurface;
    Buffer *apsBuffers[2];
    unsigned ui32DirtyState, ui32EmitMask;
    int bFullScreenViewport, bFullScreenScissor, bDrawMaskInvalid;
    struct {
        struct {
            unsigned ui32MaxFBOStencilVal, ui32FFStencil, ui32BFStencil;
            unsigned ui32FFStencilWriteMaskIn, ui32FFStencilCompareMaskIn;
            unsigned ui32BFStencilWriteMaskIn, ui32BFStencilCompareMaskIn;
            unsigned ui32FFStencilRef, ui32BFStencilRef;
            int i32FFStencilRefIn, i32BFStencilRefIn;
        } sStencil;
        struct { int i32X, i32Y; unsigned ui32Width, ui32Height; } sViewport;
        struct {
            int i32ScissorX, i32ScissorY;
            unsigned ui32ScissorWidth, ui32ScissorHeight;
        } sScissor;
    } sState;
} GLES2Context;
static int Clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
static void ApplyViewport(GLES2Context *gc) { (void)gc; }
#include "fbo_fragment_state_functions.inc"

/* Model the two state-emission gates in GLES2EmitState. Fragment secondary
 * attributes always use the surface's PDS ring; primary programs can use it
 * when the program-cache allocation falls back to the ring. */
typedef struct { Buffer *primary, *secondary; } Draw;
static Draw emit(GLES2Context *gc, Draw last) {
    if(gc->ui32DirtyState & GLES2_DIRTYFLAG_FP_STATE)
        last.primary = gc->apsBuffers[CBUF_TYPE_PDS_FRAG_BUFFER];
    if(gc->ui32DirtyState & (GLES2_DIRTYFLAG_FP_STATE | GLES2_DIRTYFLAG_FRAGPROG_CONSTANTS))
        last.secondary = gc->apsBuffers[CBUF_TYPE_PDS_FRAG_BUFFER];
    gc->ui32DirtyState = gc->ui32EmitMask = 0;
    return last;
}
static void bind(GLES2Context *gc, GLES2FrameBuffer *fb) {
    gc->sFrameBuffer.psActiveFrameBuffer = fb;
    ChangeDrawableParams(gc, fb, &fb->sReadParams, &fb->sDrawParams);
}
static GLES2FrameBuffer framebuffer(Surface *surface) {
    EGLDrawableParams params = {surface, 0, 26, 930};
    return (GLES2FrameBuffer){1, {0}, params, params};
}
static int switch_and_release(int incomplete) {
    Surface a = {{1, 1}, {1, 1}, NULL}, b = {{2, 1}, {2, 1}, NULL};
    GLES2FrameBuffer fa = framebuffer(&a), fb = framebuffer(&b);
    GLES2Context gc = {0};
    gc.psReadParams = &fa.sReadParams; gc.psDrawParams = &fa.sDrawParams;
    gc.sState.sViewport.ui32Width = gc.sState.sScissor.ui32ScissorWidth = 26;
    gc.sState.sViewport.ui32Height = gc.sState.sScissor.ui32ScissorHeight = 930;
    bind(&gc, &fa);
    gc.ui32DirtyState |= GLES2_DIRTYFLAG_FP_STATE;
    Draw first = emit(&gc, (Draw){0});
    assert(first.primary == &a.sPDSBuffer && first.secondary == &a.sPDSBuffer);
    if(incomplete) {
        fb.eStatus = GLES2_FRAMEBUFFER_STATUS_UNKNOWN;
        bind(&gc, &fb);
        assert(!gc.psRenderSurface);
        fb.eStatus = 1;
    }
    /* Same program, uniforms, orientation and viewport; only FBO changes. */
    bind(&gc, &fb);
    Draw second = emit(&gc, first);
    /* Retiring A is legal once A completes. It must not invalidate B's work. */
    a.sPDSBuffer.mapped = a.sUSSEBuffer.mapped = 0;
    if(!second.primary->mapped || !second.secondary->mapped) {
        fprintf(stderr, "FAIL: surface B still references retired surface A fragment storage (incomplete=%d)\n", incomplete);
        return 0;
    }
    assert(second.primary == &b.sPDSBuffer && second.secondary == &b.sPDSBuffer);
    assert(gc.apsBuffers[1] == &b.sUSSEBuffer);
    assert(gc.bFullScreenViewport && gc.bFullScreenScissor && gc.bDrawMaskInvalid);
    /* Drawing again without a target change reuses B's program state. */
    Draw repeated = emit(&gc, second);
    assert(repeated.primary == second.primary && repeated.secondary == second.secondary);
    /* Returning to a new/default drawable also owns its fragment programs. */
    a.sPDSBuffer.mapped = a.sUSSEBuffer.mapped = 1;
    a.hEGLSurface = &a;
    gc.sFrameBuffer.sDefaultFrameBuffer = framebuffer(&a);
    bind(&gc, &gc.sFrameBuffer.sDefaultFrameBuffer);
    Draw window = emit(&gc, second);
    b.sPDSBuffer.mapped = b.sUSSEBuffer.mapped = 0;
    assert(window.primary->mapped && window.secondary->mapped && gc.hEGLSurface == &a);
    return 1;
}
int main(void) {
    if(!switch_and_release(0) || !switch_and_release(1)) return 1;
    puts("FBO fragment state: queued target switch, incomplete target validation, retired surface and default drawable passed");
}
