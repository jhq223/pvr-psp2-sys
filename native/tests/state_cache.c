#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint32_t IMG_UINT32;
typedef int IMG_BOOL;
#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define PVR_DPF(x) ((void)0)
#define PVR_UNREFERENCED_PARAMETER(x) ((void)(x))
#define GLES2Calloc(gc, size) calloc(1, size)
#define GLES2Malloc(gc, size) malloc(size)
#define GLES2Free(gc, ptr) free(ptr)
typedef struct { unsigned ui32FrameNum; } GLES2Context;
#include "statehash.h"
#include "statehash_functions.inc"
static unsigned destroyed[32];
static void destroy(GLES2Context *gc, unsigned value) { assert(value < 32); ++destroyed[value]; }
static void insert(GLES2Context *gc, HashTable *table, unsigned hash, unsigned words, unsigned value) {
    unsigned *key = calloc(words, sizeof(*key)); assert(key); key[0] = value;
    HashTableInsert(gc, table, hash, key, words, value);
}
int main(void) {
    GLES2Context gc = {0}; HashTable table = {0}; unsigned key, item;
    assert(HashTableCreate(&gc, &table, 2, 3, destroy));
    insert(&gc, &table, 1, 1, 1); insert(&gc, &table, 2, 1, 2); insert(&gc, &table, 3, 1, 3);
    key = 1; assert(HashTableSearch(&gc, &table, 1, &key, 1, &item) && item == 1);
    insert(&gc, &table, 0, 1, 4); assert(destroyed[2] == 1 && !destroyed[1]);
    /* Same hash, different lengths must advance through the chain. */
    insert(&gc, &table, 1, 2, 5); key = 1;
    assert(HashTableDelete(&gc, &table, 1, &key, 1, &item) && item == 1);
    key = 29; assert(!HashTableDelete(&gc, &table, 1, &key, 1, &item));
    assert(table.psLRUHead->ui32Item == 5 && table.psLRUTail->ui32Item == 4);
    HashTableDestroy(&gc, &table);
    for(unsigned i = 1; i <= 5; ++i) assert(destroyed[i] == 1);
    puts("state cache: cross-bucket LRU and collision deletion passed");
}
