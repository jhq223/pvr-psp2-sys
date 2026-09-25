#include <assert.h>
#include <stdio.h>
#include <stddef.h>

typedef unsigned GLuint;
typedef int IMG_BOOL;
typedef void IMG_VOID;
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define GLES_ASSERT assert
#define GLES2_EXTENSION_GET_PROGRAM_BINARY 1
#define GLES2_NAMETYPE_PROGRAM 0
#define GLES2_SHADERTYPE_PROGRAM 2
#define GL_INVALID_VALUE 1
#define GL_INVALID_OPERATION 2
#define GLES2_DIRTYFLAG_VERTEX_PROGRAM 1
#define GLES2_DIRTYFLAG_FRAGMENT_PROGRAM 2
typedef struct { unsigned ui32Name, ui32RefCount; } GLES2NamedItem;
typedef struct {
    GLES2NamedItem sNamedItem;
    unsigned ui32Type;
    int bSuccessfulLink, bDeleting, bLoadFromBinary;
    void *psVertexShader, *psFragmentShader;
} GLES2Program;
typedef struct { GLES2Program *programs[4]; } GLES2NamesArray;
typedef struct { GLES2NamesArray *apsNamesArray[1]; } Shared;
typedef struct {
    Shared *psSharedState;
    struct { GLES2Program *psCurrentProgram; } sProgram;
    unsigned ui32DirtyState;
    int error;
} GLES2Context;
static void SetError(GLES2Context *gc, int error) { gc->error=error; }
static GLES2Program *NamedItemAddRef(GLES2NamesArray *array, GLuint name) {
    GLES2Program *p = name<4 ? array->programs[name] : NULL;
    if(p) ++p->sNamedItem.ui32RefCount;
    return p;
}
static void NamedItemDelRef(GLES2Context *gc, GLES2NamesArray *array, GLES2NamedItem *p) {
    (void)gc; (void)array;
    assert(p->ui32RefCount);
    --p->ui32RefCount;
}
#include "program_switch_functions.inc"

int main(void) {
    GLES2Program a={{1,1},2,1,0,0,NULL,NULL};
    GLES2Program b={{2,1},2,1,0,0,NULL,NULL};
    GLES2Program invalid={{3,1},2,0,0,0,NULL,NULL};
    GLES2NamesArray names={{NULL,&a,&b,&invalid}};
    Shared shared={{&names}};
    GLES2Context gc={&shared,{NULL},0,0};
    for(int n=0;n<64;++n) {
        for(unsigned id=1;id<=2;++id) {
            gc.ui32DirtyState=0;
            UseProgram(&gc,id);
            assert(gc.ui32DirtyState==3); /* detached stages still switch */
            gc.ui32DirtyState=0;
            UseProgram(&gc,id);
            assert(gc.ui32DirtyState==0); /* same program does no extra work */
            assert(names.programs[id]->sNamedItem.ui32RefCount==2);
        }
    }
    UseProgram(&gc,3);
    assert(gc.error==GL_INVALID_OPERATION && gc.sProgram.psCurrentProgram==&b);
    assert(invalid.sNamedItem.ui32RefCount==1 && gc.ui32DirtyState==0);
    UseProgram(&gc,8);
    assert(gc.error==GL_INVALID_VALUE && gc.sProgram.psCurrentProgram==&b);
    UseProgram(&gc,0);
    assert(!gc.sProgram.psCurrentProgram && gc.ui32DirtyState==3);
    assert(a.sNamedItem.ui32RefCount==1 && b.sNamedItem.ui32RefCount==1);
    gc.ui32DirtyState=0;
    UseProgram(&gc,0);
    assert(gc.ui32DirtyState==0);
    puts("128 detached program switches, redundant binds and rejected-bind ownership passed");
    return 0;
}
