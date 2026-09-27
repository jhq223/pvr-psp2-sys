#ifndef GLES2_FBO_SURFACE_CACHE_H
#define GLES2_FBO_SURFACE_CACHE_H

/* Cache native surfaces, not GL names or pixels. Leave sync slots for transfers
 * and the window. Busy/shared surfaces may temporarily exceed this target. */
#define GLES2_FBO_SURFACE_CACHE_SIZE 16U

/* The shared names lock protects the list and attachable lifetime. */
static IMG_VOID UnlinkFBOSurface(GLES2Context *gc, GLES2FrameBufferAttachable *a)
{
    GLES2ContextSharedState *s = gc->psSharedState;
    if(!a->bSurfaceCached) return;
    if(a->psSurfacePrev) a->psSurfacePrev->psSurfaceNext = a->psSurfaceNext;
    else s->psSurfaceCacheHead = a->psSurfaceNext;
    if(a->psSurfaceNext) a->psSurfaceNext->psSurfacePrev = a->psSurfacePrev;
    else s->psSurfaceCacheTail = a->psSurfacePrev;
    a->psSurfacePrev = a->psSurfaceNext = IMG_NULL;
    a->bSurfaceCached = IMG_FALSE;
    --s->ui32SurfaceCacheCount;
}

static IMG_VOID ForgetFBOSurface(GLES2Context *gc, GLES2FrameBufferAttachable *a)
{
    /* Reclamation unlinks before calling the normal destruction path while
     * already holding the names lock. Ordinary deletion takes it here. */
    if(!a->bSurfaceCached) return;
    PVRSRVLockMutex(gc->psSharedState->hPrimaryLock);
    UnlinkFBOSurface(gc, a);
    PVRSRVUnlockMutex(gc->psSharedState->hPrimaryLock);
}

static IMG_VOID TouchFBOSurface(GLES2Context *gc, GLES2FrameBuffer *fb)
{
    GLES2ContextSharedState *s = gc->psSharedState;
    GLES2FrameBufferAttachable *a = fb->apsAttachment[GLES2_COLOR_ATTACHMENT];
    if(fb->eStatus != GL_FRAMEBUFFER_COMPLETE || !a || !a->psRenderSurface
       || fb->apsAttachment[GLES2_DEPTH_ATTACHMENT]
       || fb->apsAttachment[GLES2_STENCIL_ATTACHMENT]) return;
    PVRSRVLockMutex(s->hPrimaryLock);
    if(s->psSurfaceCacheTail != a)
    {
        UnlinkFBOSurface(gc, a);
        a->psSurfacePrev = s->psSurfaceCacheTail;
        if(s->psSurfaceCacheTail) s->psSurfaceCacheTail->psSurfaceNext = a;
        else s->psSurfaceCacheHead = a;
        s->psSurfaceCacheTail = a;
        a->bSurfaceCached = IMG_TRUE;
        ++s->ui32SurfaceCacheCount;
    }
    PVRSRVUnlockMutex(s->hPrimaryLock);
}

static IMG_BOOL FBOSurfaceIsIdle(GLES2Context *gc, GLES2FrameBufferAttachable *a)
{
    EGLRenderSurface *surface = a->psRenderSurface;
    PVRSRV_CLIENT_MEM_INFO *memory;
    IMG_UINT32 i;
    /* bInExternalFrame is drawable/accumulation state, not GPU completion.
     * StartFrame sets it on FBOs too; their final kick leaves it set. */
    if(!surface || a->bGhosted || surface == gc->psRenderSurface
       || surface->hEGLSurface || surface->bInFrame
       || surface->ui32FBOAttachmentCount != 1 || surface->bDepthStencilBits)
        return IMG_FALSE;
    for(i = 0; i < GLES2_MAX_ATTACHMENTS; ++i)
        if(gc->sFrameBuffer.psActiveFrameBuffer->apsAttachment[i] == a)
            return IMG_FALSE;
    if(a->eAttachmentType == GL_TEXTURE)
    {
        GLES2Texture *texture = ((GLES2MipMapLevel *)a)->psTex;
        if(texture->ui32ValidationPins || SWTextureBusy(gc, texture)
           || KRM_IsResourceNeeded(&gc->psSharedState->psTextureManager->sKRM,
                                   &texture->sResource)) return IMG_FALSE;
#if defined(GLES2_EXTENSION_EGL_IMAGE)
        if(texture->psEGLImageSource || texture->psEGLImageTarget) return IMG_FALSE;
#endif
#if defined(GLES2_EXTENSION_TEXTURE_STREAM)
        if(texture->psBufferDevice) return IMG_FALSE;
#endif
        memory = texture->psMemInfo;
    }
    else
    {
        GLES2RenderBuffer *rb = (GLES2RenderBuffer *)a;
#if defined(GLES2_EXTENSION_EGL_IMAGE)
        if(rb->psEGLImageSource || rb->psEGLImageTarget) return IMG_FALSE;
#endif
        memory = rb->psMemInfo;
    }
    if(!memory) return IMG_FALSE;
    /* Include transfer readers and the surface's own fence, even when rendering
     * currently uses the texture's fence. Queries never wait or submit work. */
    if(SGX2DQueryBlitsComplete(gc->ps3DDevData, surface->psSyncInfo, IMG_FALSE) != PVRSRV_OK
       || (surface->psSyncInfo != surface->psRenderSurfaceSyncInfo
           && SGX2DQueryBlitsComplete(gc->ps3DDevData, surface->psRenderSurfaceSyncInfo,
                                     IMG_FALSE) != PVRSRV_OK)
       || (memory->psClientSyncInfo && memory->psClientSyncInfo != surface->psSyncInfo
           && SGX2DQueryBlitsComplete(gc->ps3DDevData, memory->psClientSyncInfo,
                                     IMG_FALSE) != PVRSRV_OK)) return IMG_FALSE;
    if(*surface->sPDSBuffer.pui32ReadOffset != surface->sPDSBuffer.ui32CommittedHWOffsetInBytes
       || *surface->sUSSEBuffer.pui32ReadOffset != surface->sUSSEBuffer.ui32CommittedHWOffsetInBytes)
        return IMG_FALSE;
    return IMG_TRUE;
}

static IMG_VOID InvalidateCachedFBOSurface(GLES2Context *gc, EGLRenderSurface *surface)
{
    GLES2NamesArray *names = gc->psSharedState->apsNamesArray[GLES2_NAMETYPE_FRAMEBUFFER];
    IMG_UINT32 i;
    /* The primary lock is already held: NamesArrayMapFunction would relock it. */
    for(i = 0; i < GLES2_DEFAULT_NAMES_ARRAY_SIZE; ++i)
    {
        GLES2NamedItem *item;
        for(item = names->apsEntry[i]; item; item = item->psNext)
        {
            GLES2FrameBuffer *fb = (GLES2FrameBuffer *)item;
            if(item->bGeneratedButUnused) continue;
            if(fb->sDrawParams.psRenderSurface == surface || fb->sReadParams.psRenderSurface == surface)
            {
                fb->eStatus = GLES2_FRAMEBUFFER_STATUS_UNKNOWN;
                fb->sDrawParams.psRenderSurface = fb->sReadParams.psRenderSurface = IMG_NULL;
                fb->sDrawParams.psSyncInfo = fb->sReadParams.psSyncInfo = IMG_NULL;
            }
        }
    }
}

static IMG_VOID TrimIdleFBOSurfaces(GLES2Context *gc)
{
    GLES2ContextSharedState *s = gc->psSharedState;
    GLES2FrameBufferAttachable *a, *next;
    PVRSRVLockMutex(s->hPrimaryLock);
    if(s->ui32RefCount == 1)
    {
        for(a = s->psSurfaceCacheHead;
            a && s->ui32SurfaceCacheCount >= GLES2_FBO_SURFACE_CACHE_SIZE; a = next)
        {
            next = a->psSurfaceNext;
            if(!FBOSurfaceIsIdle(gc, a)) continue;
            InvalidateCachedFBOSurface(gc, a->psRenderSurface);
            UnlinkFBOSurface(gc, a);
            DestroyFBOAttachableRenderSurface(gc, a);
        }
    }
    PVRSRVUnlockMutex(s->hPrimaryLock);
}

#endif
