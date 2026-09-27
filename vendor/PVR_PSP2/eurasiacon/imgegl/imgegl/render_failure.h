#ifndef KRKR_RENDER_FAILURE_H
#define KRKR_RENDER_FAILURE_H
/* File capture is restricted to diagnostic builds of the standalone probe. */
static void KrkrRenderFailure(const char *stage, unsigned error, unsigned bytes)
{
    sceClibPrintf("[PVR][SURFACE] stage=%s error=0x%X bytes=%u\n", stage, error, bytes);
#if defined(PVR_PROBE_DIAGNOSTICS) && PVR_PROBE_DIAGNOSTICS
    char text[192];
    int length = sceClibSnprintf(text, sizeof(text), "driver-error,stage=%s,error=0x%X,bytes=%u\n", stage, error, bytes);
    SceUID file;
    if(length <= 0 || length >= sizeof(text)) return;
    file = sceIoOpen("ux0:/data/pvr-driver-probe/driver-failure.csv", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if(file >= 0)
    {
        int offset = 0;
        while(offset < length)
        {
            int written = sceIoWrite(file, text + offset, length - offset);
            if(written <= 0) break;
            offset += written;
        }
        sceIoClose(file);
    }
#endif
}
#endif
