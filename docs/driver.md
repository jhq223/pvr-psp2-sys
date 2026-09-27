# 驱动使用

构建方法见 [README](../README.md#构建原生驱动)。应用需部署同一次构建生成的四个运行模块。

## 初始化

加载 GPU 扩展和 EGL 模块，调用 `PVRSRVInitializeAppHint` 取得默认配置。设置 WSEGL、GLES2 模块路径后，通过 `PVRSRVCreateVirtualAppHint` 注册，再创建 EGL 上下文和表面。

可调整的纹理工作线程参数：

| 参数 | 用途 |
| --- | --- |
| `bDisableAsyncTextureOp` | 关闭 CPU 异步纹理处理 |
| `ui32SwTexOpThreadNum` | 工作线程数，最多 4 个 |
| `ui32SwTexOpMaxUltNum` | 任务槽数，最多 4096 个 |

## BC1 / BC3 纹理

先查询扩展字符串中的 `GL_KRKR_texture_compression_bc`，再使用以下格式上传：

| 格式编号 | 格式 | 每个 4×4 块 |
| --- | --- | --- |
| `0x60000001` | BC1 RGB，Alpha 固定为 1 | 8 字节 |
| `0x60000002` | BC3 RGBA | 16 字节 |

使用 `glCompressedTexImage2D` 上传完整的 `GL_TEXTURE_2D` 第 0 级。宽高须为至少 8 的二次幂，并在设备纹理尺寸上限内。

块内数据采用标准 BC 小端编码，块间按 SGX543 的 Y 位优先 Morton 顺序排列。纹理使用非 mipmap 过滤，不支持子区域更新、自动 mipmap 或作为 FBO 附件。
