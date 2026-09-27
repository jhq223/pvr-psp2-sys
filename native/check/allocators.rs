use std::{ffi::OsStr, fs, path::Path, process::Command};

fn run(command: &mut Command) -> Result<(), String> {
    let status = command.status().map_err(|e| format!("{command:?}: {e}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("{command:?}: {status}"))
    }
}

/// Compile the existing C failure-injection harnesses against production code.
/// These tests require a host C compiler with ASan/UBSan, not the Vita compiler.
pub fn check(root: &Path, output: &Path, compiler: &OsStr) -> Result<(), String> {
    let root = root
        .canonicalize()
        .map_err(|e| format!("{}: {e}", root.display()))?;
    let root = if cfg!(windows) {
        std::path::PathBuf::from(root.to_string_lossy().trim_start_matches(r"\\?\"))
    } else {
        root
    };
    fs::create_dir_all(output).map_err(|e| e.to_string())?;
    let output = output.canonicalize().map_err(|e| e.to_string())?;
    let output = if cfg!(windows) {
        std::path::PathBuf::from(output.to_string_lossy().trim_start_matches(r"\\?\"))
    } else {
        output
    };
    let source_path = root.join("vendor/PVR_PSP2/eurasiacon/imgegl/imgegl/srv.c");
    let source = fs::read_to_string(&source_path)
        .map_err(|e| format!("{}: {e}", source_path.display()))?
        .replace("\r\n", "\n");
    let mut functions = String::new();
    for name in ["KEGLAllocDeviceMemPsp2", "KEGLFreeDeviceMemPsp2"] {
        let marker = format!("IMG_EXPORT PVRSRV_ERROR IMG_CALLCONV {name}(");
        if source.matches(&marker).count() != 1 {
            return Err(format!(
                "expected exactly one production definition of {name}"
            ));
        }
        let start = source.find(&marker).unwrap();
        let length = source[start..]
            .find("\n}")
            .ok_or_else(|| format!("unterminated {name}"))?
            + 2;
        functions.push_str(&source[start..start + length]);
        functions.push_str("\n\n");
    }
    fs::write(output.join("device_mem_functions.inc"), functions).map_err(|e| e.to_string())?;
    let source = fs::read_to_string(root.join("vendor/PVR_PSP2/eurasiacon/opengles2/shader.c"))
        .map_err(|e| e.to_string())?
        .replace("\r\n", "\n");
    let start = source
        .find("static IMG_VOID UseProgram(")
        .ok_or("missing UseProgram")?;
    let end = start
        + source[start..]
            .find("\n}")
            .ok_or("unterminated UseProgram")?
        + 2;
    fs::write(
        output.join("program_switch_functions.inc"),
        &source[start..end],
    )
    .map_err(|e| e.to_string())?;
    for name in ["render_target_alloc", "device_mem", "program_switch"] {
        let executable = output.join(format!("{name}{}", std::env::consts::EXE_SUFFIX));
        run(Command::new(compiler)
            .args([
                "-std=c99",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                "-fsanitize=address,undefined",
            ])
            .arg("-I")
            .arg(&output)
            .arg(root.join("native/tests").join(format!("{name}.c")))
            .arg("-o")
            .arg(&executable))?;
        run(&mut Command::new(executable))?;
    }
    Ok(())
}
