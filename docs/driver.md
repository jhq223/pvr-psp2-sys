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

## 离屏渲染

单上下文、仅颜色附件的 FBO 以 16 个底层渲染表面为缓存目标。需要空间时，驱动回收最久未使用且 GPU 已完成操作的底层表面；纹理像素和 GL 对象保留，再次使用时自动重建表面。16 是缓存目标，应用可以创建更多 FBO。

带深度/模板附件或共享上下文的表面由应用管理。纹理存储继续占用内存，应用应及时释放用完的图像。

## 内存用量

`graphics::Context::target_layout(size)` 用固定布局计算单采样 RGBA8 色彩存储；Vita STRIDE 的行距按 8 texel 对齐。逻辑尺寸和采样坐标保持原尺寸。`target_footprint(size)` 在显式诊断时额外调用与驱动相同参数的 GXM 查询，返回独立 USER_NC RTData 大小。

`memory_usage()` 通过本次构建的两个 proc-address getter 读取原生当前用量。纹理 USER_CDRAM/USER_NC 堆分别报告保留、使用和块数；SGXDriver USER_NC、SGXParamBuffer USER_CDRAM 分别报告注册块；RTData 按实际 kernel 映射成功分配/释放计量。PDS、USSE、状态等直接设备分配报告子分配使用量，属于已注册存储，不能再与保留量相加。各域峰值独立，各纹理堆是相邻采样；它们不表示同一时点的总物理峰值。主机 EGL 返回布局及 `None` 原生内存诊断。

## GPU 完成通知

查询 `EGL_KHR_fence_sync` 后，用 `eglCreateSyncKHR` 在当前上下文插入完成标记。`eglClientWaitSyncKHR` 的零超时调用查询一次，有限超时按经过时间等待。应用应在完成后释放提交持有的资源。销毁尚未完成的 sync 会保留其底层存储，直至 GPU 与等待者都已结束。

## BC1 / BC3 纹理

先查询扩展字符串中的 `GL_KRKR_texture_compression_bc`，再使用以下格式上传：

| 格式编号 | 格式 | 每个 4×4 块 |
| --- | --- | --- |
| `0x60000001` | BC1 RGB，Alpha 固定为 1 | 8 字节 |
| `0x60000002` | BC3 RGBA | 16 字节 |

使用 `glCompressedTexImage2D` 上传完整的 `GL_TEXTURE_2D` 第 0 级。宽高须为至少 8 的二次幂，并在设备纹理尺寸上限内。

块内数据采用标准 BC 小端编码，块间按 SGX543 的 Y 位优先 Morton 顺序排列。纹理供采样使用，过滤方式为最近邻或线性；内容更新使用完整第 0 级上传。FBO 附件及 mipmap 使用驱动支持的其他纹理格式。
