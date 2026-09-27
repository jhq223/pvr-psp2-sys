#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define IMG_INTERNAL
#define IMG_VOID void
#define IMG_NULL NULL
#define IMG_TRUE 1
#define IMG_FALSE 0
#define GL_API_EXT
#define GL_APIENTRY
#define GLES_ASSERT(x) assert(x)
#define PVR_DPF(x) ((void)0)
#define PVR_UNREFERENCED_PARAMETER(x) ((void)(x))
#define GLES2_TIME_START(x) ((void)0)
#define GLES2_TIME_STOP(x) ((void)0)
#define GLES2_MAX_VERTEX_ATTRIBS 4
#define GLES2_NAMETYPE_BUFOBJ 0
#define GLES2_NAMETYPE_VERARROBJ 1
#define GLES2_MAX_SHAREABLE_NAMETYPE 1
#define GLES2_DIRTYFLAG_VAO_BINDING 1
#define GLES2_SCHEDULE_HW_WAIT_FOR_TA 1
#define IMG_EGL_NO_ERROR 0
#define KRM_DEFAULT_WAIT_RETRIES 3
#define GL_INVALID_VALUE 1
#define GL_INVALID_OPERATION 2
#define GL_OUT_OF_MEMORY 3
typedef unsigned IMG_UINT32, GLuint;
typedef int IMG_BOOL, IMG_INT32, GLsizei;
typedef uint8_t IMG_UINT8;
typedef struct { unsigned ui32Name, ui32RefCount; } GLES2NamedItem;
typedef struct { GLES2NamedItem sNamedItem; } GLES2BufferObject;
typedef struct { int needed; } KRMResource;
typedef struct { GLES2NamedItem sNamedItem; KRMResource sResource; struct { GLES2BufferObject *psBufObj; } asVAOState[4]; GLES2BufferObject *psBoundElementBuffer; void *psMemInfo, *psPDSVertexState, *psPDSVertexShaderProgram; unsigned ui32DirtyState; } GLES2VertexArrayObject;
typedef struct { GLES2VertexArrayObject *items[8]; } GLES2NamesArray;
typedef struct { int bPrimitivesSinceLastTA; } Surface;
typedef struct { GLES2NamesArray *apsNamesArray[1]; } Shared;
typedef struct { struct { GLES2VertexArrayObject sDefaultVAO, *psActiveVAO; } sVAOMachine; int sVAOKRM, sKRMTAStatusUpdate; Surface *psRenderSurface; Shared *psSharedState; GLES2NamesArray *apsNamesArray[1]; void *ps3DDevData; } GLES2Context;
static GLES2Context ctx;
#define __GLES2_GET_CONTEXT() GLES2Context *gc = &ctx
static int wait_ok, error, adds, device_frees, retired;
static KRMResource *pending;
static void FreeVertexArrayObject(GLES2Context *, GLES2VertexArrayObject *, IMG_BOOL);
static int KRM_IsResourceNeeded(int *m, KRMResource *r) { return r->needed; }
static int KRM_IsResourceInUse(int *m, void *g, int *s, KRMResource *r) { return r->needed; }
static int KRM_WaitUntilResourceIsNotNeeded(int *m, KRMResource *r, int n) { if(wait_ok) r->needed=0; return wait_ok; }
static int ScheduleTA(GLES2Context *gc, Surface *s, int f) { return wait_ok ? 0 : 1; }
static void KRM_RetireResource(int *m, KRMResource *r) { assert(!pending); pending=r; ++retired; }
static void KRM_RemoveResourceFromAllLists(int *m, KRMResource *r) { assert(!r->needed); }
static void GLES2FREEDEVICEMEM(void *d, void *p) { ++device_frees; free(p); }
static void GLES2Free(void *gc, void *p) { free(p); }
static void SetError(GLES2Context *gc, int e) { error=e; }
static GLES2NamedItem *NamedItemAddRef(GLES2NamesArray *a, unsigned n) { ++adds; if(!a->items[n]) return NULL; ++a->items[n]->sNamedItem.ui32RefCount; return &a->items[n]->sNamedItem; }
static void NamedItemDelRef(GLES2Context *gc, GLES2NamesArray *a, GLES2NamedItem *p) { assert(p->ui32RefCount); if(!--p->ui32RefCount && a==gc->apsNamesArray[0]) FreeVertexArrayObject(gc,(GLES2VertexArrayObject *)p,0); }
static void NamedItemDelRefByName(GLES2Context *gc, GLES2NamesArray *a, unsigned n, const unsigned *names) { for(unsigned i=0;i<n;++i) if(names[i] && a->items[names[i]]) { GLES2VertexArrayObject *v=a->items[names[i]]; a->items[names[i]]=NULL; NamedItemDelRef(gc,a,&v->sNamedItem); } }
static GLES2VertexArrayObject *CreateVertexArrayObject(GLES2Context *gc, unsigned name) { GLES2VertexArrayObject *v=calloc(1,sizeof(*v)); assert(v); v->sNamedItem.ui32Name=name; return v; }
static int InsertNamedItem(GLES2NamesArray *a, GLES2NamedItem *v) { a->items[v->ui32Name]=(GLES2VertexArrayObject *)v; v->ui32RefCount=1; return 1; }
#include "vao_lifetime_functions.inc"
int main(void) {
    GLES2NamesArray arrays={0}, buffers={0}; Shared shared={{&buffers}};
    ctx.psSharedState=&shared; ctx.apsNamesArray[0]=&arrays; ctx.sVAOMachine.psActiveVAO=&ctx.sVAOMachine.sDefaultVAO;
    glBindVertexArrayOES(1); GLES2VertexArrayObject *active=ctx.sVAOMachine.psActiveVAO;
    active->psMemInfo=malloc(16); active->psPDSVertexState=malloc(8);
    int old_adds=adds; for(int i=0;i<100;++i) glBindVertexArrayOES(1);
    assert(adds==old_adds && active->sNamedItem.ui32RefCount==2);
    unsigned other=2; glDeleteVertexArraysOES(1,&other);
    assert(ctx.sVAOMachine.psActiveVAO==active && active->psMemInfo && !device_frees);
    GLES2BufferObject buffer={{3,2}}; active->asVAOState[0].psBufObj=&buffer;
    active->sResource.needed=1; unsigned name=1; glDeleteVertexArraysOES(1,&name);
    assert(ctx.sVAOMachine.psActiveVAO==&ctx.sVAOMachine.sDefaultVAO);
    assert(pending==&active->sResource && retired==1 && !device_frees && buffer.sNamedItem.ui32RefCount==2);
    pending->needed=0; DestroyVAOGhostKRM(&ctx,pending); pending=NULL;
    assert(device_frees==1 && buffer.sNamedItem.ui32RefCount==1 && !error);
    glBindVertexArrayOES(2); glDeleteVertexArraysOES(1,&other); assert(!arrays.items[2]);
    puts("VAO lifetime: unrelated deletion, repeat binding, failed wait and deferred free passed");
}
