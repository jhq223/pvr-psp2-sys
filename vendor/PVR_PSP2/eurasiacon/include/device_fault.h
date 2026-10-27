/* Permanent device failure. Metadata is published once and never reset. */
#ifndef PVR_DEVICE_FAULT_H
#define PVR_DEVICE_FAULT_H

typedef struct PvrDeviceFaultSnapshot_TAG {
    const IMG_CHAR *operation;
    IMG_INT32 code;
} PvrDeviceFaultSnapshot;

#include "device_fault_state.h"
static __inline IMG_BOOL KrkrDeviceFaulted(const SrvSysContext *context)
{
    return KrkrFaultState(&context->terminalState);
}

static __inline IMG_BOOL KrkrDeviceFault(SrvSysContext *context,
    const IMG_CHAR *operation, IMG_INT32 code)
{
    PVRSRVLockMutex(context->hMemoryStatsLock);
    if(!KrkrDeviceFaulted(context))
    {
        context->terminalOperation = operation;
        context->terminalCode = code;
#if defined(__SNC__)
        __builtin_dmb();
        do { __builtin_ldrex((IMG_UINT32 *)&context->terminalState); }
        while(__builtin_strex(1, (IMG_UINT32 *)&context->terminalState));
#else
        __atomic_store_n(&context->terminalState, 1, __ATOMIC_RELEASE);
#endif
    }
    PVRSRVUnlockMutex(context->hMemoryStatsLock);
    return IMG_FALSE;
}

static __inline IMG_INT32 KrkrQueryDeviceFault(SrvSysContext *context,
    PvrDeviceFaultSnapshot *out)
{
    if(!KrkrDeviceFaulted(context)) return 0;
    PVRSRVLockMutex(context->hMemoryStatsLock);
    out->operation = context->terminalOperation;
    out->code = context->terminalCode;
    PVRSRVUnlockMutex(context->hMemoryStatsLock);
    return 1;
}
#endif
