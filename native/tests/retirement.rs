#![cfg(target_os = "linux")]
use std::{fs, path::Path, process::Command};

#[test]
fn production_retirement_stops_at_every_failed_ownership_phase() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let output = Path::new(env!("CARGO_TARGET_TMPDIR")).join("native-retirement");
    fs::create_dir_all(&output).unwrap();
    let executable = output.join("retirement");
    let result = Command::new("cc")
        .args([
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
        ])
        .arg(root.join("native/tests/retirement.c"))
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

#[test]
fn krm_callbacks_cannot_free_later_owners_or_manager_metadata_after_failure() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let output = Path::new(env!("CARGO_TARGET_TMPDIR")).join("krm-retirement");
    fs::create_dir_all(&output).unwrap();
    let bytes = fs::read(root.join("vendor/PVR_PSP2/eurasiacon/common/kickresource.c")).unwrap();
    let source = String::from_utf8_lossy(&bytes).replace("\r\n", "\n");
    let mut functions = String::new();
    for marker in [
        "static IMG_VOID RemoveResourceFromAllLists(",
        "static IMG_VOID ReclaimUnneededResourcesInList(",
        "IMG_INTERNAL IMG_VOID KRM_Destroy(",
    ] {
        let start = source.rfind(marker).unwrap();
        let end = start + source[start..].find("\n}").unwrap() + 2;
        functions.push_str(&source[start..end]);
        functions.push('\n');
    }
    fs::write(output.join("krm_retirement_functions.inc"), functions).unwrap();
    let executable = output.join("krm-retirement");
    let result = Command::new("cc")
        .args([
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
        ])
        .arg("-I")
        .arg(&output)
        .arg(root.join("native/tests/krm_retirement.c"))
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

#[test]
fn failed_egl_and_code_heap_owners_keep_their_wrappers_and_backing() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let output = Path::new(env!("CARGO_TARGET_TMPDIR")).join("native-owner-retirement");
    fs::create_dir_all(&output).unwrap();
    for (source, names, destination) in [
        (
            "imgegl/imgegl/khronos_egl.c",
            &[
                "static EGLint _ContextDelete(",
                "static IMG_BOOL _SurfaceDelete(",
            ][..],
            "egl_owner_functions.inc",
        ),
        (
            "common/codeheap.c",
            &["IMG_INTERNAL PVRSRV_ERROR UCH_CodeHeapDestroy("][..],
            "codeheap_owner_functions.inc",
        ),
    ] {
        let source = fs::read(root.join("vendor/PVR_PSP2/eurasiacon").join(source)).unwrap();
        let source = String::from_utf8_lossy(&source).replace("\r\n", "\n");
        let mut functions = String::new();
        for name in names {
            let start = source.rfind(name).unwrap();
            let end = start + source[start..].find("\n}").unwrap() + 2;
            functions.push_str(&source[start..end]);
            functions.push('\n');
        }
        fs::write(output.join(destination), functions).unwrap();
    }
    let executable = output.join("owner-retirement");
    let result = Command::new("cc")
        .args([
            "-std=c99",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
        ])
        .arg("-I")
        .arg(&output)
        .arg(root.join("native/tests/owner_retirement.c"))
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
