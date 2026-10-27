//! Permanent native ownership failure, sampled at destruction and error boundaries.
use super::{Context, Error, Result};
use std::{cell::OnceCell, ffi::CStr, string::String, sync::Arc};

/// The first failed native retirement operation. This context remains terminal
/// and retains unresolved allocations and loaded modules until process exit.
#[derive(Debug, PartialEq, Eq)]
pub struct DeviceFault {
    /// Driver operation whose ownership or completion check failed.
    pub operation: String,
    /// Original return code at that boundary; boolean failure is zero.
    pub code: i32,
}

#[repr(C)]
struct RawFault {
    operation: *const core::ffi::c_char,
    code: i32,
}
type Query = unsafe extern "C" fn(*mut RawFault) -> i32;

#[derive(Default)]
pub(super) struct State {
    query: Option<Query>,
    cached: OnceCell<Arc<DeviceFault>>,
}
impl State {
    #[cfg(target_os = "vita")]
    pub(super) fn load(&mut self, address: *const core::ffi::c_void) -> Result<()> {
        if address.is_null() {
            return Err(Error::Unsupported("native device-fault query is required"));
        }
        self.query =
            Some(unsafe { std::mem::transmute::<*const core::ffi::c_void, Query>(address) });
        Ok(())
    }
    #[cfg(any(target_os = "vita", test))]
    pub(super) fn detach(&mut self) {
        self.query = None;
    }
    pub(super) fn cached(&self) -> Option<Arc<DeviceFault>> {
        self.cached.get().cloned()
    }
    pub(super) fn refresh(&self) -> Option<Arc<DeviceFault>> {
        if let Some(fault) = self.cached() {
            return Some(fault);
        }
        let query = self.query?;
        let mut raw = RawFault {
            operation: std::ptr::null(),
            code: 0,
        };
        if unsafe { query(&mut raw) } == 0 {
            return None;
        }
        // The matching driver publishes a process-lifetime string literal only
        // after recording the first failure under its shared services owner.
        let fault = Arc::new(DeviceFault {
            operation: unsafe { CStr::from_ptr(raw.operation) }
                .to_string_lossy()
                .into_owned(),
            code: raw.code,
        });
        self.cached
            .set(fault.clone())
            .expect("first terminal observation");
        Some(fault)
    }
    pub(super) fn check(&self) -> Result<()> {
        self.cached()
            .map_or(Ok(()), |fault| Err(Error::Terminal(fault)))
    }
}
impl Context {
    /// Refresh the first native retirement failure. Healthy draws use the cached
    /// state and the driver's existing context-entry guard. Destructors and real
    /// GL/EGL errors refresh this query so failures cannot return budget capacity.
    pub fn terminal_fault(&self) -> Option<Arc<DeviceFault>> {
        self.0.native.failure.refresh()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};
    static CALLS: AtomicU32 = AtomicU32::new(0);
    unsafe extern "C" fn failed(out: *mut RawFault) -> i32 {
        CALLS.fetch_add(1, Ordering::Relaxed);
        unsafe {
            *out = RawFault {
                operation: c"rt-remove".as_ptr(),
                code: -17,
            };
        }
        1
    }
    unsafe extern "C" fn unloaded(_: *mut RawFault) -> i32 {
        panic!("unloaded query must not be called");
    }
    #[test]
    fn detaching_the_module_preserves_cached_fault_and_removes_callable_entry() {
        let mut healthy = State {
            query: Some(unloaded),
            ..Default::default()
        };
        healthy.detach();
        assert!(healthy.refresh().is_none());
        let recorded = Arc::new(DeviceFault {
            operation: "retire".into(),
            code: -1,
        });
        let mut failed = State {
            query: Some(unloaded),
            ..Default::default()
        };
        failed.cached.set(recorded.clone()).unwrap();
        failed.detach();
        assert!(Arc::ptr_eq(&recorded, &failed.refresh().unwrap()));
        assert!(matches!(failed.check(), Err(Error::Terminal(_))));
    }
    #[test]
    fn cached_terminal_state_keeps_the_first_error_without_requerying() {
        let state = State {
            query: Some(failed),
            ..Default::default()
        };
        for _ in 0..100 {
            state.check().unwrap();
        }
        assert_eq!(CALLS.load(Ordering::Relaxed), 0);
        let first = state.refresh().unwrap();
        assert_eq!(first.operation, "rt-remove");
        assert_eq!(first.code, -17);
        assert!(matches!(state.check(), Err(Error::Terminal(_))));
        assert!(Arc::ptr_eq(&first, &state.refresh().unwrap()));
        assert_eq!(CALLS.load(Ordering::Relaxed), 1);
    }
}
