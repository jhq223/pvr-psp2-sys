#ifndef PVR_OWNED_DEVICE_MEMORY_H
#define PVR_OWNED_DEVICE_MEMORY_H
/* The adapter owns the optional sync and the memory descriptor. Clear the
 * sync only after successful release; a failed later phase preserves the
 * descriptor under the enclosing device's permanent failure policy. */
static PVRSRV_ERROR PvrFreeOwnedDeviceMem(PVRSRV_DEV_DATA *device,
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

#endif
