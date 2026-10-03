#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned IMG_UINT32;
typedef int IMG_BOOL;
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_VOID void
#define USC_UNDEF (~0u)
#define ASSERT assert
#define CBTYPE_CONTINUE 1

typedef struct Block CODEBLOCK, *PCODEBLOCK;
typedef struct { PCODEBLOCK psDest; IMG_BOOL sync_end; } EDGE;
typedef struct { PCODEBLOCK psEntry, psExit; } CFG;
typedef struct { CFG sCfg; } FUNC;
typedef struct { FUNC *psTarget; } CALL;
typedef struct { union { CALL *psCall; } u; } INST;
struct Block {
    unsigned uIdx, eType, uNumSuccs, uNumPreds, uNumDomChildren;
    IMG_BOOL bDomSync, bDomSyncEnd, bAddSyncAtStart, loop_header;
    PCODEBLOCK psIDom, psIPostDom, psExtPostDom, psLoopHeader;
    PCODEBLOCK apsDomChildren[4];
    EDGE asSuccs[2], asPreds[2];
    CFG *psOwner;
    INST *psBody;
};
typedef struct { unsigned allocations; } INTERMEDIATE_STATE, *PINTERMEDIATE_STATE;

static void *UscAlloc(PINTERMEDIATE_STATE state, size_t size) {
    void *memory = calloc(1, size);
    assert(memory);
    ++state->allocations;
    return memory;
}
static void UscFree(PINTERMEDIATE_STATE state, void *memory) {
    assert(state->allocations);
    --state->allocations;
    free(memory);
}
static IMG_BOOL Dominates(PINTERMEDIATE_STATE state, PCODEBLOCK parent, PCODEBLOCK block) {
    (void)state;
    for (; block; block = block->psIDom) if (block == parent) return IMG_TRUE;
    return IMG_FALSE;
}
static IMG_BOOL PostDominated(PINTERMEDIATE_STATE state, PCODEBLOCK block, PCODEBLOCK parent) {
    (void)state;
    for (; block; block = block->psIPostDom) if (block == parent) return IMG_TRUE;
    return IMG_FALSE;
}
static IMG_BOOL LoopContains(PCODEBLOCK header, PCODEBLOCK block) {
    for (; block; block = block->psLoopHeader) if (block == header) return IMG_TRUE;
    return IMG_FALSE;
}
static IMG_BOOL IsLoopHeader(PCODEBLOCK block) { return block->loop_header; }
static IMG_BOOL IsCall(PINTERMEDIATE_STATE state, PCODEBLOCK block) {
    (void)state;
    (void)block;
    return IMG_FALSE;
}
static void SetSyncEndOnSuccessor(PINTERMEDIATE_STATE state, PCODEBLOCK block, unsigned edge) {
    (void)state;
    assert(edge < block->uNumSuccs);
    block->asSuccs[edge].sync_end = IMG_TRUE;
}

#include "sync-flow-functions.inc"

enum { OUTER, HEADER, REGION, RETURN, GRADIENT, INNER_JOIN, OUTER_JOIN, EXIT, BLOCK_COUNT };
typedef struct { CODEBLOCK block[BLOCK_COUNT]; CFG owner; INTERMEDIATE_STATE state; } FIXTURE;

static void connect(PCODEBLOCK source, PCODEBLOCK target) {
    assert(source->uNumSuccs < 2 && target->uNumPreds < 2);
    source->asSuccs[source->uNumSuccs++].psDest = target;
    target->asPreds[target->uNumPreds++].psDest = source;
}
static void dominate(PCODEBLOCK parent, PCODEBLOCK child) {
    assert(parent->uNumDomChildren < 4);
    parent->apsDomChildren[parent->uNumDomChildren++] = child;
    child->psIDom = parent;
}

/* A divergent return bypasses a region with another branch around a gradient. */
static void setup(FIXTURE *fixture, IMG_BOOL nested, IMG_BOOL sibling_join, IMG_BOOL gradient) {
    memset(fixture, 0, sizeof(*fixture));
    PCODEBLOCK b = fixture->block;
    fixture->owner.psEntry = b + OUTER;
    fixture->owner.psExit = b + EXIT;
    for (unsigned i = 0; i < BLOCK_COUNT; ++i) {
        b[i].uIdx = i;
        b[i].psOwner = &fixture->owner;
        if (i != EXIT) b[i].psIPostDom = b + EXIT;
    }
    dominate(b + OUTER, b + HEADER);
    dominate(b + OUTER, b + EXIT);
    dominate(b + HEADER, b + REGION);
    dominate(b + HEADER, b + RETURN);
    connect(b + HEADER, b + REGION);
    connect(b + HEADER, b + RETURN);
    b[HEADER].bDomSync = b[REGION].bDomSync = gradient;
    PCODEBLOCK confluence = b + (sibling_join ? OUTER_JOIN : EXIT);
    if (sibling_join) {
        dominate(b + HEADER, confluence);
        connect(confluence, b + EXIT);
        b[HEADER].psIPostDom = confluence;
    }
    b[REGION].psIPostDom = b[RETURN].psIPostDom = confluence;
    connect(b + RETURN, confluence);
    if (nested) {
        dominate(b + REGION, b + GRADIENT);
        dominate(b + REGION, b + INNER_JOIN);
        b[GRADIENT].bDomSync = gradient;
        b[GRADIENT].psIPostDom = b + INNER_JOIN;
        b[INNER_JOIN].psIPostDom = confluence;
        b[REGION].psIPostDom = b + INNER_JOIN;
        connect(b + REGION, b + GRADIENT);
        connect(b + REGION, b + INNER_JOIN);
        connect(b + GRADIENT, b + INNER_JOIN);
        connect(b + INNER_JOIN, confluence);
    } else {
        connect(b + REGION, confluence);
    }
}

static void run(FIXTURE *fixture) {
    PEDGE_LIST edges = SetSyncStartEnd(&fixture->state, fixture->block + HEADER);
    while (edges) {
        PEDGE_LIST next = edges->psNext;
        UscFree(&fixture->state, edges);
        edges = next;
    }
    assert(!fixture->state.allocations);
}

int main(void) {
    FIXTURE fixture;
    setup(&fixture, IMG_TRUE, IMG_FALSE, IMG_TRUE);
    run(&fixture);
    assert(fixture.block[RETURN].asSuccs[0].sync_end);
    assert(fixture.block[REGION].bAddSyncAtStart);
    assert(!fixture.block[GRADIENT].bAddSyncAtStart);

    setup(&fixture, IMG_TRUE, IMG_TRUE, IMG_TRUE);
    run(&fixture);
    assert(fixture.block[RETURN].asSuccs[0].sync_end);
    assert(fixture.block[REGION].bAddSyncAtStart);

    setup(&fixture, IMG_FALSE, IMG_FALSE, IMG_TRUE);
    run(&fixture);
    assert(fixture.block[RETURN].asSuccs[0].sync_end);
    assert(!fixture.block[REGION].bAddSyncAtStart);

    setup(&fixture, IMG_TRUE, IMG_FALSE, IMG_FALSE);
    run(&fixture);
    assert(!fixture.block[RETURN].asSuccs[0].sync_end);
    assert(!fixture.block[REGION].bAddSyncAtStart);

    puts("sync flow: external return, sibling join, unnested sample and gradient-free branches passed");
    return 0;
}
