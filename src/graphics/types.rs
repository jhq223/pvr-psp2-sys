//! Native failures and driver capability descriptions.
use std::prelude::v1::*;

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
/// Driver strings queried from the current OpenGL ES context.
#[derive(Clone, Debug)]
pub struct DriverInfo {
    pub vendor: String,
    pub renderer: String,
    pub version: String,
    pub shading_language: String,
}
