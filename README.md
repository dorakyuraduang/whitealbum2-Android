# WHITE ALBUM2 静态重编译版（开发中）

面向 Windows x64 与 Android ARM64 的共享静态重编译开发项目。两平台编译同一套游戏静态函数和原生子系统，只在窗口、输入、设备、文件访问和呈现处保留平台适配。

## 版本与分支

本仓库保留两条实现路线，按分支分别维护：

| 版本 | 分支 | 实现路线 |
| --- | --- | --- |
| **共享静态重编译版（WA2 Shared Recomp）** | [main（默认）](https://github.com/dorakyuraduang/wa2-godot/tree/main) | C++ 静态函数重编译与原生子系统替换；Windows x64、Android ARM64 编译同一套游戏逻辑和静态函数源码。 |
| **Godot 移植版** | [godot](https://github.com/dorakyuraduang/wa2-godot/tree/godot) | 此前的 Godot 引擎移植工程，保留原有代码、文档、许可证和提交历史。 |

以下说明针对 `main` 的静态重编译版。Godot 版的安装、构建和许可范围请查看 [godot 分支 README](https://github.com/dorakyuraduang/wa2-godot/blob/godot/README.md)；两条实现的许可证各自适用。

安装包也请按实现类型区分：已有的 `v0.3b`、`v0.2.9` 等历史 Releases 属于 Godot 版；本分支使用 `shared-aot` 版本标识，当前为 `0.2.27-shared-aot`。两条路线的版本号分别维护。

**项目尚未完成。** 当前是“静态重编译主体 + 原生子系统重建”的混合实现，仍保留原游戏的 32 位地址布局、寄存器及部分浮点状态。完整剧情兼容、手机帧率以及接近原版的整体性能尚未验证。

**公开源代码不包含原版 EXE、PAK、生成的游戏函数、初始化 RAM 或私有分析缓存。全游戏构建还需要自行提供有权使用的既有缓存输入；全新公开克隆不能独立构建完整游戏。** MIT 只覆盖本项目新编写的代码和文档，第三方代码与游戏衍生输入各有许可边界，见 [NOTICE](NOTICE.md)。

## 当前版本

`0.2.27-shared-aot`（开发版本）。Windows 与 Android 使用相同的核心源码。

- Android 启动页：先选择资源文件夹、保存 FPS 与帧耗时显示开关，再点击“开始游戏”。
- 两平台应用图标来自根目录 `icon.ico`；Windows 嵌入原始 ICO，Android 无损转换同一图像并提供自适应桌面图标。
- 资源默认在内部存储 `/Wa2Res`，也支持系统选择器授权的本地嵌套目录或 SD 卡 `Wa2Res`；保留原版 IC 子目录，直接读取 PAK。
- 图片解压结果缓存最多 8 MiB / 32 项；返回给游戏的可修改缓冲与缓存分开。
- 大资源缓冲区增加独立的有界复用，最多 2 个空闲块、合计 4 MiB；保持旧运行时结构 ABI，保留清零、权限检查及堆销毁释放行为。
- 天气粒子的离屏绘制和模式 2 滤波可进入已接纳的共用原生绘制路径；保留粒子数、顶点、顺序和采样状态。
- 整数纹理坐标的文字和图片只计算实际覆盖的图块及其边界，避免逐字扫描整张字体图集；小数坐标保留完整计算，顶点位模式与绘制顺序经内存对照。

现有静态函数集接纳 1,692 个入口，合并为 1,381 个函数体，覆盖 119,649 个指令位置；还有 181 个延后入口和 13 个不支持的超越函数位置。未知目标和不支持行为明确报错，不添加运行时译码或 JIT。

## 使用与开发

- [Android 目录授权与启动设置](docs/ANDROID.md)
- [构建前提、工具链及验证流程](docs/BUILDING.md)
- [架构与当前边界](docs/ARCHITECTURE.md)
- [贡献约束](CONTRIBUTING.md) / [安全与隐私](SECURITY.md)
- [第三方许可证](THIRD_PARTY_NOTICES.md) / [MIT](LICENSE)
- [历史开发记录](docs/DEVELOPMENT_HISTORY.zh-CN.md)

在具备所需私有缓存与工具链的本地工作区，先运行 `python scripts/verify_shared_aot.py`，通过后运行 `python scripts/package_shared_hosts.py`。所有构建和检查必须隐藏、低优先级、单任务执行。当前约束禁止启动原版或游戏、模拟器、UI 自动操作与屏幕监控。

组件验证覆盖资源输出与字典、ABI、寄存器与浮点状态、绘制数据、内存边界、两平台同源编译、JNI、APK 签名和对齐。**编译或内存组件提速不代表完整游戏兼容或手机帧率提升相同倍数。** 每次本地交付由 `out/build-info.json` 与对应验证记录绑定具体源码和产物。

APK 和 EXE 通过本地构建生成，不作为公共源码的一部分。静态链接 FFmpeg 的分发还必须提供匹配的 `WA2-shared-relink.zip`、对应源码与配置，不能只分发一个 APK。

## License / English summary

An incomplete shared static-recompilation port with native subsystem replacements. Windows x64 and Android ARM64 compile one common game source set; platform-specific game implementations, runtime instruction decoding and JIT are forbidden. Original game files, derived game sources and private cached build inputs are not distributed here. New project code is MIT; upstream components keep their licenses. This project is unofficial and does not grant rights to the original game.
