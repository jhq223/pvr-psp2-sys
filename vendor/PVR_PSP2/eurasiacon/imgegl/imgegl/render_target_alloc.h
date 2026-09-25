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

    *target = IMG_NULL;
    info->i32DataMemblockUID = -1;
    info->hMemBlockProcRef = 0;
    if (SGXGetRenderTargetMemSize(info, &bytes) != PVRSRV_OK || bytes == 0)
        return IMG_FALSE;

    uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_NC_RW,
                                bytes, SCE_NULL);
    if (uid < 0)
        return IMG_FALSE;

    if (PVRSRVRegisterMemBlock(&context->s3D, uid, &reference, IMG_TRUE) != PVRSRV_OK)
    {
        sceKernelFreeMemBlock(uid);
        return IMG_FALSE;
    }

    info->i32DataMemblockUID = uid;
    info->hMemBlockProcRef = reference;
    if (SGXAddRenderTarget(&context->s3D, info, &result) != PVRSRV_OK)
    {
        PVRSRVUnregisterMemBlock(&context->s3D, uid);
        sceKernelFreeMemBlock(uid);
        info->i32DataMemblockUID = -1;
        info->hMemBlockProcRef = 0;
        return IMG_FALSE;
    }

    *target = result;
    return IMG_TRUE;
}
