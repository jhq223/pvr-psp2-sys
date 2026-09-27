#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define IMG_VOID void
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define PVR_DPF(x) ((void)0)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define GLES2Calloc(gc,n) calloc(1,n)
#define GLES2Realloc(gc,p,n) realloc(p,n)
#define GLES2Free(gc,p) free(p)
#define SGXBS_NO_ERROR 0
#define SGXBS_CORRUPT_BINARY_ERROR 1
#define SGXBS_OUT_OF_MEMORY_ERROR 2
#define GLSLTS_NUM_TYPES 40
#define GLSLTS_STRUCT 39
#define GLSLBV_NUM_BUILTINS_WITH_SPECIALS 100
#define GLSLTQ_NUM 12
#define GLSLPRECQ_HIGH 3
#define GLSLVMOD_ALL 31
#define HWREG_FLOAT 1
#define HWREG_TEX 2
#define GLES2_MAX_TEXTURE_UNITS 16
typedef uint8_t IMG_UINT8;
typedef uint16_t IMG_UINT16;
typedef uint32_t IMG_UINT32;
typedef int32_t IMG_INT32;
typedef char IMG_CHAR;
typedef float IMG_FLOAT;
typedef int IMG_BOOL, SGXBS_Error, GLSLBuiltInVariableID, GLSLTypeSpecifier, GLSLTypeQualifier, GLSLPrecisionQualifier, GLSLVaryingModifierFlags, GLSLHWRegType;
typedef struct { const uint8_t *pu8Buffer; uint32_t u32CurrentPosition, u32BufferSizeInBytes; int bOverflow; void *gc; void **apvAllocatedMemory; uint32_t u32NumMemoryAllocations, u32MaxMemoryAllocations, u32AllocatedBytes; } SGXBS_Buffer;
typedef struct GLSLBindingSymbol { char *pszName; int eBIVariableID, eTypeSpecifier, eTypeQualifier, ePrecisionQualifier, eVaryingModifierFlags, iActiveArraySize, iDeclaredArraySize; struct { int eRegType; union { unsigned uBaseComp; } u; unsigned uCompAllocCount, ui32CompUseMask; } sRegisterInfo; struct GLSLBindingSymbol *psBaseTypeMembers; unsigned uNumBaseTypeMembers; } GLSLBindingSymbol;
#include "binary_bounds_functions.inc"
static SGXBS_Buffer buffer(const uint8_t *p,unsigned n) { SGXBS_Buffer b={0}; b.pu8Buffer=p; b.u32BufferSizeInBytes=n; b.u32MaxMemoryAllocations=64; b.apvAllocatedMemory=calloc(64,sizeof(void *)); assert(b.apvAllocatedMemory); return b; }
static void cleanup(SGXBS_Buffer *b) { SGXBS_FreeAllocatedMemory(b); free(b->apvAllocatedMemory); }
static int unpack(uint8_t *p,unsigned n) { SGXBS_Buffer b=buffer(p,n); GLSLBindingSymbol *symbols=NULL; unsigned count=0; int e=UnpackSymbolBindings(&symbols,&count,&b,0); cleanup(&b); return e; }
int main(void) {
    /* One symbol: empty name, builtin 0, float-like type, active size 1,
       float register, one allocated component, no members. */
    uint8_t valid[]={0,1, 0, 0,0, 1,0,0,0, 0,1, 0,0, 1, 0,0, 1, 0,1, 0,0};
    assert(unpack(valid,sizeof(valid))==SGXBS_NO_ERROR);
    for(unsigned n=0;n<sizeof(valid);++n) assert(unpack(valid,n)!=SGXBS_NO_ERROR);
    for(unsigned i=0;i<6;++i) {
        unsigned offsets[]={3,5,6,7,8,13}; uint8_t bad[sizeof(valid)]; memcpy(bad,valid,sizeof(valid)); bad[offsets[i]]=255;
        assert(unpack(bad,sizeof(bad))==SGXBS_CORRUPT_BINARY_ERROR);
    }
    uint8_t bad_texture[sizeof(valid)]; memcpy(bad_texture,valid,sizeof(valid));
    bad_texture[13]=HWREG_TEX; bad_texture[15]=GLES2_MAX_TEXTURE_UNITS;
    assert(unpack(bad_texture,sizeof(bad_texture))==SGXBS_CORRUPT_BINARY_ERROR);
    memcpy(bad_texture,valid,sizeof(valid)); bad_texture[16]=255;
    assert(unpack(bad_texture,sizeof(bad_texture))==SGXBS_CORRUPT_BINARY_ERROR);
    memcpy(bad_texture,valid,sizeof(valid)); bad_texture[18]=2;
    assert(unpack(bad_texture,sizeof(bad_texture))==SGXBS_CORRUPT_BINARY_ERROR);
    uint8_t deep[19*33+2]; memset(deep,0,sizeof(deep));
    for(unsigned i=0;i<33;++i) memcpy(deep+i*19,valid,19);
    assert(unpack(deep,sizeof(deep))==SGXBS_CORRUPT_BINARY_ERROR);
    SGXBS_Buffer b=buffer(valid,sizeof(valid)); b.u32CurrentPosition=UINT32_MAX-1;
    assert(!ReadU32(&b) && b.bOverflow); b.bOverflow=0;
    assert(!ReadU16(&b) && b.bOverflow); b.bOverflow=0;
    assert(ReadFloat(&b)==0 && b.bOverflow); b.bOverflow=0;
    char *s=NULL; assert(ReadString(&b,&s)==SGXBS_CORRUPT_BINARY_ERROR && !s);
    b.u32AllocatedBytes=64U*1024U*1024U-1; assert(!SGXBS_Calloc(2,&b)); cleanup(&b);
    puts("binary bounds: valid symbols, all truncations, invalid metadata, nesting and wrapped offsets passed");
}
