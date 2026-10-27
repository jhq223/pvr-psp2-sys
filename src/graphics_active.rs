//! One process-wide owner for either graphics device.
use std::sync::atomic::AtomicBool;
pub(crate) static ACTIVE: AtomicBool = AtomicBool::new(false);
