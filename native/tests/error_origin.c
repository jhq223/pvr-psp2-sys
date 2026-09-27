#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define IMG_INTERNAL
#define IMG_VOID void
#define PVR_UNREFERENCED_PARAMETER(x) ((void)(x))
#define PVR_DPF(x) ((void)0)
#define GL_OUT_OF_MEMORY 0x505
typedef char IMG_CHAR;
typedef int IMG_INT32;
typedef unsigned GLenum;
typedef struct { int i32Error; } GLES2Context;
static char message[128];
static int reports;
static int sceClibPrintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    ++reports;
    return result;
}
#include "error_origin_functions.inc"
int main(void) {
    GLES2Context gc = {0};
    SetErrorFileLine(&gc, GL_OUT_OF_MEMORY, "C:\\driver\\texmgmt.c", 42);
    assert(gc.i32Error == GL_OUT_OF_MEMORY && reports == 1);
    assert(!strcmp(message, "[PVR][OOM] texmgmt.c:42\n"));
    SetErrorFileLine(&gc, GL_OUT_OF_MEMORY, "ignored.c", 1);
    assert(reports == 1);
    gc.i32Error = 0;
    SetErrorFileLine(&gc, 0x500, "invalid.c", 2);
    SetErrorFileLine(&gc, GL_OUT_OF_MEMORY, "ignored.c", 3);
    assert(gc.i32Error == 0x500 && reports == 1);
    gc.i32Error = 0;
    SetErrorFileLine(&gc, GL_OUT_OF_MEMORY, "/driver/drawvarray.c", 99);
    assert(reports == 2 && !strcmp(message, "[PVR][OOM] drawvarray.c:99\n"));
    puts("error origin: first error, OOM-only reporting and path trimming passed");
}
