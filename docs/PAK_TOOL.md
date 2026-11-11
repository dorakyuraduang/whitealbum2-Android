# WA2 PAK 打包 / 解包工具 1.2.0

独立的 Windows x64 图形工具。双击 `WA2_PAK_Tool-1.2.0.exe` 即可使用，无需安装 Python、运行游戏或提供游戏 EXE。工具不附带游戏资源。

首次启动默认 **English**。顶部 **Language** 下拉框可选择英文、简体中文、繁体中文、日语、韩语、乌克兰语、俄语、法语、德语、西班牙语、葡萄牙语，共 11 种语言。切换立即生效，并记住上次选择；缺少或无法识别偏好时使用英文。

界面、选择器标题、进度、结果和错误摘要随工具语言切换；详细诊断统一为英文，Windows 系统选择器自身的标准按钮沿用系统语言。**界面语言不改变文件名编码、资源内容或游戏语言。**

## 使用

- **打包**：点击 **Choose folder → Pack PAK**（选择文件夹 → 打包 PAK），选择源文件夹，再选择 PAK 的保存位置和新文件名。新包使用 **KCAP**；源文件夹本身不会成为包内的额外一层目录。PAK 请保存在源文件夹之外。
- **解包**：点击 **Choose PAK → Unpack**（选择 PAK → 解包），选择 PAK，再选择输出位置。工具会在该位置下新建与 PAK 同名的文件夹；重名时添加数字后缀。
- 修改解包后的内容，再选择该解包文件夹打包即可。**请一并保留 `.wa2pak-manifest` 和 `.wa2pak-cache`**。前者记录原名称、索引及校验值，后者保存原包快照；这两项辅助数据不会作为资源打进 PAK。已有文件名不重新转码；新增文件按所选编码命名。删除的文件不会重新打包。

### 原样重打包

默认勾选 **Preserve original PAK data**（保留原始 PAK 数据）。解包 KCAP/LAC 时保存完整原包快照，**额外占用约一个原 PAK 大小的磁盘空间**。解包目录移动或原 PAK 删除后，仍可使用快照重打包。

- **文件及目录结构均未改动**：校验快照和每个文件的 SHA-256 后，恢复完整原包，包括原压缩编码、物理排列、填充及尾部数据，输出与原包逐字节一致。完成提示显示 **Original PAK restored byte for byte**。
- **文件有改动**：未修改文件复用其原始压缩块，只有新增或修改的文件按 LZSS 选项处理。此时重建索引和数据布局，不承诺包大小或原填充、尾部数据保持不变。
- **取消勾选**：解包不保存原包快照；打包时全部按当前压缩选项重新编码，包大小可能与原版不同。保留模式下，压缩选项只影响新增和修改的文件。

保留模式不会根据文件时间判断内容是否相同；同长度、同时间戳的修改仍通过内容校验识别。原始快照缺失、被修改或与清单不匹配时，会报错，不会悄悄改成重新压缩。**1.0/1.1 版解包目录没有快照，需要用新版重新解包原 PAK，或关闭保留选项重新编码。**

可用“取消”停止操作。原 PAK 和已有目标不会被覆盖；出错或取消时清理本次临时输出。请勿在操作期间修改源文件。

## 格式与文件名

- 支持解包 **KCAP / LAC**，包括原始数据和 LZSS 压缩。带清单重新打包时沿用原包类型。需要重新编码的文件仅在压缩后变小时采用压缩；这类文件大于 256 MiB 时不压缩。保留模式下，未修改的大文件也沿用原包的压缩数据。
- KCAP 的零长度目录标记按分组文件夹呈现，重新打包时保留原来的索引顺序和名称。没有清单的新文件夹，根文件直接放入包内，各子文件夹生成 KCAP 分组。包内新名称最多 **24 个编码字节**；LAC 为 **31 字节**。超长或无法编码时会明确报错，不会截断或替换成问号。
- 文件名编码可选 **Shift-JIS / CP932、GBK / CP936、UTF-8**。默认新包和首次解包使用 CP932；有清单的文件夹默认沿用清单编码。中文补丁名称若显示乱码，请选择 GBK 后重新解包。**这个选项只影响文件名，不会转换脚本文本、字体或图片格式。**无法解码的原名会生成 `entry_编号`，原始名称字节仍保存在清单内。
- 同名文件、大小写冲突及 Windows 保留名称会使用安全名称导出，原名通过清单恢复。目录穿越、绝对路径、符号链接和目录联接不会被跟随。
- 资源包使用 32 位偏移，单包上限约 **4 GiB**；默认最多一百万条索引，单次解包总展开量上限 **64 GiB**。遇到不支持的格式或损坏数据会报错。

部分 `mv*.pak` 实际是直接改名的视频，不是文件容器。工具识别 ASF/WMV、MP4/MOV、AVI、MKV/WebM、FLV、MPEG、TS、Ogg 的常见文件头，直接导出为一个 `video.*` 文件，带清单重新打包则原样复制回 PAK，**不解码、不转码，也不修复损坏的视频**。MOV/WebM 等共享容器分别以 `.mp4`/`.mkv` 扩展名导出，不改变内容。未识别的文件头会明确报错。

这是一款针对本项目 WA2 格式的工具，其他游戏同为 `.pak` 后缀的文件不一定兼容。更换图片时仍需保持游戏所需的内部格式、尺寸和名称；打包成功不代表游戏支持任意素材。

## 构建与验证

源码在 `tools/wa2-pak-tool/`，采用项目 MIT 许可。使用已有 Zig / CMake / Ninja：

```powershell
python scripts/build_pak_tool.py
```

输出为 `out/pak-tool/WA2_PAK_Tool-1.2.0.exe` 和 `WA2-PAK-Tool-1.2.0-win64.zip`。构建脚本隐藏运行编译器、使用低于正常优先级及单 CPU / 单任务，然后执行独立文件与编解码测试；无需读取任何游戏 EXE 或生成的游戏代码。文件名含版本号，可以与旧版工具共存。

测试覆盖压缩往返、既有资源读取器兼容、KCAP/LAC、分组及同名文件、三种文件名编码、空文件、视频原样复制、损坏索引、路径越界和取消清理，以及英文默认值、语言代码往返、多语言进度和错误摘要。保留模式另外检查不同 LZSS 编码、非零填充、乱序数据区、尾部数据、反复往返逐字节一致、移动解包目录及删除原包、同长度同时间戳修改、部分压缩块复用、增删文件、辅助数据同名资源、快照损坏/缺失及旧清单处理。翻译表在编译时检查完整性。这些检查不替代图形界面人工试用或游戏内素材兼容性验证。

## English quick guide

Windows x64, standalone; no Python or game EXE required.

- English is the default on first launch. Use the visible **Language** dropdown to switch instantly between English, Simplified/Traditional Chinese, Japanese, Korean, Ukrainian, Russian, French, German, Spanish and Portuguese. The last choice is saved. A missing or unknown preference falls back to English.
- Buttons, picker titles, progress, results and error summaries are localized. Detailed diagnostics remain English; standard Windows picker controls follow Windows. UI language never changes filename encoding or resource contents.
- **Left button**: choose an input folder, then save a new PAK outside that folder.
- **Right button**: choose a PAK, then an output parent folder. A new subfolder is created automatically.
- **Preserve original PAK data** is enabled by default. Extraction keeps an original snapshot in `.wa2pak-cache`, requiring approximately one extra archive's worth of disk space. Keep this folder and `.wa2pak-manifest` together when moving the extracted folder; neither is included as a game resource.
- If no files or directory structure changed, the snapshot and file contents are checked using SHA-256 and the original archive is restored **byte for byte**, including compression, layout, padding and trailing data. If files changed, unchanged payloads are copied directly and only edited/new files are encoded. Edited archives rebuild the layout and can have a different size.
- Turn preservation off to repack everything with the selected compression setting. Older 1.0/1.1 extractions need to be extracted again for preservation; missing or corrupted snapshots produce an error rather than silent recompression.
- New archives use KCAP. KCAP/LAC stored and LZSS-compressed members are supported. Filename encodings are CP932, CP936 and UTF-8; script contents are never transcoded. Existing targets are never overwritten.
- Direct-video PAKs are copied without decoding or transcoding. Other games' PAK formats and arbitrary replacement image formats are not guaranteed compatible.
