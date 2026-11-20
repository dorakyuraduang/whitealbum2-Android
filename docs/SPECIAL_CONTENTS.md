# Special Contents 静态重编译

2026-11-20 接入 `WA2_Special_cn.exe`，输入身份为
`1a93d3126a90c68d07c0f79ec19e3b282b3084538e1f92c3456525ba93103ae1`。
授权只覆盖这个 Special 输入的静态分析；`WA2_chs.exe` 的禁读、禁重新分析约束保持有效。
生成的游戏 C++、CFG、初始内存和分析记录均只留在被 Git 忽略的本地目录。

## Android 采用一个 APK

沿用现有启动页的正篇和 **Special Contents** 入口。两者共用同一套窗口、
输入、图形、音频、文件访问和设备适配源码。两套原始程序分别生成静态 C++；
每套生成结果同时供 Windows x64 和 Android ARM64 编译，不按平台另写游戏逻辑。

Special 的原程序地址、CRT、资源表和部分 UI 与正篇不同，不能直接使用正篇地址补丁。
它在非导出的 `SpecialContentsActivity`、`:special` 进程中加载 `libwa2_special.so`。
正篇继续加载原来的运行库。进程隔离使重叠的源程序地址、SDL/JNI 会话、资源选择
和配置互不覆盖，也无需维护第二个应用的安装、签名和启动接口。

合包核验会逐字节保留已验证正篇 APK 的三个运行库和许可证条目，只新增 Special
运行库及对应入口。合包版本为 `0.2.109-special-aot`；正篇运行库的诊断版本仍是
`0.2.108-shared-aot`。安装使用 `adb install -r`，核对版本、UID、首次安装时间和
已安装 APK 的 SHA-256，不卸载、不清数据、不启动应用。

## 资源和存档

点击启动页的 **Special Contents**，选择该版本自己的原资源文件夹并授予持久读写权限。
保留它自己的 PAK 文件名和内容，不与正篇的同名 PAK 混放，也不需要解包。
文件夹名称不限；原始 PAK 不进入 APK。Special 的目录授权不会改写正篇的目录设置。

Special 存档和 INI 配置位于：

```text
<Special 资源目录>/Documents/Leaf/WHITE ALBUM2 Special Contents/
```

源程序的根目录存档/配置别名也映射到这里。正篇 IC/CC 进度目录在 Special 中仅可读；
写入、删除和移动到正篇存档目录会被拒绝。不会自动复制或合并现有存档。
当前没有为 Special 应用正篇的语言替换、附加字幕、全部解锁或固定地址加速补丁。

## 复现

在已有工具依赖的 Windows 工作区，先构建 `build/special-toolchain/winrecomp.exe`，
然后执行以下步骤。编译器与测试子进程必须隐藏、低于正常优先级、每次一个构建任务。

```powershell
python scripts/prepare_special_contents.py --exe 'D:\WHITE ALBUM2 Special Contents\WA2_Special_cn.exe'
python scripts/verify_special_contents.py
python scripts/package_launcher.py --integrated --with-special
```

首次准备检查授权和 SHA，进行静态 CFG/回调发现并保存初始内存。
后续准备复用核验过的缓存，不再读取输入 EXE。原始程序始终不执行。
验证器生成同一套公共 AOT，依次构建两种架构，运行有限的 Windows 内存检查，
比较实际编译的 Special 源文件清单与哈希，并核验 ARM64 ELF 的 16 KiB 装载对齐。
合包必须有当前源码绑定的验证记录，完成签名/内容核验后自动尝试在已授权手机原位安装。
设备不可用时明确记录安装待完成。

本地产物和记录：

- `out/android/WA2-arm64-0.2.109-special-aot.apk`
- `out/special-contents/WA2_Special_SDL2_x64.exe`（使用 `--root <Special 资源目录>`）
- `logs/special-aot-verification.json`
- `logs/special-android-installed.json`

## 验证范围

静态发现得到 1,737 个函数候选、139,324 个指令位置，合并为 1,475 个静态函数体。
所有已发现位置均生成 C++，直接调用目标闭合；这些数量不是整部游戏完成率。
有限内存检查包括 2,304 组纯整数路径差分、不可写代码证明、未知入口拒绝、
224 项导入绑定、宽字符 API、文件枚举及 Special/正篇存档写入隔离。
测试不执行游戏启动入口，不创建窗口、图形设备或音频设备，不读取屏幕。

2026-11-20 本地核验通过：两种架构使用相同的 18 个生成 C++ 文件；139,324 个
入口及 309,411 个指令内部地址逐一核对；Java 文件路径检查为 1,064 个有效案例和
25 个拒绝案例。APK 的 6 个宿主 JNI 方法已核对，其中两个按现有 `JNI_OnLoad`
服务通过 `RegisterNatives` 注册。测试包已在授权手机原位安装，版本为
`0.2.109-special-aot`（112），已安装包 SHA-256 与本地签名产物一致。
安装保留应用数据、UID 和首次安装时间，没有启动游戏或访问屏幕。

ARM64 最终库的符号审计包含 Special 函数，不包含正篇静态函数或指令解码器。
Windows 的 Zig MinSizeRel EXE 不暴露最终符号表，因此核验的是其实际编译静态库；
Windows 最终 EXE 符号审计明确记为不可用，不将它写成已完成的检查。

新增公共 ABI 适配覆盖 Special CRT 使用的宽字符文件/模块/目录接口、启动信息、
`FindFirstFileExA` 和 D3DX 43 到现有渲染实现的相同签名接口。
宽字符文件名必须能无损转换为现有 CP936 路径契约，否则返回错误 1113。
不支持的文件搜索参数返回错误 87；LFH 设置请求返回错误 120，不伪造成功。
CPU 特性查询按公共 CPUID 契约报告 CX8、TSC、SSE、SSE2，不读取物理宿主的特性。

50 处 x87 超越运算位置继续保留现有公共后端的显式停止边界。
未发现的间接入口、其他未支持行为仍明确报错，没有解释器、JIT 或运行时解码回退。
整部剧情、全部 UI/附加内容、存档兼容及手机性能尚需用户控制的实际运行验证；
组件检查和安装成功都不能证明整部游戏兼容，当前不作 Android 性能提升声明。
