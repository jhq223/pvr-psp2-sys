/* One Services owner is bound while native texture heaps exist. */
#ifndef PVR_HEAP_RETIREMENT_H
#define PVR_HEAP_RETIREMENT_H
#include "../../include/device_fault_state.h"
static void (*st_heapFault)(void *, const char *, int);
static void *st_heapFaultOwner;
static const IMG_UINT32 *st_heapFaultWord;
static unsigned int st_liveHeaps;
static __inline int PvrHeapFaulted(void) { return KrkrFaultState(st_heapFaultWord); }
static __inline int PvrHeapFault(const char *operation, int error) {
    if(st_heapFault) st_heapFault(st_heapFaultOwner, operation, error);
    return error;
}
static __inline void PvrHeapBindFault(void (*fault)(void *,const char *,int), void *owner,const IMG_UINT32 *word) {
    st_heapFault = fault; st_heapFaultOwner = owner; st_heapFaultWord = word;
}
static __inline void PvrHeapReleaseFault(void) {
    if(!st_liveHeaps && !PvrHeapFaulted()) PvrHeapBindFault(0, 0, 0);
}
#endif
