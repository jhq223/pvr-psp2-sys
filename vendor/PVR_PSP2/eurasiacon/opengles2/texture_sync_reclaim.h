#ifndef GLES2_TEXTURE_SYNC_RECLAIM_H
#define GLES2_TEXTURE_SYNC_RECLAIM_H

/* Called with the names-array primary lock held. Completed transfers no longer
 * need a fence on ordinary texture storage. Render surfaces and EGL images can
 * retain aliases to that fence and must keep it until their storage is freed. */
static IMG_VOID ReleaseIdleTextureSync(GLES2Context *gc, const IMG_VOID *unused,
                                      GLES2NamedItem *item)
{
    GLES2Texture *texture = (GLES2Texture *)item;
    PVRSRV_CLIENT_MEM_INFO *memory = texture->psMemInfo;
    PVRSRV_CLIENT_SYNC_INFO *sync;
    PVR_UNREFERENCED_PARAMETER(unused);

    /* Another sharing context may be between validation and submission. The
     * refcount is protected by the same primary lock as this enumeration. */
    if(gc->psSharedState->ui32RefCount != 1 || !memory || !memory->psClientSyncInfo
       || texture->ui32ValidationPins || texture->ui32NumRenderTargets)
        return;
#if defined(GLES2_EXTENSION_EGL_IMAGE)
    if(texture->psEGLImageSource || texture->psEGLImageTarget) return;
#endif
#if defined(GLES2_EXTENSION_TEXTURE_STREAM)
    if(texture->psBufferDevice) return;
#endif
    if(SWTextureBusy(gc, texture)
       || KRM_IsResourceNeeded(&gc->psSharedState->psTextureManager->sKRM,
                               &texture->sResource))
        return;
    sync = memory->psClientSyncInfo;
    if(SGX2DQueryBlitsComplete(gc->ps3DDevData, sync, IMG_FALSE) != PVRSRV_OK)
        return;
    if(PVRSRVFreeSyncInfo(gc->ps3DDevData, sync) == PVRSRV_OK)
    {
        memory->psClientSyncInfo = IMG_NULL;
        memory->ui32Flags |= PVRSRV_MEM_NO_SYNCOBJ;
    }
}

static IMG_VOID ReclaimIdleTextureSyncs(GLES2Context *gc)
{
    SWTextureTrimSyncs(gc);
    NamesArrayMapFunction(gc, gc->psSharedState->apsNamesArray[GLES2_NAMETYPE_TEXOBJ],
                         ReleaseIdleTextureSync, IMG_NULL);
}

#endif
