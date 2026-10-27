//! Direct GXM device boundary (interface version 1).
//!
//! Coordinates and sampled render targets use a top-left origin. Every fragment
//! GXP must retain `float4 reel_clip_rect` and discard fragments outside its
//! half-open WPOS rectangle. Programs are patched once for their vertex layout
//! and both replacement and premultiplied source-over output.
//!
//! Scenes retain every referenced allocation until a fragment notification.
//! Display scanout owns separate leases. `finish`, `reset`, and `close` drain
//! submitted work explicitly; target switches never call `sceGxmFinish`.
#![deny(missing_docs)]
#[cfg(any(target_os = "vita", test))]
mod gxp;
mod layout;
#[cfg(any(target_os = "vita", test))]
mod suballoc;
mod types;
pub use layout::{TargetFootprint, TargetLayout, TextureLayout, target_layout, texture_layout};
pub use types::*;
#[cfg(target_os = "vita")]
mod commands;
#[cfg(target_os = "vita")]
mod device;
#[cfg(target_os = "vita")]
mod memory;
#[cfg(target_os = "vita")]
mod program;
#[cfg(target_os = "vita")]
mod resources;
#[cfg(target_os = "vita")]
pub use commands::Draw;
#[cfg(target_os = "vita")]
pub use device::{Context, Fence};
#[cfg(target_os = "vita")]
pub use program::{Program, Uniform, UniformValue};
#[cfg(target_os = "vita")]
pub use resources::{Framebuffer, IndexBuffer, Target, Texture, VertexBuffer};
