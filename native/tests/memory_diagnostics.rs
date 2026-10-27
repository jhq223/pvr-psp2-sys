#![cfg(target_os = "linux")]
use std::{fs, path::Path, process::Command};

#[test]
fn counters_follow_actual_kernel_blocks_and_successful_retirement() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let output = Path::new(env!("CARGO_TARGET_TMPDIR")).join("memory-diagnostics");
    fs::create_dir_all(&output).unwrap();
    let body = fs::read_to_string(
        root.join("vendor/PVR_PSP2/eurasiacon/imgegl/imgegl/memory_diagnostics.c"),
    )
    .unwrap();
    let body = body
        .lines()
        .filter(|line| !line.starts_with("#include "))
        .collect::<Vec<_>>()
        .join("\n");
    fs::write(output.join("memory_diagnostics_functions.inc"), body).unwrap();
    let services = fs::read_to_string(root.join("vendor/PVR_PSP2/eurasiacon/imgegl/imgegl/srv.c"))
        .unwrap()
        .replace("\r\n", "\n");
    let mut functions = String::new();
    for name in ["SRV_ServicesInit", "SRV_ServicesDeInit"] {
        let start = services
            .find(&format!("IMG_INTERNAL IMG_BOOL {name}("))
            .unwrap();
        let end = start + services[start..].find("\n}").unwrap() + 2;
        functions.push_str(&services[start..end]);
        functions.push('\n');
    }
    fs::write(output.join("memory_services_functions.inc"), functions).unwrap();
    let executable = output.join("memory-diagnostics");
    let result = Command::new("cc")
        .args(["-std=c99", "-Wall", "-Wextra", "-Werror"])
        .arg("-I")
        .arg(&output)
        .arg(root.join("native/tests/memory_diagnostics.c"))
        .arg("-o")
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
