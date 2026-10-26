#![cfg(target_os = "linux")]

use std::{fs, path::Path, process::Command};

#[test]
fn texture_state_loads_cover_the_encoded_sample() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let driver = root.join("vendor/PVR_PSP2");
    let output = Path::new(env!("CARGO_TARGET_TMPDIR")).join("sample-state");
    fs::create_dir_all(&output).unwrap();
    let executable = output.join("sample-state");
    let mut compiler = Command::new("cc");
    compiler.args([
        "-std=gnu99",
        "-O1",
        "-g",
        "-fsanitize=address,undefined",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-strict-aliasing",
        "-DNDEBUG",
        "-DLINUX",
        "-DUSER",
        "-include",
    ]);
    compiler.arg(driver.join("include/gpu_es4/psp2_pvr_desc.h"));
    for include in [
        "tools/intern/usp",
        "tools/intern/usc2",
        "tools/intern/useasm",
        "include/gpu_es4/eurasia/include4",
        "include/gpu_es4/eurasia/hwdefs",
        "include/gpu_es4/eurasia/services4/include",
        "intermediates/sgxsupport",
        "intermediates/errata",
    ] {
        compiler.arg("-I").arg(driver.join(include));
    }
    let result = compiler
        .arg(root.join("native/tests/sample_state.c"))
        .arg(driver.join("tools/intern/usp/hwinst.c"))
        .arg(driver.join("tools/intern/usp/usp_inputdata.c"))
        .args(["-Wl,--gc-sections", "-lm", "-o"])
        .arg(&executable)
        .output()
        .unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
    let result = Command::new(executable).output().unwrap();
    assert!(
        result.status.success(),
        "{}",
        String::from_utf8_lossy(&result.stderr)
    );
}
