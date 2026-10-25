/* PSP2 allocations use NO_SYNCOBJ; the EGL adapter owns the optional sync.
 * Keep the descriptor alive until both release phases succeed. On a failed
 * memory release, NULL records the already released sync for the next retry. */
static PVRSRV_ERROR KEGLFreeOwnedDeviceMem(PVRSRV_DEV_DATA *device,
    PVRSRV_CLIENT_MEM_INFO *memory)
{
    PVRSRV_ERROR error;
    if(memory->psClientSyncInfo)
    {
        error = PVRSRVFreeSyncInfo(device, memory->psClientSyncInfo);
        if(error != PVRSRV_OK) return error;
        memory->psClientSyncInfo = IMG_NULL;
    }
    return PVRSRVFreeDeviceMem(device, memory);
}
