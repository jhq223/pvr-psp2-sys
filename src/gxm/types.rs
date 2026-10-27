//! Device contracts independent of the target SDK.
use std::{prelude::v1::*, sync::Arc};
/// First native failure that leaves completion or ownership unresolved.
#[derive(Debug, PartialEq, Eq)]
pub struct DeviceFault {
    /// Native operation whose first failure remains terminal.
    pub operation: &'static str,
    /// Original signed SDK return code.
    pub code: i32,
}
/// Native errors retain the actual operation and SDK status.
#[derive(Debug)]
pub enum Error {
    /// Caller input violates the documented interface.
    Invalid(&'static str),
    /// The selected native format or behavior is unavailable.
    Unsupported(&'static str),
    /// A native domain would exceed its explicit capacity.
    Budget {
        /// Native allocation domain that rejected admission.
        domain: &'static str,
        /// Required simultaneous native capacity.
        requested: u64,
        /// Configured maximum capacity for this domain.
        limit: u64,
    },
    /// Native construction failed before submission.
    Sdk(&'static str, i32),
    /// A submitted/native ownership operation failed; owners remain retained.
    Terminal(Arc<DeviceFault>),
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}
impl std::error::Error for Error {}
pub(crate) type Result<T> = std::result::Result<T, Error>;
/// Fragment notifications are the GXM submission-completion source.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompletionMode {
    /// Fragment or ordered transfer notification in the libgxm notification region.
    FragmentNotification,
}
/// GXM hardware limits; shader uniform storage is validated from each GXP.
#[derive(Clone, Debug)]
pub struct Capabilities {
    /// Maximum 2D physical texture dimension: 4096 pixels.
    pub max_texture_size: u32,
    /// Fragment sampler slots: 16.
    pub max_texture_units: u32,
    /// Vertex attributes: 16.
    pub max_vertex_attributes: u32,
    /// Direct UBC1/UBC3 sampling and portable BC block upload.
    pub bc: bool,
    /// BGRA8 sampled storage is available.
    pub bgra: bool,
    /// RGBA16F sampled storage is available.
    pub half_float: bool,
    /// Both u16 and u32 element streams are available.
    pub indices_u32: bool,
    /// Programs use offline GXP input.
    pub shader_binary: bool,
    /// Submission completion source.
    pub completion: CompletionMode,
}
/// Direct device and shader ABI identity.
#[derive(Clone, Debug)]
pub struct DriverInfo {
    /// Platform graphics library owner.
    pub vendor: String,
    /// Direct native device identity.
    pub renderer: String,
    /// This owned boundary interface version.
    pub version: String,
    /// Offline shader profiles consumed by this device.
    pub shading_language: String,
}
/// Fixed infrastructure budgets. Pixel/buffer arenas grow by suballocated blocks.
#[derive(Clone, Copy, Debug)]
pub struct Config {
    /// Maximum kernel capacity retained by each suballocation domain.
    pub pixel_arena_bytes: u64,
    /// Reserved uncached LPDDR cap for geometry and upload staging; default 16 MiB.
    pub buffer_arena_bytes: u64,
    /// Maximum simultaneously owned transfer staging; admission waits real completion at this cap.
    pub upload_staging_bytes: u64,
    /// libgxm-owned CDRAM parameter-buffer size; default 16 MiB.
    pub parameter_bytes: u32,
    /// Mapped VDM command ring size; default 128 KiB.
    pub vdm_ring_bytes: u32,
    /// Mapped vertex uniform/command ring size; default 2 MiB.
    pub vertex_ring_bytes: u32,
    /// Mapped fragment uniform/command ring size; default 512 KiB.
    pub fragment_ring_bytes: u32,
    /// Mapped fragment USSE ring size; default 16 KiB.
    pub fragment_usse_ring_bytes: u32,
    /// Fixed mapped shader-patcher data pool; default 1 MiB.
    pub patcher_buffer_bytes: u32,
    /// Fixed vertex executable pool; default 256 KiB.
    pub patcher_vertex_usse_bytes: u32,
    /// Fixed fragment executable pool; default 512 KiB.
    pub patcher_fragment_usse_bytes: u32,
    /// RT scheduling capacity, also used in exact driver-memory size queries.
    pub scenes_per_frame: u16,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            pixel_arena_bytes: 96 * 1024 * 1024,
            buffer_arena_bytes: 16 * 1024 * 1024,
            upload_staging_bytes: 4 * 1024 * 1024,
            parameter_bytes: 16 * 1024 * 1024,
            vdm_ring_bytes: 128 * 1024,
            vertex_ring_bytes: 2 * 1024 * 1024,
            fragment_ring_bytes: 512 * 1024,
            fragment_usse_ring_bytes: 16 * 1024,
            patcher_buffer_bytes: 1024 * 1024,
            patcher_vertex_usse_bytes: 256 * 1024,
            patcher_fragment_usse_bytes: 512 * 1024,
            scenes_per_frame: 4,
        }
    }
}
/// One native arena's kernel capacity and live suballocated payload.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ArenaUsage {
    /// Actual kernel block capacity, including free suballocation intervals.
    pub reserved_bytes: u64,
    /// Live suballocated payload bytes; excludes free alignment gaps.
    pub used_bytes: u64,
    /// Live kernel blocks in this arena.
    pub blocks: u32,
}
/// Independent native memory domains. Renderer tickets describe their own payloads.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MemoryUsage {
    /// Caller texture/target CDRAM arena; excludes scanout and parameter memory.
    pub pixels: ArenaUsage,
    /// Geometry and transfer staging arena; includes upload_staging_bytes.
    pub buffers: ArenaUsage,
    /// libgxm parameter-buffer reservation requested at initialization.
    pub parameter_cdram_bytes: u64,
    /// Actual mapped capacity of the four context rings.
    pub context_ring_bytes: u64,
    /// Actual mapped capacity of the three fixed patcher pools.
    pub patcher_pool_bytes: u64,
    /// Current patcher CPU storage returned by its native query.
    pub patcher_host_bytes: u64,
    /// Actual uncached kernel block capacity backing all live render targets.
    pub render_target_driver_bytes: u64,
    /// Scanout payload bytes; a subset of display_reserved_bytes.
    pub display_pixel_bytes: u64,
    /// Actual scanout CDRAM kernel block capacity, separate from pixels.
    pub display_reserved_bytes: u64,
    /// Live transfer-staging cap within the buffer arena; default 4 MiB.
    pub upload_staging_bytes: u64,
    /// Successfully ended direct scenes.
    pub scene_submissions: u64,
    /// Admission waits caused by exhausting the 256 completion slots.
    pub notification_capacity_waits: u64,
    /// Admission waits at the two-entry display queue limit.
    pub display_queue_waits: u64,
    /// Successfully completed display callbacks.
    pub display_flips: u64,
}
/// Sample/storage formats. Luma formats retain the GLES `.r` / `.ra` contract.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TextureFormat {
    /// Byte-order RGBA8, sampled as linear premultiplied color.
    Rgba8,
    /// Byte-order BGRA8.
    Bgra8,
    /// Four IEEE half-float components.
    Rgba16Float,
    /// One byte replicated to RGB with alpha one.
    Luma8,
    /// Two bytes sampled as (R,R,R,G), preserving .ra for NV12 chroma.
    LumaAlpha8,
    /// Portable row-major BC1 blocks, sampled with opaque alpha.
    Bc1Rgb,
    /// Portable row-major BC3 blocks with native alpha.
    Bc3Rgba,
}
impl TextureFormat {
    pub(crate) fn pixel_bytes(self) -> usize {
        match self {
            Self::Luma8 => 1,
            Self::LumaAlpha8 => 2,
            Self::Rgba16Float => 8,
            _ => 4,
        }
    }
    pub(crate) fn compressed(self) -> bool {
        matches!(self, Self::Bc1Rgb | Self::Bc3Rgba)
    }
}
/// Hardware minification/magnification selection for a draw.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TextureFilter {
    /// Nearest texel selection.
    Nearest,
    /// Linear interpolation of adjacent texels.
    Linear,
}
/// Pipeline output variants prepared when a program is created.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Blend {
    /// Replace all RGBA channels.
    Replace,
    /// Premultiplied source-over: source plus destination times one minus source alpha.
    Premultiplied,
}
/// Offline GXM program, with retained reflection symbols.
#[derive(Clone, Copy)]
pub enum ShaderCode<'a> {
    /// Compiler-padded GXP payload with retained reflection symbols.
    GxpBinary(&'a [u8]),
}
/// Map a caller vertex attribute identity to its retained GXP parameter name.
pub struct AttributeBinding<'a> {
    /// Caller vertex attribute identity.
    pub location: u32,
    /// Retained GXP attribute parameter name.
    pub name: &'a str,
}
/// Interleaved float input. Offsets and stride use f32 elements, matching graphics.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct VertexAttribute {
    /// Caller vertex attribute identity.
    pub location: u32,
    /// Number of f32 input components, from one through four.
    pub components: u32,
    /// Attribute offset in f32 elements.
    pub offset: u32,
}
