/* Shared atomic publication word; metadata is read only after acquire. */
#ifndef PVR_DEVICE_FAULT_STATE_H
#define PVR_DEVICE_FAULT_STATE_H
static __inline IMG_BOOL KrkrFaultState(const IMG_UINT32 *word)
{
    if(!word) return IMG_FALSE;
#if defined(__SNC__)
    IMG_UINT32 state = __builtin_ldrex((IMG_UINT32 *)word);
    __builtin_clrex();
    if(state) __builtin_dmb();
    return state ? IMG_TRUE : IMG_FALSE;
#else
    return __atomic_load_n(word, __ATOMIC_ACQUIRE) ? IMG_TRUE : IMG_FALSE;
#endif
}
#endif
