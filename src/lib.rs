//! Raw bindings for the PlayStation Vita PVR graphics libraries.
//!
//! [`gles`] exposes EGL, OpenGL ES 2.0 and PVR application hints. The crate
//! root exposes PVR2D types and functions. Calls retain the native libraries'
//! initialization, threading, pointer validity and resource lifetime rules.
//!
//! Vita builds link import stubs; applications must deploy and load the
//! matching runtime modules separately. Building on another platform permits
//! type checking and host tooling, but does not provide a graphics runtime.
#![no_std]
#![allow(non_camel_case_types, non_snake_case, non_upper_case_globals)]

use core::ffi::{c_char, c_int, c_long, c_uchar, c_uint, c_ulong, c_void};

pub mod gles;

pub type PVR2DERROR = c_int;
pub type PVR2DFORMAT = c_ulong;
pub type PVR2DBLITFLAGS = c_uint;
pub type PVR2DCONTEXTHANDLE = *mut c_void;
pub type PVR2DFLIPCHAINHANDLE = *mut c_void;

pub const PVR2D_OK: PVR2DERROR = 0;
pub const PVR2D_ARGB8888: PVR2DFORMAT = 0x04;
pub const PVR2D_PAL8: PVR2DFORMAT = 0x0a;
pub const PVR2DROPcopy: c_ulong = 0xcc;
pub const PVR2D_MEM_WRITECOMBINE: c_ulong = 0x02;
pub const PVR2D_PSP2_MEM_MAIN: c_ulong = 0x08;
pub const PVR2D_ALIGNMENT_PALETTE: c_ulong = 16;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct PVR2DMEMINFO {
    pub i32MemBlockUID: i32,
    pub pBase: *mut c_void,
    pub ui32MemSize: c_ulong,
    pub ui32DevAddr: c_ulong,
    pub ulFlags: c_ulong,
    pub hPrivateData: *mut c_void,
    pub hPrivateMapData: *mut c_void,
}

pub const PVR2D_MAX_DEVICE_NAME: usize = 20;

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct PVR2DDEVICEINFO {
    pub ulDevID: c_ulong,
    pub szDeviceName: [c_char; PVR2D_MAX_DEVICE_NAME],
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct PVR2D_ALPHABLT {
    pub eAlpha1: c_int,
    pub bAlpha1Invert: c_int,
    pub eAlpha2: c_int,
    pub bAlpha2Invert: c_int,
    pub eAlpha3: c_int,
    pub bAlpha3Invert: c_int,
    pub eAlpha4: c_int,
    pub bAlpha4Invert: c_int,
    pub bPremulAlpha: c_int,
    pub bTransAlpha: c_int,
    pub bUpdateAlphaLookup: c_int,
    pub uAlphaLookup0: c_uchar,
    pub uAlphaLookup1: c_uchar,
    pub uGlobalRGB: c_uchar,
    pub uGlobalA: c_uchar,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct PVR2D_SURFACE {
    pub pSurfMemInfo: *mut PVR2DMEMINFO,
    pub SurfOffset: c_ulong,
    pub Stride: c_long,
    pub Format: PVR2DFORMAT,
    pub SurfWidth: c_ulong,
    pub SurfHeight: c_ulong,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct PVR2DBLTINFO {
    pub CopyCode: c_ulong,
    pub Colour: c_ulong,
    pub ColourKey: c_ulong,
    pub GlobalAlphaValue: c_uchar,
    pub AlphaBlendingFunc: c_uchar,
    pub BlitFlags: PVR2DBLITFLAGS,
    pub pDstMemInfo: *mut PVR2DMEMINFO,
    pub DstOffset: c_ulong,
    pub DstStride: c_long,
    pub DstX: c_long,
    pub DstY: c_long,
    pub DSizeX: c_long,
    pub DSizeY: c_long,
    pub DstFormat: PVR2DFORMAT,
    pub DstSurfWidth: c_ulong,
    pub DstSurfHeight: c_ulong,
    pub pSrcMemInfo: *mut PVR2DMEMINFO,
    pub SrcOffset: c_ulong,
    pub SrcStride: c_long,
    pub SrcX: c_long,
    pub SrcY: c_long,
    pub SizeX: c_long,
    pub SizeY: c_long,
    pub SrcFormat: PVR2DFORMAT,
    pub pPalMemInfo: *mut PVR2DMEMINFO,
    pub PalOffset: c_ulong,
    pub SrcSurfWidth: c_ulong,
    pub SrcSurfHeight: c_ulong,
    pub pMaskMemInfo: *mut PVR2DMEMINFO,
    pub MaskOffset: c_ulong,
    pub MaskStride: c_long,
    pub MaskX: c_long,
    pub MaskY: c_long,
    pub MaskSurfWidth: c_ulong,
    pub MaskSurfHeight: c_ulong,
    pub pAlpha: *mut PVR2D_ALPHABLT,
    pub uSrcChromaPlane1: c_ulong,
    pub uSrcChromaPlane2: c_ulong,
    pub uDstChromaPlane1: c_ulong,
    pub uDstChromaPlane2: c_ulong,
    pub ColourKeyMask: c_ulong,
    pub pPat: *mut PVR2D_SURFACE,
    pub PatX: c_long,
    pub PatY: c_long,
}

unsafe extern "C" {
    pub fn PVR2DGetAPIRev(lRevMajor: *mut c_long, lRevMinor: *mut c_long) -> PVR2DERROR;
    pub fn PVR2DEnumerateDevices(pDevInfo: *mut PVR2DDEVICEINFO) -> c_int;
    pub fn PVR2DCreateDeviceContext(
        ulDevID: c_ulong,
        phContext: *mut PVR2DCONTEXTHANDLE,
        ulFlags: c_ulong,
    ) -> PVR2DERROR;
    pub fn PVR2DDestroyDeviceContext(hContext: PVR2DCONTEXTHANDLE) -> PVR2DERROR;
    pub fn PVR2DGetScreenMode(
        hContext: PVR2DCONTEXTHANDLE,
        pFormat: *mut PVR2DFORMAT,
        plWidth: *mut c_long,
        plHeight: *mut c_long,
        plStride: *mut c_long,
        piRefreshRate: *mut c_int,
    ) -> PVR2DERROR;
    pub fn PVR2DMemAlloc(
        hContext: PVR2DCONTEXTHANDLE,
        ulBytes: c_ulong,
        ulAlign: c_ulong,
        ulFlags: c_ulong,
        ppsMemInfo: *mut *mut PVR2DMEMINFO,
    ) -> PVR2DERROR;
    pub fn PVR2DMemFree(hContext: PVR2DCONTEXTHANDLE, psMemInfo: *mut PVR2DMEMINFO) -> PVR2DERROR;
    pub fn PVR2DBlt(hContext: PVR2DCONTEXTHANDLE, pBltInfo: *mut PVR2DBLTINFO) -> PVR2DERROR;
    pub fn PVR2DQueryBlitsComplete(
        hContext: PVR2DCONTEXTHANDLE,
        pMemInfo: *const PVR2DMEMINFO,
        uiWaitForComplete: c_uint,
    ) -> PVR2DERROR;
    pub fn PVR2DCreateFlipChain(
        hContext: PVR2DCONTEXTHANDLE,
        ulFlags: c_ulong,
        ulNumBuffers: c_ulong,
        ulWidth: c_ulong,
        ulHeight: c_ulong,
        eFormat: PVR2DFORMAT,
        plStride: *mut c_long,
        pulFlipChainID: *mut c_ulong,
        phFlipChain: *mut PVR2DFLIPCHAINHANDLE,
    ) -> PVR2DERROR;
    pub fn PVR2DDestroyFlipChain(
        hContext: PVR2DCONTEXTHANDLE,
        hFlipChain: PVR2DFLIPCHAINHANDLE,
    ) -> PVR2DERROR;
    pub fn PVR2DGetFlipChainBuffers(
        hContext: PVR2DCONTEXTHANDLE,
        hFlipChain: PVR2DFLIPCHAINHANDLE,
        pulNumBuffers: *mut c_ulong,
        psMemInfo: *mut *mut PVR2DMEMINFO,
    ) -> PVR2DERROR;
    pub fn PVR2DPresentFlip(
        hContext: PVR2DCONTEXTHANDLE,
        hFlipChain: PVR2DFLIPCHAINHANDLE,
        psMemInfo: *mut PVR2DMEMINFO,
        lRenderID: c_long,
    ) -> PVR2DERROR;
}

#[cfg(target_pointer_width = "32")]
const _: [(); 28] = [(); core::mem::size_of::<PVR2DMEMINFO>()];
#[cfg(target_pointer_width = "32")]
const _: [(); 24] = [(); core::mem::size_of::<PVR2DDEVICEINFO>()];
#[cfg(target_pointer_width = "32")]
const _: [(); 172] = [(); core::mem::size_of::<PVR2DBLTINFO>()];
