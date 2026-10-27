#define TEXTURE_FAST_NO_MAIN
#include "texture_fast.c"
static void SWTextureWait(GLES2Context *gc,GLES2Texture *texture) {}
static int TexMgrGhostTexture(GLES2Context *gc,GLES2Texture *texture) { return 0; }
static void KRM_RemoveResourceFromAllLists(int *krm,int *resource) {}
#define GLES2FREEDEVICEMEM_HEAP(gc,info) ((void)0)
#define GLES2FreeAsync(gc,pointer) GLES2Free(gc,pointer)
/* Exercise the production fallback without rerunning allocator-reclaim mocks;
 * texture_allocation.c independently checks the real allocation transaction. */
static int fail_candidate_allocation(GLES2Context *gc,GLES2Texture *texture) {
    ++allocation_calls; return IMG_FALSE;
}
#define CreateTextureMemory fail_candidate_allocation
#include "texture_bc_reuse_functions.inc"
#undef CreateTextureMemory

static void run_format(unsigned block_bytes, unsigned pixel_format) {
    GLES2TextureManager manager={0}; Shared shared={&manager}; GLES2Context gc={.psSharedState=&shared};
    GLES2TextureFormat format={block_bytes,1,pixel_format}, other={block_bytes,1,pixel_format};
    unsigned length=4*block_bytes;
    unsigned char *memory=malloc(length), pixels[64], previous[64]; assert(memory);
    PVRSRV_CLIENT_MEM_INFO info={.uAllocSize=length,.pvLinAddr=memory};
    GLES2MipMapLevel level={.ui32Width=8,.ui32Height=8,.ui32ImageSize=length,
        .pui8Buffer=GLES2_LOADED_LEVEL,.psTexFormat=&format,.eRequestedFormat=7};
    GLES2Texture texture={.psFormat=&format,.ui32NumLevels=1,.psMipLevel=&level,.psMemInfo=&info,.bResidence=1};
    unsigned allocations=allocation_calls;
    allocation_failure=1;
    for(unsigned pass=0;pass<32;++pass) {
        memset(pixels,(int)pass,length);
        assert(TextureUploadNativeBC(&gc,&texture,7,&format,8,8,pixels));
        assert(texture.psMemInfo==&info && info.pvLinAddr==memory);
        assert(!memcmp(memory,pixels,length));
        assert(allocation_calls==allocations);
    }
    memcpy(previous,memory,length);
    busy=1;
    assert(!TextureUploadNativeBC(&gc,&texture,7,&format,8,8,pixels));
    assert(allocation_calls==++allocations); busy=0;
    info.psClientSyncInfo=&texture; sync_ok=0;
    assert(!TextureUploadNativeBC(&gc,&texture,7,&format,8,8,pixels));
    assert(allocation_calls==allocations); sync_ok=1; info.psClientSyncInfo=NULL;
    assert(!TextureUploadNativeBC(&gc,&texture,7,&format,16,8,pixels));
    assert(allocation_calls==++allocations);
    assert(!TextureUploadNativeBC(&gc,&texture,7,&other,8,8,pixels));
    assert(allocation_calls==++allocations);
    info.uAllocSize=length-1;
    assert(!TextureUploadNativeBC(&gc,&texture,7,&format,8,8,pixels));
    assert(allocation_calls==++allocations); info.uAllocSize=length;
    texture.ui32NumLevels=2;
    assert(!TextureUploadNativeBC(&gc,&texture,7,&format,8,8,pixels));
    assert(allocation_calls==++allocations); texture.ui32NumLevels=1;
    assert(texture.psMemInfo==&info && info.pvLinAddr==memory);
    assert(!memcmp(memory,previous,length));
    free(memory); allocation_failure=0;
}
int main(void) {
    run_format(8,PVRSRV_PIXEL_FORMAT_BC1);
    run_format(16,PVRSRV_PIXEL_FORMAT_BC3);
    puts("native BC reuse: 64 identical-footprint replacements allocate zero device storage; busy/transfer/dimension/format/capacity/mip guards retain old data on allocation failure");
}
