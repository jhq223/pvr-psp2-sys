#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uintptr_t IMG_UINT32;
typedef uintptr_t IMG_UINTPTR_T;
typedef int IMG_BOOL;
#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define GLSLPT_FRAGMENT 1
#define PVR_DPF(x) ((void)0)
#define PVR_UNREFERENCED_PARAMETER(x) ((void)(x))
#define GLES_ASSERT assert
#define PVR_ASSERT assert
#define PVRSRVMemSet memset
static unsigned lock_depth, allocations, forbid_allocation;
static void *allocate(size_t bytes) { ++allocations; assert(!forbid_allocation); return calloc(1, bytes); }
#define GLES2Calloc(gc, bytes) allocate(bytes)
#define GLES2Malloc(gc, bytes) allocate(bytes)
#define GLES2Free(gc, ptr) free(ptr)
#define KRM_ENTER_CRITICAL_SECTION(m) do { assert(lock_depth++ == 0); } while(0)
#define KRM_EXIT_CRITICAL_SECTION(m) do { assert(lock_depth-- == 1); } while(0)

typedef struct KRMResource {
    unsigned ui32FirstAttachment, ui32Waiters, pending;
    struct KRMResource *psNext, *psPrev;
} KRMResource;
typedef struct { KRMResource *psResourceList, *psGhostList; unsigned bInitialized; } KRMKickResourceManager;
typedef struct Code { unsigned id; } UCH_UseCodeBlock;
typedef struct Ref { unsigned refs, id; } Ref;
typedef struct GLES2USEShaderVariant_TAG GLES2USEShaderVariant;
typedef struct GLES2PDSCodeVariant_TAG {
    UCH_UseCodeBlock *psCodeBlock;
    IMG_UINT32 *pui32HashCompare, ui32HashCompareSizeInDWords, tHashValue;
    GLES2USEShaderVariant *psUSEVariant;
    struct GLES2PDSCodeVariant_TAG *psNext;
} GLES2PDSCodeVariant;
typedef struct { GLES2USEShaderVariant *psVariant; unsigned eProgramType; } GLES2ProgramShader;
struct GLES2USEShaderVariant_TAG {
    GLES2USEShaderVariant *psNext;
    GLES2ProgramShader *psProgramShader;
    KRMResource sResource;
    UCH_UseCodeBlock *psCodeBlock;
    void *psPatchedShader;
    Ref *psSecondaryUploadTask, *psScratchMem, *psIndexableTempsMem;
    GLES2PDSCodeVariant *psPDSVariant;
};
typedef struct GLES2Context GLES2Context;
#include "statehash.h"
typedef struct { KRMKickResourceManager sUSEShaderVariantKRM; int hSecondaryLock; } Shared;
struct GLES2Context {
    unsigned ui32FrameNum;
    Shared *psSharedState;
    struct {
        HashTable sPDSFragmentVariantHashTable;
        GLES2USEShaderVariant *psCurrentVertexVariant, *psCurrentFragmentVariant;
        void *pvUniPatchContext;
    } sProgram;
};
static unsigned code_frees[64], ref_frees[64], patched_frees;
static void *completion_hook;
static KRMResource *completion_resource;
static void reclaim(GLES2Context *gc);
static void PVRSRVLockMutex(int lock) { assert(lock_depth++ == 0); }
static void PVRSRVUnlockMutex(int lock) { assert(lock_depth-- == 1); }
static int IsResourceNeeded(KRMKickResourceManager *manager, KRMResource *r) { assert(lock_depth); return r->pending; }
static void RemoveResourceFromAllLists(KRMKickResourceManager *m, KRMResource *r) {
    if(r->psPrev) r->psPrev->psNext = r->psNext;
    else if(m->psResourceList == r) m->psResourceList = r->psNext;
    else if(m->psGhostList == r) m->psGhostList = r->psNext;
    if(r->psNext) r->psNext->psPrev = r->psPrev;
}
static void KRM_RemoveResourceFromAllLists(KRMKickResourceManager *m, KRMResource *r) { RemoveResourceFromAllLists(m, r); }
static int KRM_IsResourceNeeded(KRMKickResourceManager *m, KRMResource *r) { return r->pending; }
static void UCH_CodeHeapFree(UCH_UseCodeBlock *code) { if(code) { assert(!code_frees[code->id]++); free(code); } }
static void del_ref(Ref *ref) { if(ref) { assert(ref->refs); if(!--ref->refs) { assert(!ref_frees[ref->id]++); free(ref); } } }
#define USESecondaryUploadTaskDelRef(gc, ref) del_ref(ref)
#define ShaderScratchMemDelRef(gc, ref) del_ref(ref)
#define ShaderIndexableTempsMemDelRef(gc, ref) del_ref(ref)
static void PVRUniPatchDestroyHWShader(void *context, void *patched) {
    assert(patched);
    if(patched == completion_hook) {
        assert(completion_resource->ui32Waiters == 1);
        completion_resource->pending = 0;
        reclaim(context);
        assert(!code_frees[5]);
    }
    ++patched_frees; free(patched);
}
void DestroyUSEShaderVariant(GLES2Context *gc, GLES2USEShaderVariant *variant);
#include "statehash_functions.inc"
#include "shader_retirement_krm_functions.inc"
#include "shader_lifetime_functions.inc"

static UCH_UseCodeBlock *code(unsigned id) { UCH_UseCodeBlock *p = allocate(sizeof(*p)); p->id = id; return p; }
static Ref *ref(unsigned id) { Ref *p = allocate(sizeof(*p)); p->refs = 1; p->id = id; return p; }
static GLES2USEShaderVariant *variant(GLES2Context *gc, GLES2ProgramShader *owner, unsigned id, unsigned pending) {
    GLES2USEShaderVariant *v = allocate(sizeof(*v));
    v->psProgramShader = owner; v->psNext = owner->psVariant; owner->psVariant = v;
    v->psCodeBlock = code(id); v->psPatchedShader = allocate(8);
    v->psSecondaryUploadTask = ref(id); v->psScratchMem = ref(id + 10); v->psIndexableTempsMem = ref(id + 20);
    KRMKickResourceManager *m = &gc->psSharedState->sUSEShaderVariantKRM;
    v->sResource.pending = pending; v->sResource.psNext = m->psResourceList;
    if(m->psResourceList) m->psResourceList->psPrev = &v->sResource;
    m->psResourceList = &v->sResource;
    return v;
}
static void add_pds(GLES2Context *gc, GLES2USEShaderVariant *v, unsigned id) {
    GLES2PDSCodeVariant *p = allocate(sizeof(*p));
    p->psCodeBlock = code(id); p->psUSEVariant = v; p->psNext = v->psPDSVariant; v->psPDSVariant = p;
    p->pui32HashCompare = allocate(sizeof(IMG_UINT32)); *p->pui32HashCompare = id;
    p->tHashValue = id % 2; p->ui32HashCompareSizeInDWords = 1;
    assert(HashTableInsert(gc, &gc->sProgram.sPDSFragmentVariantHashTable, p->tHashValue,
                           p->pui32HashCompare, 1, (IMG_UINT32)p, IMG_NULL));
}
static void reclaim(GLES2Context *gc) {
    KRMKickResourceManager *m = &gc->psSharedState->sUSEShaderVariantKRM;
    ReclaimUnneededResourcesInList(m, &m->psGhostList, DestroyUSECodeVariantGhostKRM, gc, IMG_TRUE);
    assert(!lock_depth);
}
int main(void) {
    Shared shared = {.sUSEShaderVariantKRM.bInitialized = 1};
    GLES2Context gc = {.psSharedState = &shared};
    gc.sProgram.pvUniPatchContext = &gc;
    GLES2ProgramShader owner = {.eProgramType = GLSLPT_FRAGMENT};
    assert(HashTableCreate(&gc, &gc.sProgram.sPDSFragmentVariantHashTable, 2, 32, DestroyHashedPDSVariant));
    GLES2USEShaderVariant *idle = variant(&gc, &owner, 1, 0);
    GLES2USEShaderVariant *pending = variant(&gc, &owner, 2, 1);
    GLES2USEShaderVariant *last = variant(&gc, &owner, 3, 1);
    add_pds(&gc, idle, 31); add_pds(&gc, pending, 32); add_pds(&gc, pending, 33); add_pds(&gc, last, 34);
    gc.sProgram.psCurrentFragmentVariant = pending;
    unsigned before = allocations;
    forbid_allocation = 1;
    FreeListOfFragmentUSEVariants(&gc, &owner.psVariant);
    assert(allocations == before && !owner.psVariant && !gc.sProgram.psCurrentFragmentVariant);
    assert(code_frees[1] && code_frees[31] && !code_frees[2] && !code_frees[3]);
    assert(patched_frees == 3 && !gc.sProgram.sPDSFragmentVariantHashTable.ui32NumEntries);
    assert(!shared.sUSEShaderVariantKRM.psResourceList);
    /* The owning program can disappear immediately; ghosts must not dereference it. */
    memset(&owner, 0xA5, sizeof(owner));
    reclaim(&gc);
    assert(!code_frees[2] && !code_frees[32] && !code_frees[33] && !ref_frees[2]);
    pending->sResource.pending = 0; pending->sResource.ui32Waiters = 1;
    reclaim(&gc); assert(!code_frees[2]);
    pending->sResource.ui32Waiters = 0;
    reclaim(&gc); assert(code_frees[2] && code_frees[32] && code_frees[33] && ref_frees[2]);
    assert(!code_frees[3] && shared.sUSEShaderVariantKRM.psGhostList == &last->sResource);
    last->sResource.pending = 0;
    reclaim(&gc); assert(code_frees[3] && code_frees[34] && !shared.sUSEShaderVariantKRM.psGhostList);
    forbid_allocation = 0;
    /* Ordinary cache eviction still destroys the item; detach only removes metadata. */
    owner = (GLES2ProgramShader){.eProgramType = GLSLPT_FRAGMENT};
    idle = variant(&gc, &owner, 4, 0); add_pds(&gc, idle, 35); add_pds(&gc, idle, 36);
    IMG_UINT32 key = 35, item;
    assert(HashTableDelete(&gc, &gc.sProgram.sPDSFragmentVariantHashTable, 35 % 2, &key, 1, &item));
    assert(code_frees[35] && !code_frees[36] && idle->psPDSVariant->psNext == NULL);
    DestroyUSEShaderVariant(&gc, idle);
    assert(!owner.psVariant && code_frees[4] && code_frees[36] && !shared.sUSEShaderVariantKRM.psResourceList);
    /* A different context sees GPU completion while CPU teardown is in progress. */
    pending = variant(&gc, &owner, 5, 1); add_pds(&gc, pending, 37);
    completion_hook = pending->psPatchedShader; completion_resource = &pending->sResource;
    GhostUSEShaderVariant(&gc, pending);
    assert(!pending->sResource.ui32Waiters && !code_frees[5] && !code_frees[37]);
    reclaim(&gc); assert(code_frees[5] && code_frees[37]);
    /* A context's cache can disappear while shared program code is pending. */
    pending = variant(&gc, &owner, 6, 1); add_pds(&gc, pending, 38);
    idle = variant(&gc, &owner, 7, 0); add_pds(&gc, idle, 39);
    HashTableDestroy(&gc, &gc.sProgram.sPDSFragmentVariantHashTable);
    assert(code_frees[39] && !idle->psPDSVariant);
    assert(!code_frees[38] && !code_frees[6] && !pending->psPDSVariant->pui32HashCompare);
    /* This descriptor represents a context that has already been destroyed. */
    memset(&gc.sProgram.sPDSFragmentVariantHashTable, 0xA5,
           sizeof(gc.sProgram.sPDSFragmentVariantHashTable));
    forbid_allocation = 1;
    FreeListOfFragmentUSEVariants(&gc, &owner.psVariant);
    assert(code_frees[7] && !code_frees[38] && !owner.psVariant);
    pending->sResource.pending = 0;
    reclaim(&gc);
    assert(code_frees[6] && code_frees[38] && !shared.sUSEShaderVariantKRM.psGhostList);
    puts("shader lifetime: allocation-free deletion, pending code, waiter pins, cache eviction and context teardown passed");
}
