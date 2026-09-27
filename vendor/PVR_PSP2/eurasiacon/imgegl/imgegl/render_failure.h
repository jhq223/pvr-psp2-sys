#ifndef KRKR_RENDER_FAILURE_H
#define KRKR_RENDER_FAILURE_H
static void KrkrRenderFailure(const char *stage, unsigned error, unsigned bytes)
{
    sceClibPrintf("[PVR][SURFACE] stage=%s error=0x%X bytes=%u\n", stage, error, bytes);
}
#endif
