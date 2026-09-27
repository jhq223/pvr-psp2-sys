#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned IMG_UINT32;
typedef uintptr_t IMG_UINTPTR_T;
typedef int IMG_BOOL;
typedef int PVRSRV_ERROR;
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
typedef struct { unsigned needed; } KRMResource;
typedef struct { unsigned uAllocSize; } PVRSRV_CLIENT_MEM_INFO;
typedef struct { struct { unsigned ui32RefCount; } sNamedItem; KRMResource sResource; PVRSRV_CLIENT_MEM_INFO *psMemInfo; unsigned ui32AllocAlign; } GLES2BufferObject;
typedef struct { int sBufferObjectKRM; unsigned ui32RefCount; } Shared;
typedef struct { Shared *psSharedState; } GLES2Context;
static int fail_host, fail_device, fail_ghost, wait_ok = 1, live_device;
static KRMResource *retired;
static void *GLES2Calloc(GLES2Context *gc, size_t size) { return fail_host ? NULL : calloc(1, size); }
static void GLES2Free(GLES2Context *gc, void *p) { free(p); }
static int GLES2ALLOCDEVICEMEM_HEAP(GLES2Context *gc, unsigned flags, unsigned size, unsigned alignment, PVRSRV_CLIENT_MEM_INFO **result) {
    if(fail_device) { --fail_device; return -1; }
    *result = malloc(sizeof(**result)); assert(*result); (*result)->uAllocSize = size; ++live_device; return 0;
}
static void GLES2FREEDEVICEMEM_HEAP(GLES2Context *gc, PVRSRV_CLIENT_MEM_INFO *mem) { --live_device; free(mem); }
static int KRM_GhostResource(int *manager, KRMResource *original, KRMResource *ghost) {
    if(fail_ghost) return 0;
    ghost->needed = original->needed; original->needed = 0; retired = ghost; return 1;
}
static int KRM_IsResourceNeeded(int *manager, KRMResource *resource) { return resource->needed; }
static void KRM_RetireResource(int *manager, KRMResource *resource) { retired = resource; }
static void KRM_RemoveResourceFromAllLists(int *manager, KRMResource *resource) { if(retired == resource) retired = NULL; }
static int WaitUntilBufObjNotUsed(GLES2Context *gc, GLES2BufferObject *buffer) { return wait_ok; }
#include "buffer_storage_functions.inc"
static GLES2BufferObject *buffer(GLES2Context *gc) {
    GLES2BufferObject *b = calloc(1, sizeof(*b)); assert(b);
    assert(!GLES2ALLOCDEVICEMEM_HEAP(gc, 0, 64, 16, &b->psMemInfo)); b->ui32AllocAlign = 16; b->sResource.needed = 1; return b;
}
int main(void) {
    Shared shared = {0, 1}; GLES2Context gc = {&shared}; GLES2BufferObject *b = buffer(&gc);
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
    puts("buffer storage: allocation rollback, fallback, ghost ownership, deletion and zero size passed");
}
