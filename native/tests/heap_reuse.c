#define main allocation_cases
#include "heap_allocation.c"
#undef main
#define SCE_HEAP_ERROR_INVALID_POINTER (-2)
static void sceClibMspaceFree(void *msp,void *ptr) { assert(msp && ptr); }
static int sceClibMspaceIsHeapEmpty(void *msp) { assert(msp); return 1; }
struct SceClibMspaceStats { unsigned capacity,current_in_use; };
static void sceClibMspaceMallocStats(void *m,struct SceClibMspaceStats *s) { (void)m;(void)s;assert(0); }
static unsigned sceClibMspaceMallocUsableSize(void *p) { assert(p);return 64; }
static void *sceClibMspaceRealloc(void *m,void *p,unsigned n) { (void)m;(void)p;(void)n;return NULL; }
static void *sceClibMspaceReallocalign(void *m,void *p,unsigned n,unsigned a) { (void)m;(void)p;(void)n;(void)a;return NULL; }
static void sceClibMemcpy(void *to,void *from,unsigned n) { (void)to;(void)from;assert(n==64); }
#include "heap_release_functions.inc"
int main(void) {
    for(unsigned pool=0;pool<3;++pool) {
        reset(SUCCESS,pool);
        SceHeapAllocOptParam opt={sizeof(opt),64};
        void *a=sceHeapAllocHeapMemoryWithOption(&head,4U*1024U*1024U,&opt); assert(a);
        assert(!sceHeapFreeHeapMemory(&head,a));
        assert(head.spare && blocks==1 && mappings==1 && !head.info.ordblks);
        void *b=sceHeapAllocHeapMemoryWithOption(&head,4U*1024U*1024U,&opt);
        assert(a==b && !head.spare && allocs==2 && blocks==1);
        assert(!sceHeapFreeHeapMemory(&head,b));
        unsigned bytes=(unsigned)block_size;
        assert(sceHeapTrimEmpty(&head)==bytes);
        assert(!blocks && !mappings && !mspaces && !head.spare);
        assert(head.info.hblks==1 && head.info.arena==4096);
        a=sceHeapAllocHeapMemoryWithOption(&head,4U*1024U*1024U,&opt); assert(a);
        assert(!sceHeapFreeHeapMemory(&head,a));
        /* An incompatible 4 MiB spare must be unmapped before requesting 9 MiB. */
        a=sceHeapAllocHeapMemoryWithOption(&head,9U*1024U*1024U,&opt); assert(a);
        assert(!head.spare && blocks==1 && mappings==1);
        assert(!sceHeapFreeHeapMemory(&head,a));
        assert(!blocks && !mappings && !mspaces && !head.spare);
        assert(head.info.hblks==1 && head.info.arena==4096 && !head.info.ordblks);
    }
    for(unsigned mode=FAIL_UNMAP; mode<=FAIL_FREE; ++mode) {
        reset(SUCCESS,1);
        SceHeapAllocOptParam opt={sizeof(opt),64};
        void *p=sceHeapAllocHeapMemoryWithOption(&head,4096,&opt);assert(p);
        assert(!sceHeapFreeHeapMemory(&head,p));
        SceHeapMspaceLink *retained=head.spare;
        int arena=head.info.arena,hblks=head.info.hblks;
        failure=mode;
        assert(!sceHeapTrimEmpty(&head) && terminal_word && terminal_code==(mode==FAIL_UNMAP?107:-108));
        assert(head.spare==retained && head.prim.next==retained && blocks==1);
        assert(head.info.arena==arena && head.info.hblks==hblks);
        unsigned unmapped=unmap_calls,freed=free_calls;
        assert(!sceHeapTrimEmpty(&head));
        assert(!sceHeapAllocHeapMemoryWithOption(&head,4096,&opt));
        assert(sceHeapFreeHeapMemory(&head,p)==SCE_HEAP_ERROR_INVALID_ID);
        unsigned before_locks=lock_calls,size=0;SceHeapMspaceLink *owner=NULL;
        assert(!sceHeapReallocHeapMemoryWithOption(&head,p,64,NULL));
        assert(_sceHeapQueryBlockInfo(&head,p,&size,NULL,&owner)==SCE_HEAP_ERROR_INVALID_ID);
        assert(sceHeapGetTotalFreeSize(&head)==SCE_HEAP_ERROR_INVALID_ID);
        assert(lock_calls==before_locks && !head.lwmtx.locked && !owner && !size);

        PvrHeapReleaseFault();
        assert(st_heapFaultOwner==NULL && st_heapFaultWord==&terminal_word);
        assert(unmap_calls==unmapped && free_calls==freed);
        /* Test-only cleanup of retained mocks, after validating no re-entry. */
        free(block);block=NULL;blocks=mappings=mspaces=0;
    }
    /* Fallback allocated a new primary slot, then retiring the old extension
     * failed. The terminal call must never publish that new pointer. */
    reset(SUCCESS,1);head.bsize=16U*1024U*1024U;
    SceHeapAllocOptParam fallback_opt={sizeof(fallback_opt),64};
    void *old=sceHeapAllocHeapMemoryWithOption(&head,1024,&fallback_opt);assert(old);
    recursive_mock=1;use_primary=1;failure=FAIL_UNMAP;
    assert(!sceHeapReallocHeapMemoryWithOption(&head,old,128,NULL));
    assert(terminal_word && terminal_code==107 && !head.lwmtx.locked && blocks==1);
    free(block);block=NULL;blocks=mappings=mspaces=0;
    reset(SUCCESS,1);st_liveHeaps=0;PvrHeapReleaseFault();
    assert(!st_heapFault && !st_heapFaultOwner && !st_heapFaultWord);
    puts("heap reuse: USER/UNC/CDRAM mapped block reuse, size mismatch trimming and 8 MiB bound passed");
}
