/* Production EGL wrappers and code heaps retain every later owner on failure. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned IMG_UINT32;
typedef int IMG_BOOL;
typedef int EGLint;
typedef int PVRSRV_ERROR;
typedef int WSEGLError;
#define IMG_TRUE 1
#define IMG_FALSE 0
#define IMG_NULL NULL
#define PVRSRV_OK 0
#define PVR_DPF(x) ((void)0)
#define IMG_INTERNAL
#define SUPPORT_OPENGLES2
#define IMGEGL_CONTEXT_OPENGLES2 2
#define EGL_SUCCESS 0
#define EGL_NOT_INITIALIZED 1
#define EGL_BAD_ALLOC 2
#define EGL_SURFACE_PBUFFER 1
#define EGL_SURFACE_WINDOW 2
#define WSEGL_SUCCESS 0
typedef struct { int terminal; } SrvSysContext;
static int KrkrDeviceFaulted(const SrvSysContext *c) { return c->terminal; }
static int KrkrDeviceFault(SrvSysContext *c,const char *op,int code) { (void)op;(void)code;c->terminal=1;return 0; }
typedef struct { int (*pfnWSEGL_DeleteDrawable)(void*); } Table;
typedef struct { Table *pWSEGL_FT; } Display;
typedef struct { Display *psDpy; void *psCfg,*hClientContext; int eContextType; } KEGL_CONTEXT;
typedef struct { Display *psDpy; void *psCfg; int type; union { struct { void *hDrawable,*native; } window,pixmap; } u; } KEGL_SURFACE;
typedef struct { SrvSysContext sSysContext; struct { int (*pfnGLESDestroyGC)(void*); } spfnOGLES2; } Global;
typedef struct { Global *psGlobalData; } Thread;
typedef Thread *TLS;
static Global global;
static Thread thread = {&global};
static unsigned fail_gc, fail_surface, fail_drawable, gc_calls, surface_calls, drawable_calls, unlinks, config_drops, host_drops, backing_drops, locks;
static TLS IMGEGLGetTLSValue(void) { return &thread; }
static void EGLThreadLock(TLS t) { (void)t; assert(locks++ == 0); }
static void EGLThreadUnlock(TLS t) { (void)t; assert(--locks == 0); }
static void EGLReleaseThreadLockWSEGL(Display *d,TLS t) { (void)d;(void)t; }
static void EGLReacquireThreadLockWSEGL(Display *d,TLS t) { (void)d;(void)t; }
static void _DpyContextUnlink(Display *d,KEGL_CONTEXT *c) { (void)d;(void)c;++unlinks; }
static void _DpySurfaceUnlink(Display *d,KEGL_SURFACE *s) { (void)d;(void)s;++unlinks; }
static void CFGC_Unlink(void *c) { (void)c;++config_drops; }
static void EGLFree(void *p) { assert(p);++host_drops; }
static int SRV_DestroySurface(SrvSysContext *c,KEGL_SURFACE *s) { (void)c;(void)s;++surface_calls;return !fail_surface; }
static void GWS_DeletePBufferDrawable(KEGL_SURFACE *s,SrvSysContext *c) { (void)s;(void)c;++backing_drops; }
static int destroy_gc(void *gc) { (void)gc;++gc_calls;return !fail_gc; }
static int destroy_drawable(void *d) { (void)d;++drawable_calls;return fail_drawable ? -7 : 0; }
#include "egl_owner_functions.inc"
typedef struct Memory { struct Memory *psNext; unsigned id; } PVRSRV_CLIENT_MEM_INFO;
typedef struct Block { struct Block *psNext; } UCH_UseCodeBlock;
typedef struct { PVRSRV_CLIENT_MEM_INFO *psCodeMemory; UCH_UseCodeBlock *psFreeBlockList; int *ps3DDevData; int i32AllocationsNotDeallocated; } UCH_UseCodeHeap;
static unsigned code_frees,metadata_frees,code_failure;
static int PvrFreeOwnedDeviceMem(int *d,PVRSRV_CLIENT_MEM_INFO *m) { (void)d;++code_frees;return m->id == code_failure ? -19 : 0; }
static void PVRSRVFreeUserModeMem(void *p) { assert(p);++metadata_frees; }
#define PVRSRVMemSet memset
#include "codeheap_owner_functions.inc"
int main(void) {
    global.spfnOGLES2.pfnGLESDestroyGC=destroy_gc;
    Table table={destroy_drawable}; Display display={&table};
    KEGL_CONTEXT context={&display,&table,&global,IMGEGL_CONTEXT_OPENGLES2};
    KEGL_SURFACE surface={0};surface.psDpy=&display;surface.psCfg=&table;surface.type=EGL_SURFACE_WINDOW;surface.u.window.hDrawable=&surface;surface.u.window.native=&context;
    fail_gc=1;
    assert(_ContextDelete(&context,0)==EGL_BAD_ALLOC);
    assert(gc_calls==1 && !unlinks && !config_drops && !host_drops && !locks);
    assert(_ContextDelete(&context,0)==EGL_BAD_ALLOC && gc_calls==1);
    global.sSysContext.terminal=0;fail_gc=0;
    assert(_ContextDelete(&context,0)==EGL_SUCCESS);
    assert(unlinks==1 && config_drops==1 && host_drops==1 && !locks);
    unlinks=config_drops=host_drops=0;fail_surface=1;
    assert(!_SurfaceDelete(&global.sSysContext,&surface));
    assert(surface_calls==1 && !drawable_calls && !unlinks && !config_drops && !host_drops);
    assert(!_SurfaceDelete(&global.sSysContext,&surface) && surface_calls==1);
    global.sSysContext.terminal=0;fail_surface=0;fail_drawable=1;
    assert(!_SurfaceDelete(&global.sSysContext,&surface));
    assert(surface_calls==2 && drawable_calls==1 && !unlinks && !config_drops && !host_drops);
    assert(!_SurfaceDelete(&global.sSysContext,&surface) && surface_calls==2);
    global.sSysContext.terminal=0;fail_drawable=0;
    assert(_SurfaceDelete(&global.sSysContext,&surface));
    assert(unlinks==1 && config_drops==1 && host_drops==2);
    PVRSRV_CLIENT_MEM_INFO second={NULL,2},first={&second,1};
    UCH_UseCodeBlock block={NULL}; int device=0;
    UCH_UseCodeHeap heap={&first,&block,&device,0};
    code_failure=2;
    assert(UCH_CodeHeapDestroy(&heap)==-19);
    assert(heap.psCodeMemory==&second && heap.psFreeBlockList==&block && code_frees==2 && !metadata_frees);
    /* Fresh healthy fixtures use the unchanged success release order. */
    code_failure=0;heap.psCodeMemory=&first;
    assert(UCH_CodeHeapDestroy(&heap)==0 && metadata_frees==2 && !heap.psCodeMemory);
    puts("EGL wrappers and code heaps stop before freeing dependent owners");
    return 0;
}
