#ifndef KRKR_PVR_SDK_EXTENSIONS_H
#define KRKR_PVR_SDK_EXTENSIONS_H

/* Firmware entry points used by PVR_PSP2's VDSuite project but omitted from
 * the retail SDK headers. These are declarations, not replacement functions.
 * Layout/signatures checked against the local vita-headers firmware database;
 * all code is compiled and linked with the official PSVSDK tools. */
#include <kernel.h>
#include <stddef.h>
#include <stdarg.h>
#define SCE_SYSMODULE_INTERNAL_ULT 0x80000025
typedef void *SceClibMspace;
struct SceClibMspaceStats {
    SceSize capacity;
    SceSize reserved;
    SceSize peak_in_use;
    SceSize current_in_use;
};
typedef char KrkrMspaceStatsSize[(sizeof(struct SceClibMspaceStats) == 16) ? 1 : -1];
SceClibMspace sceClibMspaceCreate(void *, SceSize);
void sceClibMspaceDestroy(SceClibMspace);
void *sceClibMspaceMalloc(SceClibMspace, SceSize);
void *sceClibMspaceMemalign(SceClibMspace, SceSize, SceSize);
void *sceClibMspaceRealloc(SceClibMspace, void *, SceSize);
void *sceClibMspaceReallocalign(SceClibMspace, void *, SceSize, SceSize);
void sceClibMspaceFree(SceClibMspace, void *);
SceBool sceClibMspaceIsHeapEmpty(SceClibMspace);
SceSize sceClibMspaceMallocUsableSize(void *);
void sceClibMspaceMallocStats(SceClibMspace, struct SceClibMspaceStats *);
void *sceKernelGetTLSAddr(int);
int sceSysmoduleLoadModuleInternal(unsigned int);
int sceDisplayGetMaximumFrameBufResolution(unsigned int *, unsigned int *);
typedef struct SceKernelSegmentInfo {
    SceSize size;
    SceUInt perms;
    void *vaddr;
    SceSize memsz;
    SceSize filesz;
    SceUInt reserved;
} SceKernelSegmentInfo;
typedef struct SceKernelModuleInfo {
    SceSize size;
    SceUID modid;
    SceUInt16 modattr;
    SceUInt8 modver[2];
    char module_name[28];
    SceUInt reserved;
    void *start_entry, *stop_entry, *exit_entry;
    void *exidx_top, *exidx_btm, *extab_top, *extab_btm;
    void *tlsInit;
    SceSize tlsInitSize, tlsAreaSize;
    char path[256];
    SceKernelSegmentInfo segments[4];
    SceUInt state;
} SceKernelModuleInfo;
typedef char KrkrModuleInfoSize[(sizeof(SceKernelModuleInfo) == 0x1B8) ? 1 : -1];
typedef char KrkrModuleSegmentsOffset[(offsetof(SceKernelModuleInfo, segments) == 0x154) ? 1 : -1];
int sceKernelGetModuleInfo(SceUID, SceKernelModuleInfo *);
int sceClibPrintf(const char *, ...);
int sceClibSnprintf(char *, SceSize, const char *, ...);
int sceClibVsnprintf(char *, SceSize, const char *, va_list);
char *sceClibStrrchr(const char *, int);
char *sceClibStrncpy(char *, const char *, SceSize);
int sceClibStrcmp(const char *, const char *);
int sceClibStrncmp(const char *, const char *, SceSize);
int sceClibStrncasecmp(const char *, const char *, SceSize);
SceSize sceClibStrnlen(const char *, SceSize);
#endif
