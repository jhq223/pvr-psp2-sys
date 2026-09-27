#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef int SceUID;
typedef size_t SceSize;
typedef uintptr_t SceUIntPtr;
typedef void *SceClibMspace;
typedef struct { uint64_t opaque[8]; } SceKernelLwMutexWork;
#define SCE_UID_NAMELEN 31
#include "psp2/heaplib_internal.h"
int main(void) {
    SceHeapWorkInternal header;
    uintptr_t start = (uintptr_t)(&header.prim + 1);
    assert(start >= (uintptr_t)&header.memblockType + sizeof(header.memblockType));
    assert(start >= (uintptr_t)&header.spare + sizeof(header.spare));
    size_t header_bytes = (size_t)((char *)(&header.prim + 1) - (char *)&header);
    header.prim.size = 4096 - header_bytes;
    assert(start + header.prim.size == (uintptr_t)&header + 4096);
    assert(_sceHeapIsPointerInBound(&header.prim, (void *)(start + header.prim.size - 1)));
    assert(!_sceHeapIsPointerInBound(&header.prim, (void *)(start + header.prim.size)));
    puts("heap layout: metadata exclusion and primary block bounds passed");
}
