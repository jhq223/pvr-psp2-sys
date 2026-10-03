#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t IMG_UINT32;
typedef uintptr_t IMG_UINTPTR_T;
typedef int IMG_BOOL;
typedef int GLES2_MEMERROR;
typedef unsigned UCH_UseCodeHeap;
typedef uint32_t *PPVR_USE_INST;
#define IMG_VOID void
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define GLES2_NO_ERROR 0
#define GLES2_HOST_MEM_ERROR 1
#define GLES2_TA_USECODE_ERROR 2
#define GLES2_3D_USECODE_ERROR 3
#define GLES2_NAMETYPE_PROGRAM 0
#define USP_HWSHADER_FLAGS_SAPROG_LABEL_AT_END 1
#define EURASIA_USE_INSTRUCTION_SIZE 8
#define USE_INST_LENGTH 2
#define EURASIA_USE1_END 0x80000000U
#define PVR_DPF(x) ((void)0)
#define GLES_ASSERT assert
#define GLES2_TIME_START(x) ((void)0)
#define GLES2_TIME_STOP(x) ((void)0)
#define GLES2_INC_COUNT(x, y) ((void)0)
#define GLES2MemCopy memcpy
typedef struct { IMG_UINT32 *pui32LinAddress; } UCH_UseCodeBlock;
typedef struct { unsigned ui32RefCount; UCH_UseCodeBlock *psSecondaryCodeBlock; } GLES2USESecondaryUploadTask;
typedef struct { GLES2USESecondaryUploadTask *psSecondaryUploadTask; } GLES2SharedShaderState;
typedef struct { unsigned uSAUpdateInstCount, uFlags; uint32_t *puSAUpdateInsts; } USP_HW_SHADER;
typedef struct UploadShared {
        int hPrimaryLock, sUSEShaderVariantKRM;
        void *apsNamesArray[1];
        UCH_UseCodeHeap *psUSEVertexCodeHeap, *psUSEFragmentCodeHeap;
} UploadShared;
typedef struct UploadSystem { int hPerProcRef; } UploadSystem;
typedef struct {
    UploadShared *psSharedState;
    UploadSystem *psSysContext;
} GLES2Context;
static unsigned lock_depth, host_fail, code_failures, alloc_calls, code_calls, map_calls, reclaim_calls;
static unsigned wrappers_live;
static UCH_UseCodeBlock block;
static uint32_t instructions[8];
static void *allocate(size_t bytes) { ++alloc_calls; if(host_fail) return NULL; ++wrappers_live; return calloc(1, bytes); }
static void release(void *p) { if(p) { assert(wrappers_live--); free(p); } }
#define GLES2Calloc(gc, bytes) allocate(bytes)
#define GLES2Free(gc, ptr) release(ptr)
static void PVRSRVLockMutex(int mutex) { assert(lock_depth++ == 0); }
static void PVRSRVUnlockMutex(int mutex) { assert(lock_depth-- == 1); }
static void USESecondaryUploadTaskAddRef(GLES2Context *gc, GLES2USESecondaryUploadTask *task) { assert(lock_depth == 1); ++task->ui32RefCount; }
static UCH_UseCodeBlock *UCH_CodeHeapAllocate(UCH_UseCodeHeap *heap, unsigned bytes, int ref) {
    assert(lock_depth == 1 && bytes <= sizeof(instructions)); ++code_calls;
    if(code_failures) { --code_failures; return NULL; } return &block;
}
static void NamesArrayMapFunction(GLES2Context *gc, void *names, void *callback, void *attachment) { assert(lock_depth); ++map_calls; }
static void KRM_ReclaimUnneededResources(GLES2Context *gc, int *manager) { assert(lock_depth); ++reclaim_calls; }
#define DestroyVertexVariants NULL
static void BuildNOP(PPVR_USE_INST p, unsigned a, IMG_BOOL b) { p[0] = 0x1234; p[1] = 0; }
#include "shader_upload_functions.inc"
static void clear_counts(void) { alloc_calls = code_calls = map_calls = reclaim_calls = 0; }
int main(void) {
    UploadShared *shared = calloc(1, sizeof(*shared));
    UploadSystem *sys = calloc(1, sizeof(*sys));
    GLES2Context gc = {.psSharedState = shared, .psSysContext = sys};
    uint32_t source[2] = {0xABC, 0xDEF};
    USP_HW_SHADER patched = {1, 0, source};
    GLES2SharedShaderState state = {0};
    block.pui32LinAddress = instructions;
    host_fail = 1;
    assert(SetupUSESecondaryUploadTask(&gc, &patched, &state, IMG_TRUE) == GLES2_HOST_MEM_ERROR);
    assert(!lock_depth && !wrappers_live && !state.psSecondaryUploadTask && !code_calls);
    host_fail = 0;
    for(unsigned vertex = 0; vertex <= 1; ++vertex) {
        clear_counts(); code_failures = 2;
        assert(SetupUSESecondaryUploadTask(&gc, &patched, &state, vertex) == (vertex ? GLES2_TA_USECODE_ERROR : GLES2_3D_USECODE_ERROR));
        assert(!lock_depth && !wrappers_live && !state.psSecondaryUploadTask && code_calls == 2);
        assert(vertex ? map_calls == 1 && !reclaim_calls : reclaim_calls == 1 && !map_calls);
    }
    clear_counts(); code_failures = 1; patched.uFlags = USP_HWSHADER_FLAGS_SAPROG_LABEL_AT_END;
    assert(SetupUSESecondaryUploadTask(&gc, &patched, &state, IMG_FALSE) == GLES2_NO_ERROR);
    assert(!lock_depth && wrappers_live == 1 && code_calls == 2 && reclaim_calls == 1);
    assert(state.psSecondaryUploadTask->ui32RefCount == 2 && instructions[0] == source[0] && instructions[1] == source[1]);
    assert(instructions[2] == 0x1234 && instructions[3] == EURASIA_USE1_END);
    clear_counts(); host_fail = 1;
    assert(SetupUSESecondaryUploadTask(&gc, &patched, &state, IMG_FALSE) == GLES2_NO_ERROR);
    assert(!lock_depth && !alloc_calls && !code_calls && state.psSecondaryUploadTask->ui32RefCount == 3);
    release(state.psSecondaryUploadTask); free(shared); free(sys);
    puts("shader upload: host/device OOM release the lock; retries and shared-task references passed");
}
