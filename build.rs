use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    if cfg!(feature = "build-driver") {
        build_driver();
    }
    println!("cargo:rerun-if-changed=stubs/pvr2d.S");
    for archive in [
        "vendor/vitasdk-stubs/liblibIMGEGL_stub_weak.a",
        "vendor/vitasdk-stubs/liblibgpu_es4_ext_stub_weak.a",
        "vendor/vitasdk-stubs/liblibGLESv2_stub_weak.a",
    ] {
        println!("cargo:rerun-if-changed={archive}");
    }
    println!("cargo:rerun-if-env-changed=TARGET_CC");
    println!("cargo:rerun-if-env-changed=TARGET_AR");

    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("vita") {
        return;
    }

    let manifest = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let output = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let archive = output.join("libpvr_psp2_stubs.a");
    let compiler = env::var_os("TARGET_CC").unwrap_or_else(|| "arm-vita-eabi-gcc".into());
    let archiver = env::var_os("TARGET_AR").unwrap_or_else(|| "arm-vita-eabi-ar".into());
    let object = output.join("pvr2d_stubs.o");
    run(
        Command::new(&compiler)
            .arg("-c")
            .arg(manifest.join("stubs/pvr2d.S"))
            .arg("-o")
            .arg(&object),
        "assemble PVR VitaSDK stubs",
    );

    let _ = fs::remove_file(&archive);
    run(
        Command::new(archiver).arg("crs").arg(&archive).arg(object),
        "archive PVR VitaSDK stubs",
    );

    println!("cargo:rustc-link-search=native={}", output.display());
    println!("cargo:rustc-link-lib=static=pvr_psp2_stubs");
    println!(
        "cargo:rustc-link-search=native={}",
        manifest.join("vendor/vitasdk-stubs").display()
    );
    println!("cargo:rustc-link-lib=static=libIMGEGL_stub_weak");
    println!("cargo:rustc-link-lib=static=libgpu_es4_ext_stub_weak");
    println!("cargo:rustc-link-lib=static=libGLESv2_stub_weak");
}

#[cfg(not(windows))]
fn build_driver() {
    panic!(
        "build-driver currently uses the official Windows PSVSDK; run this feature with Windows Cargo"
    );
}

#[cfg(windows)]
fn build_driver() {
    for name in [
        "SCE_PSP2_SDK_DIR",
        "CMAKE",
        "CMAKE_GENERATOR",
        "CMAKE_MAKE_PROGRAM",
        "RUSTC",
    ] {
        println!("cargo:rerun-if-env-changed={name}");
    }
    println!("cargo:rerun-if-changed=native");
    println!("cargo:rerun-if-changed=vendor/PVR_PSP2");
    let sdk = env::var_os("SCE_PSP2_SDK_DIR")
        .expect("build-driver requires SCE_PSP2_SDK_DIR pointing to the official PSVSDK root");
    let cmake = env::var_os("CMAKE").unwrap_or_else(|| "cmake".into());
    let manifest = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let output = PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("pvr-driver");
    let mut configure = Command::new(&cmake);
    configure
        .arg("-S")
        .arg(manifest.join("native"))
        .arg("-B")
        .arg(&output)
        .arg(format!("-DPSVSDK={}", PathBuf::from(sdk).display()));
    if let Some(generator) = env::var_os("CMAKE_GENERATOR") {
        configure.arg("-G").arg(generator);
    }
    if let Some(make) = env::var_os("CMAKE_MAKE_PROGRAM") {
        configure.arg(format!(
            "-DCMAKE_MAKE_PROGRAM={}",
            PathBuf::from(make).display()
        ));
    }
    if let Some(rustc) = env::var_os("RUSTC") {
        configure.arg(format!("-DPVR_RUSTC={}", PathBuf::from(rustc).display()));
    }
    run(&mut configure, "configure PVR with the official PSVSDK");
    run(
        Command::new(cmake)
            .arg("--build")
            .arg(&output)
            .arg("--parallel")
            .arg(env::var_os("NUM_JOBS").unwrap_or_else(|| "1".into())),
        "build and verify all four native PVR modules",
    );
    println!(
        "cargo:warning=PVR driver modules: {}",
        output.join("module").display()
    );
    println!("cargo:modules_dir={}", output.join("module").display());
}

fn run(command: &mut Command, operation: &str) {
    let status = command
        .status()
        .unwrap_or_else(|error| panic!("failed to {operation}: {error}"));
    assert!(status.success(), "failed to {operation}: {status}");
}
