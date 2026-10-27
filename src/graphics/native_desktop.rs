//! Owned desktop EGL pbuffer. Native handles stay inside the graphics boundary.
use super::*;
use khronos_egl as egl;

pub(super) struct Native {
    pub(super) failure: fault::State,
    pub(super) display: EGLDisplay,
    active: bool,
    initialized: bool,
    pub(super) context: EGLContext,
    surface: EGLSurface,
    size: [u32; 2],
    api: Option<egl::DynamicInstance<egl::EGL1_4>>,
}

impl Native {
    pub(super) fn headless(size: [u32; 2], vsync: bool) -> Result<Self> {
        if size.contains(&0) || size.iter().any(|&n| n > i32::MAX as u32) {
            return Err(Error::Invalid("pbuffer dimensions"));
        }
        let library = if cfg!(target_os = "windows") {
            "libEGL.dll"
        } else if cfg!(target_os = "macos") {
            "libEGL.dylib"
        } else {
            "libEGL.so.1"
        };
        let api =
            unsafe { egl::DynamicInstance::<egl::EGL1_4>::load_required_from_filename(library) }
                .map_err(|error| Error::Library {
                    library,
                    message: error.to_string(),
                })?;
        if ACTIVE
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .is_err()
        {
            return Err(Error::Invalid("a graphics context is already alive"));
        }
        let mut native = Self {
            failure: Default::default(),
            display: std::ptr::null_mut(),
            active: true,
            initialized: false,
            context: std::ptr::null_mut(),
            surface: std::ptr::null_mut(),
            size,
            api: Some(api),
        };
        // DEFAULT_DISPLAY lets the host's EGL platform select its existing display
        // connection; the surface itself is a pbuffer and never creates a window.
        let display = unsafe { native.api().get_display(egl::DEFAULT_DISPLAY) }
            .ok_or_else(|| native.error("eglGetDisplay"))?;
        native.display = display.as_ptr();
        native
            .api()
            .initialize(display)
            .map_err(error("eglInitialize"))?;
        native.initialized = true;
        native
            .api()
            .bind_api(egl::OPENGL_ES_API)
            .map_err(error("eglBindAPI"))?;
        let config = native
            .api()
            .choose_first_config(
                display,
                &[
                    egl::RED_SIZE,
                    8,
                    egl::GREEN_SIZE,
                    8,
                    egl::BLUE_SIZE,
                    8,
                    egl::ALPHA_SIZE,
                    8,
                    egl::DEPTH_SIZE,
                    0,
                    egl::STENCIL_SIZE,
                    0,
                    egl::SURFACE_TYPE,
                    egl::PBUFFER_BIT,
                    egl::RENDERABLE_TYPE,
                    egl::OPENGL_ES2_BIT,
                    egl::NONE,
                ],
            )
            .map_err(error("eglChooseConfig"))?
            .ok_or(Error::Unsupported("RGBA ES2 pbuffer configuration"))?;
        let surface = native
            .api()
            .create_pbuffer_surface(
                display,
                config,
                &[
                    egl::WIDTH,
                    size[0] as i32,
                    egl::HEIGHT,
                    size[1] as i32,
                    egl::NONE,
                ],
            )
            .map_err(error("eglCreatePbufferSurface"))?;
        native.surface = surface.as_ptr();
        let context = native
            .api()
            .create_context(
                display,
                config,
                None,
                &[egl::CONTEXT_CLIENT_VERSION, 2, egl::NONE],
            )
            .map_err(error("eglCreateContext"))?;
        native.context = context.as_ptr();
        native
            .api()
            .make_current(display, Some(surface), Some(surface), Some(context))
            .map_err(error("eglMakeCurrent"))?;
        native.set_vsync(vsync)?;
        // Report the actual allocated extent, including any host clamping.
        native.size = [
            native
                .api()
                .query_surface(display, surface, egl::WIDTH)
                .map_err(error("eglQuerySurface(width)"))? as u32,
            native
                .api()
                .query_surface(display, surface, egl::HEIGHT)
                .map_err(error("eglQuerySurface(height)"))? as u32,
        ];
        Ok(native)
    }

    fn api(&self) -> &egl::DynamicInstance<egl::EGL1_4> {
        self.api.as_ref().expect("owned EGL library")
    }
    fn display(&self) -> egl::Display {
        // Only handles returned by this retained EGL instance enter Native.
        unsafe { egl::Display::from_ptr(self.display) }
    }
    pub(super) fn size(&self) -> [u32; 2] {
        self.size
    }
    pub(super) fn proc_address(&self, name: &str) -> *const core::ffi::c_void {
        self.api()
            .get_proc_address(name)
            .map_or(std::ptr::null(), |address| {
                address as *const core::ffi::c_void
            })
    }
    pub(super) fn has_extension(&self, extension: &str) -> bool {
        self.api()
            .query_string(Some(self.display()), egl::EXTENSIONS)
            .is_ok_and(|extensions| {
                extensions
                    .to_bytes()
                    .split(|&v| v == b' ')
                    .any(|v| v == extension.as_bytes())
            })
    }
    pub(super) fn error(&self, operation: &'static str) -> Error {
        Error::Egl(
            operation,
            self.api().get_error().map_or(egl::SUCCESS, i32::from),
        )
    }
    pub(super) fn set_vsync(&self, enabled: bool) -> Result<()> {
        self.api()
            .swap_interval(self.display(), i32::from(enabled))
            .map_err(error("eglSwapInterval"))
    }
    pub(super) fn present(&self) -> Result<()> {
        let surface = unsafe { egl::Surface::from_ptr(self.surface) };
        self.api()
            .swap_buffers(self.display(), surface)
            .map_err(error("eglSwapBuffers"))
    }
    pub(super) fn close(&mut self) -> Result<()> {
        if let Some(fault) = self.failure.refresh() {
            return Err(Error::Terminal(fault));
        }
        if !self.context.is_null() {
            self.api()
                .make_current(self.display(), None, None, None)
                .map_err(error("eglMakeCurrent(close)"))?;
            let context = unsafe { egl::Context::from_ptr(self.context) };
            self.api()
                .destroy_context(self.display(), context)
                .map_err(error("eglDestroyContext"))?;
            self.context = std::ptr::null_mut();
        }
        if !self.surface.is_null() {
            let surface = unsafe { egl::Surface::from_ptr(self.surface) };
            self.api()
                .destroy_surface(self.display(), surface)
                .map_err(error("eglDestroySurface"))?;
            self.surface = std::ptr::null_mut();
        }
        if self.initialized {
            self.api()
                .terminate(self.display())
                .map_err(error("eglTerminate"))?;
            self.initialized = false;
        }
        self.display = std::ptr::null_mut();
        if self.active {
            self.api()
                .release_thread()
                .map_err(error("eglReleaseThread"))?;
            self.active = false;
            ACTIVE.store(false, Ordering::Release);
        }
        Ok(())
    }
}
impl Drop for Native {
    fn drop(&mut self) {
        if self.close().is_err() {
            // Native owners whose cleanup failed can still call into this library.
            std::mem::forget(self.api.take());
        }
    }
}
fn error(operation: &'static str) -> impl FnOnce(egl::Error) -> Error {
    move |error| Error::Egl(operation, error.into())
}
