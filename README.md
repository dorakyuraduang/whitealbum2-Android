# WHITE ALBUM2 静态重编译

**简体中文** · [English](README.en.md)

面向 **Android ARM64** 的静态重编译移植，各平台共用游戏逻辑。当前开发版本：**0.2.71-shared-aot**。游戏资源需自行准备，直接读取原版 PAK，无需批量解包。

**[下载与发行页面](https://github.com/dorakyuraduang/whitealbum2-Android/releases)**

## 快速启动

要求：**Android 8.0 及以上、ARM64、OpenGL ES 3.0**。

1. 安装 `WA2-arm64-<版本名>.apk`。
2. 将完整游戏资源复制到手机的任意文件夹，保留原有 `IC` 子目录。**文件夹名称不限，PAK 不用解压。**
3. 打开应用，选择这个游戏文件夹，并授予读写权限。
4. 选择游戏语言，按需设置 FPS、长按快进和补充字幕，点击 **“开始游戏”**。

游戏语言默认中文，所选文件夹根目录需有对应的脚本包和字库包：

| 游戏语言 | 脚本包 | 字库包 |
| --- | --- | --- |
| 简体中文（默认） | `ck-gal.pak` | `fon.pak` |
| 日本語 | `script.pak` | `fnt.pak` |
| English | `en.pak` | `fon_en.pak` |
| Українська | `ua.pak` | `font_ua.pak` |

这两类包还需配合完整的背景、立绘、音频、影片等原版资源。启动器界面语言与游戏语言分别设置。

英文补丁还需保留原版 `script.pak` 作为基础数据后备。在启动页选择 **English** 即应用英文资源；`fon_en.pak` 必须是配套游戏字库 PAK。已有带 `fnt` 分组的英文 `en.pak` 可用 `python scripts/prepare_english_font.py --root "游戏目录"` 生成独立字库，原包保持不变，详见[语言资源说明](docs/LANGUAGE_RESOURCES.md)。

英文字库应从同一份 `en.pak` 生成。请先备份已有的 `fon_en.pak`；把中文字库改名不能得到配套英文字库。

乌克兰语已适配 UA 补丁 **0.1.5**，在启动页的“游戏语言”中选择 **Українська**。两个配套文件使用游戏的 **KCAP 格式**，制作内容如下：

- **`ua.pak`**：保留补丁 `en.pak` 中的 `bak`、`grp` 分组，以其 `script` 分组为基础覆盖乌克兰语 `script/` 文件；将 `IC/script/` 单独放入 **`ic/script`** 分组，避免同名场景互相覆盖。
- **`font_ua.pak`**：将补丁 `fnt/` 中的两张大号字体图集，与 `en.pak` 的 `fnt` 分组中的两张小号图集合并，四张图直接放在包内，保留原文件名。

脚本保留原编码和 `.txt`/`.bnr` 配对。可用[资源准备工具](scripts/prepare_ukrainian_resources.py)从原始 0.1.5 ZIP 生成两个包，放到所选游戏目录根目录，并保留原版 `script.pak`、`IC` 子目录等完整资源；具体步骤见[乌克兰语说明](docs/UKRAINIAN_SUPPORT.md)。

## 存档、配置与字幕

### 普通存档

**游戏存档全部外置。** `<游戏目录>` 指启动页选择的游戏文件夹。普通存档按 PC 目录布局放置：

```text
<游戏目录>/Documents/Leaf/WHITE ALBUM2/
```

其中 `Sys.sav` 保存全局进度和解锁状态，`save_00.sav` ～ `save_99.sav` 为手动存档，`save_Q.sav` 为快速存档。导入 PC 存档时，退出游戏后将文件复制到上述目录，并关闭测试模式后读档。

已有根目录同名存档也兼容读取；两处同时存在同名文件时，根目录文件优先。

### 测试模式存档

开启“测试模式（全部解锁）”后，独立使用同一外置游戏目录中的：

```text
<游戏目录>/test-mode/
```

例如 `test-mode/Sys.sav`、`test-mode/save_00.sav`。关闭测试模式即可恢复使用普通存档。

### 配置

安卓启动器选项保存在 `<游戏目录>/native-launcher.wa2cfg`。游戏配置沿用 `SYSTEM.ini`、`D3D.ini`，保留已有文件所在目录。

### 补充字幕

默认内置正篇中文字幕，可通过“补充字幕”开关控制。使用外置字幕时，在 **“字幕来源”** 中手动选择 UTF-8 的 `sub.yaml` 或原 Todokanai `subtitles` 文件，并开启补充字幕。外置文件替换整份内置补充字幕，Todokanai 文件无需转换为 YAML。

剧情对白由所选脚本和字库提供，补充字幕语言由字幕文件内容决定。UA 0.1.5 附带的字幕为英文，可自行翻译后选择。

## 文档与开发

- [Android 安装与目录授权](docs/ANDROID.md)
- [游戏语言与汉化补丁适配](docs/LANGUAGE_RESOURCES.md)
- [乌克兰语资源打包与字幕使用](docs/UKRAINIAN_SUPPORT.md)
- [外置字幕规则（中文）](docs/EXTERNAL_SUBTITLES.md) / [English](docs/EXTERNAL_SUBTITLES.en.md)
- [构建与验证](docs/BUILDING.md) / [架构](docs/ARCHITECTURE.md) / [开发记录](docs/DEVELOPMENT_HISTORY.zh-CN.md)

`main` 为静态重编译版；此前的 Godot 移植版见 [godot 分支](https://github.com/dorakyuraduang/whitealbum2-Android/tree/godot)。独立原生迁移代码保留为实验，项目仍在开发中，完整剧情兼容性与设备性能尚待实测。

公开仓库不包含原游戏 EXE、PAK、生成的游戏函数或初始化数据；完整 AOT 构建需要自行提供相应输入，详见构建文档。

## 许可

原创代码与文档采用 [MIT](LICENSE)，原游戏及第三方内容遵循各自许可，见 [NOTICE](NOTICE.md) 和 [第三方声明](THIRD_PARTY_NOTICES.md)。分发静态链接 FFmpeg 的程序时，需同时提供匹配的重新链接材料及对应源码、配置，详见构建文档。
