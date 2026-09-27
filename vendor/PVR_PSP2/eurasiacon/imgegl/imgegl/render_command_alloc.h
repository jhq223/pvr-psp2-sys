#include "render_failure.h"

/* These two circular command buffers are consumed only by their render
 * surface. GPU 3D status writes advance psStatusUpdateMemInfo/read offsets;
 * their separate psClientSyncInfo is never submitted or waited on. Keep
 * the surface sync and all status-update allocations unchanged. */
static PVRSRV_ERROR KrkrAllocRenderCommandBuffer(SrvSysContext *psSysContext,
    PVRSRV_DEV_DATA *device, IMG_HANDLE heap, IMG_UINT32 bytes, IMG_UINT32 alignment,
    PVRSRV_CLIENT_MEM_INFO **out, const char *stage)
{
    PVRSRV_ERROR error = IMGEGLALLOCDEVICEMEM(device, heap,
        PVRSRV_MEM_READ | PVRSRV_MEM_NO_SYNCOBJ, bytes, alignment, out);
    if(error != PVRSRV_OK) KrkrRenderFailure(stage, (unsigned)error, bytes);
    return error;
}
