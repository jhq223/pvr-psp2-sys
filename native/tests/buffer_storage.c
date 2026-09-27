#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TIMING 1
typedef unsigned IMG_UINT32;
typedef uintptr_t IMG_UINTPTR_T;
typedef int IMG_BOOL;
typedef int PVRSRV_ERROR;
typedef unsigned GLenum;
typedef int GLsizeiptr;
#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define PVRSRV_OK 0
#define PVRSRV_MEM_READ 1
#define PVRSRV_MAP_GC_MMU 2
#define GLES_ASSERT assert
#define PVR_DPF(x) ((void)0)
#define PVR_UNREFERENCED_PARAMETER(x) ((void)(x))
#define GL_APICALL
#define GL_APIENTRY
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88e4
#define GL_DYNAMIC_DRAW 0x88e8
#define GL_STREAM_DRAW 0x88e0
#define GL_INVALID_ENUM 0x500
#define GL_INVALID_VALUE 0x501
#define GL_INVALID_OPERATION 0x502
#define GL_OUT_OF_MEMORY 0x505
#define GLES2_DIRTYFLAG_VAO_ATTRIB_STREAM 1
#define GLES2_DIRTYFLAG_VAO_ELEMENT_BUFFER 2
#define EURASIA_CACHE_LINE_SIZE 16
#define EURASIA_VDM_INDEX_FETCH_BURST_SIZE 16
#define ALIGNCOUNT(n,a) (((n)+(a)-1)&~((a)-1))
#define GLES2_TIME_START(x) ((void)0)
#define GLES2_TIME_STOP(x) ((void)0)
#define GLES2MemCopy memcpy
#define VAO(gc) 1
#define VAO_INDEX_BUFFER_OBJECT(gc) 1
typedef struct { unsigned needed; } KRMResource;
typedef struct { unsigned uAllocSize; void *pvLinAddr; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { struct { unsigned ui32RefCount; } sNamedItem; KRMResource sResource; PVRSRV_CLIENT_MEM_INFO *psMemInfo; unsigned ui32AllocAlign, ui32BufferSize, eUsage; int bRangeCached, bMapped; } GLES2BufferObject;
typedef struct { unsigned ui32DirtyState; GLES2BufferObject *psBoundElementBuffer; } GLES2VertexArrayObject;
typedef struct { int sBufferObjectKRM; unsigned ui32RefCount; } Shared;
typedef struct { Shared *psSharedState; unsigned ui32VBOMemCurrent, ui32VBOHighWaterMark, ui32DirtyState;
    struct { GLES2VertexArrayObject *psActiveVAO; } sVAOMachine;
    struct { GLES2BufferObject *psActiveBuffer[2]; } sBufferObject;
} GLES2Context;
static int fail_host, fail_device, fail_ghost, wait_ok = 1, live_device;
static unsigned error, waits;
static GLES2Context *current;
#define __GLES2_GET_CONTEXT() GLES2Context *gc = current
static KRMResource *retired;
static void SetError(GLES2Context *gc, unsigned value) { error = value; }
static void *GLES2Calloc(GLES2Context *gc, size_t size) { return fail_host ? NULL : calloc(1, size); }
static void GLES2Free(GLES2Context *gc, void *p) { free(p); }
static int GLES2ALLOCDEVICEMEM_HEAP(GLES2Context *gc, unsigned flags, unsigned size, unsigned alignment, PVRSRV_CLIENT_MEM_INFO **result) {
    *result = NULL;
    if(fail_device) { --fail_device; return -1; }
    *result = malloc(sizeof(**result)); assert(*result); (*result)->uAllocSize = size;
    (*result)->pvLinAddr = calloc(1, size); assert((*result)->pvLinAddr); ++live_device; return 0;
}
static void GLES2FREEDEVICEMEM_HEAP(GLES2Context *gc, PVRSRV_CLIENT_MEM_INFO *mem) { --live_device; free(mem->pvLinAddr); free(mem); }
static int KRM_GhostResource(int *manager, KRMResource *original, KRMResource *ghost) {
    if(fail_ghost) return 0;
    ghost->needed = original->needed; original->needed = 0; retired = ghost; return 1;
}
static int KRM_IsResourceNeeded(int *manager, KRMResource *resource) { return resource->needed; }
static void KRM_RetireResource(int *manager, KRMResource *resource) { retired = resource; }
static void KRM_RemoveResourceFromAllLists(int *manager, KRMResource *resource) { if(retired == resource) retired = NULL; }
static void KRM_DestroyUnneededGhosts(GLES2Context *gc, int *manager) { assert(!retired); }
static int WaitUntilBufObjNotUsed(GLES2Context *gc, GLES2BufferObject *buffer) {
    ++waits;
    if(wait_ok) buffer->sResource.needed = 0;
    return wait_ok;
}
#include "buffer_storage_functions.inc"
static GLES2BufferObject *buffer(GLES2Context *gc) {
    GLES2BufferObject *b = calloc(1, sizeof(*b)); assert(b);
    assert(!GLES2ALLOCDEVICEMEM_HEAP(gc, 0, 64, 16, &b->psMemInfo)); b->ui32AllocAlign = 16; b->sResource.needed = 1; return b;
}
/* Exercise the API entry point, including rollback from the optional orphan path. */
static void buffer_data_fallback(int host_failure, int device_failures, int wait_success,
                                 unsigned size, int expect_success)
{
    Shared shared = {0, 1};
    GLES2VertexArrayObject vao = {0};
    GLES2Context gc = {.psSharedState = &shared, .sVAOMachine = {&vao}};
    GLES2BufferObject *b = buffer(&gc);
    PVRSRV_CLIENT_MEM_INFO *old = b->psMemInfo;
    unsigned char update[80]; memset(update, 42, sizeof(update));
    b->ui32BufferSize = 48; b->bRangeCached = 1;
    gc.sBufferObject.psActiveBuffer[0] = b; current = &gc;
    error = waits = 0; fail_host = host_failure; fail_device = device_failures; wait_ok = wait_success;
    glBufferData(GL_ARRAY_BUFFER, size, update, GL_STREAM_DRAW);
    assert(waits == 1);
    if(expect_success)
    {
        assert(!error && b->ui32BufferSize == size && !b->bRangeCached);
        assert(!memcmp(b->psMemInfo->pvLinAddr, update, size));
        if(size <= 48) assert(b->psMemInfo == old && live_device == 1);
    }
    else
    {
        assert(error == GL_OUT_OF_MEMORY && b->ui32BufferSize == 48 && b->bRangeCached);
        if(!wait_success)
        {
            assert(b->psMemInfo == old && b->sResource.needed);
            assert(((unsigned char *)old->pvLinAddr)[0] == 0);
        }
        else assert(!b->psMemInfo);
    }
    assert(!retired);
    fail_host = fail_device = 0; wait_ok = 1; b->sResource.needed = 0;
    FreeBufferObject(&gc, b, 0); assert(!live_device);
}

static void buffer_data_orphan(void)
{
    Shared shared = {0, 1};
    GLES2VertexArrayObject vao = {0};
    GLES2Context gc = {.psSharedState = &shared, .sVAOMachine = {&vao}};
    GLES2BufferObject *b = buffer(&gc);
    PVRSRV_CLIENT_MEM_INFO *old = b->psMemInfo;
    unsigned char update[48]; memset(update, 42, sizeof(update));
    gc.sBufferObject.psActiveBuffer[0] = b; current = &gc; error = waits = 0;
    glBufferData(GL_ARRAY_BUFFER, sizeof(update), update, GL_STREAM_DRAW);
    assert(!error && !waits && retired && b->psMemInfo != old && live_device == 2);
    assert(b->ui32BufferSize == sizeof(update) && !memcmp(b->psMemInfo->pvLinAddr, update, sizeof(update)));
    assert(vao.ui32DirtyState & GLES2_DIRTYFLAG_VAO_ATTRIB_STREAM);
    DestroyBufferObjectGhostKRM(&gc, retired); retired = NULL;
    FreeBufferObject(&gc, b, 0); assert(!live_device);
}
int main(void) {
    Shared shared = {0, 1}; GLES2Context gc = {.psSharedState = &shared}; GLES2BufferObject *b = buffer(&gc);
    PVRSRV_CLIENT_MEM_INFO *old = b->psMemInfo;
    fail_host = 1; assert(!ReplaceBufferStorage(&gc, b, 128, 32)); fail_host = 0;
    fail_device = 2; assert(!ReplaceBufferStorage(&gc, b, 128, 32));
    fail_ghost = 1; assert(!ReplaceBufferStorage(&gc, b, 128, 32)); fail_ghost = 0;
    assert(b->psMemInfo == old && b->sResource.needed && live_device == 1 && !retired);
    fail_device = 1; assert(ReplaceBufferStorage(&gc, b, 128, 32));
    assert(b->psMemInfo != old && b->psMemInfo->uAllocSize == 128 && b->ui32AllocAlign == 32 && live_device == 2);
    DestroyBufferObjectGhostKRM(&gc, retired); retired = NULL;
    FreeBufferObject(&gc, b, 0); assert(!live_device);
    b = buffer(&gc); FreeBufferObject(&gc, b, 0); assert(retired == &b->sResource && live_device == 1);
    DestroyBufferObjectGhostKRM(&gc, retired); retired = NULL;
    b = buffer(&gc); wait_ok = 0; FreeBufferObject(&gc, b, 1); assert(retired == &b->sResource && live_device == 1);
    DestroyBufferObjectGhostKRM(&gc, retired); retired = NULL; wait_ok = 1;
    b = buffer(&gc); assert(ReplaceBufferStorage(&gc, b, 0, 16)); assert(!b->psMemInfo);
    DestroyBufferObjectGhostKRM(&gc, retired); retired = NULL; FreeBufferObject(&gc, b, 0); assert(!live_device);
    buffer_data_fallback(1, 0, 1, 48, 1); /* Host allocation failure: wait and reuse. */
    buffer_data_fallback(0, 2, 1, 48, 1); /* Both device heaps fail: wait and reuse. */
    buffer_data_fallback(1, 0, 1, 32, 1); /* Smaller update keeps the existing allocation. */
    buffer_data_fallback(1, 0, 1, 80, 1); /* A larger update reallocates after waiting. */
    buffer_data_fallback(0, 2, 1, 80, 1); /* Replacement fails, safe reallocation succeeds. */
    buffer_data_fallback(1, 0, 0, 48, 0); /* Failed wait never overwrites busy storage. */
    buffer_data_fallback(0, 2, 0, 48, 0);
    buffer_data_fallback(0, 4, 1, 80, 0); /* Safe reallocation also fails: report OOM. */
    buffer_data_orphan(); /* Successful orphaning still avoids waiting. */
    puts("buffer storage: allocation rollback, API fallback, ghost ownership, deletion and zero size passed");
}
