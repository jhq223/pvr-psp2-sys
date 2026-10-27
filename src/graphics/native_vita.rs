//! Vita module loading and EGL window ownership.
use super::*;
use std::ffi::{CStr, CString};
use vitasdk_sys::*;
const WIDTH: u32 = 960;
const HEIGHT: u32 = 544;

pub(super) struct Native {
    pub(super) failure: fault::State,
    pub(super) display: EGLDisplay,
    active: bool,
    surface: EGLSurface,
    pub(super) context: EGLContext,
    modules: Vec<i32>,
}
impl Native {
    pub(super) fn close(&mut self) -> Result<()> {
        if let Some(fault) = self.failure.refresh() {
            return Err(Error::Terminal(fault));
        }
        unsafe {
            if !self.context.is_null() {
                egl_ok(
                    "eglMakeCurrent(close)",
                    eglMakeCurrent(
                        self.display,
                        std::ptr::null_mut(),
                        std::ptr::null_mut(),
                        std::ptr::null_mut(),
                    ),
                )?;
                egl_ok(
                    "eglDestroyContext",
                    eglDestroyContext(self.display, self.context),
                )?;
                self.context = std::ptr::null_mut();
            }
            if !self.surface.is_null() {
                egl_ok(
                    "eglDestroySurface",
                    eglDestroySurface(self.display, self.surface),
                )?;
                self.surface = std::ptr::null_mut();
            }
            if !self.display.is_null() {
                egl_ok("eglTerminate", eglTerminate(self.display))?;
                self.display = std::ptr::null_mut();
            }
            // Successful EGL termination ends the query entry point lifetime.
            // Detach before any module unload, including partially failed unloads.
            self.failure.detach();
            while let Some(&module) = self.modules.last() {
                let mut status = 0;
                let result = sceKernelStopUnloadModule(
                    module,
                    0,
                    std::ptr::null_mut(),
                    0,
                    std::ptr::null_mut(),
                    &mut status,
                );
                if result < 0 {
                    return Err(Error::Sdk("stop/unload PVR module", result));
                }
                if status < 0 {
                    return Err(Error::Sdk("stop PVR module", status));
                }
                self.modules.pop();
            }
        }
        if self.active {
            self.active = false;
            ACTIVE.store(false, Ordering::Release);
        }
        Ok(())
    }
}
impl Drop for Native {
    fn drop(&mut self) {
        if self.close().is_err() {
            // Native Services may still reference both data and executable
            // module code. Preserve those native owners through process exit.
            std::mem::forget(std::mem::take(&mut self.modules));
        }
    }
}

impl Native {
    pub(super) fn new(module_directory: &str, vsync: bool) -> Result<Self> {
        if ACTIVE
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .is_err()
        {
            return Err(Error::Invalid("a PVR context is already alive"));
        }
        let mut native = Self {
            failure: Default::default(),
            display: std::ptr::null_mut(),
            active: true,
            surface: std::ptr::null_mut(),
            context: std::ptr::null_mut(),
            modules: Vec::new(),
        };
        for path in [
            "vs0:sys/external/libfios2.suprx",
            "vs0:sys/external/libc.suprx",
        ] {
            let _ = load_module(path);
        }
        let path = |file: &str| std::format!("{}/{file}", module_directory.trim_end_matches('/'));
        native
            .modules
            .push(load_module(&path("libgpu_es4_ext.suprx"))?);
        native.modules.push(load_module(&path("libIMGEGL.suprx"))?);
        let mut hint = PvrAppHint::default();
        if unsafe { PVRSRVInitializeAppHint(&mut hint) } == 0 {
            return Err(Error::Invalid("PVR defaults unavailable"));
        }
        set_path(&mut hint.window_system, &path("libpvrPSP2_WSEGL.suprx"))?;
        set_path(&mut hint.gles2, &path("libGLESv2.suprx"))?;
        hint.overload_tex_layout = PVR_TEXTURE_LAYOUT_STRIDE;
        hint.flush_behaviour = PVR_FLUSH_KICK_3D;
        hint.enable_memory_speed_test = 0;
        if unsafe { PVRSRVCreateVirtualAppHint(&mut hint) } == 0 {
            return Err(Error::Invalid("PVR hints rejected"));
        }
        let fault_query = native.proc_address("eglQueryDeviceFaultVITA");
        native.failure.load(fault_query)?;
        unsafe {
            native.display = eglGetDisplay(0);
            if native.display.is_null() {
                return Err(egl_error("eglGetDisplay"));
            }
            egl_ok(
                "eglInitialize",
                eglInitialize(native.display, std::ptr::null_mut(), std::ptr::null_mut()),
            )?;
            egl_ok("eglBindAPI", eglBindAPI(EGL_OPENGL_ES_API))?;
            let attributes = [
                EGL_RED_SIZE,
                8,
                EGL_GREEN_SIZE,
                8,
                EGL_BLUE_SIZE,
                8,
                EGL_ALPHA_SIZE,
                8,
                EGL_DEPTH_SIZE,
                0,
                EGL_STENCIL_SIZE,
                0,
                EGL_SURFACE_TYPE,
                EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                EGL_RENDERABLE_TYPE,
                EGL_OPENGL_ES2_BIT,
                EGL_NONE,
            ];
            let mut config = std::ptr::null_mut();
            let mut count = 0;
            egl_ok(
                "eglChooseConfig",
                eglChooseConfig(
                    native.display,
                    attributes.as_ptr(),
                    &mut config,
                    1,
                    &mut count,
                ),
            )?;
            if count != 1 || config.is_null() {
                return Err(Error::Invalid("no RGBA EGL configuration"));
            }
            native.surface = eglCreateWindowSurface(
                native.display,
                config,
                std::ptr::null_mut(),
                std::ptr::null(),
            );
            if native.surface.is_null() {
                return Err(egl_error("eglCreateWindowSurface"));
            }
            native.context = eglCreateContext(
                native.display,
                config,
                std::ptr::null_mut(),
                [EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE].as_ptr(),
            );
            if native.context.is_null() {
                return Err(egl_error("eglCreateContext"));
            }
            egl_ok(
                "eglMakeCurrent",
                eglMakeCurrent(
                    native.display,
                    native.surface,
                    native.surface,
                    native.context,
                ),
            )?;
            egl_ok(
                "eglSwapInterval",
                eglSwapInterval(native.display, i32::from(vsync)),
            )?;
        }
        Ok(native)
    }
    pub(super) fn size(&self) -> [u32; 2] {
        [WIDTH, HEIGHT]
    }
    pub(super) fn proc_address(&self, name: &str) -> *const core::ffi::c_void {
        let name = CString::new(name).expect("GL entry name");
        unsafe { eglGetProcAddress(name.as_ptr()) }
    }
    pub(super) fn has_extension(&self, extension: &str) -> bool {
        let extensions = unsafe { eglQueryString(self.display, 0x3055) };
        !extensions.is_null()
            && unsafe { CStr::from_ptr(extensions) }
                .to_bytes()
                .split(|&v| v == b' ')
                .any(|v| v == extension.as_bytes())
    }
    pub(super) fn error(&self, operation: &'static str) -> Error {
        egl_error(operation)
    }
    pub(super) fn set_vsync(&self, enabled: bool) -> Result<()> {
        egl_ok("eglSwapInterval", unsafe {
            eglSwapInterval(self.display, i32::from(enabled))
        })
    }
    pub(super) fn present(&self) -> Result<()> {
        egl_ok("eglSwapBuffers", unsafe {
            eglSwapBuffers(self.display, self.surface)
        })
    }
}
fn load_module(path: &str) -> Result<i32> {
    let path = CString::new(path).map_err(|_| Error::Invalid("module path contains NUL"))?;
    let mut status = 0;
    let id = unsafe {
        sceKernelLoadStartModule(
            path.as_ptr(),
            0,
            std::ptr::null_mut(),
            0,
            std::ptr::null_mut(),
            &mut status,
        )
    };
    if id < 0 {
        Err(Error::Sdk("load module", id))
    } else if status < 0 {
        Err(Error::Sdk("start module", status))
    } else {
        Ok(id)
    }
}
fn set_path(slot: &mut [core::ffi::c_char; 256], path: &str) -> Result<()> {
    if path.len() >= slot.len() || path.as_bytes().contains(&0) {
        return Err(Error::Invalid("PVR module path"));
    }
    slot.fill(0);
    for (out, input) in slot.iter_mut().zip(path.bytes()) {
        *out = input as core::ffi::c_char;
    }
    Ok(())
}
fn egl_error(operation: &'static str) -> Error {
    Error::Egl(operation, unsafe { eglGetError() })
}
fn egl_ok(operation: &'static str, value: EGLBoolean) -> Result<()> {
    if value == EGL_TRUE {
        Ok(())
    } else {
        Err(egl_error(operation))
    }
}
