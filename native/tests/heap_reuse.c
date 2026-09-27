#define main allocation_cases
#include "heap_allocation.c"
#undef main
#define SCE_HEAP_ERROR_INVALID_POINTER (-2)
static void sceClibMspaceFree(void *msp,void *ptr) { assert(msp && ptr); }
static int sceClibMspaceIsHeapEmpty(void *msp) { assert(msp); return 1; }
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
    puts("heap reuse: USER/UNC/CDRAM mapped block reuse, size mismatch trimming and 8 MiB bound passed");
}
