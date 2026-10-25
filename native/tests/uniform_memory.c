/* Exercise the production constant uploader with SGX543 four-word texture state. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t IMG_UINT32;
typedef uint16_t IMG_UINT16;
typedef float IMG_FLOAT;
typedef int GLES2_MEMERROR;
#define IMG_INTERNAL
#define IMG_FALSE 0
#define GLES2_NO_ERROR 0
#define GLES2_TA_BUFFER_ERROR 2
#define GLES2_3D_BUFFER_ERROR 3
#define GLES2_SHADERTYPE_VERTEX 0
#define GLES2_SHADERTYPE_FRAGMENT 1
#define CBUF_TYPE_PDS_VERT_BUFFER 0
#define CBUF_TYPE_PDS_FRAG_BUFFER 1
#define EURASIA_TAG_TEXTURE_STATE_SIZE 4
#define PDS_NUM_TEXTURE_IMAGE_CHUNKS 4
#define USP_HW_CONST_FMT_F32 0
#define USP_HW_CONST_FMT_F16 1
#define USP_HW_CONST_FMT_C10 2
#define CLEARMASK(shift, width) (~(((1U << (width)) - 1U) << (shift)))
#define GLES2_INC_COUNT(timer, count) ((void)0)

typedef struct { unsigned uiAddr; } IMG_DEV_VIRTADDR;
typedef struct {
    unsigned uSrcIdx, uSrcShift;
    uint16_t uDestIdx, uDestShift;
    unsigned eFormat;
} USP_HW_CONST_LOAD;
typedef struct {
    uint16_t uTexCtrWrdIdx;
    unsigned uDestIdx;
    uint16_t uTagSize;
} USP_HW_TEXSTATE_LOAD;
typedef struct {
    uint16_t uTextureIdx, uChunkIdx;
    unsigned auWord[4], auMask[4];
} USP_TEX_CTR_WORDS;
typedef struct {
    unsigned uMemConstCount, uMemTexStateCount;
    USP_HW_CONST_LOAD *psMemConstLoads;
    USP_HW_TEXSTATE_LOAD *psMemTexStateLoads;
    USP_TEX_CTR_WORDS *psTextCtrWords;
} USP_HW_SHADER;
typedef struct {
    float *pfConstantData;
    IMG_DEV_VIRTADDR uUSEConstsDataBaseAddress;
    unsigned ui32USEConstsDataSizeinDWords;
} GLES2ProgramShader;
typedef struct { GLES2ProgramShader sVertex, sFragment; } GLES2Program;
typedef struct { USP_HW_SHADER *psPatchedShader; } Variant;
typedef struct { unsigned aui32TAGControlWord[16][4]; } TextureState;
typedef struct {
    struct { GLES2Program *psCurrentProgram; Variant *psCurrentVertexVariant, *psCurrentFragmentVariant; } sProgram;
    struct { TextureState sVertexTextureState, sFragmentTextureState; } sPrim;
    void *apsBuffers;
} GLES2Context;

enum { GUARD = 4, CAPACITY = 128 };
#define CANARY UINT32_C(0xa55ac33c)
static unsigned allocation[CAPACITY], reserved, committed, buffer_type, allocations, updates;
static int allocation_fails;
static IMG_UINT32 *CBUF_GetBufferSpace(void *buffers, unsigned words, unsigned type, int force) {
    assert(words + 2 * GUARD < CAPACITY);
    ++allocations; reserved = words; buffer_type = type;
    return allocation_fails ? NULL : allocation + GUARD;
}
static void CBUF_UpdateBufferPos(void *buffers, unsigned words, unsigned type) {
    assert(type == buffer_type); ++updates; committed = words;
}
static IMG_DEV_VIRTADDR CBUF_GetBufferDeviceAddress(void *buffers, void *pointer, unsigned type) {
    assert(type == buffer_type && pointer == allocation + GUARD);
    return (IMG_DEV_VIRTADDR){0x10000000U + type * 0x1000U};
}
/* Conversion arithmetic is covered separately; retain distinct deterministic
 * packed lanes here to check the production destination masks and offsets. */
static uint16_t GLES2ConvertFloatToF16(float value) { return (uint16_t)(value * 128.0f); }
static uint16_t GLES2ConvertFloatToC10(float value) { return (uint16_t)(value * 16.0f); }

#include "uniform_memory_functions.inc"

static uint32_t bits(float value) { uint32_t out; memcpy(&out, &value, sizeof(out)); return out; }
static void run_case(unsigned stage, int packed, unsigned textures, unsigned tag_size, int check_bounds, int check_stage) {
    float vertex_constants[16], fragment_constants[16];
    for(unsigned i=0;i<16;i++) { vertex_constants[i]=(float)(i+1); fragment_constants[i]=(float)(i+17); }
    GLES2Program program={.sVertex={.pfConstantData=vertex_constants},.sFragment={.pfConstantData=fragment_constants}};
    USP_HW_CONST_LOAD constants[14];
    unsigned constant_count=packed ? 4 : 14;
    unsigned texture_start=packed ? 3 : 14;
    for(unsigned i=0;i<constant_count;i++) constants[i]=(USP_HW_CONST_LOAD){.uSrcIdx=i,.uDestIdx=(uint16_t)i,.eFormat=USP_HW_CONST_FMT_F32};
    if(packed) {
        constants[1]=(USP_HW_CONST_LOAD){.uSrcIdx=1,.uDestIdx=1,.uDestShift=0,.eFormat=USP_HW_CONST_FMT_F16};
        constants[2]=(USP_HW_CONST_LOAD){.uSrcIdx=2,.uDestIdx=1,.uDestShift=16,.eFormat=USP_HW_CONST_FMT_F16};
        constants[3]=(USP_HW_CONST_LOAD){.uSrcIdx=3,.uDestIdx=2,.eFormat=USP_HW_CONST_FMT_C10};
    }
    USP_HW_TEXSTATE_LOAD maps[4]; USP_TEX_CTR_WORDS words[4];
    for(unsigned i=0;i<textures;i++) {
        maps[i]=(USP_HW_TEXSTATE_LOAD){.uTexCtrWrdIdx=(uint16_t)i,.uDestIdx=texture_start+i*4,.uTagSize=(uint16_t)tag_size};
        words[i]=(USP_TEX_CTR_WORDS){.uTextureIdx=(uint16_t)(3-i),.uChunkIdx=(uint16_t)(i%2)};
        for(unsigned j=0;j<4;j++) { words[i].auWord[j]=0x00005500U; words[i].auMask[j]=0xffff00ffU; }
    }
    USP_HW_SHADER shader={constant_count,textures,constants,maps,words};
    Variant variant={&shader};
    GLES2Context gc={.sProgram={&program,&variant,&variant}};
    for(unsigned chunk=0;chunk<16;chunk++) for(unsigned word=0;word<4;word++) {
        gc.sPrim.sVertexTextureState.aui32TAGControlWord[chunk][word]=0x11000000U | chunk*256U | word;
        gc.sPrim.sFragmentTextureState.aui32TAGControlWord[chunk][word]=0x22000000U | chunk*256U | word;
    }
    for(unsigned i=0;i<CAPACITY;i++) allocation[i]=CANARY;
    reserved=committed=allocations=updates=0;
    assert(WriteUSEShaderMemConsts(&gc,stage)==(allocation_fails ? (stage ? GLES2_3D_BUFFER_ERROR : GLES2_TA_BUFFER_ERROR) : GLES2_NO_ERROR));
    assert(allocations==1);
    GLES2ProgramShader *selected=stage ? &program.sFragment : &program.sVertex;
    GLES2ProgramShader *other=stage ? &program.sVertex : &program.sFragment;
    if(allocation_fails) {
        assert(updates==0 && selected->uUSEConstsDataBaseAddress.uiAddr==0);
        for(unsigned i=0;i<CAPACITY;i++) assert(allocation[i]==CANARY);
        return;
    }
    if(check_bounds) {
        for(unsigned i=0;i<GUARD;i++) assert(allocation[i]==CANARY);
        for(unsigned i=GUARD+reserved;i<CAPACITY;i++) {
            if(allocation[i]!=CANARY) fprintf(stderr,"texture-state canary overwritten: stage=%u reserved=%u written_index=%u\n",stage,reserved,i-GUARD);
            assert(allocation[i]==CANARY);
        }
        assert(reserved==constant_count+textures*EURASIA_TAG_TEXTURE_STATE_SIZE);
    }
    assert(updates==1 && committed==reserved);
    assert(selected->ui32USEConstsDataSizeinDWords==reserved);
    assert(selected->uUSEConstsDataBaseAddress.uiAddr==0x10000000U+stage*0x1000U);
    assert(other->uUSEConstsDataBaseAddress.uiAddr==0 && other->ui32USEConstsDataSizeinDWords==0);
    float *input=stage ? fragment_constants : vertex_constants;
    assert(allocation[GUARD]==bits(input[0]));
    if(packed) {
        assert(allocation[GUARD+1]==((uint32_t)GLES2ConvertFloatToF16(input[1]) | ((uint32_t)GLES2ConvertFloatToF16(input[2])<<16)));
        assert(allocation[GUARD+2]==((CANARY&~1023U)|(GLES2ConvertFloatToC10(input[3])&1023U)));
    } else {
        for(unsigned i=0;i<constant_count;i++) assert(allocation[GUARD+i]==bits(input[i]));
    }
    if(check_stage) {
        TextureState *state=stage ? &gc.sPrim.sFragmentTextureState : &gc.sPrim.sVertexTextureState;
        for(unsigned i=0;i<textures;i++) for(unsigned j=0;j<4;j++) {
            unsigned chunk=words[i].uChunkIdx+words[i].uTextureIdx*PDS_NUM_TEXTURE_IMAGE_CHUNKS;
            unsigned expected=(state->aui32TAGControlWord[chunk][j]&words[i].auMask[j])|words[i].auWord[j];
            if(allocation[GUARD+maps[i].uDestIdx+j]!=expected) fprintf(stderr,"wrong texture stage: stage=%u texture=%u word=%u\n",stage,i,j);
            assert(allocation[GUARD+maps[i].uDestIdx+j]==expected);
        }
    }
}
int main(int argc, char **argv) {
    if(argc==2 && !strcmp(argv[1],"bounds")) { run_case(1,0,4,4,1,0); return 0; }
    if(argc==2 && !strcmp(argv[1],"stage")) { run_case(1,1,1,1,0,1); return 0; }
    for(unsigned stage=0;stage<2;stage++) for(int packed=0;packed<2;packed++)
        for(unsigned textures=0;textures<=4;textures++) for(unsigned tag=1;tag<=4;tag*=2)
            run_case(stage,packed,textures,tag,1,1);
    allocation_fails=1;
    for(unsigned stage=0;stage<2;stage++) run_case(stage,0,4,4,1,1);
    puts("uniform memory: 60 layouts, vertex/fragment texture states, SGX four-word canaries, packed F16/C10 constants and allocation failures passed");
}
