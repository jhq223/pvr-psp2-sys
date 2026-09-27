use std::{ffi::OsStr, fs, path::Path, process::Command};

fn run(command: &mut Command) -> Result<(), String> {
    let status = command.status().map_err(|e| format!("{command:?}: {e}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("{command:?}: {status}"))
    }
}

pub fn check(root: &Path, output: &Path, compiler: &OsStr) -> Result<(), String> {
    let root = root.canonicalize().map_err(|e| e.to_string())?;
    fs::create_dir_all(output).map_err(|e| e.to_string())?;
    let output = output.canonicalize().map_err(|e| e.to_string())?;
    let source = root.join("vendor/PVR_PSP2/eurasiacon");
    for (name, file) in [
        ("twiddle", "common/twiddle.c"),
        ("statehash", "opengles2/statehash.c"),
        ("swtexop", "opengles2/psp2/swtexop.c"),
    ] {
        let body = fs::read_to_string(source.join(file)).map_err(|e| e.to_string())?;
        let body = body
            .lines()
            .filter(|line| !line.starts_with("#include "))
            .collect::<Vec<_>>()
            .join("\n");
        fs::write(output.join(format!("{name}_functions.inc")), body).map_err(|e| e.to_string())?;
    }
    let mut uniform = String::new();
    for (file, markers) in [
        (
            "opengles2/uniform.h",
            vec!["static __inline IMG_UINT32 UniformNameHash("],
        ),
        (
            "opengles2/shader.c",
            vec![
                "static IMG_VOID FreeUniformLookups(",
                "static IMG_BOOL AssignUniformLocations(",
            ],
        ),
        (
            "opengles2/uniform.c",
            vec![
                "IMG_INTERNAL GLES2Uniform *FindUniformFromLocation(",
                "GL_APICALL int  GL_APIENTRY glGetUniformLocation(",
            ],
        ),
    ] {
        let body = fs::read_to_string(source.join(file))
            .map_err(|e| e.to_string())?
            .replace("\r\n", "\n");
        for marker in markers {
            let start = body
                .find(marker)
                .ok_or_else(|| format!("missing {marker}"))?;
            let end = start + body[start..].find("\n}").ok_or("unterminated function")? + 2;
            uniform.push_str(&body[start..end]);
            uniform.push('\n');
        }
    }
    fs::write(output.join("uniform_functions.inc"), uniform).map_err(|e| e.to_string())?;
    let body = fs::read_to_string(source.join("common/kickresource.c"))
        .map_err(|e| e.to_string())?
        .replace("\r\n", "\n");
    let mut wait = String::new();
    for marker in [
        "static IMG_BOOL WaitUntilResourceIsNotNeeded(",
        "IMG_INTERNAL IMG_BOOL KRM_WaitUntilResourceIsNotNeeded(",
        "static IMG_VOID ReclaimUnneededResourcesInList(",
        "IMG_INTERNAL IMG_VOID KRM_RetireResource(",
    ] {
        let start = body
            .find(marker)
            .ok_or_else(|| format!("missing {marker}"))?;
        let end = start + body[start..].find("\n}").ok_or("unterminated function")? + 2;
        wait.push_str(&body[start..end]);
        wait.push('\n');
    }
    fs::write(output.join("resource_wait_functions.inc"), wait).map_err(|e| e.to_string())?;
    let body = fs::read_to_string(source.join("opengles2/bufobj.c"))
        .map_err(|e| e.to_string())?
        .replace("\r\n", "\n");
    let mut buffer = String::new();
    for marker in [
        "IMG_INTERNAL IMG_VOID DestroyBufferObjectGhostKRM(",
        "static IMG_BOOL ReplaceBufferStorage(",
        "static IMG_VOID FreeBufferObject(",
        "GL_APICALL void GL_APIENTRY glBufferData(",
    ] {
        let start = body
            .find(marker)
            .ok_or_else(|| format!("missing {marker}"))?;
        let end = start + body[start..].find("\n}").ok_or("unterminated function")? + 2;
        buffer.push_str(&body[start..end]);
        buffer.push('\n');
    }
    fs::write(output.join("buffer_storage_functions.inc"), buffer).map_err(|e| e.to_string())?;
    let mut validation = String::new();
    for (file, marker) in [
        (
            "opengles2/texmgmt.c",
            "static IMG_VOID ReclaimTextureMemKRM(",
        ),
        (
            "opengles2/texmgmt.c",
            "IMG_INTERNAL IMG_VOID SetupTextureState(",
        ),
        (
            "opengles2/sgxif.c",
            "static IMG_VOID PrepareTextureDependencies(",
        ),
    ] {
        let body = fs::read_to_string(source.join(file))
            .map_err(|e| e.to_string())?
            .replace("\r\n", "\n");
        let start = body
            .find(marker)
            .ok_or_else(|| format!("missing {marker}"))?;
        let end = start + body[start..].find("\n}").ok_or("unterminated function")? + 2;
        validation.push_str(&body[start..end]);
        validation.push('\n');
    }
    fs::write(output.join("texture_validation_functions.inc"), validation)
        .map_err(|e| e.to_string())?;
    for (name, file, markers) in [
        (
            "error_origin",
            "opengles2/misc.c",
            vec!["IMG_INTERNAL IMG_VOID SetErrorFileLine("],
        ),
        (
            "texture_lifetime",
            "opengles2/texmgmt.c",
            vec![
                "static IMG_VOID DestroyTextureGhostKRM(",
                "static IMG_VOID GhostTextureStorage(",
                "IMG_INTERNAL IMG_BOOL TexMgrGhostTexture(",
                "static IMG_VOID FreeTexture(",
            ],
        ),
        (
            "vao_lifetime",
            "opengles2/vertexarrobj.c",
            vec![
                "static IMG_BOOL WaitUntilVAONotUsed(",
                "static IMG_VOID FreeVertexArrayObjectInternalPointers(GLES2Context *gc, GLES2VertexArrayObject *psVAO)\n{",
                "IMG_INTERNAL IMG_VOID DestroyVAOGhostKRM(",
                "static IMG_VOID FreeVertexArrayObject(",
                "GL_API_EXT void GL_APIENTRY glBindVertexArrayOES(",
                "GL_API_EXT void GL_APIENTRY glDeleteVertexArraysOES(",
            ],
        ),
        (
            "draw_bounds",
            "opengles2/drawvarray.c",
            vec![
                "static IMG_BOOL ValidateIndexBufferRange(",
                "static IMG_BOOL DetermineMinAndMaxIndices(",
                "static const IMG_UINT16* TransformIndicesTo16Bits(",
                "static IMG_BOOL AddMultiDrawCount(",
                "GL_API_EXT void GL_APIENTRY glMultiDrawArraysEXT(",
                "GL_API_EXT void GL_APIENTRY glMultiDrawElementsEXT(",
            ],
        ),
        (
            "binary_bounds",
            "opengles2/binshader.c",
            vec![
                "static IMG_VOID* SGXBS_Calloc(",
                "static void SGXBS_FreeAllocatedMemory(",
                "static SGXBS_Error ReadString(",
                "static IMG_UINT8 ReadU8(",
                "static IMG_UINT16 ReadU16(",
                "static IMG_UINT32 ReadU32(",
                "static IMG_FLOAT ReadFloat(",
                "static IMG_UINT16 ReadArrayHeader(",
                "static SGXBS_Error UnpackSymbolBindings(",
            ],
        ),
        (
            "texture_dependencies",
            "opengles2/drawvarray.c",
            vec![
                "static IMG_BOOL AttachTextureDependency(",
                "static IMG_BOOL AttachUsedTexturesToCurrentSurface(",
                "static IMG_BOOL AttachAllUsedResourcesToCurrentSurface(",
            ],
        ),
        (
            "swap_failures",
            "../gpu_es4_ext/eurasia/services4/srvclient/bridged/bridged_pvr_dc_glue.c",
            vec![
                "static IMG_INT32 _dcSwapChainThread(",
                "PVRSRV_ERROR IMG_CALLCONV PVRSRVSwapToDCBuffer(",
            ],
        ),
    ] {
        let body = fs::read_to_string(source.join(file))
            .map_err(|e| e.to_string())?
            .replace("\r\n", "\n");
        let mut extracted = String::new();
        for marker in markers {
            let start = body
                .find(marker)
                .ok_or_else(|| format!("missing {marker}"))?;
            let end = start + body[start..].find("\n}").ok_or("unterminated function")? + 2;
            extracted.push_str(&body[start..end]);
            extracted.push('\n');
        }
        fs::write(output.join(format!("{name}_functions.inc")), extracted)
            .map_err(|e| e.to_string())?;
    }
    for name in [
        "texture_region",
        "state_cache",
        "async_texture",
        "uniform_lookup",
        "resource_wait",
        "buffer_storage",
        "heap_layout",
        "texture_lifetime",
        "vao_lifetime",
        "draw_bounds",
        "binary_bounds",
        "swap_failures",
        "texture_dependencies",
        "texture_validation",
        "error_origin",
    ] {
        let executable = output.join(format!("{name}{}", std::env::consts::EXE_SUFFIX));
        run(Command::new(compiler)
            .args([
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                "-Wno-unused-function",
                "-Wno-unused-variable",
                "-fsanitize=address,undefined",
                "-pthread",
                "-g",
            ])
            .arg("-I")
            .arg(&output)
            .arg("-I")
            .arg(source.join("common"))
            .arg("-I")
            .arg(source.join("opengles2"))
            .arg(root.join("native/tests").join(format!("{name}.c")))
            .arg("-o")
            .arg(&executable))?;
        run(&mut Command::new(executable))?;
    }
    Ok(())
}
