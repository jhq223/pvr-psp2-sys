#include "render_failure.h"
/* The caller owns the returned target. Until SGXAddRenderTarget succeeds,
 * this helper owns the UID and registration and unwinds them on failure. */
static IMG_BOOL KrkrAddRenderTarget(SrvSysContext *context,
                                  SGX_ADDRENDTARG *info,
                                  IMG_HANDLE *target,
                                  const char *name)
{
    IMG_UINT32 bytes = 0;
    IMG_SID reference = 0;
    IMG_HANDLE result = IMG_NULL;
    SceUID uid;
    PVRSRV_ERROR error;

    if(KrkrDeviceFaulted(context)) return IMG_FALSE;
    *target = IMG_NULL;
    info->i32DataMemblockUID = -1;
    info->hMemBlockProcRef = 0;
    error = SGXGetRenderTargetMemSize(info, &bytes);
    if (error != PVRSRV_OK || bytes == 0)
    {
        KrkrRenderFailure("rt-size", (unsigned)error, bytes);
        return IMG_FALSE;
    }

    uid = KrkrAllocRenderTargetBlock(context, name, bytes);
    if (uid < 0)
    {
        KrkrRenderFailure("rt-block", (unsigned)uid, bytes);
        return IMG_FALSE;
    }

    error = PVRSRVRegisterMemBlock(&context->s3D, uid, &reference, IMG_TRUE);
    if (error != PVRSRV_OK)
    {
        KrkrRenderFailure("rt-register", (unsigned)error, bytes);
        { IMG_INT32 retired = KrkrFreeRenderTargetBlock(context, uid);
          if(retired < 0) return KrkrDeviceFault(context, "rt-rollback-free", retired); }
        return IMG_FALSE;
    }

    info->i32DataMemblockUID = uid;
    info->hMemBlockProcRef = reference;
    error = SGXAddRenderTarget(&context->s3D, info, &result);
    if (error != PVRSRV_OK)
    {
        KrkrRenderFailure("rt-add", (unsigned)error, bytes);
        sceClibPrintf("[PVR][RTALLOC] stage=add error=0x%X bytes=%u size=%ux%u rtdata=%u queued=%u\n",
            (unsigned)error, bytes, info->ui32NumPixelsX, info->ui32NumPixelsY,
            info->ui32NumRTData, info->ui32MaxQueuedRenders);
        error = PVRSRVUnregisterMemBlock(&context->s3D, uid);
        if(error != PVRSRV_OK) return KrkrDeviceFault(context, "rt-rollback-unregister", error);
        { IMG_INT32 retired = KrkrFreeRenderTargetBlock(context, uid);
          if(retired < 0) return KrkrDeviceFault(context, "rt-rollback-free", retired); }
        info->i32DataMemblockUID = -1;
        info->hMemBlockProcRef = 0;
        return IMG_FALSE;
    }

    *target = result;
    return IMG_TRUE;
}
