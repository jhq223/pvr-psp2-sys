# pvr-psp2-sys

PS Vita 的 PVR2D、EGL 和 OpenGL ES 2.0 Rust 绑定，附带原生驱动构建工具。

## 添加依赖

```toml
[target.'cfg(target_os = "vita")'.dependencies]
pvr-psp2-sys = { git = "https://github.com/jhq223/pvr-psp2-sys.git" }
```

需要 Rust 1.95+ 和 VitaSDK。构建时使用 `arm-vita-eabi-gcc`、`arm-vita-eabi-ar`；也可通过 `TARGET_CC`、`TARGET_AR` 指定路径。

PVR2D 接口位于 crate 根模块，EGL 和 GLES2 接口位于 `gles` 模块。绑定使用 `no_std`，调用方式与 C API 一致。

## 部署

GLES2 应用需要以下四个模块：

| 文件 | 用途 |
| --- | --- |
| `libgpu_es4_ext.suprx` | GPU 服务、内存与同步 |
| `libpvrPSP2_WSEGL.suprx` | 显示与交换缓冲 |
| `libIMGEGL.suprx` | EGL 上下文与表面 |
| `libGLESv2.suprx` | OpenGL ES 2.0 |

将模块放入应用的资源目录，并在 application hints 中设置 WSEGL 和 GLES2 的路径。初始化步骤和 BC1 / BC3 纹理格式见 [驱动使用](docs/driver.md)。

使用 PVR2D 还需单独提供 `libpvr2d`。

## 构建原生驱动

在 Windows 上安装官方 PSVSDK、CMake 3.22+ 和 Ninja，将工具加入 PATH。在仓库根目录运行：

```powershell
$env:SCE_PSP2_SDK_DIR = 'C:/SDK/PSVita/sdk'
$env:CMAKE_GENERATOR = 'Ninja'
cargo build --features build-driver
```

构建结束会打印模块目录 `.../out/pvr-driver/module`。依赖本 crate 的 `build.rs` 也可通过 `DEP_PVR_PSP2_MODULES_DIR` 读取该路径。

也可以用 CMake 指定输出目录：

```powershell
cmake -S native -B target/pvr-native -G Ninja "-DPSVSDK=$env:SCE_PSP2_SDK_DIR"
cmake --build target/pvr-native --parallel 8
```

生成的模块位于 `target/pvr-native/module/`。

## 测试

```sh
cargo test
cargo clippy --all-targets -- -D warnings
```

原生 C 测试需要支持 ASan / UBSan 的 Clang 或 GCC，通过 `CC` 指定编译器：

```sh
cargo run --example pvr-native-check -- allocators . target/pvr-allocator-tests
cargo run --example pvr-native-check -- optimizations . target/pvr-optimization-tests
```
