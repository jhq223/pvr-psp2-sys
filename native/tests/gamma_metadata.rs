#![cfg(target_os = "linux")]

use std::{fs, path::Path, process::Command};

#[test]
fn sampler_gamma_is_independent_and_uses_the_core_output_format() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let output = Path::new(env!("CARGO_TARGET_TMPDIR")).join("gamma-metadata");
    fs::create_dir_all(&output).unwrap();
    let driver = root.join("vendor/PVR_PSP2");
    let source = fs::read_to_string(driver.join("tools/intern/usp/usp_sample.c"))
        .unwrap()
        .replace("\r\n", "\n");
    let function = |name: &str| {
        let start = source.find(&format!("static IMG_VOID {name}(")).unwrap();
        let rest = &source[start..];
        &rest[..rest.find("\n}").unwrap() + 2]
    };
    fs::write(
        output.join("gamma-functions.inc"),
        format!(
            "{}\n{}\n",
            function("CalcTexChanBitCountAndFmt"),
            function("SetTextureChanFlags")
        ),
    )
    .unwrap();
    for core in ["SGX543", "SGX540"] {
        let executable = output.join(core);
        let result = Command::new("cc")
            .args(["-std=c99", "-Wall", "-Wextra", "-Werror", "-DLINUX"])
            .arg(format!("-D{core}"))
            .arg("-I")
            .arg(&output)
            .arg("-I")
            .arg(driver.join("include/gpu_es4/eurasia/hwdefs"))
            .arg("-I")
            .arg(driver.join("include/gpu_es4/eurasia/include4"))
            .arg(root.join("native/tests/gamma_metadata.c"))
            .arg("-o")
            .arg(&executable)
            .output()
            .unwrap();
        assert!(
            result.status.success(),
            "{core}: {}",
            String::from_utf8_lossy(&result.stderr)
        );
        let result = Command::new(executable).output().unwrap();
        assert!(
            result.status.success(),
            "{core}: {}",
            String::from_utf8_lossy(&result.stderr)
        );
    }
}
