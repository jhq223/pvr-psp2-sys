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
static int fail_allocation;
#define GLES2Malloc(gc, size) (fail_allocation ? NULL : malloc(size))
#define GLES2Free(gc, ptr) free(ptr)
typedef struct { unsigned ui32FrameNum; } GLES2Context;
#include "statehash.h"
#include "statehash_functions.inc"
static unsigned destroyed[32], pending[32];
static IMG_BOOL can_destroy(GLES2Context *gc, unsigned value) {
    (void)gc; return !pending[value];
}
static void destroy(GLES2Context *gc, unsigned value) { assert(value < 32); ++destroyed[value]; }
static void insert(GLES2Context *gc, HashTable *table, unsigned hash, unsigned words, unsigned value) {
    unsigned *key = calloc(words, sizeof(*key)); assert(key); key[0] = value;
    assert(HashTableInsert(gc, table, hash, key, words, value, IMG_NULL));
}
static IMG_BOOL insert_pending(GLES2Context *gc, HashTable *table, unsigned value) {
    unsigned *key = malloc(sizeof(*key)); assert(key); *key = value;
    IMG_BOOL inserted = HashTableInsert(gc, table, value, key, 1, value, can_destroy);
    if(!inserted) free(key);
    return inserted;
}
static void pending_cache_eviction(void) {
    GLES2Context gc = {0}; HashTable table = {0}; unsigned item, key;
    assert(HashTableCreate(&gc, &table, 2, 2, destroy));
    assert(insert_pending(&gc, &table, 6)); assert(insert_pending(&gc, &table, 7));
    pending[6] = 1;
    assert(insert_pending(&gc, &table, 8));
    assert(!destroyed[6] && destroyed[7] == 1);
    pending[8] = 1;
    assert(!HashTableCanInsert(&gc, &table, can_destroy));
    assert(!insert_pending(&gc, &table, 9));
    assert(table.ui32NumEntries == 2 && !destroyed[6] && !destroyed[8]);
    key = 6;
    assert(HashTableSearch(&gc, &table, 6, &key, 1, &item) && item == 6);
    pending[8] = 0;
    assert(HashTableCanInsert(&gc, &table, can_destroy));
    assert(insert_pending(&gc, &table, 9));
    assert(destroyed[8] == 1 && !destroyed[6]);
    pending[6] = 0;
    HashTableDestroy(&gc, &table);
    for(unsigned i = 6; i <= 9; ++i) assert(destroyed[i] == 1);
    puts("state cache: pending GPU entries retained, completion allows eviction");
}
int main(void) {
    GLES2Context gc = {0}; HashTable table = {0}; unsigned key, item;
    assert(HashTableCreate(&gc, &table, 2, 3, destroy));
    insert(&gc, &table, 1, 1, 1); insert(&gc, &table, 2, 1, 2); insert(&gc, &table, 3, 1, 3);
    key = 20; fail_allocation = 1;
    assert(!HashTableInsert(&gc, &table, 0, &key, 1, 20, IMG_NULL));
    assert(!destroyed[1] && !destroyed[2] && !destroyed[3]);
    fail_allocation = 0;
    key = 1; assert(HashTableSearch(&gc, &table, 1, &key, 1, &item) && item == 1);
    insert(&gc, &table, 0, 1, 4); assert(destroyed[2] == 1 && !destroyed[1]);
    /* Same hash, different lengths must advance through the chain. */
    insert(&gc, &table, 1, 2, 5); key = 1;
    assert(HashTableDelete(&gc, &table, 1, &key, 1, &item) && item == 1);
    key = 29; assert(!HashTableDelete(&gc, &table, 1, &key, 1, &item));
    assert(table.psLRUHead->ui32Item == 5 && table.psLRUTail->ui32Item == 4);
    HashTableDestroy(&gc, &table);
    for(unsigned i = 1; i <= 5; ++i) assert(destroyed[i] == 1);
    pending_cache_eviction();
    puts("state cache: cross-bucket LRU and collision deletion passed");
}
