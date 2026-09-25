//! Host-only validation for the official SDK's native PVR build.
#![deny(warnings)]

#[path = "check/abi.rs"]
mod abi;
#[path = "check/allocators.rs"]
mod allocators;

use std::{env, ffi::OsString, fs, path::Path, process::ExitCode};

fn run(args: &[OsString]) -> Result<(), String> {
    match args {
        [command, expected, archive] if command == "abi" => {
            let manifest = fs::read_to_string(expected).map_err(|e| format!("{expected:?}: {e}"))?;
            let data = fs::read(archive).map_err(|e| format!("{archive:?}: {e}"))?;
            let count = abi::verify(&manifest, &data)?;
            println!("Verified {count} original export NIDs: {}", Path::new(archive).display());
            Ok(())
        }
        [command, root, output] if command == "allocators" => {
            let cc = env::var_os("CC").unwrap_or_else(|| "cc".into());
            allocators::check(Path::new(root), Path::new(output), &cc)
        }
        _ => Err("usage: pvr-native-check abi <expected.nids> <rebuilt_stub.a>\n       pvr-native-check allocators <pvr-psp2-sys directory> <test output directory>\nSet CC to select the host C compiler for allocator tests.".into()),
    }
}

fn main() -> ExitCode {
    match run(&env::args_os().skip(1).collect::<Vec<_>>()) {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("PVR validation failed: {error}");
            ExitCode::FAILURE
        }
    }
}
