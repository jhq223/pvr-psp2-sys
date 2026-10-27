//! Accounted target layout and independent native allocator diagnostics.
use super::{Context, Error, Native, Result};

/// A single-sample RGBA8 color payload, including the driver's row padding.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TargetLayout {
    /// Color payload bytes, including the Vita driver's row-pitch padding.
    /// Host EGL reports the tightly packed accounting layout.
    pub texture_bytes: u64,
    /// Bytes between consecutive color rows in that accounting layout.
    pub row_stride_bytes: u64,
}

/// A color layout and its separately owned native render-target bookkeeping.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct TargetFootprint {
    /// Color texture layout used by renderer admission and allocation.
    pub texture: TargetLayout,
    /// USER_NC render-target bookkeeping from the actual GXM size query.
    /// Host EGL has no corresponding exact query and reports `None`.
    pub render_target_driver_bytes: Option<u64>,
}

/// One texture allocator's current snapshot, including its retained spare blocks.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct HeapMemoryUsage {
    /// Kernel memory reserved by this heap; includes used and free capacity.
    pub reserved_bytes: u64,
    /// Allocated heap capacity, including allocator metadata.
    pub used_bytes: u64,
    /// Number of kernel blocks retained by this heap.
    pub blocks: u32,
}

/// Native allocator domains sampled independently of renderer-owned tickets.
/// Heap snapshots are adjacent queries, rather than one atomic aggregate.
/// Direct allocations are suballocations of registered storage and must not be
/// added to its reserved bytes. Historical per-domain peaks are also separate.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct DriverMemoryUsage {
    /// The texture allocator's current USER_CDRAM reservation and use.
    pub texture_cdram: HeapMemoryUsage,
    /// The texture allocator's current USER_NC reservation and use.
    pub texture_uncached: HeapMemoryUsage,
    /// The registered SGXDriver USER_NC kernel block's actual mapped bytes.
    pub driver_user_nc_reserved_bytes: u64,
    /// The registered SGXParamBuffer USER_CDRAM block's actual mapped bytes.
    pub parameter_cdram_reserved_bytes: u64,
    /// Actual mapped USER_NC bytes of currently allocated render-target blocks.
    pub render_target_user_nc_bytes: u64,
    /// Simultaneous high-water mark of render-target USER_NC blocks.
    pub render_target_user_nc_peak_bytes: u64,
    /// Current direct device suballocations, including PDS/USSE/status storage.
    pub direct_device_bytes: u64,
    /// High-water mark of those suballocations; included in registered storage.
    pub direct_device_peak_bytes: u64,
    /// Successfully completed native surface constructions.
    pub surface_creates: u64,
    /// Successfully completed native surface retirements.
    pub surface_retires: u64,
}

#[repr(C)]
#[derive(Default)]
struct RawHeap {
    reserved: u32,
    used: u32,
    blocks: u32,
}
#[repr(C)]
#[derive(Default)]
struct RawTextureHeaps {
    cdram: RawHeap,
    uncached: RawHeap,
}
#[repr(C)]
#[derive(Default)]
struct RawDriver {
    registered_unc: u32,
    parameter_cdram: u32,
    rt_live: u32,
    rt_peak: u32,
    direct_live: u32,
    direct_peak: u32,
    creates: u64,
    retires: u64,
}
type TextureQuery = unsafe extern "C" fn(*mut RawTextureHeaps) -> i32;
type DriverQuery = unsafe extern "C" fn(*mut RawDriver) -> i32;
#[derive(Clone, Copy)]
pub(super) struct Api {
    texture: TextureQuery,
    driver: DriverQuery,
}
impl Api {
    pub(super) fn load(native: &Native) -> Option<Self> {
        #[cfg(target_os = "vita")]
        {
            let texture = native.proc_address("glQueryTextureMemoryVITA");
            let driver = native.proc_address("eglQueryDriverMemoryVITA");
            if texture.is_null() || driver.is_null() {
                return None;
            }
            Some(unsafe {
                Self {
                    texture: std::mem::transmute::<*const core::ffi::c_void, TextureQuery>(texture),
                    driver: std::mem::transmute::<*const core::ffi::c_void, DriverQuery>(driver),
                }
            })
        }
        #[cfg(not(target_os = "vita"))]
        {
            let _ = native;
            None
        }
    }
    fn query(self) -> Result<DriverMemoryUsage> {
        let mut texture = RawTextureHeaps::default();
        let mut driver = RawDriver::default();
        let status = unsafe { (self.texture)(&mut texture) };
        if status != 0 {
            return Err(Error::Sdk("query texture heaps", status));
        }
        let status = unsafe { (self.driver)(&mut driver) };
        if status != 0 {
            return Err(Error::Sdk("query native driver memory", status));
        }
        let heap = |raw: RawHeap| HeapMemoryUsage {
            reserved_bytes: raw.reserved.into(),
            used_bytes: raw.used.into(),
            blocks: raw.blocks,
        };
        Ok(DriverMemoryUsage {
            texture_cdram: heap(texture.cdram),
            texture_uncached: heap(texture.uncached),
            driver_user_nc_reserved_bytes: driver.registered_unc.into(),
            parameter_cdram_reserved_bytes: driver.parameter_cdram.into(),
            render_target_user_nc_bytes: driver.rt_live.into(),
            render_target_user_nc_peak_bytes: driver.rt_peak.into(),
            direct_device_bytes: driver.direct_live.into(),
            direct_device_peak_bytes: driver.direct_peak.into(),
            surface_creates: driver.creates,
            surface_retires: driver.retires,
        })
    }
}

fn layout(size: [u32; 2], row_alignment: u32) -> TargetLayout {
    let row = u64::from(size[0]).div_ceil(u64::from(row_alignment)) * u64::from(row_alignment) * 4;
    TargetLayout {
        texture_bytes: row * u64::from(size[1]),
        row_stride_bytes: row,
    }
}
impl Context {
    /// Compute the immutable single-sample RGBA8 color layout with integer arithmetic.
    /// This performs no native memory query, allocation or submission. Vita uses
    /// the same eight-texel STRIDE pitch as this context's texture hint.
    pub fn target_layout(&self, size: [u32; 2]) -> Result<TargetLayout> {
        self.validate_size(size)?;
        Ok(layout(size, if cfg!(target_os = "vita") { 8 } else { 1 }))
    }
    /// Explicitly query the complete target footprint without allocating it.
    /// RT bookkeeping uses the same GXM parameters as the native allocation helper.
    /// Use `target_layout` for ordinary renderer planning and admission.
    pub fn target_footprint(&self, size: [u32; 2]) -> Result<TargetFootprint> {
        let texture = self.target_layout(size)?;
        #[cfg(target_os = "vita")]
        {
            let params = vitasdk_sys::SceGxmRenderTargetParams {
                flags: 0,
                width: size[0] as u16,
                height: size[1] as u16,
                scenesPerFrame: 1,
                multisampleMode: 0,
                multisampleLocations: 0,
                driverMemBlock: -1,
            };
            let mut bytes = 0;
            let result = unsafe { vitasdk_sys::sceGxmGetRenderTargetMemSize(&params, &mut bytes) };
            if result < 0 {
                return Err(Error::Sdk("render target footprint", result));
            }
            Ok(TargetFootprint {
                render_target_driver_bytes: Some(bytes.into()),
                texture,
            })
        }
        #[cfg(not(target_os = "vita"))]
        {
            Ok(TargetFootprint {
                texture,
                render_target_driver_bytes: None,
            })
        }
    }
    /// Sample native allocator domains when the matching driver exposes them.
    /// Querying never waits for GPU completion or changes allocator ownership.
    pub fn memory_usage(&self) -> Result<Option<DriverMemoryUsage>> {
        self.open()?;
        self.0.memory.map(Api::query).transpose()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn stride_payload_matches_eight_texel_boundaries_without_per_texture_heap_rounding() {
        for (size, bytes) in [
            ([1, 1], 32),
            ([4, 4], 128),
            ([161, 87], 58_464),
            ([644, 348], 902_016),
            ([960, 544], 2_088_960),
            ([961, 544], 2_106_368),
        ] {
            assert_eq!(layout(size, 8).texture_bytes, bytes);
            assert_eq!(
                layout(size, 1).texture_bytes,
                u64::from(size[0]) * u64::from(size[1]) * 4
            );
        }
        assert_eq!(std::mem::size_of::<RawTextureHeaps>(), 24);
        assert_eq!(std::mem::size_of::<RawDriver>(), 40);
    }
}
