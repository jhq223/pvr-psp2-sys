#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t IMG_UINT32;
typedef uint8_t IMG_UINT8;
typedef int32_t IMG_INT32;
typedef char IMG_CHAR;
typedef int IMG_BOOL;
typedef unsigned GLuint;
#define IMG_VOID void
#define IMG_INTERNAL
#define IMG_NULL NULL
#define IMG_FALSE 0
#define IMG_TRUE 1
#define GL_APICALL
#define GL_APIENTRY
#define GL_INVALID_OPERATION 1
#define GLES2_TIME_START(x) ((void)0)
#define GLES2_TIME_STOP(x) ((void)0)
#define PVR_DPF(x) ((void)0)
#define PVR_UNREFERENCED_PARAMETER(x) ((void)(x))
#define GLES2Free(gc, p) free(p)
#define __GLES2_GET_CONTEXT_RETURN(x) GLES2Context *gc = &context
typedef struct { int error; } GLES2Context;
typedef struct { char *pszName; int i32Location; unsigned ui32ActiveArraySize, ui32DeclaredArraySize; } GLES2Uniform;
typedef struct {
    int bSuccessfulLink;
    unsigned ui32UniformLocationCount, ui32UniformNameMask, ui32NumActiveUniforms, ui32NumActiveUserUniforms;
    GLES2Uniform **ppsUniformLocations, **ppsUniformNames, **ppsActiveUserUniforms, *psActiveUniforms;
} GLES2Program;
static GLES2Context context;
static GLES2Program program;
static GLES2Program *GetNamedProgram(GLES2Context *gc, unsigned name) { return name == 1 ? &program : NULL; }
static void SetError(GLES2Context *gc, unsigned error) { gc->error = error; }
#include "uniform_functions.inc"
int main(void) {
    GLES2Uniform uniforms[] = {{"x", 0, 1, 0}, {"array", 0, 3, 7}, {"lights[0].color", 0, 1, 0}, {"builtin", -1, 1, 0}};
    GLES2Uniform *users[] = {&uniforms[0], &uniforms[1], &uniforms[2]};
    program = (GLES2Program){.bSuccessfulLink = 1, .ui32NumActiveUniforms = 4, .ui32NumActiveUserUniforms = 3,
        .ppsActiveUserUniforms = users, .psActiveUniforms = uniforms};
    assert(AssignUniformLocations(&program));
    assert(glGetUniformLocation(1, "x") == 1);
    assert(glGetUniformLocation(1, "array") == 2);
    assert(glGetUniformLocation(1, "array[0]") == 2);
    assert(glGetUniformLocation(1, "array[2]") == 4);
    assert(glGetUniformLocation(1, "lights[0].color") == 9);
    const char *invalid[] = {NULL, "", "[", "]", "[]", "gl_x", "array[]", "array[-1]", "array[+1]", "array[3]", "array[2147483648]", "array[999999999999999999999]", "x[0]", "missing", "array[1]junk"};
    for(unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) assert(glGetUniformLocation(1, invalid[i]) == -1);
    for(int i = -1; i <= 10; ++i) {
        GLES2Uniform *expected = i == 1 ? &uniforms[0] : (i >= 2 && i <= 4 ? &uniforms[1] : (i == 9 ? &uniforms[2] : NULL));
        assert(FindUniformFromLocation(&context, &program, i) == expected);
    }
    assert(AssignUniformLocations(&program)); /* Relink frees and rebuilds both tables. */
    program.bSuccessfulLink = 0; assert(glGetUniformLocation(1, "x") == -1 && context.error == GL_INVALID_OPERATION);
    FreeUniformLookups(&program); assert(!program.ppsUniformLocations && !program.ppsUniformNames);
    puts("uniform lookups: names, arrays, malformed indices, inactive locations and relinking passed");
}
