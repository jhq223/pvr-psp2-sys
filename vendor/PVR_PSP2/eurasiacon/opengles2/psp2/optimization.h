#ifndef PVR_OPTIMIZATION_H
#define PVR_OPTIMIZATION_H
/* Independent compile-time switches for controlled device comparisons. */
#ifndef PVR_OPT_MASK
#define PVR_OPT_MASK 1023U
#endif
#define PVR_OPT(n) ((PVR_OPT_MASK & (1U << ((n) - 1))) != 0)
#endif
