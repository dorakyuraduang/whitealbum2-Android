# Windows 程序静态重编译到 Android 的工作流

适用于要把自有或明确获准移植的 Windows 原生程序带到 Android 的项目。以 **PE32/i386 → Android ARM64** 为实操示例。先用自制样例验证工具链，再按目标程序的实际依赖移植和测量性能。Windows x64 可以用作离线调试宿主。文末有可交给另一个项目的任务模板。

静态重编译在开发机上把机器指令转换为 C++，再用 Android NDK 编译。应用运行时调用已编译的函数，保留原程序的寄存器、地址和调用约定，由适配层提供文件、窗口、音频等服务。原 EXE 在分析和映像初始化阶段只作为数据处理。

~~~mermaid
flowchart TD
    A["确认输入架构、版本和依赖"] --> B["离线分析 PE、控制流和回调"]
    B --> C["生成共用 C++ 和静态入口表"]
    C --> D["补齐原 ABI 与 Android 服务"]
    D --> E["NDK 编译、APK 签名与验收"]
    E --> F["真机测量并定位瓶颈"]
    F --> G["优化一处并做语义回归"]
    G --> F
~~~

## 1. 判断目标适不适合这条路线

| 输入情况 | 第一步 |
| --- | --- |
| 有可移植的源码 | 先评估直接用 NDK 或原引擎编译。把静态重编译留给确实只有机器码的部分。 |
| 原生 PE32，机器类型为 i386 | 用 WinRecomp 验证纯计算函数。列出实际用到的 CRT、Win32 API 和图形、音频、存储依赖。 |
| PE32+（如 x86-64、ARM64）或其他机器类型 | 先验证支持该机器类型、指令和调用约定的转换器。下文的 WinRecomp 示例限定在 i386。 |
| .NET 或其他托管程序 | 先识别 CLR 等运行时和原生依赖，从对应的跨平台运行时或可移植源码入手。混合程序逐个模块评估。 |
| 依赖动态生成代码、运行时装载 DLL、驱动或复杂 COM 服务 | 做小实验确认这些依赖能否改接受支持的接口。把缺少实现的部分列为明确的阻塞项。 |

给每个目标建立版本档案，记录输入 SHA-256、PE 机器类型、映像基址、入口、所需 DLL 及其版本、目标 Android ABI 和最低 API 级别。记录可支持的功能以及未验证的功能。注意区分文件偏移、RVA 和原程序虚拟地址 VA。命令中的 `--entry` 和 `--seed` 使用 VA。 [Microsoft PE 规范](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)

## 2. 用自制 PE32 样例跑通生成工具

以下命令从新项目根目录执行。工具需要 Git、Python 3、CMake 3.20+、Ninja 和 x64 的 C++20 工具链。Windows 可使用 x64 MSVC 开发者 PowerShell。这里固定到 WA2 记录过的 WinRecomp 上游提交，只拉构建需要的 Zydis 子模块及其依赖。 [WinRecomp](https://github.com/Zippingrocks/WindowsRecompTool) · [WA2 的版本记录](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/patches/upstream.json)

~~~powershell
$toolSource = 'build/tools/WinRecomp'
$toolBuild = 'build/winrecomp'
$toolRevision = '28903ea344b77ea97b6794151b18c3b63f8e3363'
New-Item -ItemType Directory -Force 'build/tools' | Out-Null
git clone --no-checkout https://github.com/Zippingrocks/WindowsRecompTool.git $toolSource
if ($LASTEXITCODE -ne 0) { throw 'WinRecomp clone failed' }
git -C $toolSource checkout --detach $toolRevision
if ($LASTEXITCODE -ne 0) { throw 'WinRecomp checkout failed' }
git -C $toolSource submodule update --init --recursive third_party/zydis
if ($LASTEXITCODE -ne 0) { throw 'Zydis checkout failed' }
cmake -S $toolSource -B $toolBuild -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'WinRecomp configuration failed' }
cmake --build $toolBuild --target winrecomp --parallel 1
~~~

先生成只有 `mov eax, 42` 和 `ret` 两条指令的自制样例。用 WinRecomp 自带的测试工具构造文件，样例只供静态读取。

~~~powershell
@'
import sys
from pathlib import Path

sys.path.insert(0, "build/tools/WinRecomp/tests")
from fixtures import make_pe

probe = Path("build/aot-probe/input.exe")
probe.parent.mkdir(parents=True, exist_ok=True)
probe.write_bytes(make_pe(bytes.fromhex("b8 2a 00 00 00 c3")))
'@ | python -
~~~

下列命令生成 PE 清单、控制流图（CFG）、指令支持清单、单个代码片段和原始 C++ 工程。首次执行使用新的输出目录，`project` 要求 `raw` 尚未存在。

~~~powershell
$winRecomp = (Resolve-Path 'build/winrecomp/winrecomp.exe').Path
$sourcePe = 'build/aot-probe/input.exe'
$probeDir = 'build/aot-probe'

function Invoke-WinRecomp {
    & $winRecomp @args
    if ($LASTEXITCODE -ne 0) { throw "WinRecomp failed with exit code $LASTEXITCODE" }
}

Invoke-WinRecomp analyze $sourcePe "$probeDir/pe.json"
Invoke-WinRecomp cfg $sourcePe "$probeDir/cfg.json"
Invoke-WinRecomp coverage $sourcePe "$probeDir/coverage.json"

$graph = Get-Content "$probeDir/cfg.json" -Raw | ConvertFrom-Json
$coverage = Get-Content "$probeDir/coverage.json" -Raw | ConvertFrom-Json
if ($graph.budget_exhausted -or $graph.diagnostics.Count -gt 0 -or $coverage.unsupported.Count -gt 0) {
    throw 'Review CFG diagnostics and unsupported instruction forms before generating'
}

Invoke-WinRecomp lift $sourcePe "$probeDir/entry.cpp"
Invoke-WinRecomp project $sourcePe "$probeDir/raw"
~~~

样例应发现两条指令，诊断和未支持指令清单为空。`entry.cpp` 用于验证一个函数，`raw/` 用于检查原始生成代码。上游生成工程以 x64 为目标，Android 工程从其中选取生成的 C++ 和可移植运行时组件，建立自己的 NDK 构建入口。 [WinRecomp 的生成说明](https://github.com/Zippingrocks/WindowsRecompTool#generate-a-native-project)

做内存测试时，给样例建立可读写的测试栈，栈顶写入一个约定的停止地址。令 EIP 指向样例入口，调用生成的函数，检查 EAX 为 42、ESP 前进 4 字节且执行停在约定地址。先验证这条最小路径，再加入导入、回调和浮点测试。

## 3. 为实际程序恢复可达代码

1. **固定输入。** 对确认有权处理的目标文件做版本和 SHA-256 核对。分析结果放入本地缓存，记录输入、工具版本、分析选项、生成器和缓存产物的哈希。后续构建读取已核验的 CFG、初始映像数据和原始生成 C++。变更输入或工具后按新身份生成并重验。
2. **收集入口和候选回调。** 从 PE 入口、导出和 TLS 回调出发，跟踪实际用到的直接调用和分支。单独核对 CRT 初始化表、虚表、跳转表、导入 API 注册的回调、异常处理入口。候选地址须有可执行节范围、指令边界、交叉引用和表边界的依据。独立静态分析或已有的带类型清单可以辅助确认。 [Ghidra 无界面静态分析](https://ghidra.re/ghidra_docs/api/ghidra/app/util/headless/HeadlessAnalyzer.html)
3. **补齐控制流。** 每个已确认的新入口以 `--seed 0x…` 加入，CFG、覆盖检查和工程生成使用同一组种子。把普通内部跳转、跨函数尾调用、导入调用和间接回调分别解析到已注册入口。发现错误、越界、重复解码或未支持指令时记录原因，再继续扩大覆盖。单纯读到指向代码段的数值只能作为候选。
4. **生成共享 C++。** 小片段可以先用生成的静态 `switch`。大程序可按基本块或函数合并，已知直接调用走静态目标，间接调用走经过核对的原地址分发表。生成 `sources.cmake` 和源码哈希清单，供 Android 和其他验证宿主编译同一批文件。未知目标在运行时记录原 PC、调用位置和输入身份并明确停止。将解码器留在构建工具中。
5. **重建启动所需映像。** 按 PE 的虚拟地址布局映射头部与节，处理零填充、访问权限和导入表。仅在有实现和测试的情况下支持重定位、静态 TLS、线程和异常展开。原始文件布局与映射后的地址布局分别记录，防止把两种映像缓存混用。 [WA2 的准备与生成流程](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/docs/SPECIAL_CONTENTS.md#复现)

遇到未知地址时，先用同版本缓存定位调用点，检查是缺少入口、跳到了指令内部、导入未绑定，还是栈或函数指针损坏。预算耗尽时，先区分离线发现的上限与运行时执行保护，再用一段可以在内存中重现的输入查具体循环或指令。

新版本和同引擎的另一个程序也要建立自己的入口、数据布局、资源和 ABI 配置。只有代码节、相关布局和导入、回调调用契约核对一致后，才考虑共用一套静态函数并独立加载各版本数据。每个地址相关的快路径再绑定对应版本的指令或函数签名。

## 4. 实现公共运行时和 Android 适配

| 层 | 具体交付 |
| --- | --- |
| 原程序 CPU 和内存 | 维护 32 位寄存器、标志、EIP、栈和 x87/SSE 状态。原地址用固定宽度整数，统一走检查过的读写接口。显式处理小端、未对齐、跨页、权限和整数回绕。 |
| 导入、回调与调度 | 按 DLL、名称或序号登记所需导入，逐个验证参数宽度、结构体、返回值、栈清理和错误码。处理 Windows 句柄、回调、消息、等待和线程局部状态。未实现接口返回已知的合法失败或报告缺口。 |
| 窗口、图形、输入 | 把原窗口消息、渲染命令和输入语义映射到应用的统一接口。Android 宿主接收 Surface 和前后台事件，处理缩放、触摸与硬件输入。可以用 SDL 等接设备，再按需实现旧图形接口。 |
| 文件、配置、存档 | 公共层保留原文件语义、编码规则和路径布局，Android 通过文件或授权 URI 读取资源。资源目录与可写存档命名空间分开，处理授权失效、查找大小写、随机读取和保存中途失败。 |
| 音频与视频 | 共享解码、混音和播放状态逻辑。宿主负责输出设备、缓冲区和生命周期。校验音频时钟、进度、暂停恢复和丢帧时序。 |
| JNI 与启动器 | JNI 传递资源授权、配置、输入、帧计数和生命周期事件。复用共享逻辑。界面语言和资源版本分别读取，数据跨 JNI 尽量分批传输。 |

优先验证一个完整调用边界，包括 `cdecl`、`stdcall` 或目标所用约定、栈参数和寄存器、宿主回调返回路径。ARM64 上的浮点环境和布局要按原程序语义实现，重点核对扩展精度、中间舍入和浮点异常。遇到 x64 专用汇编或浮点辅助函数时，先补齐可移植实现，再做差分测试。 [Microsoft x86 调用约定](https://learn.microsoft.com/en-us/cpp/cpp/argument-passing-and-naming-conventions) · [Android JNI 指南](https://developer.android.com/ndk/guides/jni-tips)

用户可通过系统目录选择器授权游戏或程序资源。保留 URI 持久授权，核对文档提供方能否为归档读取提供随机访问。应用部署包按许可范围包含内容，专有资源可以保存在用户所选目录。 [Android 存储访问框架](https://developer.android.com/training/data-storage/shared/documents-files)

启动时由 Android 入口把资源授权和配置交给 C++。公共运行时建立初始映像、栈和堆，绑定兼容服务，再按目标程序要求调用初始化入口。之后由宿主驱动事件和呈现，公共代码维持程序的逻辑状态。前后台切换、有外部回调及中途失败时，分别验证窗口、音频和存储的恢复与释放。

## 5. 用 Android NDK 编译并验证 APK

在 `build/aot-probe/CMakeLists.txt` 保存下面这个最小的链接样例，先验证第 2 节生成的 C++ 能编为 ARM64 库。该库只含测试片段。实际应用在自己的 CMake 入口中引用相同的生成清单，再链接公共运行时、兼容层与 JNI。

~~~cmake
cmake_minimum_required(VERSION 3.20)
project(aot_probe LANGUAGES CXX)

add_library(aot_probe SHARED entry.cpp)
target_compile_features(aot_probe PRIVATE cxx_std_20)
target_include_directories(aot_probe PRIVATE ../tools/WinRecomp/include)
target_link_options(aot_probe PRIVATE
    -Wl,--no-undefined
    -Wl,-z,max-page-size=16384
    -Wl,-z,common-page-size=16384
)
~~~

~~~powershell
# 先将 ANDROID_NDK_HOME 设为本机已安装的 NDK 根目录
$ndkToolchain = Join-Path $env:ANDROID_NDK_HOME 'build/cmake/android.toolchain.cmake'
cmake -S build/aot-probe -B build/aot-probe-arm64 -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$ndkToolchain" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 `
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if ($LASTEXITCODE -ne 0) { throw 'Android configuration failed' }
cmake --build build/aot-probe-arm64 --parallel 1
~~~

样例输出 `build/aot-probe-arm64/libaot_probe.so`，可用 NDK 的 `llvm-readelf -h -l` 确认架构及装载段。示例的 API 26 可按项目依赖调整。正式工程检查 `compile_commands.json` 中实际编译的生成文件及其哈希。公共源可由 Windows 的 C++ 工具链另外编译做离线验证。 [Android NDK 的 CMake 工具链](https://developer.android.com/ndk/guides/cmake) · [CMake 编译命令清单](https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html)

正式应用用 Gradle 的 `externalNativeBuild` 指向宿主 CMake，按目标 ABI 打包 `.so`，注册 JNI 并接入 Android 生命周期。核对动态依赖、JNI 签名、`arm64-v8a` 库列表、签名和 APK 哈希。检查每个 ELF 的装载段、RELRO 和 APK 中未压缩库的 16 KiB 对齐。运行时向系统查询宿主页大小，guest 内存的分页语义独立维护。 [Gradle 接入本机库](https://developer.android.com/studio/projects/gradle-external-native-builds) · [Android 16 KiB 页面兼容](https://developer.android.com/guide/practices/page-sizes)

使用同一个应用 ID、签名证书与更高的版本码做覆盖更新。在已授权设备上可用 `adb -s <设备序列号> install -r <APK>`，核对安装后的版本和包哈希，并检查原应用数据是否保留。设备不可用时记为待验证。 [ADB 手册](https://developer.android.com/tools/adb)

## 6. 按阶段验收

| 阶段 | 通过条件 |
| --- | --- |
| 纯函数 | 自制输入取固定预期，与生成代码对照返回值、寄存器、标志、栈和内存写入。覆盖边界、异常和未注册地址。 |
| 运行时与兼容层 | 用内存和临时文件检查导入、回调、编码、文件枚举和保存读回，确认错误位置和状态一致。 |
| 库与安装包 | 比较生成源码清单，核查链接符号与打包内容。对照本地 APK、已安装版本和应用数据。 |
| 实际功能 | 在项目允许的范围内，由测试者在 Android 上走冷启动、首屏、关键页面、存档或配置、音视频和前后台恢复。逐项记录可用、失败、待验证。 |

首次可交付目标是 **自制函数的语义检查通过 + ARM64 库链接通过 + 一份真实目标的依赖和阻塞清单**。组件通过率、可达指令数、安装成功和整程序可用分别记录。

## 7. 真机性能优化

1. **建立可重复基线。** 选择能复现问题的操作，如从启动到首个有效画面、打开设置、读档、连续滚动或快进。用同一台 Android 设备、场景或输入、资源、画质、帧率限制和刷新率比较基线与候选，分别测冷启动和缓存预热后的行为。记录 APK SHA-256、设备、采样时间、温度或降频状态。重复同一操作，保留原始样本。
2. **拆开观测。** 应用内记录启动各阶段耗时、CPU 更新耗时、渲染提交数、帧耗时 P50/P95、超出帧预算的比例、I/O 次数和字节数、缓存命中、内存和音频欠载。快进或批量任务同时记录每秒处理的条目数。可用设备上的呈现时间线时，区别应用提交、实际呈现和重复帧。先用短时应用级采样定位线程和原函数热点，再用系统跟踪区分运行、等待与 GPU 或呈现阻塞。符号和原 PC 映射要与待测库对应。 [Android Simpleperf](https://developer.android.com/ndk/guides/simpleperf) · [Perfetto FrameTimeline](https://perfetto.dev/docs/data-sources/frametimeline)
3. **一次只改一个瓶颈。** 保留同场景对照。命中可替换热路径时，用相同输入比较输出字节、浮点结果、CPU/内存状态、对外调用及故障行为。在实际调用入口和真实调用链上分别验证，确保直调与间接调用均能命中优化。快路径不适用时恢复到已验证的静态路径。
4. **复测并决定是否保留。** 以目标场景的 P95 帧耗时、操作等待或吞吐改善为准，同时看内存、音频、动画、计时和存档回归。Windows 片段计时适合找原因，Android 收益以同条件真机对照为准。保留前后 APK 版本、日志和回归结果。缺少同场景样本时标为真机收益待验证。

60 帧目标的帧预算约为 16.7 毫秒。用帧耗时的第 95 百分位（P95）观察长帧。启动慢时分段记录映像装载、初始化、首次资源读取和首帧呈现。打开页面、读档等单次操作，记录从输入到结果可用的端到端耗时。 [Android 帧节奏指南](https://developer.android.com/games/sdk/frame-pacing)

| 观察到的瓶颈 | 优先尝试 | 核对 |
| --- | --- | --- |
| 页面打开时大量重复文件查询或读取 | 单次操作复用目录快照，缓存归档索引，批量读取有效载荷 | I/O 次数和页面等待，文件变更后能重新读取，缓存有容量上限。 |
| CPU 时间集中在字符串、像素、字形、粒子或解码循环 | 用公共批处理函数替换已核验的热点，保留副作用与边界语义 | 对照原静态代码的输出、标志、字典、栈和跨页行为。用真实调用链统计接纳率。 |
| CPU 时间集中在分派、寄存器搬运或日志 | 合并热基本块、已知直调，减少重复写回。用发布配置复测，保留可定位错误的诊断 | 记录热函数开销、库大小、错误处理和恢复状态。 |
| 高 FPS 读数却有明显卡顿 | 跟踪实际帧间隔和 CPU、GPU 等待。减少重复上传和提交，按显示刷新节奏呈现 | 观察长帧和重复帧，核对动画和输入延迟。 |
| 快进或批处理阶段停顿，线程常在等待 | 标记等待的调用点，仅优化已证实的空闲等待或冗余呈现 | 记录逻辑前进速度。保留脚本、动画、音频、保存及其他必要等待。 |

## 8. 留下可复用的产物

公开源码仓库适合保存通用分析器接入、生成器、运行时、Android 适配、目标契约、构建脚本、版本清单与自制测试。把原程序、私有 CFG、生成的游戏代码、初始映像、资源、存档和签名密钥放在本地受控目录。发行前按授权和依赖许可核对 APK 与配套材料。WA2 静态链接 FFmpeg 时，提供匹配版本的源码、配置和经核验的重新链接材料。 [WA2 的许可范围](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/NOTICE.md) · [FFmpeg 项目许可说明](https://ffmpeg.org/legal.html)

从 WA2 借用结构时，先读通用部分，再用新项目的代码和证据建立版本档案。WA2 的宿主和生成器包含目标专用适配，公共仓库只提供其中可公开的源码与脚本。

| 想参考什么 | WA2 对应文件 |
| --- | --- |
| 首次分析后如何保存 CFG、种子和缓存哈希 | [prepare_special_contents.py](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/scripts/prepare_special_contents.py) |
| 怎样把原始生成结果合成函数和统一源码清单 | [generate_special_aot.py](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/scripts/generate_special_aot.py) |
| 怎样让多个宿主引用同一批 AOT 源码 | [共享 AOT 的 CMake 接线](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/recompiled/special/CMakeLists.txt) · [宿主入口](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/hosts/CMakeLists.txt) |
| 怎样记录链接、有限内存测试及设备检查 | [静态产物验证](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/scripts/verify_special_contents.py) · [Android 安装核验](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/scripts/install_verified_android.py) |
| 怎样证明快进和资源优化覆盖了真实调用路径 | [SC 的优化与回归记录](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/docs/SPECIAL_CONTENTS.md#02124手机快进采样与图片处理) · [AOT 快进时序](https://github.com/dorakyuraduang/whitealbum2-Android/blob/main/docs/AOT_FAST_FORWARD.md) |

## 可直接交给另一个项目的任务

~~~text
请按《Windows 程序静态重编译到 Android 的工作流》推进本项目。

授权输入与版本：<Windows 程序路径、版本和可分析范围>
目标设备：<Android ABI、最低系统版本、测试设备>
优先功能：<最先要跑通的一条操作>
性能场景：<可复现的启动、页面或运行场景>

先确认输入架构、SHA-256、依赖、适用工具和缺口。用自制 PE 样例验证
离线生成与 ARM64 链接。再挑选一个边界明确的目标函数，建立有证据的
入口、导入和内存回归。报告生成清单、测试结果、可支持范围和阻塞项。

后续按实际用到的接口补足兼容层，用同一份生成 C++ 构建所有宿主。
Android 接入写在平台适配层。功能可核验后建立同设备同场景的性能基线，
根据计数和采样逐项优化，保留差分测试和真机复测记录。
~~~
