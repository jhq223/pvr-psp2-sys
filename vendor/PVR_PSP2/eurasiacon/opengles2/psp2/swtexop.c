#include <kernel.h>
#include "../context.h"
#include "../texture.h"
#include "swtexop.h"

#define SW_RETIRE_COUNT 8192U
#define SW_WORKERS 4U
#define SW_POOL_COUNT 8U
#define SW_NONE 0xffffffffU
#define SW_BUCKETS 64U
#define SW_BYTE_BUDGET (8U * 1024U * 1024U)
#define SW_SYNC_POOL 8U
/* Mipmap workers can retire two buffers per face/level while the producer waits. */
#define SW_RETIRE_RESERVE (SW_WORKERS * 12U * GLES2_MAX_TEXTURE_MIPMAP_LEVELS)

typedef struct SWJob {
    IMG_UINT32 next, state, activeNext, activePrev, textureNext, texturePrev, bytes;
    IMG_UINT64 serial;
    SceUID thread;
    IMG_BOOL mip;
    GLES2Texture *texture;
    IMG_VOID *source;
    PVRSRV_CLIENT_SYNC_INFO *sync;
    IMG_SID modification;
    union { SWTexUploadArg upload; SWTexMipGenArg mipmap; } args;
} SWJob;

typedef struct SWRetired {
    IMG_VOID *pointer;
    IMG_UINT64 serial, exclude, transfer;
    IMG_UINT32 stagingSize;
} SWRetired;

typedef struct SWTextureState {
    GLES2Context *gc;
    SceKernelLwMutexWork lock;
    SceUID work, wake, done, space, cleanup;
    SceUID workers[SW_WORKERS];
    IMG_UINT32 workerCount, jobCount, freeJob, firstJob, lastJob, active;
    SWJob *jobs;
    IMG_UINT64 serial, transfer, completedTransfer;
    IMG_UINT32 activeHead, buckets[SW_BUCKETS], activeBytes, retiredBytes;
    PVRSRV_CLIENT_SYNC_INFO *syncPool[SW_SYNC_POOL];
    IMG_UINT32 syncCount;
    IMG_BOOL stopping, closing;
    SWRetired retired[SW_RETIRE_COUNT];
    IMG_UINT32 head, tail, count;
    IMG_VOID *pool[SW_POOL_COUNT];
    IMG_UINT32 poolSize[SW_POOL_COUNT];
} SWTextureState;

static IMG_VOID Lock(SWTextureState *state) { sceKernelLockLwMutex(&state->lock, 1, SCE_NULL); }
static IMG_VOID Unlock(SWTextureState *state) { sceKernelUnlockLwMutex(&state->lock, 1); }

static IMG_UINT32 TextureBucket(const GLES2Texture *texture)
{
    return ((IMG_UINTPTR_T)texture >> 4) & (SW_BUCKETS - 1);
}

/* Both indexes contain active jobs only; all links are protected by lock. */
static IMG_VOID LinkJob(SWTextureState *state, IMG_UINT32 index)
{
    SWJob *job = &state->jobs[index];
    IMG_UINT32 bucket = TextureBucket(job->texture);
    job->activePrev = job->texturePrev = SW_NONE;
    job->activeNext = state->activeHead;
    if(job->activeNext != SW_NONE) state->jobs[job->activeNext].activePrev = index;
    state->activeHead = index;
    job->textureNext = state->buckets[bucket];
    if(job->textureNext != SW_NONE) state->jobs[job->textureNext].texturePrev = index;
    state->buckets[bucket] = index;
    state->activeBytes += job->bytes;
}

static IMG_VOID UnlinkJob(SWTextureState *state, IMG_UINT32 index)
{
    SWJob *job = &state->jobs[index];
    if(job->activePrev != SW_NONE) state->jobs[job->activePrev].activeNext = job->activeNext;
    else state->activeHead = job->activeNext;
    if(job->activeNext != SW_NONE) state->jobs[job->activeNext].activePrev = job->activePrev;
    if(job->texturePrev != SW_NONE) state->jobs[job->texturePrev].textureNext = job->textureNext;
    else state->buckets[TextureBucket(job->texture)] = job->textureNext;
    if(job->textureNext != SW_NONE) state->jobs[job->textureNext].texturePrev = job->texturePrev;
    state->activeBytes -= job->bytes;
}

static IMG_BOOL HasJob(SWTextureState *state, GLES2Texture *texture, SceUID caller)
{
    IMG_UINT32 i;
    if(!state->active) return IMG_FALSE;
    if(!PVR_OPT(6))
    {
        for(i = 0; i < state->jobCount; ++i)
            if(state->jobs[i].state && state->jobs[i].thread != caller &&
               (!texture || state->jobs[i].texture == texture)) return IMG_TRUE;
        return IMG_FALSE;
    }
    i = texture ? state->buckets[TextureBucket(texture)] : state->activeHead;
    while(i != SW_NONE)
    {
        SWJob *job = &state->jobs[i];
        if(job->thread != caller && (!texture || job->texture == texture)) return IMG_TRUE;
        i = texture ? job->textureNext : job->activeNext;
    }
    return IMG_FALSE;
}

IMG_VOID SWTextureWait(GLES2Context *gc, GLES2Texture *texture)
{
    SWTextureState *state = gc->psSWTexture;
    SceUID caller;
    if(!state) return;
    Lock(state);
    /* Read active under the lock: dispatch and completion update it here too. */
    if(!state->active) { Unlock(state); return; }
    caller = sceKernelGetThreadId();
    while(HasJob(state, texture, caller))
    {
        sceKernelClearEventFlag(state->done, ~1U);
        Unlock(state);
        sceKernelWaitEventFlag(state->done, 1, SCE_KERNEL_EVF_WAITMODE_OR, SCE_NULL, SCE_NULL);
        Lock(state);
    }
    Unlock(state);
}

IMG_BOOL SWTextureBusy(GLES2Context *gc, GLES2Texture *texture)
{
    SWTextureState *state = gc->psSWTexture;
    IMG_BOOL busy;
    if(!state) return IMG_FALSE;
    Lock(state);
    busy = state->active && HasJob(state, texture, sceKernelGetThreadId());
    Unlock(state);
    return busy;
}

static IMG_BOOL CanRetire(SWTextureState *state, const SWRetired *entry)
{
    IMG_UINT32 i;
    for(i = state->activeHead; i != SW_NONE; i = state->jobs[i].activeNext)
    {
        SWJob *job = &state->jobs[i];
        if(job->state && job->serial <= entry->serial && job->serial != entry->exclude &&
           (job->mip || job->source == entry->pointer)) return IMG_FALSE;
    }
    return IMG_TRUE;
}

static IMG_VOID ReleaseRetired(SWTextureState *state, SWRetired *entry)
{
    IMG_UINT32 i;
    if(entry->stagingSize && entry->stagingSize <= 1024U * 1024U)
    {
        Lock(state);
        for(i = 0; i < SW_POOL_COUNT && !state->closing; ++i)
        {
            if(!state->pool[i])
            {
                state->pool[i] = entry->pointer;
                state->poolSize[i] = entry->stagingSize;
                Unlock(state);
                return;
            }
        }
        Unlock(state);
    }
    if(entry->stagingSize) sceHeapFreeHeapMemory(state->gc->pvUNCHeap, entry->pointer);
    else GLES2Free(state->gc, entry->pointer);
}

static IMG_INT32 Cleanup(SceSize size, IMG_VOID *argument)
{
    SWTextureState *state = *(SWTextureState **)argument;
    SWRetired batch[64];
    PVR_UNREFERENCED_PARAMETER(size);
    for(;;)
    {
        IMG_UINT32 examined, count = 0, i;
        IMG_UINT64 transfer = 0;
        IMG_BOOL again, closing;
        Lock(state);
        examined = state->count;
        while(examined-- && count < 64)
        {
            SWRetired entry = state->retired[state->head];
            state->head = (state->head + 1) % SW_RETIRE_COUNT;
            --state->count;
            if(CanRetire(state, &entry))
            {
                batch[count++] = entry;
                state->retiredBytes -= entry.stagingSize;
                if(entry.transfer > transfer) transfer = entry.transfer;
            }
            else
            {
                state->retired[state->tail] = entry;
                state->tail = (state->tail + 1) % SW_RETIRE_COUNT;
                ++state->count;
            }
        }
        again = count == 64;
        closing = state->closing && !state->count;
        if(count) sceKernelSetEventFlag(state->space, 1);
        Unlock(state);
        if(count)
        {
            /* The fixed batch contains only transfers submitted before this wait. */
            if(!PVR_OPT(6) || state->gc->psSharedState->ui32RefCount != 1 || transfer > state->completedTransfer)
            {
                while(SGXWaitTransfer(state->gc->ps3DDevData, state->gc->psSysContext->hTransferContext) != PVRSRV_OK)
                    sceKernelDelayThread(1000);
                state->completedTransfer = transfer;
            }
            for(i = 0; i < count; ++i) ReleaseRetired(state, &batch[i]);
        }
        if(closing) break;
        if(!again)
            sceKernelWaitEventFlag(state->wake, 1,
                SCE_KERNEL_EVF_WAITMODE_OR | SCE_KERNEL_EVF_WAITMODE_CLEAR_PAT, SCE_NULL, SCE_NULL);
    }
    return 0;
}

static IMG_VOID Retire(GLES2Context *gc, IMG_VOID *pointer, IMG_UINT32 stagingSize)
{
    SWTextureState *state = gc->psSWTexture;
    SWRetired entry;
    IMG_UINT32 i, limit = SW_RETIRE_COUNT - SW_RETIRE_RESERVE;
    SceUID caller = sceKernelGetThreadId();
    if(!pointer) return;
    if(!state)
    {
        while(SGXWaitTransfer(gc->ps3DDevData, gc->psSysContext->hTransferContext) != PVRSRV_OK)
            sceKernelDelayThread(1000);
        if(stagingSize) sceHeapFreeHeapMemory(gc->pvUNCHeap, pointer);
        else GLES2Free(gc, pointer);
        return;
    }
    entry.pointer = pointer;
    entry.stagingSize = stagingSize;
    entry.exclude = 0;
    Lock(state);
    entry.serial = state->serial;
    entry.transfer = state->transfer;
    for(i = state->activeHead; i != SW_NONE; i = state->jobs[i].activeNext)
        if(state->jobs[i].thread == caller)
        { entry.exclude = state->jobs[i].serial; limit = SW_RETIRE_COUNT; break; }
    while(state->count >= limit || (PVR_OPT(6) && !entry.exclude && stagingSize && state->retiredBytes &&
        (stagingSize > SW_BYTE_BUDGET || state->retiredBytes > SW_BYTE_BUDGET - stagingSize)))
    {
        sceKernelClearEventFlag(state->space, ~1U);
        sceKernelSetEventFlag(state->wake, 1);
        Unlock(state);
        sceKernelWaitEventFlag(state->space, 1, SCE_KERNEL_EVF_WAITMODE_OR, SCE_NULL, SCE_NULL);
        Lock(state);
    }
    state->retired[state->tail] = entry;
    state->tail = (state->tail + 1) % SW_RETIRE_COUNT;
    ++state->count;
    state->retiredBytes += stagingSize;
    sceKernelSetEventFlag(state->wake, 1);
    Unlock(state);
}

IMG_VOID texOpAsyncAddForCleanup(GLES2Context *gc, IMG_PVOID pointer) { Retire(gc, pointer, 0); }

static IMG_UINT32 StagingSize(IMG_UINT32 size)
{
    IMG_UINT32 capacity = 4096;
    if(size > 1024U * 1024U) return size;
    while(capacity < size) capacity <<= 1;
    return capacity;
}

IMG_VOID *SWTextureAllocStaging(GLES2Context *gc, IMG_UINT32 size)
{
    SWTextureState *state = gc->psSWTexture;
    IMG_UINT32 i, capacity = StagingSize(size);
    IMG_VOID *pointer = IMG_NULL;
    if(state)
    {
        Lock(state);
        for(i = 0; i < SW_POOL_COUNT; ++i)
            if(state->pool[i] && state->poolSize[i] == capacity)
            { pointer = state->pool[i]; state->pool[i] = IMG_NULL; break; }
        Unlock(state);
    }
    return pointer ? pointer : GLES2MallocHeapUNC(gc, capacity);
}

IMG_VOID SWTextureFreeStaging(GLES2Context *gc, IMG_VOID *pointer, IMG_UINT32 size)
{ Retire(gc, pointer, StagingSize(size)); }

static IMG_VOID Execute(SWJob *job)
{
    if(job->mip)
    {
        SWTexMipGenArg *arg = &job->args.mipmap;
        MakeTextureMipmapLevelsSoftware(arg->gc, arg->psTex, arg->ui32Face, arg->ui32MaxFace, arg->bIsNonPow2);
    }
    else
    {
        SWTexUploadArg *arg = &job->args.upload;
        TextureUpload(&arg->psTex, &arg->psMipLevel, arg->ui32OffsetInBytes, &arg->psTexFmt,
                      arg->ui32Face, arg->ui32Lod, arg->ui32TopUsize, arg->ui32TopVsize);
    }
}

static IMG_INT32 Worker(SceSize size, IMG_VOID *argument)
{
    SWTextureState *state = *(SWTextureState **)argument;
    PVR_UNREFERENCED_PARAMETER(size);
    for(;;)
    {
        SWJob *job;
        IMG_UINT32 index;
        sceKernelWaitSema(state->work, 1, SCE_NULL);
        Lock(state);
        if(state->firstJob == SW_NONE)
        { IMG_BOOL stop = state->stopping; Unlock(state); if(stop) break; else continue; }
        index = state->firstJob;
        job = &state->jobs[index];
        state->firstJob = job->next;
        if(state->firstJob == SW_NONE) state->lastJob = SW_NONE;
        job->thread = sceKernelGetThreadId();
        Unlock(state);
        Execute(job);
        if(job->modification)
        {
            PVRSRVModifyCompleteSyncOps(state->gc->psSysContext->psConnection, job->modification);
            PVRSRVDestroySyncInfoModObj(state->gc->psSysContext->psConnection, job->modification);
        }
        Lock(state);
        UnlinkJob(state, index);
        job->state = 0;
        job->thread = -1;
        job->next = state->freeJob;
        state->freeJob = index;
        --state->active;
        sceKernelSetEventFlag(state->done, 1);
        sceKernelSetEventFlag(state->wake, 1);
        Unlock(state);
    }
    return 0;
}

static IMG_BOOL Dispatch(GLES2Context *gc, SWJob *input)
{
    SWTextureState *state = gc->psSWTexture;
    PVRSRV_ERROR error;
    IMG_UINT32 index = SW_NONE;
    /* Object mutation is serialized with earlier CPU uploads, even for a sync fallback. */
    SWTextureWait(gc, input->texture);
    if(input->sync)
    {
        do { error = SGX2DQueryBlitsComplete(gc->ps3DDevData, input->sync, IMG_TRUE); }
        while(error == PVRSRV_ERROR_TIMEOUT);
        if(error != PVRSRV_OK) { SetError(gc, GL_OUT_OF_MEMORY); return IMG_FALSE; }
    }
    PVRSRVLockMutex(gc->psSharedState->hPrimaryLock);
    if(state)
    {
        Lock(state);
        /* Shared objects use the synchronous path, so another context cannot miss a CPU job. */
        if(!state->stopping && state->workerCount && gc->psSharedState->ui32RefCount == 1 &&
           (!PVR_OPT(6) || (input->bytes <= SW_BYTE_BUDGET && state->activeBytes <= SW_BYTE_BUDGET - input->bytes)))
        {
            index = state->freeJob;
            if(index != SW_NONE) state->freeJob = state->jobs[index].next;
        }
        Unlock(state);
    }
    if(index == SW_NONE) { PVRSRVUnlockMutex(gc->psSharedState->hPrimaryLock); Execute(input); return IMG_TRUE; }
    input->modification = 0;
    if(input->sync)
    {
        error = PVRSRVCreateSyncInfoModObj(gc->psSysContext->psConnection, &input->modification);
        if(error == PVRSRV_OK)
        {
            error = PVRSRVModifyPendingSyncOps(gc->psSysContext->psConnection, input->modification,
                &input->sync, 1, PVRSRV_MODIFYSYNCOPS_FLAGS_WO_INC, IMG_NULL, IMG_NULL);
            if(error != PVRSRV_OK)
                PVRSRVDestroySyncInfoModObj(gc->psSysContext->psConnection, input->modification);
        }
        if(error != PVRSRV_OK)
        {
            Lock(state); state->jobs[index].next = state->freeJob; state->freeJob = index; Unlock(state);
            PVRSRVUnlockMutex(gc->psSharedState->hPrimaryLock);
            Execute(input);
            return IMG_TRUE;
        }
    }
    Lock(state);
    input->state = 1;
    input->serial = ++state->serial;
    input->thread = -1;
    input->next = SW_NONE;
    state->jobs[index] = *input;
    LinkJob(state, index);
    if(state->lastJob != SW_NONE) state->jobs[state->lastJob].next = index;
    else state->firstJob = index;
    state->lastJob = index;
    ++state->active;
    Unlock(state);
    sceKernelSignalSema(state->work, 1);
    PVRSRVUnlockMutex(gc->psSharedState->hPrimaryLock);
    return IMG_TRUE;
}

IMG_INTERNAL IMG_VOID SWTextureUpload(GLES2Context *gc, GLES2Texture *texture, GLES2MipMapLevel *level,
    IMG_UINT32 offset, GLES2TextureFormat *format, IMG_UINT32 face, IMG_UINT32 lod, IMG_UINT32 width, IMG_UINT32 height)
{
    SWJob job = {0};
    SWTexUploadArg *arg = &job.args.upload;
    SWTextureWait(gc, texture);
    job.texture = texture; job.source = level->pui8Buffer;
    job.bytes = level->ui32ImageSize;
#if defined(GLES2_EXTENSION_EGL_IMAGE)
    job.sync = texture->psEGLImageTarget ? texture->psEGLImageTarget->psMemInfo->psClientSyncInfo : texture->psMemInfo->psClientSyncInfo;
#else
    job.sync = texture->psMemInfo->psClientSyncInfo;
#endif
    arg->gc = gc; arg->psTex = *texture; arg->psMipLevel = *level; arg->psTexFmt = *format;
    arg->ui32OffsetInBytes = offset; arg->ui32Face = face; arg->ui32Lod = lod;
    arg->ui32TopUsize = width; arg->ui32TopVsize = height;
    if(!Dispatch(gc, &job)) texture->bUploadFailed = IMG_TRUE;
}

IMG_INTERNAL IMG_BOOL SWMakeTextureMipmapLevels(GLES2Context *gc, GLES2Texture *texture,
    IMG_UINT32 face, IMG_UINT32 maxFace, IMG_BOOL nonPowerOfTwo)
{
    SWJob job = {0};
    SWTexMipGenArg *arg = &job.args.mipmap;
    job.texture = texture; job.mip = IMG_TRUE;
    job.bytes = texture->psMemInfo ? texture->psMemInfo->uAllocSize : texture->psMipLevel[0].ui32ImageSize;
    job.sync = texture->psMemInfo ? texture->psMemInfo->psClientSyncInfo : IMG_NULL;
#if defined(GLES2_EXTENSION_EGL_IMAGE)
    if(texture->psEGLImageTarget) job.sync = texture->psEGLImageTarget->psMemInfo->psClientSyncInfo;
#endif
    arg->gc = gc; arg->psTex = texture; arg->ui32Face = face; arg->ui32MaxFace = maxFace;
    arg->bIsNonPow2 = nonPowerOfTwo;
    return Dispatch(gc, &job);
}

IMG_VOID SWTextureStopWorkers(GLES2Context *gc)
{
    SWTextureState *state = gc->psSWTexture;
    IMG_UINT32 i;
    if(!state) return;
    SWTextureWait(gc, IMG_NULL);
    Lock(state); state->stopping = IMG_TRUE; Unlock(state);
    for(i = 0; i < state->workerCount; ++i) sceKernelSignalSema(state->work, 1);
    for(i = 0; i < state->workerCount; ++i)
    {
        sceKernelWaitThreadEnd(state->workers[i], SCE_NULL, SCE_NULL);
        sceKernelDeleteThread(state->workers[i]);
    }
    state->workerCount = 0;
}

IMG_VOID SWTextureDestroy(GLES2Context *gc)
{
    SWTextureState *state = gc->psSWTexture;
    IMG_UINT32 i;
    if(!state) return;
    SWTextureStopWorkers(gc);
    Lock(state); state->closing = IMG_TRUE; sceKernelSetEventFlag(state->wake, 1); Unlock(state);
    sceKernelWaitThreadEnd(state->cleanup, SCE_NULL, SCE_NULL);
    sceKernelDeleteThread(state->cleanup);
    for(i = 0; i < SW_POOL_COUNT; ++i)
        if(state->pool[i]) sceHeapFreeHeapMemory(gc->pvUNCHeap, state->pool[i]);
    for(i = 0; i < state->syncCount; ++i)
        PVRSRVFreeSyncInfo(gc->ps3DDevData, state->syncPool[i]);
    sceKernelDeleteSema(state->work);
    sceKernelDeleteEventFlag(state->space); sceKernelDeleteEventFlag(state->done); sceKernelDeleteEventFlag(state->wake);
    sceKernelDeleteLwMutex(&state->lock);
    GLES2Free(IMG_NULL, state->jobs); GLES2Free(IMG_NULL, state);
    gc->psSWTexture = IMG_NULL;
}

IMG_BOOL SWTextureInit(GLES2Context *gc)
{
    SWTextureState *state = calloc(1, sizeof(*state));
    IMG_UINT32 i, count;
    IMG_BOOL locked = IMG_FALSE;
    if(!state) return IMG_FALSE;
    state->gc = gc;
    state->work = state->wake = state->done = state->space = state->cleanup = -1;
    state->firstJob = state->lastJob = state->freeJob = state->activeHead = SW_NONE;
    for(i = 0; i < SW_BUCKETS; ++i) state->buckets[i] = SW_NONE;
    count = gc->sAppHints.bDisableAsyncTextureOp ? 0 : MIN(gc->sAppHints.ui32SwTexOpThreadNum, SW_WORKERS);
    state->jobCount = count ? MIN(gc->sAppHints.ui32SwTexOpMaxUltNum, 4096U) : 0;
    if(!state->jobCount) count = 0;
    if(state->jobCount)
    {
        state->jobs = calloc(state->jobCount, sizeof(*state->jobs));
        if(!state->jobs) goto failed;
        for(i = 0; i < state->jobCount; ++i)
        { state->jobs[i].next = i + 1; state->jobs[i].thread = -1; }
        state->jobs[state->jobCount - 1].next = SW_NONE;
        state->freeJob = 0;
    }
    if(sceKernelCreateLwMutex(&state->lock, "GLES2 texture jobs", 0, 0, SCE_NULL) < 0) goto failed;
    locked = IMG_TRUE;
    state->wake = sceKernelCreateEventFlag("GLES2 retire wake", 0, 0, SCE_NULL);
    state->done = sceKernelCreateEventFlag("GLES2 jobs done", SCE_KERNEL_EVF_ATTR_MULTI, 0, SCE_NULL);
    state->space = sceKernelCreateEventFlag("GLES2 retire space", SCE_KERNEL_EVF_ATTR_MULTI, 0, SCE_NULL);
    state->work = sceKernelCreateSema("GLES2 upload work", 0, 0, state->jobCount + SW_WORKERS, SCE_NULL);
    if(state->wake < 0 || state->done < 0 || state->space < 0 || state->work < 0) goto failed;
    gc->psSWTexture = state;
    state->cleanup = sceKernelCreateThread("GLES2 texture retire", Cleanup, SCE_KERNEL_LOWEST_PRIORITY_USER,
        16 * 1024, 0, 0, SCE_NULL);
    if(state->cleanup < 0) goto failed;
    if(sceKernelStartThread(state->cleanup, sizeof(state), &state) < 0) goto failed;
    for(i = 0; i < count; ++i)
    {
        SceUID thread = sceKernelCreateThread("GLES2 texture worker", Worker, gc->sAppHints.ui32SwTexOpThreadPriority,
            64 * 1024, 0, gc->sAppHints.ui32SwTexOpThreadAffinity, SCE_NULL);
        if(thread < 0) break;
        if(sceKernelStartThread(thread, sizeof(state), &state) < 0) { sceKernelDeleteThread(thread); break; }
        state->workers[state->workerCount++] = thread;
    }
    /* Failure to start a worker leaves a fully usable synchronous upload path. */
    return IMG_TRUE;
failed:
    gc->psSWTexture = IMG_NULL;
    if(state->cleanup >= 0) sceKernelDeleteThread(state->cleanup);
    if(state->work >= 0) sceKernelDeleteSema(state->work);
    if(state->space >= 0) sceKernelDeleteEventFlag(state->space);
    if(state->done >= 0) sceKernelDeleteEventFlag(state->done);
    if(state->wake >= 0) sceKernelDeleteEventFlag(state->wake);
    if(locked) sceKernelDeleteLwMutex(&state->lock);
    GLES2Free(IMG_NULL, state->jobs); GLES2Free(IMG_NULL, state);
    return IMG_FALSE;
}

/* Publish only after SGXQueueTransfer returns, before retiring its input.
 * Publishing before submission lets concurrent CPU cleanup mark an unsubmitted
 * epoch complete. CPU-only batches need no GPU transfer wait. */
IMG_VOID SWTextureTransferSubmitted(GLES2Context *gc)
{
    SWTextureState *state = gc->psSWTexture;
    if(state) { Lock(state); ++state->transfer; Unlock(state); }
}

IMG_VOID SWTextureTrimStaging(GLES2Context *gc)
{
    SWTextureState *state = gc->psSWTexture;
    IMG_VOID *unused[SW_POOL_COUNT];
    IMG_UINT32 i;
    if(!state) return;
    Lock(state);
    for(i = 0; i < SW_POOL_COUNT; ++i)
    { unused[i] = state->pool[i]; state->pool[i] = IMG_NULL; }
    Unlock(state);
    for(i = 0; i < SW_POOL_COUNT; ++i)
        if(unused[i]) sceHeapFreeHeapMemory(gc->pvUNCHeap, unused[i]);
}

PVRSRV_CLIENT_SYNC_INFO *SWTextureAcquireSync(GLES2Context *gc)
{
    SWTextureState *state = gc->psSWTexture;
    PVRSRV_CLIENT_SYNC_INFO *sync = IMG_NULL;
    if(state)
    {
        Lock(state);
        if(state->syncCount) sync = state->syncPool[--state->syncCount];
        Unlock(state);
    }
    if(!sync && PVRSRVAllocSyncInfo(gc->ps3DDevData, &sync) != PVRSRV_OK) return IMG_NULL;
    return sync;
}

IMG_VOID SWTextureReleaseSync(GLES2Context *gc, PVRSRV_CLIENT_SYNC_INFO *sync)
{
    SWTextureState *state = gc->psSWTexture;
    if(!sync) return;
    if(PVR_OPT(7) && state && SGX2DQueryBlitsComplete(gc->ps3DDevData, sync, IMG_FALSE) == PVRSRV_OK)
    {
        Lock(state);
        if(!state->closing && state->syncCount < SW_SYNC_POOL)
        { state->syncPool[state->syncCount++] = sync; Unlock(state); return; }
        Unlock(state);
    }
    PVRSRVFreeSyncInfo(gc->ps3DDevData, sync);
}
