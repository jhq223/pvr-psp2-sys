#![cfg(target_os = "linux")]

use std::{fs, path::Path, process::Command};

#[test]
fn early_return_around_nested_gradient_region_has_entry_sync() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let output = Path::new(env!("CARGO_TARGET_TMPDIR")).join("compiler-sync-flow");
    fs::create_dir_all(&output).unwrap();
    let source = fs::read_to_string(root.join("vendor/PVR_PSP2/tools/intern/usc2/pregalloc.c"))
        .unwrap()
        .replace("\r\n", "\n");
    let edges = source
        .split_once("typedef struct _EDGE_LIST_")
        .unwrap()
        .1
        .split_once("//First pass - locate syncs")
        .unwrap()
        .0;
    let sync = source
        .split_once("static IMG_VOID SetSyncEnd(")
        .unwrap()
        .1
        .split_once("static\nIMG_VOID SortDomChildren(")
        .unwrap()
        .0;
    fs::write(
        output.join("sync-flow-functions.inc"),
        format!("typedef struct _EDGE_LIST_{edges}\nstatic IMG_VOID SetSyncEnd({sync}"),
    )
    .unwrap();
    let executable = output.join("sync-flow");
    let result = Command::new("cc")
        .args(["-std=c99", "-Wall", "-Wextra", "-Werror"])
        .arg("-I")
        .arg(&output)
        .arg(root.join("native/tests/compiler_sync.c"))
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
