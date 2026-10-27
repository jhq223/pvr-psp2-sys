/* Successful native allocation/free boundaries, independent of GL object names. */
#include "egl_internal.h"
#include "srv.h"
#include "memory_diagnostics.h"

static IMG_INT32 MemoryBlockBytes(SceUID uid, IMG_UINT32 *bytes)
{
    IMG_VOID *base = IMG_NULL;
    SceKernelMemBlockInfo info;
    IMG_INT32 result;
    *bytes = 0;
    if(uid <= 0) return 0;
    result = sceKernelGetMemBlockBase(uid, &base);
    if(result < 0) return result;
    info.size = sizeof(info);
    result = sceKernelGetMemBlockInfoByAddr(base, &info);
    if(result < 0) return result;
    *bytes = info.mappedSize;
    return 0;
}

static IMG_VOID CounterAdd(IMG_UINT32 *live, IMG_UINT32 *peak, IMG_UINT32 bytes)
{
    *live += bytes;
    if(*live > *peak) *peak = *live;
}

SceUID KrkrAllocRenderTargetBlock(SrvSysContext *context, const IMG_CHAR *name, IMG_UINT32 bytes)
{
    SceUID uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_NC_RW, bytes, SCE_NULL);
    IMG_UINT32 mapped = 0;
    IMG_INT32 query;
    if(uid < 0) return uid;
    query = MemoryBlockBytes(uid, &mapped);
    PVRSRVLockMutex(context->hMemoryStatsLock);
    if(query < 0) context->memoryCounters.valid = IMG_FALSE;
    else if(context->memoryCounters.valid)
        CounterAdd(&context->memoryCounters.renderTargetLive, &context->memoryCounters.renderTargetPeak, mapped);
    PVRSRVUnlockMutex(context->hMemoryStatsLock);
    return uid;
}

IMG_INT32 KrkrFreeRenderTargetBlock(SrvSysContext *context, SceUID uid)
{
    IMG_UINT32 mapped = 0;
    IMG_INT32 query = MemoryBlockBytes(uid, &mapped);
    IMG_INT32 result = sceKernelFreeMemBlock(uid);
    PVRSRVLockMutex(context->hMemoryStatsLock);
    if(query < 0) context->memoryCounters.valid = IMG_FALSE;
    else if(result >= 0 && context->memoryCounters.valid)
    {
        if(mapped > context->memoryCounters.renderTargetLive) context->memoryCounters.valid = IMG_FALSE;
        else context->memoryCounters.renderTargetLive -= mapped;
    }
    PVRSRVUnlockMutex(context->hMemoryStatsLock);
    return result;
}

IMG_VOID KrkrDirectDeviceAllocated(SrvSysContext *context, IMG_UINT32 bytes)
{
    PVRSRVLockMutex(context->hMemoryStatsLock);
    CounterAdd(&context->memoryCounters.directLive, &context->memoryCounters.directPeak, bytes);
    PVRSRVUnlockMutex(context->hMemoryStatsLock);
}
IMG_VOID KrkrDirectDeviceFreed(SrvSysContext *context, IMG_UINT32 bytes)
{
    PVRSRVLockMutex(context->hMemoryStatsLock);
    if(bytes > context->memoryCounters.directLive) context->memoryCounters.valid = IMG_FALSE;
    else context->memoryCounters.directLive -= bytes;
    PVRSRVUnlockMutex(context->hMemoryStatsLock);
}
IMG_VOID KrkrSurfaceCreated(SrvSysContext *context)
{
    PVRSRVLockMutex(context->hMemoryStatsLock);
    ++context->memoryCounters.surfaceCreates;
    PVRSRVUnlockMutex(context->hMemoryStatsLock);
}
IMG_VOID KrkrSurfaceRetired(SrvSysContext *context)
{
    PVRSRVLockMutex(context->hMemoryStatsLock);
    ++context->memoryCounters.surfaceRetires;
    PVRSRVUnlockMutex(context->hMemoryStatsLock);
}
IMG_INT32 KrkrQueryDriverMemory(SrvSysContext *context, PvrDriverMemorySnapshot *out)
{
    IMG_INT32 result;
    if(!out || !context->hMemoryStatsLock) return -1;
    result = MemoryBlockBytes(context->hDriverMemUID, &out->registeredUNC);
    if(result < 0) return result;
    result = MemoryBlockBytes(context->h3DParamBufMemUID, &out->parameterCDRAM);
    if(result < 0) return result;
    PVRSRVLockMutex(context->hMemoryStatsLock);
    if(!context->memoryCounters.valid) { PVRSRVUnlockMutex(context->hMemoryStatsLock); return -1; }
    out->renderTargetLive = context->memoryCounters.renderTargetLive;
    out->renderTargetPeak = context->memoryCounters.renderTargetPeak;
    out->directLive = context->memoryCounters.directLive;
    out->directPeak = context->memoryCounters.directPeak;
    out->surfaceCreates = context->memoryCounters.surfaceCreates;
    out->surfaceRetires = context->memoryCounters.surfaceRetires;
    PVRSRVUnlockMutex(context->hMemoryStatsLock);
    return 0;
}
