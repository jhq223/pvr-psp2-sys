//! Host-only validation for the official SDK's native PVR build.
#![deny(warnings)]

#[path = "check/abi.rs"]
mod abi;
#[path = "check/allocators.rs"]
mod allocators;
#[path = "check/depfile.rs"]
mod depfile;
#[path = "check/optimizations.rs"]
mod optimizations;

use std::{env, ffi::OsString, fs, path::Path, process::ExitCode};

fn run(args: &[OsString]) -> Result<(), String> {
    match args {
        [command, input, output, object] if command == "depfile" => {
            let source = fs::read_to_string(input).map_err(|e| e.to_string())?;
            let data = depfile::normalize(&source, &object.to_string_lossy())?;
            fs::write(output, data).map_err(|e| e.to_string())
        }
        [command, expected, archive] if command == "abi" => {
            let manifest = fs::read_to_string(expected).map_err(|e| format!("{expected:?}: {e}"))?;
            let data = fs::read(archive).map_err(|e| format!("{archive:?}: {e}"))?;
            let count = abi::verify(&manifest, &data)?;
            println!("Verified {count} original export NIDs: {}", Path::new(archive).display());
            Ok(())
        }
        [command, root, output] if command == "optimizations" => {
            let cc = env::var_os("CC").unwrap_or_else(|| "cc".into());
            optimizations::check(Path::new(root), Path::new(output), &cc)
        }
        [command, root, output] if command == "allocators" => {
            let cc = env::var_os("CC").unwrap_or_else(|| "cc".into());
            allocators::check(Path::new(root), Path::new(output), &cc)
        }
        _ => Err("usage: pvr-native-check abi <expected.nids> <rebuilt_stub.a>\n       pvr-native-check allocators <pvr-psp2-sys directory> <test output directory>\n       pvr-native-check optimizations <pvr-psp2-sys directory> <test output directory>\nSet CC to select the host C compiler for allocator tests.".into()),
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
