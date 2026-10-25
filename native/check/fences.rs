use std::{ffi::OsStr, fs, path::Path, process::Command};

/// Compile the production EGL fence helpers against a deterministic device/clock.
pub fn check(root: &Path, output: &Path, compiler: &OsStr) -> Result<(), String> {
    fs::create_dir_all(output).map_err(|e| e.to_string())?;
    let source =
        fs::read_to_string(root.join("vendor/PVR_PSP2/eurasiacon/imgegl/imgegl/egl_sync.c"))
            .map_err(|e| e.to_string())?
            .replace("\r\n", "\n");
    let mut helpers = String::new();
    for marker in [
        "static IMG_BOOL _fencePending(",
        "static PVRSRV_ERROR _queryFence(",
        "static EGLBoolean _syncComplete(",
        "static EGLBoolean _deleteSync(",
        "static void _cleanupSyncs(",
        "static EGLint _waitFence(KEGL_SYNC *psSync, SrvSysContext *psSysContext, EGLTimeKHR timeout)\n{",
        "IMG_INTERNAL EGLBoolean TerminateSyncs(",
    ] {
        let start = source
            .find(marker)
            .ok_or_else(|| format!("missing {marker}"))?;
        let end = start
            + source[start..]
                .find("\n}")
                .ok_or("unterminated fence helper")?
            + 2;
        helpers.push_str(&source[start..end]);
        helpers.push('\n');
    }
    fs::write(output.join("egl_fence_functions.inc"), helpers).map_err(|e| e.to_string())?;
    let executable = output.join(format!("egl_fence{}", std::env::consts::EXE_SUFFIX));
    let status = Command::new(compiler)
        .args([
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-g",
        ])
        .arg("-I")
        .arg(output)
        .arg(root.join("native/tests/egl_fence.c"))
        .arg("-o")
        .arg(&executable)
        .status()
        .map_err(|e| e.to_string())?;
    if !status.success() {
        return Err(format!("fence helper compilation: {status}"));
    }
    let status = Command::new(&executable)
        .status()
        .map_err(|e| e.to_string())?;
    if !status.success() {
        return Err(format!("fence helper behavior: {status}"));
    }
    Ok(())
}
