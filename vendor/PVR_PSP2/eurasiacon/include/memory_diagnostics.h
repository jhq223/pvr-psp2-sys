/* Native allocator diagnostics. Reserved domains and suballocations stay separate. */
#ifndef PVR_MEMORY_DIAGNOSTICS_H
#define PVR_MEMORY_DIAGNOSTICS_H

typedef struct PvrDriverMemorySnapshot_TAG {
    IMG_UINT32 registeredUNC, parameterCDRAM;
    IMG_UINT32 renderTargetLive, renderTargetPeak, directLive, directPeak;
    IMG_UINT64 surfaceCreates, surfaceRetires;
} PvrDriverMemorySnapshot;

typedef struct PvrMemoryCounters_TAG {
    IMG_UINT32 renderTargetLive, renderTargetPeak, directLive, directPeak;
    IMG_UINT64 surfaceCreates, surfaceRetires;
    IMG_BOOL valid;
} PvrMemoryCounters;

#endif
