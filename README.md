# pvr-psp2-sys

PlayStation Vita PVR 图形库的 Rust FFI，以及配套原生 GLES2 驱动的构建工具。

- crate 根模块提供 PVR2D 类型和函数声明。
- `gles` 模块提供 EGL、OpenGL ES 2.0 和 PVR application hint 声明。
- `build-driver` feature 可在 Windows 上构建四个 GLES2 运行时模块。

Rust 接口库使用 `no_std`，没有运行时 Rust 依赖。接口保留原生 C API 的调用方式，不提供安全封装或自动资源管理。

## 添加依赖

从 GitHub 添加依赖：

```toml
[target.'cfg(target_os = "vita")'.dependencies]
pvr-psp2-sys = { git = "https://github.com/jhq223/pvr-psp2-sys.git" }
```

需要 Rust 1.95 或更新版本。Vita 目标构建使用 `arm-vita-eabi-gcc` 和 `arm-vita-eabi-ar` 生成 PVR2D 导入桩，并链接随 crate 提供的 EGL、GPU 扩展和 GLES2 弱导入桩。可通过 `TARGET_CC`、`TARGET_AR` 指定工具路径。

导入桩只记录模块及函数编号，不包含驱动实现。应用负责部署、加载匹配的原生模块，并遵守上下文、线程、指针和资源生命周期要求。在 Windows 或 Linux 上构建接口库可用于类型检查与主机工具，但不会提供桌面图形实现。

## 初始化与运行时部署

GLES2 应用需要以下四个模块：

| 模块 | 职责 |
| --- | --- |
| `libgpu_es4_ext.suprx` | GPU 服务、内存、同步和传输支持 |
| `libpvrPSP2_WSEGL.suprx` | 显示表面和交换缓冲的平台适配 |
| `libIMGEGL.suprx` | EGL 上下文、表面及模块加载 |
| `libGLESv2.suprx` | GLES2 绘制、纹理、FBO 和着色器编译 |

先加载 GPU 扩展和 EGL，初始化 application hints，再创建 EGL 上下文和表面。EGL 按配置加载 WSEGL 与 GLES2，因此相应模块也必须部署在可加载路径中。

`PvrAppHint::default()` 仅将 Rust 结构清零。应先调用 `PVRSRVInitializeAppHint` 填充驱动默认值，再覆盖所需配置，并通过 `PVRSRVCreateVirtualAppHint` 注册配置。模块路径、缓冲大小等字段须与应用部署方式一致。

PVR2D API 另外需要匹配的 `libpvr2d` 运行时；上述四模块构建不生成 PVR2D 模块。

## 构建原生驱动

此功能目前使用 Windows 版官方 PSVSDK，需要自行准备：

- SDK 中的 `psp2snc.exe`、`psp2ld.exe` 和 `armlibgen.exe`。
- CMake 3.22 或更新版本、Ninja、Rust。

以下命令在 crate 根目录运行；将 SDK 路径替换为实际安装位置：

```powershell
$env:SCE_PSP2_SDK_DIR = 'C:/SDK/PSVita/sdk'
$env:CMAKE_GENERATOR = 'Ninja'
cargo build --features build-driver
```

`cmake`、`ninja` 和 `rustc` 默认从 PATH 查找；可通过 `CMAKE`、`CMAKE_MAKE_PROGRAM`、`RUSTC` 指定路径。

构建完成后会打印 `PVR driver modules: .../out/pvr-driver/module`。该目录包含四个 `.suprx` 和对应导入桩；将所需模块作为应用资源部署。直接依赖本 crate 的构建脚本也可从 `DEP_PVR_PSP2_MODULES_DIR` 读取输出目录，此变量仅在启用 `build-driver` 时提供。

驱动使用固定的 Release/O3/Thumb2 配置，Cargo 的 dev/release 配置只影响 Rust 部分。构建会校验四个模块的原有导出编号，不自动复制模块到应用，也不生成 VPK。默认关闭 `build-driver`，使用预编译模块时无需官方 SDK。

需要固定输出目录时，可以直接运行 CMake：

```powershell
cmake -S native -B target/pvr-native -G Ninja "-DPSVSDK=$env:SCE_PSP2_SDK_DIR"
cmake --build target/pvr-native --parallel 8
```

此方式的模块位于 `target/pvr-native/module/`；Ninja 不在 PATH 时，配置命令需另加 `-DCMAKE_MAKE_PROGRAM=<ninja 路径>`。

## 驱动兼容性

随附驱动包含程序切换、异步纹理上传寿命、分配失败回收和 SGX543 编译器修复，并提供有限的 BC1 / BC3 纹理扩展。接口约束及与旧模块的差异见 [驱动兼容性](docs/driver.md)。更换公共结构或模块导入导出时，应一起构建、部署匹配的模块。

## 检查与测试

在 crate 根目录运行 Rust 检查，无需启用 `build-driver`：

```sh
cargo test
cargo clippy --all-targets -- -D warnings
cargo doc --no-deps
```

`native/tests/` 还包含原生资源分配和程序切换的故障注入测试，直接调用生产源码中的相关函数。运行这些测试需要支持 GCC 风格参数及 ASan/UBSan 的主机 C 编译器；通过 `CC` 选择编译器，默认使用 `cc`：

```sh
cargo run --example pvr-native-check -- allocators . target/pvr-allocator-tests
```

主机测试覆盖 ABI 校验和 CPU 端资源处理。ARM 模块的加载、GPU 同步、显示与性能需要在 Vita 上验证。

## 源码布局

| 路径 | 内容 |
| --- | --- |
| `src/` | Rust FFI |
| `stubs/`、`vendor/vitasdk-stubs/` | VitaSDK 链接器使用的导入桩 |
| `vendor/PVR_PSP2/` | PVR_PSP2 驱动源码，保留原有版权声明 |
| `native/` | CMake 工程、SDK 补充声明、导出编号校验和主机测试 |

构建所需的驱动源码随 crate 提供，不依赖外部参考目录。官方 PSVSDK 由使用者自行提供。
