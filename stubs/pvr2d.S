.arch armv7a

.section .vitalink.fstubs.libpvr2d,"ax",%progbits

.macro pvr2d_stub name, nid
    .align 4
    .global \name
    .type \name, %function
\name:
    .word 0x00000000
    .word 0xD1F3865A
    .word \nid
    .align 4
.endm

pvr2d_stub PVR2DGetAPIRev,             0x68058C98
pvr2d_stub PVR2DEnumerateDevices,      0x2F9B2EF0
pvr2d_stub PVR2DCreateDeviceContext,   0x1484E661
pvr2d_stub PVR2DDestroyDeviceContext,  0x11EBCE82
pvr2d_stub PVR2DGetScreenMode,         0xD90B7DBC
pvr2d_stub PVR2DMemAlloc,              0xD93850BA
pvr2d_stub PVR2DMemFree,               0xAF02AF23
pvr2d_stub PVR2DBlt,                   0xB867C2D0
pvr2d_stub PVR2DQueryBlitsComplete,    0x5B8AB3BE
pvr2d_stub PVR2DCreateFlipChain,       0xB753EE01
pvr2d_stub PVR2DDestroyFlipChain,      0x19D673E5
pvr2d_stub PVR2DGetFlipChainBuffers,   0x09EF4372
pvr2d_stub PVR2DPresentFlip,           0x71383580
