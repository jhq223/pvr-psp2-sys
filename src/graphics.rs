//! Thread-bound PVR/EGL ownership and typed GLES 2 commands.
//! Upload calls consume borrowed CPU storage before returning. GPU object and
//! context lifetimes are retained by owned handles; callers retain their own
//! allocation accounting until the submission fence completes and handles drop.
use crate::gles::*;
use glow::HasContext;
use std::prelude::v1::*;
use std::{
    cell::Cell,
    ops::Range,
    rc::Rc,
    sync::atomic::{AtomicBool, Ordering},
};

static ACTIVE: AtomicBool = AtomicBool::new(false);

/// GPU command completion supplied by the active EGL driver.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompletionMode {
    Fence,
}
/// Native failures retain their operation and actual device error.
#[derive(Debug)]
pub enum Error {
    Invalid(&'static str),
    Unsupported(&'static str),
    Library {
        library: &'static str,
        message: String,
    },
    Sdk(&'static str, i32),
    Egl(&'static str, i32),
    Gl(&'static str, u32),
    Shader {
        stage: &'static str,
        log: String,
    },
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}
impl std::error::Error for Error {}
type Result<T> = std::result::Result<T, Error>;

/// Capabilities queried once from this context's actual driver.
#[derive(Clone, Debug)]
pub struct Capabilities {
    pub max_texture_size: u32,
    pub max_texture_units: u32,
    pub max_vertex_attributes: u32,
    pub max_fragment_uniform_vectors: u32,
    pub bc: bool,
    pub bgra: bool,
    pub half_float: bool,
    pub indices_u32: bool,
    pub shader_binary: bool,
    pub completion: CompletionMode,
}
#[cfg(target_os = "vita")]
#[path = "graphics/native_vita.rs"]
mod native;
#[cfg(not(target_os = "vita"))]
#[path = "graphics/native_desktop.rs"]
mod native;
use native::Native;

type Sync = *mut core::ffi::c_void;
type CreateSync = unsafe extern "system" fn(EGLDisplay, u32, *const i32) -> Sync;
type DestroySync = unsafe extern "system" fn(EGLDisplay, Sync) -> u32;
type WaitSync = unsafe extern "system" fn(EGLDisplay, Sync, i32, u64) -> i32;
#[derive(Clone, Copy)]
struct FenceApi {
    create: CreateSync,
    destroy: DestroySync,
    wait: WaitSync,
}
/// Driver strings queried from the current OpenGL ES context.
#[derive(Clone, Debug)]
pub struct DriverInfo {
    pub vendor: String,
    pub renderer: String,
    pub version: String,
    pub shading_language: String,
}
struct Inner {
    gl: glow::Context,
    caps: Capabilities,
    native: Native,
    vsync: Cell<bool>,
    fences: FenceApi,
    enabled_attributes: Vec<Cell<bool>>,
}
/// An owned GPU command fence; polling performs one native nonblocking query.
pub struct Fence {
    owner: Rc<Inner>,
    sync: Sync,
}
impl Fence {
    /// Returns true only when all commands preceding this fence have completed.
    pub fn poll(&self) -> Result<bool> {
        self.wait_ns(0)
    }
    /// Wait at most the requested duration; false means the deadline expired.
    pub fn wait(&self, timeout: std::time::Duration) -> Result<bool> {
        self.wait_ns(timeout.as_nanos().min(u128::from(u64::MAX - 1)) as u64)
    }
    fn wait_ns(&self, timeout: u64) -> Result<bool> {
        let api = self.owner.fences;
        let status = unsafe { (api.wait)(self.owner.native.display, self.sync, 0, timeout) };
        match status {
            0x30f6 => Ok(true),
            0x30f5 => Ok(false),
            _ => Err(self.owner.native.error("eglClientWaitSyncKHR")),
        }
    }
}
impl Drop for Fence {
    fn drop(&mut self) {
        unsafe {
            (self.owner.fences.destroy)(self.owner.native.display, self.sync);
        }
    }
}

impl Drop for Inner {
    fn drop(&mut self) {
        if !self.native.context.is_null() {
            unsafe {
                self.gl.finish();
            }
        }
    }
}
/// The sole live native context. Rc ownership makes all derived resources
/// !Send and !Sync and keeps EGL alive until the last resource is destroyed.
#[derive(Clone)]
pub struct Context(Rc<Inner>);
impl Context {
    /// Load matching PVR modules from a directory such as `app0:module`.
    #[cfg(target_os = "vita")]
    pub fn new(module_directory: &str, vsync: bool) -> Result<Self> {
        Self::from_native(Native::new(module_directory, vsync)?, vsync)
    }
    /// Create a thread-bound desktop OpenGL ES context and an RGBA8 pbuffer.
    /// The host EGL implementation must provide ES2 and EGL_KHR_fence_sync.
    /// No native window is created. Pbuffer swap interval does not pace frames;
    /// callers use submission fences to measure completed work.
    #[cfg(not(target_os = "vita"))]
    pub fn headless(size: [u32; 2], vsync: bool) -> Result<Self> {
        Self::from_native(Native::headless(size, vsync)?, vsync)
    }
    fn from_native(native: Native, vsync: bool) -> Result<Self> {
        let gl = unsafe { glow::Context::from_loader_function(|name| native.proc_address(name)) };
        let has_fence = native.has_extension("EGL_KHR_fence_sync");
        let fences = if has_fence {
            let create = native.proc_address("eglCreateSyncKHR");
            let destroy = native.proc_address("eglDestroySyncKHR");
            let wait = native.proc_address("eglClientWaitSyncKHR");
            if create.is_null() || destroy.is_null() || wait.is_null() {
                return Err(Error::Unsupported("incomplete EGL fence entry points"));
            }
            unsafe {
                FenceApi {
                    create: std::mem::transmute::<*const core::ffi::c_void, CreateSync>(create),
                    destroy: std::mem::transmute::<*const core::ffi::c_void, DestroySync>(destroy),
                    wait: std::mem::transmute::<*const core::ffi::c_void, WaitSync>(wait),
                }
            }
        } else {
            return Err(Error::Unsupported("EGL_KHR_fence_sync is required"));
        };
        let ext = gl.supported_extensions();
        let caps = unsafe {
            Capabilities {
                max_texture_size: gl.get_parameter_i32(glow::MAX_TEXTURE_SIZE).max(0) as u32,
                max_texture_units: gl.get_parameter_i32(glow::MAX_TEXTURE_IMAGE_UNITS).max(0)
                    as u32,
                max_vertex_attributes: gl.get_parameter_i32(glow::MAX_VERTEX_ATTRIBS).max(0) as u32,
                max_fragment_uniform_vectors: gl
                    .get_parameter_i32(glow::MAX_FRAGMENT_UNIFORM_VECTORS)
                    .max(0) as u32,
                bc: if cfg!(target_os = "vita") {
                    ext.contains("GL_KRKR_texture_compression_bc")
                } else {
                    ext.contains("GL_EXT_texture_compression_s3tc")
                        || (ext.contains("GL_EXT_texture_compression_dxt1")
                            && ext.contains("GL_ANGLE_texture_compression_dxt5"))
                },
                bgra: ext.contains("GL_EXT_texture_format_BGRA8888")
                    || ext.contains("GL_IMG_texture_format_BGRA8888"),
                half_float: ext.contains("GL_OES_texture_half_float")
                    && ext.contains("GL_OES_texture_half_float_linear"),
                indices_u32: ext.contains("GL_OES_element_index_uint"),
                shader_binary: ext.contains("GL_IMG_shader_binary"),
                completion: CompletionMode::Fence,
            }
        };
        let context = Self(Rc::new(Inner {
            enabled_attributes: (0..caps.max_vertex_attributes)
                .map(|_| Cell::new(false))
                .collect(),
            gl,
            caps,
            native,
            vsync: Cell::new(vsync),
            fences,
        }));
        context.check("context initialization")?;
        Ok(context)
    }
    /// Close only after every resource/fence owner is released. A rejected or
    /// failed close returns the same context, retaining remaining native owners.
    pub fn close(mut self) -> std::result::Result<(), (Self, Error)> {
        let Some(inner) = Rc::get_mut(&mut self.0) else {
            return Err((self, Error::Invalid("graphics resources still own context")));
        };
        if !inner.native.context.is_null() {
            unsafe {
                inner.gl.finish();
            }
        }
        if let Err(error) = inner.native.close() {
            return Err((self, error));
        }
        Ok(())
    }
    /// Actual limits and formats queried from this native context.
    pub fn capabilities(&self) -> &Capabilities {
        &self.0.caps
    }
    /// Physical window or pbuffer dimensions.
    pub fn output_size(&self) -> [u32; 2] {
        self.0.native.size()
    }
    /// Identify the actual driver used for capability and performance reports.
    pub fn driver_info(&self) -> Result<DriverInfo> {
        self.open()?;
        let info = unsafe {
            DriverInfo {
                vendor: self.0.gl.get_parameter_string(glow::VENDOR),
                renderer: self.0.gl.get_parameter_string(glow::RENDERER),
                version: self.0.gl.get_parameter_string(glow::VERSION),
                shading_language: self
                    .0
                    .gl
                    .get_parameter_string(glow::SHADING_LANGUAGE_VERSION),
            }
        };
        self.check("driver information")?;
        Ok(info)
    }
    /// Currently accepted native swap interval.
    pub fn vsync(&self) -> bool {
        self.0.vsync.get()
    }
    pub fn set_vsync(&self, enabled: bool) -> Result<()> {
        self.open()?;
        self.0.native.set_vsync(enabled)?;
        self.0.vsync.set(enabled);
        Ok(())
    }
    /// Native buffer exchange. It is independent of GPU completion accounting.
    pub fn present(&self) -> Result<()> {
        self.open()?;
        self.0.native.present()
    }
    /// Insert a hardware fence for preceding graphics commands.
    pub fn fence(&self) -> Result<Fence> {
        self.open()?;
        let api = self.0.fences;
        let sync = unsafe { (api.create)(self.0.native.display, 0x30f9, [EGL_NONE].as_ptr()) };
        if sync.is_null() {
            return Err(self.0.native.error("eglCreateSyncKHR"));
        }
        Ok(Fence {
            owner: self.0.clone(),
            sync,
        })
    }
    /// A real GPU completion boundary for all preceding commands in this context.
    pub fn finish(&self) -> Result<()> {
        self.open()?;
        unsafe {
            self.0.gl.finish();
        }
        self.check("glFinish")
    }
    /// Submit queued commands without waiting for their completion.
    pub fn flush(&self) -> Result<()> {
        self.open()?;
        unsafe {
            self.0.gl.flush();
        }
        self.check("glFlush")
    }
    pub fn check(&self, operation: &'static str) -> Result<()> {
        self.open()?;
        let code = unsafe { self.0.gl.get_error() };
        if code == glow::NO_ERROR {
            Ok(())
        } else {
            Err(Error::Gl(operation, code))
        }
    }
    fn same(&self, owner: &Rc<Inner>) -> Result<()> {
        self.open()?;
        if Rc::ptr_eq(&self.0, owner) {
            Ok(())
        } else {
            Err(Error::Invalid("resource belongs to another context"))
        }
    }
    pub fn create_texture(&self, size: [u32; 2], format: TextureFormat) -> Result<Texture> {
        self.validate_size(size)?;
        let (internal, external, sample) = self.pixel_format(format)?;
        if format.is_compressed() {
            return Err(Error::Invalid("compressed texture requires block upload"));
        }
        let name = unsafe {
            self.0
                .gl
                .create_texture()
                .map_err(|_| Error::Invalid("texture allocation"))?
        };
        let texture = Texture(Rc::new(TextureInner {
            owner: self.0.clone(),
            name,
            size,
            format,
            filter: Cell::new(TextureFilter::Linear),
        }));
        unsafe {
            let gl = &self.0.gl;
            gl.bind_texture(glow::TEXTURE_2D, Some(name));
            gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 1);
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_MIN_FILTER,
                glow::LINEAR as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_MAG_FILTER,
                glow::LINEAR as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_WRAP_S,
                glow::CLAMP_TO_EDGE as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_WRAP_T,
                glow::CLAMP_TO_EDGE as i32,
            );
            gl.tex_image_2d(
                glow::TEXTURE_2D,
                0,
                internal,
                size[0] as i32,
                size[1] as i32,
                0,
                external,
                sample,
                glow::PixelUnpackData::Slice(None),
            );
        }
        self.check("texture allocation")?;
        Ok(texture)
    }
    /// Upload BC1/BC3 blocks without RGBA expansion. Vita reorders blocks into
    /// caller-accounted staging; desktop EGL consumes standard S3TC blocks.
    /// Both paths validate the same payload and staging extent.
    pub fn create_bc_texture(
        &self,
        size: [u32; 2],
        format: TextureFormat,
        blocks: &[u8],
        staging: &mut [u8],
    ) -> Result<Texture> {
        self.validate_size(size)?;
        if !self.0.caps.bc {
            return Err(Error::Unsupported("native BC compression"));
        }
        if !format.is_compressed() || size.iter().any(|&n| n < 8 || !n.is_power_of_two()) {
            return Err(Error::Invalid("BC requires power-of-two dimensions >=8"));
        }
        let block = if format == TextureFormat::Bc1Rgb {
            8
        } else {
            16
        };
        let bytes = (size[0] as usize / 4)
            .checked_mul(size[1] as usize / 4)
            .and_then(|n| n.checked_mul(block))
            .ok_or(Error::Invalid("BC extent"))?;
        if blocks.len() != bytes || staging.len() != bytes {
            return Err(Error::Invalid("BC payload extent"));
        }
        #[cfg(target_os = "vita")]
        let (compressed_format, payload) = {
            reorder_blocks(size, block, blocks, staging);
            (if block == 8 { 0x60000001 } else { 0x60000002 }, &*staging)
        };
        #[cfg(not(target_os = "vita"))]
        let (compressed_format, payload) = (
            if block == 8 {
                glow::COMPRESSED_RGB_S3TC_DXT1_EXT as i32
            } else {
                glow::COMPRESSED_RGBA_S3TC_DXT5_EXT as i32
            },
            blocks,
        );
        let name = unsafe {
            self.0
                .gl
                .create_texture()
                .map_err(|_| Error::Invalid("texture allocation"))?
        };
        let texture = Texture(Rc::new(TextureInner {
            owner: self.0.clone(),
            name,
            size,
            format,
            filter: Cell::new(TextureFilter::Linear),
        }));
        unsafe {
            let gl = &self.0.gl;
            gl.bind_texture(glow::TEXTURE_2D, Some(name));
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_MIN_FILTER,
                glow::LINEAR as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_MAG_FILTER,
                glow::LINEAR as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_WRAP_S,
                glow::CLAMP_TO_EDGE as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_WRAP_T,
                glow::CLAMP_TO_EDGE as i32,
            );
            gl.compressed_tex_image_2d(
                glow::TEXTURE_2D,
                0,
                compressed_format,
                size[0] as i32,
                size[1] as i32,
                0,
                bytes as i32,
                payload,
            );
        }
        self.check("compressed texture upload")?;
        Ok(texture)
    }
    /// Upload visible rows, respecting producer padding without hidden copies.
    pub fn upload(
        &self,
        texture: &Texture,
        origin: [u32; 2],
        size: [u32; 2],
        bytes: &[u8],
        stride: usize,
    ) -> Result<()> {
        self.same(&texture.0.owner)?;
        let (_, format, sample) = self.pixel_format(texture.0.format)?;
        let row = (size[0] as usize)
            .checked_mul(texture.0.format.pixel_bytes())
            .ok_or(Error::Invalid("texture row"))?;
        let span = if size[1] == 0 {
            0
        } else {
            (size[1] as usize - 1)
                .checked_mul(stride)
                .and_then(|n| n.checked_add(row))
                .ok_or(Error::Invalid("texture span"))?
        };
        if texture.0.format.is_compressed()
            || size.contains(&0)
            || stride < row
            || bytes.len() < span
            || (0..2).any(|i| {
                origin[i]
                    .checked_add(size[i])
                    .is_none_or(|n| n > texture.0.size[i])
            })
        {
            return Err(Error::Invalid("texture upload bounds"));
        }
        unsafe {
            let gl = &self.0.gl;
            gl.bind_texture(glow::TEXTURE_2D, Some(texture.0.name));
            gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 1);
            if row == stride {
                gl.tex_sub_image_2d(
                    glow::TEXTURE_2D,
                    0,
                    origin[0] as i32,
                    origin[1] as i32,
                    size[0] as i32,
                    size[1] as i32,
                    format,
                    sample,
                    glow::PixelUnpackData::Slice(Some(&bytes[..span])),
                );
            } else {
                for y in 0..size[1] as usize {
                    gl.tex_sub_image_2d(
                        glow::TEXTURE_2D,
                        0,
                        origin[0] as i32,
                        (origin[1] + y as u32) as i32,
                        size[0] as i32,
                        1,
                        format,
                        sample,
                        glow::PixelUnpackData::Slice(Some(&bytes[y * stride..y * stride + row])),
                    );
                }
            }
        }
        self.check("texture row upload")
    }
    /// Set repeat behavior for one sampled image before the ordered draw.
    pub fn set_repeat(&self, texture: &Texture, repeat: [bool; 2]) -> Result<()> {
        self.same(&texture.0.owner)?;
        unsafe {
            let gl = &self.0.gl;
            gl.bind_texture(glow::TEXTURE_2D, Some(texture.0.name));
            for (axis, enabled) in [
                (glow::TEXTURE_WRAP_S, repeat[0]),
                (glow::TEXTURE_WRAP_T, repeat[1]),
            ] {
                gl.tex_parameter_i32(
                    glow::TEXTURE_2D,
                    axis,
                    if enabled {
                        glow::REPEAT
                    } else {
                        glow::CLAMP_TO_EDGE
                    } as i32,
                );
            }
        }
        self.check("texture repeat")
    }
    pub fn framebuffer(&self, texture: &Texture) -> Result<Framebuffer> {
        self.same(&texture.0.owner)?;
        if texture.0.format != TextureFormat::Rgba8 {
            return Err(Error::Unsupported("color attachment format"));
        }
        let name = unsafe {
            self.0
                .gl
                .create_framebuffer()
                .map_err(|_| Error::Invalid("framebuffer allocation"))?
        };
        let target = Framebuffer(Rc::new(FramebufferInner {
            owner: self.0.clone(),
            name,
            texture: texture.clone(),
        }));
        unsafe {
            let gl = &self.0.gl;
            gl.bind_framebuffer(glow::FRAMEBUFFER, Some(name));
            gl.framebuffer_texture_2d(
                glow::FRAMEBUFFER,
                glow::COLOR_ATTACHMENT0,
                glow::TEXTURE_2D,
                Some(texture.0.name),
                0,
            );
            if gl.check_framebuffer_status(glow::FRAMEBUFFER) != glow::FRAMEBUFFER_COMPLETE {
                return Err(Error::Unsupported("incomplete color framebuffer"));
            }
        }
        self.check("framebuffer attachment")?;
        Ok(target)
    }
    /// Load the supplied target shader payloads, bind attributes and link an owned program.
    pub fn create_program(
        &self,
        vertex: ShaderCode<'_>,
        fragment: ShaderCode<'_>,
        attributes: &[AttributeBinding<'_>],
    ) -> Result<Program> {
        self.open()?;
        let gl = &self.0.gl;
        let name = unsafe {
            gl.create_program()
                .map_err(|_| Error::Invalid("program allocation"))?
        };
        let program = Program(Rc::new(ProgramInner {
            owner: self.0.clone(),
            name,
        }));
        for (stage, label, code) in [
            (glow::VERTEX_SHADER, "vertex", vertex),
            (glow::FRAGMENT_SHADER, "fragment", fragment),
        ] {
            let shader = unsafe {
                gl.create_shader(stage)
                    .map_err(|_| Error::Invalid("shader allocation"))?
            };
            let result = (|| {
                unsafe {
                    match code {
                        #[cfg(not(target_os = "vita"))]
                        ShaderCode::Source(source) => {
                            gl.shader_source(shader, source);
                            gl.compile_shader(shader);
                        }
                        ShaderCode::SgxBinary(bytes) => {
                            if !self.0.caps.shader_binary || bytes.len() > i32::MAX as usize {
                                return Err(Error::Unsupported("SGX shader binary"));
                            }
                            type Load = unsafe extern "system" fn(
                                i32,
                                *const u32,
                                u32,
                                *const core::ffi::c_void,
                                i32,
                            );
                            let address = self.0.native.proc_address("glShaderBinary");
                            if address.is_null() {
                                return Err(Error::Unsupported("glShaderBinary"));
                            }
                            let load: Load = std::mem::transmute(address);
                            load(
                                1,
                                &shader.0.get(),
                                0x8c0a,
                                bytes.as_ptr().cast(),
                                bytes.len() as i32,
                            );
                        }
                    }
                    if !gl.get_shader_compile_status(shader) {
                        return Err(Error::Shader {
                            stage: label,
                            log: gl.get_shader_info_log(shader),
                        });
                    }
                    gl.attach_shader(name, shader);
                }
                Ok(())
            })();
            unsafe {
                gl.delete_shader(shader);
            }
            result?;
        }
        unsafe {
            for attribute in attributes {
                if attribute.location >= self.0.caps.max_vertex_attributes {
                    return Err(Error::Invalid("attribute location"));
                }
                gl.bind_attrib_location(name, attribute.location, attribute.name);
            }
            gl.link_program(name);
            if !gl.get_program_link_status(name) {
                return Err(Error::Shader {
                    stage: "link",
                    log: gl.get_program_info_log(name),
                });
            }
        }
        self.check("program link")?;
        Ok(program)
    }
    /// Interleaved f32 attributes, validated once before GPU upload.
    pub fn vertex_buffer(
        &self,
        data: &[f32],
        stride: u32,
        attributes: &[VertexAttribute],
    ) -> Result<VertexBuffer> {
        self.open()?;
        if stride == 0
            || stride > i32::MAX as u32 / 4
            || std::mem::size_of_val(data) > i32::MAX as usize
            || !data.len().is_multiple_of(stride as usize)
            || attributes.iter().any(|a| {
                a.components == 0
                    || a.components > 4
                    || a.location >= self.0.caps.max_vertex_attributes
                    || a.offset
                        .checked_add(a.components)
                        .is_none_or(|n| n > stride)
            })
        {
            return Err(Error::Invalid("vertex layout"));
        }
        let name = unsafe {
            self.0
                .gl
                .create_buffer()
                .map_err(|_| Error::Invalid("vertex buffer allocation"))?
        };
        let out = VertexBuffer {
            owner: self.0.clone(),
            name,
            count: data.len() / stride as usize,
            capacity: data.len(),
            stride,
            attributes: attributes.to_vec(),
        };
        unsafe {
            self.0.gl.bind_buffer(glow::ARRAY_BUFFER, Some(name));
            self.0.gl.buffer_data_u8_slice(
                glow::ARRAY_BUFFER,
                float_bytes(data),
                glow::STATIC_DRAW,
            );
        }
        self.check("vertex upload")?;
        Ok(out)
    }
    pub fn index_buffer(&self, indices: &[u32]) -> Result<IndexBuffer> {
        self.open()?;
        if std::mem::size_of_val(indices) > i32::MAX as usize {
            return Err(Error::Invalid("index buffer extent"));
        }
        if !self.0.caps.indices_u32 {
            return Err(Error::Unsupported("32-bit element indices"));
        }
        let name = unsafe {
            self.0
                .gl
                .create_buffer()
                .map_err(|_| Error::Invalid("index buffer allocation"))?
        };
        let out = IndexBuffer {
            owner: self.0.clone(),
            name,
            count: indices.len(),
            capacity: indices.len(),
            maximum: indices.iter().copied().max().unwrap_or(0),
        };
        unsafe {
            self.0
                .gl
                .bind_buffer(glow::ELEMENT_ARRAY_BUFFER, Some(name));
            let data = std::slice::from_raw_parts(
                indices.as_ptr().cast::<u8>(),
                std::mem::size_of_val(indices),
            );
            self.0
                .gl
                .buffer_data_u8_slice(glow::ELEMENT_ARRAY_BUFFER, data, glow::STATIC_DRAW);
        }
        self.check("index upload")?;
        Ok(out)
    }
    /// Replace active vertices within the original allocation and layout.
    /// Reuse completed submissions to avoid an implicit driver wait. Allocation
    /// capacity is unchanged; draws are validated against the new active count.
    pub fn update_vertices(&self, buffer: &mut VertexBuffer, data: &[f32]) -> Result<()> {
        self.open()?;
        if !Rc::ptr_eq(&self.0, &buffer.owner)
            || data.len() > buffer.capacity
            || !data.len().is_multiple_of(buffer.stride as usize)
        {
            return Err(Error::Invalid("vertex update extent or owner"));
        }
        unsafe {
            self.0.gl.bind_buffer(glow::ARRAY_BUFFER, Some(buffer.name));
            self.0
                .gl
                .buffer_sub_data_u8_slice(glow::ARRAY_BUFFER, 0, float_bytes(data));
        }
        self.check("vertex update")?;
        buffer.count = data.len() / buffer.stride as usize;
        Ok(())
    }
    /// Replace active u32 indices without reallocating their storage. The maximum
    /// index and active count are updated together after a successful upload.
    pub fn update_indices(&self, buffer: &mut IndexBuffer, indices: &[u32]) -> Result<()> {
        self.open()?;
        if !Rc::ptr_eq(&self.0, &buffer.owner) || indices.len() > buffer.capacity {
            return Err(Error::Invalid("index update extent or owner"));
        }
        unsafe {
            self.0
                .gl
                .bind_buffer(glow::ELEMENT_ARRAY_BUFFER, Some(buffer.name));
            let data = std::slice::from_raw_parts(
                indices.as_ptr().cast::<u8>(),
                std::mem::size_of_val(indices),
            );
            self.0
                .gl
                .buffer_sub_data_u8_slice(glow::ELEMENT_ARRAY_BUFFER, 0, data);
        }
        self.check("index update")?;
        buffer.count = indices.len();
        buffer.maximum = indices.iter().copied().max().unwrap_or(0);
        Ok(())
    }
    pub fn clear(&self, target: Target<'_>, color: [f32; 4]) -> Result<()> {
        self.bind_target(target)?;
        unsafe {
            let gl = &self.0.gl;
            gl.disable(glow::SCISSOR_TEST);
            gl.color_mask(true, true, true, true);
            gl.clear_color(color[0], color[1], color[2], color[3]);
            gl.clear(glow::COLOR_BUFFER_BIT);
        }
        self.check("clear")
    }
    /// Draw with linear filtering, including after a preceding nearest-filtered
    /// draw. No client pointers or GL names escape.
    pub fn draw(&self, draw: Draw<'_>) -> Result<()> {
        self.draw_filtered(draw, TextureFilter::Linear)
    }
    /// Bind one hardware filtering mode for all sampled textures in this draw.
    ///
    /// Filtering belongs to the draw, not the immutable texture cache identity.
    /// Texture state is updated only when its mode changes. Subsequent ordinary
    /// `draw` calls explicitly restore linear filtering for internal consumers.
    pub fn draw_filtered(&self, draw: Draw<'_>, filter: TextureFilter) -> Result<()> {
        self.same(&draw.program.0.owner)?;
        self.same(&draw.vertices.owner)?;
        self.validate_size(draw.size)?;
        self.bind_target(draw.target)?;
        if draw.scissor.is_some_and(|[x, y, w, h]| {
            x.checked_add(w).is_none_or(|n| n > draw.size[0])
                || y.checked_add(h).is_none_or(|n| n > draw.size[1])
        }) {
            return Err(Error::Invalid("draw scissor"));
        }
        if draw.range.start > draw.range.end || draw.range.end > i32::MAX as u32 {
            return Err(Error::Invalid("draw range"));
        }
        if draw.textures.len() > self.0.caps.max_texture_units as usize {
            return Err(Error::Unsupported("texture unit count"));
        }
        for texture in draw.textures {
            self.same(&texture.0.owner)?;
            if let Target::Framebuffer(target) = draw.target
                && Rc::ptr_eq(&texture.0, &target.0.texture.0)
            {
                return Err(Error::Invalid("framebuffer texture feedback"));
            }
        }
        if let Some(indices) = draw.indices {
            self.same(&indices.owner)?;
            if draw.range.end as usize > indices.count
                || (draw.range.start != draw.range.end
                    && indices.maximum as usize >= draw.vertices.count)
            {
                return Err(Error::Invalid("indexed vertex bounds"));
            }
        } else if draw.range.end as usize > draw.vertices.count {
            return Err(Error::Invalid("vertex range"));
        }
        unsafe {
            let gl = &self.0.gl;
            gl.viewport(0, 0, draw.size[0] as i32, draw.size[1] as i32);
            gl.disable(glow::DEPTH_TEST);
            gl.disable(glow::CULL_FACE);
            gl.color_mask(true, true, true, true);
            if let Some([x, y, w, h]) = draw.scissor {
                gl.enable(glow::SCISSOR_TEST);
                gl.scissor(x as i32, y as i32, w as i32, h as i32);
            } else {
                gl.disable(glow::SCISSOR_TEST);
            }
            match draw.blend {
                Blend::Replace => gl.disable(glow::BLEND),
                Blend::Premultiplied => {
                    gl.enable(glow::BLEND);
                    gl.blend_equation(glow::FUNC_ADD);
                    gl.blend_func(glow::ONE, glow::ONE_MINUS_SRC_ALPHA);
                }
            }
            gl.use_program(Some(draw.program.0.name));
            for (unit, texture) in draw.textures.iter().enumerate() {
                gl.active_texture(glow::TEXTURE0 + unit as u32);
                gl.bind_texture(glow::TEXTURE_2D, Some(texture.0.name));
                if texture.0.filter.get() != filter {
                    let mode = match filter {
                        TextureFilter::Nearest => glow::NEAREST,
                        TextureFilter::Linear => glow::LINEAR,
                    };
                    // The validated texture belongs to this current context.
                    // GLES2 stores min/mag filtering on the texture object.
                    gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MIN_FILTER, mode as i32);
                    gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_MAG_FILTER, mode as i32);
                    texture.0.filter.set(filter);
                }
            }
            for uniform in draw.uniforms {
                self.uniform(draw.program, *uniform)?;
            }
            gl.bind_buffer(glow::ARRAY_BUFFER, Some(draw.vertices.name));
            // This context owns all attribute state. Keep unchanged layouts
            // enabled instead of invalidating the driver's vertex fetch state.
            for (index, enabled) in self.0.enabled_attributes.iter().enumerate() {
                let wanted = draw
                    .vertices
                    .attributes
                    .iter()
                    .any(|a| a.location as usize == index);
                if enabled.get() != wanted {
                    if wanted {
                        gl.enable_vertex_attrib_array(index as u32);
                    } else {
                        gl.disable_vertex_attrib_array(index as u32);
                    }
                    enabled.set(wanted);
                }
            }
            for a in &draw.vertices.attributes {
                gl.vertex_attrib_pointer_f32(
                    a.location,
                    a.components as i32,
                    glow::FLOAT,
                    false,
                    (draw.vertices.stride * 4) as i32,
                    (a.offset * 4) as i32,
                );
            }
            let count = (draw.range.end - draw.range.start) as i32;
            if let Some(indices) = draw.indices {
                gl.bind_buffer(glow::ELEMENT_ARRAY_BUFFER, Some(indices.name));
                gl.draw_elements(
                    glow::TRIANGLES,
                    count,
                    glow::UNSIGNED_INT,
                    (draw.range.start * 4) as i32,
                );
            } else {
                gl.bind_buffer(glow::ELEMENT_ARRAY_BUFFER, None);
                gl.draw_arrays(glow::TRIANGLES, draw.range.start as i32, count);
            }
        }
        self.check("draw")
    }
    /// RGBA8 readback uses native bottom-up rows. The output is caller-owned.
    pub fn read_rgba(&self, target: &Framebuffer, output: &mut [u8]) -> Result<()> {
        let size = target.size();
        let bytes = size[0] as u64 * size[1] as u64 * 4;
        if bytes != output.len() as u64 {
            return Err(Error::Invalid("readback output extent"));
        }
        self.bind_target(Target::Framebuffer(target))?;
        unsafe {
            self.0.gl.pixel_store_i32(glow::PACK_ALIGNMENT, 1);
            self.0.gl.read_pixels(
                0,
                0,
                size[0] as i32,
                size[1] as i32,
                glow::RGBA,
                glow::UNSIGNED_BYTE,
                glow::PixelPackData::Slice(Some(output)),
            );
        }
        self.check("readback")
    }
    fn bind_target(&self, target: Target<'_>) -> Result<()> {
        self.open()?;
        let name = match target {
            Target::Screen => None,
            Target::Framebuffer(target) => {
                self.same(&target.0.owner)?;
                Some(target.0.name)
            }
        };
        unsafe {
            self.0.gl.bind_framebuffer(glow::FRAMEBUFFER, name);
        }
        Ok(())
    }
    fn uniform(&self, program: &Program, value: UniformValue<'_>) -> Result<()> {
        let (uniform, data) = value.parts();
        if !Rc::ptr_eq(&uniform.program.0, &program.0) {
            return Err(Error::Invalid("uniform program ownership"));
        }
        let gl = &self.0.gl;
        let location = Some(&uniform.location);
        unsafe {
            match data {
                UniformData::Int(value) => gl.uniform_1_i32(location, value),
                UniformData::Float(value) => gl.uniform_1_f32(location, value),
                UniformData::Vec2(value) => gl.uniform_2_f32_slice(location, &value),
                UniformData::Vec3(value) => gl.uniform_3_f32_slice(location, &value),
                UniformData::Vec4(value) => gl.uniform_4_f32_slice(location, &value),
                UniformData::Vec4Array(value) => gl.uniform_4_f32_slice(
                    location,
                    std::slice::from_raw_parts(value.as_ptr().cast::<f32>(), value.len() * 4),
                ),
            }
        }
        Ok(())
    }
    fn open(&self) -> Result<()> {
        if self.0.native.context.is_null() {
            Err(Error::Invalid("graphics context closing"))
        } else {
            Ok(())
        }
    }
    fn validate_size(&self, size: [u32; 2]) -> Result<()> {
        self.open()?;
        if size.contains(&0) || size.iter().any(|&n| n > self.0.caps.max_texture_size) {
            Err(Error::Invalid("texture dimensions"))
        } else {
            Ok(())
        }
    }
    fn pixel_format(&self, format: TextureFormat) -> Result<(i32, u32, u32)> {
        let value = match format {
            TextureFormat::Rgba8 => (glow::RGBA as i32, glow::RGBA, glow::UNSIGNED_BYTE),
            TextureFormat::Luma8 => (glow::LUMINANCE as i32, glow::LUMINANCE, glow::UNSIGNED_BYTE),
            TextureFormat::LumaAlpha8 => (
                glow::LUMINANCE_ALPHA as i32,
                glow::LUMINANCE_ALPHA,
                glow::UNSIGNED_BYTE,
            ),
            TextureFormat::Bgra8 if self.0.caps.bgra => {
                (glow::BGRA as i32, glow::BGRA, glow::UNSIGNED_BYTE)
            }
            TextureFormat::Rgba16Float if self.0.caps.half_float => {
                (glow::RGBA as i32, glow::RGBA, 0x8d61)
            }
            _ => return Err(Error::Unsupported("texture format")),
        };
        Ok(value)
    }
}
fn float_bytes(value: &[f32]) -> &[u8] {
    unsafe { std::slice::from_raw_parts(value.as_ptr().cast::<u8>(), std::mem::size_of_val(value)) }
}
#[cfg(any(target_os = "vita", test))]
fn reorder_blocks(size: [u32; 2], bytes: usize, input: &[u8], output: &mut [u8]) {
    let width = size[0] / 4;
    let height = size[1] / 4;
    let width_bits = width.ilog2();
    let height_bits = height.ilog2();
    let (mut x_mask, mut y_mask, mut bit) = (0usize, 0usize, 0);
    for axis in 0..width_bits.max(height_bits) {
        if axis < height_bits {
            y_mask |= 1 << bit;
            bit += 1;
        }
        if axis < width_bits {
            x_mask |= 1 << bit;
            bit += 1;
        }
    }
    x_mask *= bytes;
    y_mask *= bytes;
    let (mut source, mut row) = (0usize, 0usize);
    // Advance only this axis's Morton bits. After the shorter axis runs out,
    // the longer axis continues in the upper bits of the same byte offset.
    for _ in 0..height {
        let mut column = 0usize;
        for _ in 0..width {
            let destination = row | column;
            output[destination..destination + bytes]
                .copy_from_slice(&input[source..source + bytes]);
            source += bytes;
            column = column.wrapping_sub(x_mask) & x_mask;
        }
        row = row.wrapping_sub(y_mask) & y_mask;
    }
}

#[cfg(test)]
mod texture_reorder_tests {
    use super::reorder_blocks;
    use std::vec;

    #[test]
    fn rectangular_bc_blocks_preserve_row_major_content() {
        for size in [
            [8, 8],
            [8, 512],
            [512, 16],
            [1024, 256],
            [1024, 1024],
            [512, 2048],
        ] {
            for bytes in [8, 16] {
                let (width, height) = (size[0] / 4, size[1] / 4);
                let length = width as usize * height as usize * bytes;
                let mut source = vec![0; length];
                for (index, block) in source.chunks_exact_mut(bytes).enumerate() {
                    for word in block.chunks_exact_mut(8) {
                        word.copy_from_slice(&(index as u64).to_le_bytes());
                    }
                }
                let mut actual = vec![0xff; length];
                reorder_blocks(size, bytes, &source, &mut actual);
                // Direct bit interleaving is the previous address oracle.
                for y in 0..height {
                    for x in 0..width {
                        let (mut destination, mut bit) = (0usize, 0);
                        for axis in 0..width.ilog2().max(height.ilog2()) {
                            if axis < height.ilog2() {
                                destination |= (((y >> axis) & 1) as usize) << bit;
                                bit += 1;
                            }
                            if axis < width.ilog2() {
                                destination |= (((x >> axis) & 1) as usize) << bit;
                                bit += 1;
                            }
                        }
                        let linear = (y as usize * width as usize + x as usize) * bytes;
                        assert_eq!(
                            actual[destination * bytes..(destination + 1) * bytes],
                            source[linear..linear + bytes],
                            "{size:?}, {bytes}-byte blocks, ({x}, {y})",
                        );
                    }
                }
            }
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TextureFormat {
    Rgba8,
    Bgra8,
    Rgba16Float,
    Luma8,
    LumaAlpha8,
    Bc1Rgb,
    Bc3Rgba,
}
impl TextureFormat {
    fn is_compressed(self) -> bool {
        matches!(self, Self::Bc1Rgb | Self::Bc3Rgba)
    }
    fn pixel_bytes(self) -> usize {
        match self {
            Self::Luma8 => 1,
            Self::LumaAlpha8 => 2,
            Self::Rgba16Float => 8,
            _ => 4,
        }
    }
}
struct TextureInner {
    owner: Rc<Inner>,
    name: glow::NativeTexture,
    size: [u32; 2],
    format: TextureFormat,
    filter: Cell<TextureFilter>,
}
impl Drop for TextureInner {
    fn drop(&mut self) {
        unsafe {
            self.owner.gl.delete_texture(self.name);
        }
    }
}
#[derive(Clone)]
pub struct Texture(Rc<TextureInner>);
impl PartialEq for Texture {
    /// Compare the owned allocation without querying GL or exposing native handles.
    fn eq(&self, other: &Self) -> bool {
        Rc::ptr_eq(&self.0, &other.0)
    }
}
impl Eq for Texture {}

impl Texture {
    pub fn size(&self) -> [u32; 2] {
        self.0.size
    }
    pub fn format(&self) -> TextureFormat {
        self.0.format
    }
}
struct FramebufferInner {
    owner: Rc<Inner>,
    name: glow::NativeFramebuffer,
    texture: Texture,
}
impl Drop for FramebufferInner {
    fn drop(&mut self) {
        unsafe {
            self.owner.gl.delete_framebuffer(self.name);
        }
    }
}
#[derive(Clone)]
pub struct Framebuffer(Rc<FramebufferInner>);
impl Framebuffer {
    pub fn size(&self) -> [u32; 2] {
        self.0.texture.size()
    }
    pub fn texture(&self) -> &Texture {
        &self.0.texture
    }
}
#[derive(Clone, Copy)]
pub enum Target<'a> {
    Screen,
    Framebuffer(&'a Framebuffer),
}
/// Target shader payload: SGX binary on Vita, GLSL source for desktop probes.
#[derive(Clone, Copy)]
pub enum ShaderCode<'a> {
    /// Precompiled SGX program for the maintained Vita driver.
    SgxBinary(&'a [u8]),
    /// GLSL ES source compiled by the desktop driver.
    #[cfg(not(target_os = "vita"))]
    Source(&'a str),
}
pub struct AttributeBinding<'a> {
    pub location: u32,
    pub name: &'a str,
}
struct ProgramInner {
    owner: Rc<Inner>,
    name: glow::NativeProgram,
}
impl Drop for ProgramInner {
    fn drop(&mut self) {
        unsafe {
            self.owner.gl.delete_program(self.name);
        }
    }
}
#[derive(Clone)]
pub struct Program(Rc<ProgramInner>);
impl Program {
    pub fn uniform(&self, name: &str) -> Option<Uniform> {
        unsafe {
            self.0
                .owner
                .gl
                .get_uniform_location(self.0.name, name)
                .map(|location| Uniform {
                    program: self.clone(),
                    location,
                })
        }
    }
}
#[derive(Clone)]
pub struct Uniform {
    program: Program,
    location: glow::NativeUniformLocation,
}
#[derive(Clone, Copy)]
pub enum UniformValue<'a> {
    Int(&'a Uniform, i32),
    Float(&'a Uniform, f32),
    Vec2(&'a Uniform, [f32; 2]),
    Vec3(&'a Uniform, [f32; 3]),
    Vec4(&'a Uniform, [f32; 4]),
    Vec4Array(&'a Uniform, &'a [[f32; 4]]),
}
enum UniformData<'a> {
    Int(i32),
    Float(f32),
    Vec2([f32; 2]),
    Vec3([f32; 3]),
    Vec4([f32; 4]),
    Vec4Array(&'a [[f32; 4]]),
}
impl<'a> UniformValue<'a> {
    fn parts(self) -> (&'a Uniform, UniformData<'a>) {
        match self {
            Self::Int(u, v) => (u, UniformData::Int(v)),
            Self::Float(u, v) => (u, UniformData::Float(v)),
            Self::Vec2(u, v) => (u, UniformData::Vec2(v)),
            Self::Vec3(u, v) => (u, UniformData::Vec3(v)),
            Self::Vec4(u, v) => (u, UniformData::Vec4(v)),
            Self::Vec4Array(u, v) => (u, UniformData::Vec4Array(v)),
        }
    }
}
#[derive(Clone, Copy)]
pub struct VertexAttribute {
    pub location: u32,
    pub components: u32,
    pub offset: u32,
}
pub struct VertexBuffer {
    owner: Rc<Inner>,
    name: glow::NativeBuffer,
    count: usize,
    capacity: usize,
    stride: u32,
    attributes: Vec<VertexAttribute>,
}
impl Drop for VertexBuffer {
    fn drop(&mut self) {
        unsafe {
            self.owner.gl.delete_buffer(self.name);
        }
    }
}
pub struct IndexBuffer {
    owner: Rc<Inner>,
    name: glow::NativeBuffer,
    count: usize,
    capacity: usize,
    maximum: u32,
}
impl Drop for IndexBuffer {
    fn drop(&mut self) {
        unsafe {
            self.owner.gl.delete_buffer(self.name);
        }
    }
}
/// Hardware minification and magnification filtering for a sampled draw.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TextureFilter {
    /// Select the nearest texel without interpolation.
    Nearest,
    /// Interpolate adjacent texels in both texture axes.
    Linear,
}
#[derive(Clone, Copy)]
pub enum Blend {
    Replace,
    Premultiplied,
}
pub struct Draw<'a> {
    pub target: Target<'a>,
    pub size: [u32; 2],
    pub program: &'a Program,
    pub vertices: &'a VertexBuffer,
    pub indices: Option<&'a IndexBuffer>,
    pub range: Range<u32>,
    pub textures: &'a [&'a Texture],
    pub uniforms: &'a [UniformValue<'a>],
    pub blend: Blend,
    pub scissor: Option<[u32; 4]>,
}
