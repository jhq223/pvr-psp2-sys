//! EGL, OpenGL ES 2.0 and application-hint declarations for the PVR Vita driver.
//!
//! Initialize application hints with [`PVRSRVInitializeAppHint`] before changing
//! individual settings. [`PvrAppHint::default`] only zeroes the Rust storage; it
//! does not populate the driver's defaults.
use core::ffi::{c_char, c_float, c_int, c_uchar, c_uint, c_void};

pub type EGLBoolean = c_uint;
pub type EGLenum = c_uint;
pub type EGLint = c_int;
pub type EGLNativeDisplayType = c_int;
pub type EGLNativeWindowType = *mut c_void;
pub type EGLDisplay = *mut c_void;
pub type EGLSurface = *mut c_void;
pub type EGLContext = *mut c_void;
pub type EGLConfig = *mut c_void;

pub const EGL_FALSE: EGLBoolean = 0;
pub const EGL_TRUE: EGLBoolean = 1;
pub const EGL_DONT_CARE: EGLint = -1;
pub const EGL_SUCCESS: EGLint = 0x3000;
pub const EGL_BUFFER_SIZE: EGLint = 0x3020;
pub const EGL_ALPHA_SIZE: EGLint = 0x3021;
pub const EGL_BLUE_SIZE: EGLint = 0x3022;
pub const EGL_GREEN_SIZE: EGLint = 0x3023;
pub const EGL_RED_SIZE: EGLint = 0x3024;
pub const EGL_DEPTH_SIZE: EGLint = 0x3025;
pub const EGL_STENCIL_SIZE: EGLint = 0x3026;
pub const EGL_SURFACE_TYPE: EGLint = 0x3033;
pub const EGL_NONE: EGLint = 0x3038;
pub const EGL_RENDERABLE_TYPE: EGLint = 0x3040;
pub const EGL_PBUFFER_BIT: EGLint = 0x0001;
pub const EGL_WINDOW_BIT: EGLint = 0x0004;
pub const EGL_OPENGL_ES2_BIT: EGLint = 0x0004;
pub const EGL_CONTEXT_CLIENT_VERSION: EGLint = 0x3098;
pub const EGL_OPENGL_ES_API: EGLenum = 0x30a0;

pub type GLenum = c_uint;
pub type GLboolean = c_uchar;
pub type GLbitfield = c_uint;
pub type GLint = c_int;
pub type GLsizei = c_int;
pub type GLuint = c_uint;

pub const GL_FALSE: GLboolean = 0;
pub const GL_COLOR_BUFFER_BIT: GLbitfield = 0x0000_4000;
pub const GL_TRIANGLE_STRIP: GLenum = 0x0005;
pub const GL_FRAMEBUFFER: GLenum = 0x8d40;
pub const GL_COLOR_ATTACHMENT0: GLenum = 0x8ce0;
pub const GL_FRAMEBUFFER_COMPLETE: GLenum = 0x8cd5;
pub const GL_NO_ERROR: GLenum = 0;
pub const GL_UNPACK_ALIGNMENT: GLenum = 0x0cf5;
pub const GL_UNSIGNED_BYTE: GLenum = 0x1401;
pub const GL_FLOAT: GLenum = 0x1406;
pub const GL_RGBA: GLenum = 0x1908;
pub const GL_LUMINANCE: GLenum = 0x1909;
pub const GL_FRAGMENT_SHADER: GLenum = 0x8b30;
pub const GL_VERTEX_SHADER: GLenum = 0x8b31;
pub const GL_COMPILE_STATUS: GLenum = 0x8b81;
pub const GL_LINK_STATUS: GLenum = 0x8b82;
pub const GL_VENDOR: GLenum = 0x1f00;
pub const GL_RENDERER: GLenum = 0x1f01;
pub const GL_VERSION: GLenum = 0x1f02;
pub const GL_NEAREST: GLint = 0x2600;
pub const GL_TEXTURE_MAG_FILTER: GLenum = 0x2800;
pub const GL_TEXTURE_MIN_FILTER: GLenum = 0x2801;
pub const GL_TEXTURE_WRAP_S: GLenum = 0x2802;
pub const GL_TEXTURE_WRAP_T: GLenum = 0x2803;
pub const GL_TEXTURE0: GLenum = 0x84c0;
pub const GL_TEXTURE1: GLenum = 0x84c1;
pub const GL_CLAMP_TO_EDGE: GLint = 0x812f;
pub const GL_TEXTURE_2D: GLenum = 0x0de1;

#[repr(C)]
pub struct PvrAppHint {
    pub pds_frag_buffer_size: c_uint,
    pub param_buffer_size: c_uint,
    pub driver_memory_size: c_uint,
    pub external_z_buffer_mode: c_uint,
    pub external_z_buffer_x_size: c_uint,
    pub external_z_buffer_y_size: c_uint,
    pub dump_profile_data: c_int,
    pub profile_start_frame: c_uint,
    pub profile_end_frame: c_uint,
    pub disable_metrics_output: c_int,
    pub window_system: [c_char; 256],
    pub gles1: [c_char; 256],
    pub gles2: [c_char; 256],
    fields_before_flush_behaviour: [c_uint; 7],
    pub flush_behaviour: c_uint,
    fields_before_memory_speed_test: [c_uint; 12],
    pub enable_memory_speed_test: c_int,
    fields_before_sw_tex_op_cleanup_delay: [c_uint; 9],
    pub sw_tex_op_cleanup_delay: c_uint,
    fields_before_overload_tex_layout: [c_uint; 10],
    pub overload_tex_layout: c_uint,
    remaining_fields: [u8; 28],
}

const _: [(); 1004] = [(); core::mem::size_of::<PvrAppHint>()];
// Offsets follow the shipped driver's services.h, whose layout includes
// EnableMemorySpeedTest and EnableAppTextureDependency (the short public
// psp2_pvr_hint.h omits them). Never use that shorter header for this ABI.
const _: [(); 836] = [(); core::mem::offset_of!(PvrAppHint, flush_behaviour)];
const _: [(); 888] = [(); core::mem::offset_of!(PvrAppHint, enable_memory_speed_test)];
const _: [(); 928] = [(); core::mem::offset_of!(PvrAppHint, sw_tex_op_cleanup_delay)];
const _: [(); 972] = [(); core::mem::offset_of!(PvrAppHint, overload_tex_layout)];

pub const PVR_FLUSH_KICK_3D: c_uint = 2;
pub const PVR_TEXTURE_LAYOUT_STRIDE: c_uint = 0x11;

impl Default for PvrAppHint {
    fn default() -> Self {
        Self {
            pds_frag_buffer_size: 0,
            param_buffer_size: 0,
            driver_memory_size: 0,
            external_z_buffer_mode: 0,
            external_z_buffer_x_size: 0,
            external_z_buffer_y_size: 0,
            dump_profile_data: 0,
            profile_start_frame: 0,
            profile_end_frame: 0,
            disable_metrics_output: 0,
            window_system: [0; 256],
            gles1: [0; 256],
            gles2: [0; 256],
            fields_before_flush_behaviour: [0; 7],
            flush_behaviour: 0,
            fields_before_memory_speed_test: [0; 12],
            enable_memory_speed_test: 0,
            fields_before_sw_tex_op_cleanup_delay: [0; 9],
            sw_tex_op_cleanup_delay: 0,
            fields_before_overload_tex_layout: [0; 10],
            overload_tex_layout: 0,
            remaining_fields: [0; 28],
        }
    }
}

unsafe extern "C" {
    pub fn PVRSRVInitializeAppHint(hint: *mut PvrAppHint) -> c_uint;
    pub fn PVRSRVCreateVirtualAppHint(hint: *mut PvrAppHint) -> c_uint;

    pub fn eglGetDisplay(display_id: EGLNativeDisplayType) -> EGLDisplay;
    pub fn eglInitialize(display: EGLDisplay, major: *mut EGLint, minor: *mut EGLint)
    -> EGLBoolean;
    pub fn eglChooseConfig(
        display: EGLDisplay,
        attributes: *const EGLint,
        configs: *mut EGLConfig,
        config_size: EGLint,
        config_count: *mut EGLint,
    ) -> EGLBoolean;
    pub fn eglCreateWindowSurface(
        display: EGLDisplay,
        config: EGLConfig,
        window: EGLNativeWindowType,
        attributes: *const EGLint,
    ) -> EGLSurface;
    pub fn eglBindAPI(api: EGLenum) -> EGLBoolean;
    pub fn eglCreateContext(
        display: EGLDisplay,
        config: EGLConfig,
        shared_context: EGLContext,
        attributes: *const EGLint,
    ) -> EGLContext;
    pub fn eglMakeCurrent(
        display: EGLDisplay,
        draw: EGLSurface,
        read: EGLSurface,
        context: EGLContext,
    ) -> EGLBoolean;
    pub fn eglSwapInterval(display: EGLDisplay, interval: EGLint) -> EGLBoolean;
    pub fn eglSwapBuffers(display: EGLDisplay, surface: EGLSurface) -> EGLBoolean;
    pub fn eglDestroyContext(display: EGLDisplay, context: EGLContext) -> EGLBoolean;
    pub fn eglDestroySurface(display: EGLDisplay, surface: EGLSurface) -> EGLBoolean;
    pub fn eglTerminate(display: EGLDisplay) -> EGLBoolean;
    pub fn eglGetError() -> EGLint;
    pub fn eglGetProcAddress(name: *const c_char) -> *const c_void;

    pub fn glActiveTexture(texture: GLenum);
    pub fn glAttachShader(program: GLuint, shader: GLuint);
    pub fn glBindAttribLocation(program: GLuint, index: GLuint, name: *const c_char);
    pub fn glBindFramebuffer(target: GLenum, framebuffer: GLuint);
    pub fn glBindTexture(target: GLenum, texture: GLuint);
    pub fn glCheckFramebufferStatus(target: GLenum) -> GLenum;
    pub fn glClear(mask: GLbitfield);
    pub fn glClearColor(red: c_float, green: c_float, blue: c_float, alpha: c_float);
    pub fn glCompileShader(shader: GLuint);
    pub fn glCreateProgram() -> GLuint;
    pub fn glCreateShader(shader_type: GLenum) -> GLuint;
    pub fn glDeleteFramebuffers(count: GLsizei, framebuffers: *const GLuint);
    pub fn glDeleteProgram(program: GLuint);
    pub fn glDeleteShader(shader: GLuint);
    pub fn glDeleteTextures(count: GLsizei, textures: *const GLuint);
    pub fn glDrawArrays(mode: GLenum, first: GLint, count: GLsizei);
    pub fn glEnableVertexAttribArray(index: GLuint);
    pub fn glFramebufferTexture2D(
        target: GLenum,
        attachment: GLenum,
        texture_target: GLenum,
        texture: GLuint,
        level: GLint,
    );
    pub fn glGenFramebuffers(count: GLsizei, framebuffers: *mut GLuint);
    pub fn glGenTextures(count: GLsizei, textures: *mut GLuint);
    pub fn glGetError() -> GLenum;
    pub fn glGetProgramInfoLog(
        program: GLuint,
        capacity: GLsizei,
        length: *mut GLsizei,
        log: *mut c_char,
    );
    pub fn glGetProgramiv(program: GLuint, parameter: GLenum, value: *mut GLint);
    pub fn glGetShaderInfoLog(
        shader: GLuint,
        capacity: GLsizei,
        length: *mut GLsizei,
        log: *mut c_char,
    );
    pub fn glGetShaderiv(shader: GLuint, parameter: GLenum, value: *mut GLint);
    pub fn glGetString(name: GLenum) -> *const c_uchar;
    pub fn glGetUniformLocation(program: GLuint, name: *const c_char) -> GLint;
    pub fn glLinkProgram(program: GLuint);
    pub fn glPixelStorei(parameter: GLenum, value: GLint);
    pub fn glShaderSource(
        shader: GLuint,
        count: GLsizei,
        strings: *const *const c_char,
        lengths: *const GLint,
    );
    pub fn glTexImage2D(
        target: GLenum,
        level: GLint,
        internal_format: GLint,
        width: GLsizei,
        height: GLsizei,
        border: GLint,
        format: GLenum,
        pixel_type: GLenum,
        pixels: *const c_void,
    );
    pub fn glTexParameteri(target: GLenum, parameter: GLenum, value: GLint);
    pub fn glTexSubImage2D(
        target: GLenum,
        level: GLint,
        x: GLint,
        y: GLint,
        width: GLsizei,
        height: GLsizei,
        format: GLenum,
        pixel_type: GLenum,
        pixels: *const c_void,
    );
    pub fn glUniform1f(location: GLint, value: c_float);
    pub fn glUniform1i(location: GLint, value: GLint);
    pub fn glUseProgram(program: GLuint);
    pub fn glVertexAttribPointer(
        index: GLuint,
        size: GLint,
        element_type: GLenum,
        normalized: GLboolean,
        stride: GLsizei,
        pointer: *const c_void,
    );
    pub fn glViewport(x: GLint, y: GLint, width: GLsizei, height: GLsizei);
}
