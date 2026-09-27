#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef uint8_t IMG_UINT8;
typedef uint16_t IMG_UINT16;
typedef uint32_t IMG_UINT32;
typedef uint64_t IMG_UINT64;
typedef int32_t IMG_INT32;
typedef int IMG_BOOL;
typedef int SceUID;
typedef size_t SceSize;
typedef pthread_mutex_t SceKernelLwMutexWork;
typedef void *IMG_PVOID;
typedef unsigned IMG_SID;
typedef int PVRSRV_ERROR;
#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_NULL NULL
#define SCE_NULL NULL
#define SCE_KERNEL_EVF_WAITMODE_OR 1
#define SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT 2
#define SCE_KERNEL_EVF_ATTR_MULTI 0
#define SCE_KERNEL_LOWEST_PRIORITY_USER 0
#define GLES2_MAX_TEXTURE_MIPMAP_LEVELS 12
#define PVRSRV_OK 0
#define PVRSRV_ERROR_TIMEOUT 1
#define GL_OUT_OF_MEMORY 0x505
#define PVRSRV_MODIFYSYNCOPS_FLAGS_WO_INC 1
#define PVR_UNREFERENCED_PARAMETER(x) ((void)(x))
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef struct { int unused; } PVRSRV_CLIENT_SYNC_INFO;
typedef struct { PVRSRV_CLIENT_SYNC_INFO *psClientSyncInfo; } Memory;
typedef struct { unsigned id; Memory *psMemInfo; int bUploadFailed; } GLES2Texture;
typedef struct { void *pui8Buffer; } GLES2MipMapLevel;
typedef struct { int dummy; } GLES2TextureFormat;
typedef struct { void *psConnection, *hTransferContext; } System;
typedef struct { pthread_mutex_t *hPrimaryLock; unsigned ui32RefCount; } Shared;
typedef struct {
    unsigned bDisableAsyncTextureOp, ui32SwTexOpThreadNum, ui32SwTexOpMaxUltNum,
        ui32SwTexOpThreadPriority, ui32SwTexOpThreadAffinity;
} Hints;
typedef struct {
    struct SWTextureState *psSWTexture;
    void *pvUNCHeap, *ps3DDevData;
    System *psSysContext;
    Shared *psSharedState;
    Hints sAppHints;
} GLES2Context;
typedef struct { GLES2Context *gc; GLES2Texture psTex; GLES2MipMapLevel psMipLevel;
    GLES2TextureFormat psTexFmt; unsigned ui32OffsetInBytes, ui32Face, ui32Lod, ui32TopUsize, ui32TopVsize; } SWTexUploadArg;
typedef struct { GLES2Context *gc; GLES2Texture *psTex; unsigned ui32Face, ui32MaxFace; int bIsNonPow2; } SWTexMipGenArg;

typedef struct {
    int live, kind, started;
    pthread_mutex_t mutex;
    pthread_cond_t cv;
    pthread_t thread;
    unsigned value;
    int (*entry)(SceSize, void *);
    void *argument;
} Handle;
static Handle handles[64];
static _Thread_local int thread_id;
static int fail_at, calls;
static atomic_int uploads, mipmaps, frees, waits, syncs, hold_upload, entered, release_upload, hold_transfer;
static atomic_int fail_sync;
static int fail(void) { return ++calls == fail_at; }
static void delay(void) { struct timespec t = {0, 1000000}; nanosleep(&t, NULL); }
static int handle(int kind) {
    if(fail()) return -1;
    for(int i = 1; i < 64; ++i) if(!handles[i].live) {
        handles[i] = (Handle){.live = 1, .kind = kind};
        assert(!pthread_mutex_init(&handles[i].mutex, NULL));
        assert(!pthread_cond_init(&handles[i].cv, NULL)); return i;
    }
    abort();
}
static int remove_handle(int id) {
    assert(id > 0 && handles[id].live);
    assert(!pthread_mutex_destroy(&handles[id].mutex));
    assert(!pthread_cond_destroy(&handles[id].cv)); handles[id].live = 0; return 0;
}
static int sceKernelCreateLwMutex(SceKernelLwMutexWork *m, const char *name, int attr, int count, void *options) {
    return fail() ? -1 : pthread_mutex_init(m, NULL);
}
static int sceKernelDeleteLwMutex(SceKernelLwMutexWork *m) { return pthread_mutex_destroy(m); }
static int sceKernelLockLwMutex(SceKernelLwMutexWork *m, int count, void *timeout) { return pthread_mutex_lock(m); }
static int sceKernelUnlockLwMutex(SceKernelLwMutexWork *m, int count) { return pthread_mutex_unlock(m); }
static int PVRSRVLockMutex(pthread_mutex_t *m) { return pthread_mutex_lock(m); }
static int PVRSRVUnlockMutex(pthread_mutex_t *m) { return pthread_mutex_unlock(m); }
static int sceKernelCreateEventFlag(const char *name, int attr, int initial, void *options) {
    int id = handle(1); if(id > 0) handles[id].value = initial; return id;
}
static int sceKernelSetEventFlag(int id, unsigned pattern) {
    Handle *h = &handles[id]; pthread_mutex_lock(&h->mutex); h->value |= pattern;
    pthread_cond_broadcast(&h->cv); pthread_mutex_unlock(&h->mutex); return 0;
}
static int sceKernelClearEventFlag(int id, unsigned mask) {
    Handle *h = &handles[id]; pthread_mutex_lock(&h->mutex); h->value &= mask;
    pthread_mutex_unlock(&h->mutex); return 0;
}
static int sceKernelWaitEventFlag(int id, unsigned pattern, int mode, void *result, void *timeout) {
    Handle *h = &handles[id]; pthread_mutex_lock(&h->mutex);
    while(!(h->value & pattern)) pthread_cond_wait(&h->cv, &h->mutex);
    if(mode & SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT) h->value &= ~pattern;
    pthread_mutex_unlock(&h->mutex); return 0;
}
static int sceKernelDeleteEventFlag(int id) { return remove_handle(id); }
static int sceKernelCreateSema(const char *name, int attr, int initial, int max, void *options) {
    int id = handle(2); if(id > 0) handles[id].value = initial; return id;
}
static int sceKernelSignalSema(int id, int count) {
    Handle *h = &handles[id]; pthread_mutex_lock(&h->mutex); h->value += count;
    pthread_cond_broadcast(&h->cv); pthread_mutex_unlock(&h->mutex); return 0;
}
static int sceKernelWaitSema(int id, int count, void *timeout) {
    Handle *h = &handles[id]; pthread_mutex_lock(&h->mutex);
    while(h->value < (unsigned)count) pthread_cond_wait(&h->cv, &h->mutex);
    h->value -= count; pthread_mutex_unlock(&h->mutex); return 0;
}
static int sceKernelDeleteSema(int id) { return remove_handle(id); }
static int sceKernelGetThreadId(void) { return thread_id; }
static void *thread_main(void *argument) {
    thread_id = (int)(intptr_t)argument; Handle *h = &handles[thread_id];
    h->entry(sizeof(void *), &h->argument); return NULL;
}
static int sceKernelCreateThread(const char *name, int (*entry)(SceSize, void *), int priority,
                                unsigned stack, int attr, int affinity, void *options) {
    int id = handle(3); if(id > 0) handles[id].entry = entry; return id;
}
static int sceKernelStartThread(int id, size_t size, void *argument) {
    if(fail()) return -1;
    handles[id].argument = *(void **)argument; handles[id].started = 1;
    return pthread_create(&handles[id].thread, NULL, thread_main, (void *)(intptr_t)id);
}
static int sceKernelWaitThreadEnd(int id, void *status, void *timeout) { return pthread_join(handles[id].thread, NULL); }
static int sceKernelDeleteThread(int id) { return remove_handle(id); }
static int sceKernelDelayThread(unsigned usec) { delay(); return 0; }
static void GLES2Free(GLES2Context *gc, void *pointer) { if(pointer) ++frees; free(pointer); }
static int sceHeapFreeHeapMemory(void *heap, void *pointer) { GLES2Free(NULL, pointer); return 0; }
static void *GLES2MallocHeapUNC(GLES2Context *gc, unsigned size) { return malloc(size); }
static int SGXWaitTransfer(void *device, void *context) {
    ++waits; while(atomic_load(&hold_transfer)) delay(); return 0;
}
static int SGX2DQueryBlitsComplete(void *device, PVRSRV_CLIENT_SYNC_INFO *sync, int wait) { return 0; }
static void SetError(GLES2Context *gc, unsigned error) { assert(error == GL_OUT_OF_MEMORY); }
static int PVRSRVCreateSyncInfoModObj(void *connection, unsigned *id) {
    if(atomic_load(&fail_sync) == 1) return -1;
    *id = 1; ++syncs; return 0;
}
static int PVRSRVModifyPendingSyncOps(void *connection, unsigned id, void *sync, unsigned count, int flags, void *a, void *b) {
    return atomic_load(&fail_sync) == 2 ? -1 : 0;
}
static int PVRSRVDestroySyncInfoModObj(void *connection, unsigned id) { --syncs; return 0; }
static int PVRSRVModifyCompleteSyncOps(void *connection, unsigned id) { return 0; }
static void TextureUpload(GLES2Texture *texture, GLES2MipMapLevel *level, unsigned offset,
                          GLES2TextureFormat *format, unsigned face, unsigned lod, unsigned width, unsigned height) {
    if(texture->id == 1 && atomic_load(&hold_upload)) {
        atomic_store(&entered, 1); while(!atomic_load(&release_upload)) delay();
    }
    assert(*(unsigned char *)level->pui8Buffer == 17); ++uploads;
}
static void MakeTextureMipmapLevelsSoftware(GLES2Context *gc, GLES2Texture *texture, unsigned face, unsigned maxface, int npot) { ++mipmaps; }
#include "swtexop_functions.inc"
static void clean_handles(void) { for(int i = 1; i < 64; ++i) assert(!handles[i].live); assert(!syncs); }
static void upload(GLES2Context *gc, GLES2Texture *texture, void *data) {
    GLES2MipMapLevel level = {data}; GLES2TextureFormat format = {0};
    SWTextureUpload(gc, texture, &level, 0, &format, 0, 0, 1, 1);
}
int main(void) {
    alarm(20);
    pthread_mutex_t primary = PTHREAD_MUTEX_INITIALIZER;
    Shared shared = {&primary, 1}; System sys = {0};
    GLES2Context gc = {.psSharedState = &shared, .psSysContext = &sys, .sAppHints = {0, 2, 1, 0, 0}};
    /* Every kernel-object creation/start failure must unwind or leave the sync path usable. */
    for(fail_at = 1; fail_at <= 11; ++fail_at) {
        calls = 0; if(SWTextureInit(&gc)) SWTextureDestroy(&gc);
        assert(!gc.psSWTexture); clean_handles();
    }
    fail_at = 0; calls = 0; assert(SWTextureInit(&gc));
    PVRSRV_CLIENT_SYNC_INFO sync = {0}; Memory mem = {&sync}; GLES2Texture a = {1, &mem, 0}, b = {2, &mem, 0};
    unsigned char *source = malloc(8), other = 17; memset(source, 17, 8);
    hold_upload = 1; upload(&gc, &a, source); while(!entered) delay();
    assert(SWTextureBusy(&gc, &a) && !SWTextureBusy(&gc, &b));
    texOpAsyncAddForCleanup(&gc, source);
    int before = frees;
    upload(&gc, &b, &other); /* Full job pool takes the synchronous path. */
    assert(uploads == 1 && frees == before);
    release_upload = 1; SWTextureWait(&gc, &a); assert(uploads == 2);
    assert(!SWTextureBusy(&gc, &a));
    for(int failure = 1; failure <= 2; ++failure) {
        fail_sync = failure; upload(&gc, &b, &other); assert(!syncs);
    }
    fail_sync = 0; shared.ui32RefCount = 2; upload(&gc, &b, &other); assert(!gc.psSWTexture->active);
    shared.ui32RefCount = 1;
    assert(SWMakeTextureMipmapLevels(&gc, &b, 0, 1, 0)); SWTextureWait(&gc, NULL); assert(mipmaps == 1);
    /* Batch retirement, wraparound, pooled staging, and draining on shutdown. */
    hold_transfer = 1; int initial_waits = waits;
    for(int i = 0; i < 1000; ++i) texOpAsyncAddForCleanup(&gc, malloc(8));
    hold_transfer = 0;
    for(int i = 0; i < 10000; ++i) texOpAsyncAddForCleanup(&gc, malloc(8));
    SWTextureFreeStaging(&gc, SWTextureAllocStaging(&gc, 17), 17);
    SWTextureStopWorkers(&gc); SWTextureDestroy(&gc);
    assert(!gc.psSWTexture); assert(waits - initial_waits < 11000); clean_handles();
    assert(!pthread_mutex_destroy(&primary));
    puts("async textures: failure unwind, pool saturation, resource waits, retirement and shutdown passed");
}
