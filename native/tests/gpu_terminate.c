#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t IMG_UINT32;
typedef uint8_t IMG_UINT8;
typedef int IMG_BOOL, GLES2_MEMERROR;
typedef struct { uint32_t uiAddr; } IMG_DEV_VIRTADDR;
#define IMG_INTERNAL
#define IMG_TRUE 1
#define GLES_ASSERT(x) assert(x)
#define GLES2_INC_COUNT(x,n) ((void)0)
#define GLES2_NO_ERROR 0
#define GLES2_TA_BUFFER_ERROR 1
#define GLES2_GPU_WAIT_ERROR 9
#define GLES2_SCHEDULE_HW_DISCARD_SCENE 1U
#define GLES2_SCHEDULE_HW_LAST_IN_SCENE 2U
#define SGX_FEATURE_EDM_VERTEX_PDSADDR_FULL_RANGE
#define EURASIA_PDS_DOUTU1_MODE_PARALLEL 1U
#define SGX_VTXSHADER_USE_CODE_BASE_INDEX 0U
#define PDS_TERMINATE_SAUPDATE_DWORDS 16U
#define VDM_CTRL_TERMINATE_DWORDS 16U
#define CBUF_TYPE_VDM_CTRL_BUFFER 0
#define GLES2_EMITSTATE_STATEUPDATE 1U
#define EURASIA_TAOBJTYPE_STATE 0x100U
#define EURASIA_TAOBJTYPE_TERMINATE 0x200U
#define EURASIA_TAPDSSTATE_BASEADDR_ALIGNSHIFT 0U
#define EURASIA_TAPDSSTATE_BASEADDR_SHIFT 0U
#define EURASIA_TAPDSSTATE_BASEADDR_CLRMSK 0xf0000000U
#define EURASIA_TAPDSSTATE_DATASIZE_ALIGNSHIFT 0U
#define EURASIA_TAPDSSTATE_DATASIZE_SHIFT 0U
#define EURASIA_TAPDSSTATE_DATASIZE_CLRMSK 0xffffff00U
#define EURASIA_TAPDSSTATE_SEC_EXEC 0x100U
#define EURASIA_TAPDSSTATE_SECONDARY 0x200U
#define EURASIA_TAPDSSTATE_USEATTRIBUTESIZE_ALIGNSHIFTINREGISTERS 0U
#define EURASIA_TAPDSSTATE_USEATTRIBUTESIZE_SHIFT 0U
#define EURASIA_TAPDSSTATE_USEPIPE_ALL 0x400U
#define EURASIA_TAPDSSTATE_USEPIPE_1 0x800U
#define EURASIA_TAPDSSTATE_PARTITIONS_SHIFT 16U
#define EURASIA_TAPDSSTATE_LASTTASK 0x1000U
#define EURASIA_TAPDSSTATE_SD 0x2000U
#define EURASIA_TAPDSSTATE_MTE_EMIT 0x4000U
#define ALIGNCOUNTINBLOCKS(n,a) (n)
typedef struct { void *pvLinAddr; IMG_DEV_VIRTADDR sDevVAddr; } Mem;
typedef struct {
    Mem *psSAUpdatePDSMemInfo, *psTerminatePDSMemInfo, *psTerminateUSEMemInfo;
    unsigned ui32PDSDataSize, ui32SAUpdatePDSDataSize, ui32TerminateRegion;
    IMG_DEV_VIRTADDR uPDSCodeAddress, uSAUpdateCodeAddress;
} EGLTerminateState;
typedef struct { EGLTerminateState sTerm; unsigned ui32TerminateRegion; } EGLRenderSurface;
typedef struct { unsigned ui32CurrentWriteOffsetInBytes, ui32CommittedPrimOffsetInBytes; } Ring;
typedef struct { IMG_DEV_VIRTADDR uUSEVertexHeapBase; } Sys;
typedef struct {
    struct { Mem *psDummyVertUSECode; } sProgram;
    Sys *psSysContext;
    Ring *apsBuffers[1];
    unsigned ui32EmitMask;
} GLES2Context;
typedef struct {
    unsigned ui32TerminateRegion, aui32USETaskControl[3], ui32DataSize;
    uint32_t *pui32DataSegment;
} PDS_TERMINATE_STATE_PROGRAM;
typedef struct { unsigned aui32USETaskControl[3], ui32DataSize; int bKickUSEDummyProgram; } PDS_PIXEL_SHADER_SA_PROGRAM;
static unsigned waits, generated, patched, space_requests, updates;
static int wait_ok, space_ok;
static uint32_t vdm[16];
static int WaitForTA(GLES2Context *gc) { ++waits; return wait_ok; }
static void SetUSEExecutionAddress(unsigned *task, unsigned phase, IMG_DEV_VIRTADDR address, IMG_DEV_VIRTADDR base, unsigned index) {}
static uint32_t *PDSGeneratePixelShaderSAProgram(PDS_PIXEL_SHADER_SA_PROGRAM *program, uint32_t *code) {
    ++generated; code[0] = 0x5555; program->ui32DataSize = 4; return code + 1;
}
static void PDSGenerateTerminateStateProgram(PDS_TERMINATE_STATE_PROGRAM *program, uint32_t *code) {
    ++generated; code[0] = program->ui32TerminateRegion;
    program->pui32DataSegment = code; program->ui32DataSize = 4;
}
static void PDSPatchTerminateStateProgram(PDS_TERMINATE_STATE_PROGRAM *program, uint32_t *code) {
    ++patched; code[0] = program->ui32TerminateRegion;
}
static uint32_t *CBUF_GetBufferSpace(Ring **rings, unsigned words, unsigned type, int terminate) {
    ++space_requests; return space_ok ? vdm : NULL;
}
static void CBUF_UpdateBufferPos(Ring **rings, unsigned words, unsigned type) {
    ++updates; rings[type]->ui32CurrentWriteOffsetInBytes += words * 4;
}
#include "gpu_terminate_functions.inc"

int main(void) {
    uint32_t sa[16], terminate[16];
    memset(sa, 0xcc, sizeof(sa)); memset(terminate, 0xdd, sizeof(terminate));
    Mem sa_mem = {sa, {0x1000}}, terminate_mem = {terminate, {0x2000}}, use_mem = {NULL, {0x3000}};
    Sys sys = {{0}}; Ring ring = {0};
    GLES2Context gc = {.sProgram = {&use_mem}, .psSysContext = &sys, .apsBuffers = {&ring}};
    EGLRenderSurface surface = {.sTerm = {.psSAUpdatePDSMemInfo = &sa_mem, .psTerminatePDSMemInfo = &terminate_mem, .psTerminateUSEMemInfo = &use_mem}, .ui32TerminateRegion = 3};
    EGLTerminateState before = surface.sTerm;
    assert(OutputTerminateState(&gc, &surface, GLES2_SCHEDULE_HW_LAST_IN_SCENE) == GLES2_GPU_WAIT_ERROR);
    assert(waits == 1 && !generated && !patched && !space_requests && !updates);
    assert(!memcmp(&before, &surface.sTerm, sizeof(before)) && sa[0] == 0xccccccccU && terminate[0] == 0xddddddddU);
    wait_ok = space_ok = 1;
    assert(OutputTerminateState(&gc, &surface, GLES2_SCHEDULE_HW_LAST_IN_SCENE) == GLES2_NO_ERROR);
    assert(waits == 2 && generated == 2 && !patched && space_requests == 1 && updates == 1);
    assert(surface.sTerm.ui32TerminateRegion == 3 && terminate[0] == 3);
    /* Reuse of an unchanged terminate program needs no wait or code generation. */
    assert(OutputTerminateState(&gc, &surface, 0) == GLES2_NO_ERROR && waits == 2 && generated == 2);
    surface.ui32TerminateRegion = 4; wait_ok = 0; before = surface.sTerm;
    unsigned previous_space = space_requests;
    assert(OutputTerminateState(&gc, &surface, GLES2_SCHEDULE_HW_LAST_IN_SCENE) == GLES2_GPU_WAIT_ERROR);
    assert(waits == 3 && !patched && space_requests == previous_space && terminate[0] == 3);
    assert(!memcmp(&before, &surface.sTerm, sizeof(before)));
    wait_ok = 1;
    assert(OutputTerminateState(&gc, &surface, GLES2_SCHEDULE_HW_LAST_IN_SCENE) == GLES2_NO_ERROR);
    assert(patched == 1 && surface.sTerm.ui32TerminateRegion == 4 && terminate[0] == 4);
    previous_space = space_requests;
    assert(OutputTerminateState(&gc, &surface, GLES2_SCHEDULE_HW_DISCARD_SCENE) == GLES2_NO_ERROR);
    assert(waits == 4 && space_requests == previous_space);
    space_ok = 0;
    assert(OutputTerminateState(&gc, &surface, 0) == GLES2_TA_BUFFER_ERROR);
    puts("TA terminate state: initial generation and region patch preserve GPU code on failed wait; unchanged/discard paths stay asynchronous");
}
