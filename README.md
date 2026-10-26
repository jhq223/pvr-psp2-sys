# pvr-psp2-sys

PS Vita 的 PVR2D、EGL 和 OpenGL ES 2.0 Rust 绑定，附带原生驱动构建工具。

[更新记录](CHANGELOG.md)

## 添加依赖

```toml
[target.'cfg(target_os = "vita")'.dependencies]
pvr-psp2-sys = { git = "https://github.com/jhq223/pvr-psp2-sys.git" }
```

需要 Rust 1.95+ 和 VitaSDK。构建时使用 `arm-vita-eabi-gcc`、`arm-vita-eabi-ar`；也可通过 `TARGET_CC`、`TARGET_AR` 指定路径。

PVR2D 接口位于 crate 根模块，EGL 和 GLES2 接口位于 `gles` 模块。原始绑定使用 `no_std`，调用方式与 C API 一致。

可选的 `std` 接口按所有权管理原生资源：

| Feature | 接口 |
| --- | --- |
| `platform` | 控制器、触摸、系统键盘、电源通知、事件等待、AudioOut 和文件 IO |
| `graphics` | EGL 上下文、纹理、着色器程序、绘制和 GPU fence |
| `video` | 从可定位的数据流读取并硬解 H.264/AAC，原生分配接入调用者的内存额度 |

接口文档位于对应 Rust 声明旁。

## PC GLES 测量

启用 `graphics` 后，`graphics::Context::headless([960, 544], false)` 使用主机 EGL 创建 RGBA8 pbuffer，并复用 Vita 的纹理、绘制和 fence 接口。主机需提供 EGL 1.4、OpenGL ES 2.0 和 `EGL_KHR_fence_sync`。Linux 加载 `libEGL.so.1`，Windows 加载 `libEGL.dll`。

PC 通过 `ShaderCode::Source` 编译 GLSL ES，通过标准 S3TC 上传 BC1/BC3；Vita 继续使用 SGX 二进制、原生压缩格式和块重排。`driver_info()` 返回实际驱动信息。pbuffer 测量通过 fence 等待提交完成；应用自行控制帧率。

```sh
cargo test --locked --offline --features graphics --test graphics -- --ignored --test-threads=1 --nocapture
```

该检查使用离屏上下文执行上传、绘制、交换与 fence，记录主机驱动耗时，并验证资源持有及关闭后重建。

WSL 使用已安装的 Mesa D3D12 驱动时，可在相同命令前设置以下环境。`LD_PRELOAD` 使 D3D12 代码库覆盖线程退出清理阶段，纹理、上下文和 surface 仍显式释放。

```sh
EGL_PLATFORM=surfaceless GALLIUM_DRIVER=d3d12 \
LD_PRELOAD=/usr/lib/wsl/lib/libd3d12core.so \
cargo test --locked --offline --release --features graphics --test graphics -- --ignored --test-threads=1 --nocapture
```

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
cargo run --example pvr-native-check -- fences . target/pvr-fence-tests
```
