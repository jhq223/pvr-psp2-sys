/* Ordered retirement stops permanently at the first failed ownership phase. */
#ifndef PVR_RENDER_TARGET_RETIRE_H
#define PVR_RENDER_TARGET_RETIRE_H

static IMG_BOOL KrkrRetireRenderTarget(SrvSysContext *context, IMG_HANDLE *target)
{
    SceUID uid = -1;
    IMG_BOOL internal;
    /* Every slot is created by KrkrAddRenderTarget with a caller-owned UID,
     * matching the services_test SGXRemove -> unregister -> free contract. */
    PVRSRV_ERROR error;
    IMG_INT32 result;
    if(KrkrDeviceFaulted(context)) return IMG_FALSE;
    if(!*target) return IMG_TRUE;
    error = SGXGetRenderTargetDriverMemBlock(&context->s3D, *target, &uid, &internal);
    if(error != PVRSRV_OK) return KrkrDeviceFault(context, "rt-owner", error);
    error = SGXRemoveRenderTarget(&context->s3D, context->hRenderContext, *target);
    if(error != PVRSRV_OK) return KrkrDeviceFault(context, "rt-remove", error);
    error = PVRSRVUnregisterMemBlock(&context->s3D, uid);
    if(error != PVRSRV_OK) return KrkrDeviceFault(context, "rt-unregister", error);
    result = KrkrFreeRenderTargetBlock(context, uid);
    if(result < 0) return KrkrDeviceFault(context, "rt-free", result);
    *target = IMG_NULL;
    return IMG_TRUE;
}

static IMG_BOOL KrkrRetireSurfaceZs(SrvSysContext *context, EGLRenderSurface *surface)
{
    PVRSRV_ERROR error;
    IMG_INT32 result;
    if(KrkrDeviceFaulted(context)) return IMG_FALSE;
    if(!surface->psZSBufferMemInfo) return IMG_TRUE;
    error = PVRSRVUnmapMemoryFromGpu(&context->s3D,
        surface->psZSBufferMemInfo->pvLinAddr, 0, IMG_FALSE);
    if(error != PVRSRV_OK) return KrkrDeviceFault(context, "zs-unmap", error);
    result = sceKernelFreeMemBlock(surface->hZSBufferMemBlockUID);
    if(result < 0) return KrkrDeviceFault(context, "zs-free", result);
    PVRSRVFreeUserModeMem(surface->psZSBufferMemInfo);
    surface->psZSBufferMemInfo = IMG_NULL;
    surface->hZSBufferMemBlockUID = -1;
    return IMG_TRUE;
}

/* Client flush/unbind/invalidations have completed 3D before retirement.
 * This separate poll protects the surface command ring consumed by TA. */
static IMG_BOOL KrkrWaitRenderSurface(SrvSysContext *context, EGLRenderSurface *surface)
{
    PVRSRV_ERROR error;
    if(KrkrDeviceFaulted(context)) return IMG_FALSE;
    error = PVRSRVPollForValue(context->psConnection,
        context->sHWInfo.sMiscInfo.hOSGlobalEvent,
        surface->sPDSBuffer.pui32ReadOffset,
        surface->sPDSBuffer.ui32CommittedHWOffsetInBytes,
        0xFFFFFFFF, 1000, 1000);
    if(error != PVRSRV_OK) return KrkrDeviceFault(context, "surface-wait", error);
    return IMG_TRUE;
}

static IMG_BOOL KrkrRetireSurfaceMemory(SrvSysContext *psSysContext,
    PVRSRV_CLIENT_MEM_INFO **memory)
{
    PVRSRV_ERROR error;
    if(KrkrDeviceFaulted(psSysContext)) return IMG_FALSE;
    if(!*memory) return IMG_TRUE;
    error = IMGEGLFREEDEVICEMEM(&psSysContext->s3D, *memory);
    if(error != PVRSRV_OK) return KrkrDeviceFault(psSysContext, "surface-memory", error);
    *memory = IMG_NULL;
    return IMG_TRUE;
}

static IMG_BOOL KrkrRetireSurfaceStorage(SrvSysContext *context,
    EGLRenderSurface *surface, IMG_BOOL wait)
{
    PVRSRV_ERROR error;
    if(KrkrDeviceFaulted(context)) return IMG_FALSE;
    if(wait && !KrkrWaitRenderSurface(context, surface)) return IMG_FALSE;
    if(!KrkrRetireSurfaceMemory(context, &surface->sTerm.psTerminatePDSMemInfo) ||
       !KrkrRetireSurfaceMemory(context, &surface->sTerm.psSAUpdatePDSMemInfo) ||
       !KrkrRetireSurfaceMemory(context, &surface->sTerm.psTerminateUSEMemInfo) ||
       !KrkrRetireSurfaceMemory(context, &surface->sPDSBuffer.psMemInfo) ||
       !KrkrRetireSurfaceMemory(context, &surface->sUSSEBuffer.psMemInfo)) return IMG_FALSE;
    if(surface->psRenderSurfaceSyncInfo)
    {
        error = PVRSRVFreeSyncInfo(&context->s3D, surface->psRenderSurfaceSyncInfo);
        if(error != PVRSRV_OK) return KrkrDeviceFault(context, "surface-sync", error);
        if(surface->psSyncInfo == surface->psRenderSurfaceSyncInfo) surface->psSyncInfo = IMG_NULL;
        surface->psRenderSurfaceSyncInfo = IMG_NULL;
    }
    if(!KrkrRetireSurfaceMemory(context, &surface->sRenderStatusUpdate.psMemInfo) ||
       !KrkrRetireSurfaceMemory(context, &surface->sUSSEBuffer.psStatusUpdateMemInfo) ||
       !KrkrRetireSurfaceMemory(context, &surface->sPDSBuffer.psStatusUpdateMemInfo) ||
       !KrkrRetireRenderTarget(context, &surface->ahRenderTarget[EGL_RENDER_TARGET_NOAA_INDEX]) ||
       !KrkrRetireRenderTarget(context, &surface->ahRenderTarget[EGL_RENDER_TARGET_AA_INDEX]) ||
       !KrkrRetireSurfaceZs(context, surface)) return IMG_FALSE;
    if(surface->hMutex)
    {
        error = PVRSRVDestroyMutex(surface->hMutex);
        if(error != PVRSRV_OK) return KrkrDeviceFault(context, "surface-mutex", error);
        surface->hMutex = IMG_NULL;
    }
    return IMG_TRUE;
}
#endif
