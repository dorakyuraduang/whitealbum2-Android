# WHITE ALBUM2 静态重编译

**简体中文** · [English](README.en.md)

面向 **Android ARM64** 的静态重编译移植，各平台共用游戏逻辑。当前开发版本：**0.2.98-shared-aot**。游戏资源需自行准备，直接读取原版 PAK，无需批量解包。

**[下载与发行页面](https://github.com/dorakyuraduang/whitealbum2-Android/releases)**

## 快速启动

要求：**Android 8.0 及以上、ARM64、OpenGL ES 3.0**。

1. 安装 `WA2-arm64-<版本名>.apk`。
2. 将完整游戏资源复制到手机的任意文件夹，保留原有 `IC` 子目录。**文件夹名称不限，PAK 不用解压。**
3. 打开应用，选择这个游戏文件夹，并授予读写权限。
4. 选择游戏语言，按需设置 FPS、长按快进和补充字幕，点击 **“开始游戏”**。

影片支持 MP4/MOV、MKV/WebM、AVI、WMV/ASF、MPEG/TS、FLV 等常用格式，以及原版和 **Final Full Patch 1.0.3.0** 的视频 PAK。保留游戏原文件名和 `IC` 路径；无法读取、解析或解码的影片会自动跳过并继续剧情，原因记录在运行诊断中。详见[影片格式与容错](docs/MOVIE_FORMATS.md)。

游戏语言默认中文。所选文件夹保留完整原版资源，再按语言放置补丁：

| 游戏语言 | 脚本包 | 字库包 |
| --- | --- | --- |
| 简体中文（默认） | `ck-gal.pak` | `fon.pak` |
| 日本語 | `script.pak` | `fnt.pak` |
| English | `en.pak`，外置覆盖用 `script/`、`IC/script/` | 外置 `fnt/`，缺少的图集从 `en.pak` 的 `fnt` 分组读取 |
| Українська | PC 补丁的 `en.pak`、`script/`、`IC/script/` | PC 补丁的 `fnt/`，其余图集从 `en.pak` 读取 |

这两类包还需配合完整的背景、立绘、音频、影片等原版资源。启动器界面默认跟随手机系统首选语言，无对应翻译时使用英文；手动选择后记住该选择。界面语言与游戏语言分别设置。

支持[外置资源覆盖](docs/LOOSE_RESOURCES.md)：图片使用 `grp/`、`bak/`、`char/`，日、英、乌克兰语脚本和字库优先使用 PC 布局的 `script/`、`IC/script/`、`fnt/`；中文保持 `ck-gal/`、`fon/`。保留原文件名，外置文件优先，缺失时回退到包内资源。

英文选择 **English**；UA **0.1.5** 补丁选择 **Українська**，保留它所需的姓名与文字处理。两者都保留原版 `script.pak` 和 `IC` 资源，脚本不转码，`.txt`/`.bnr` 配对放置。PC 补丁自带的 `en.pak`、`script/`、`IC/script/`、`fnt/` 可直接按原布局复制，不需要再打包。

旧版已准备的 `fon_en.pak`，或 `ua.pak`、`font_ua.pak` 仍可使用；对应旧版外置目录也保留兼容。详见[语言资源](docs/LANGUAGE_RESOURCES.md)与[乌克兰语说明](docs/UKRAINIAN_SUPPORT.md)。补丁 EXE/DLL 不参与运行。

## 存档、配置与字幕

### 普通存档

**游戏存档全部外置。** `<游戏目录>` 指启动页选择的游戏文件夹。普通存档按 PC 目录布局放置：

```text
<游戏目录>/Documents/Leaf/WHITE ALBUM2/
```

其中 `Sys.sav` 保存全局进度和解锁状态，`save_00.sav` ～ `save_99.sav` 为手动存档，`save_Q.sav` 为快速存档。导入 PC 存档时，退出游戏后将文件复制到上述目录，并关闭测试模式后读档。

从 0.2.76 起，普通存档只在上述目录读取和保存，首次保存会自动创建目录。游戏根目录的旧 `.sav` 不再读取、合并或自动搬迁；需要继续使用的存档请自行复制到上述目录。

### 测试模式存档

开启“测试模式（全部解锁）”后，独立使用同一外置游戏目录中的：

```text
<游戏目录>/test-mode/
```

例如 `test-mode/Sys.sav`、`test-mode/save_00.sav`。关闭测试模式即可恢复使用普通存档。

### 配置

安卓启动器选项保存在 `<游戏目录>/native-launcher.wa2cfg`。游戏配置沿用 `SYSTEM.ini`、`D3D.ini`，保留已有文件所在目录。

### 补充字幕

默认内置正篇中文字幕，可通过“补充字幕”开关控制。使用外置字幕时，在 **“字幕来源”** 中手动选择 UTF-8 的 `sub.yaml` 或原 Todokanai `subtitles` 文件，并开启补充字幕。外置文件替换整份内置补充字幕，Todokanai 文件无需转换为 YAML；英文字母和空格按实际字宽居中、换行。

剧情对白由所选脚本和字库提供，补充字幕语言由字幕文件内容决定。UA 0.1.5 附带的字幕为英文，可自行翻译后选择。

## 文档与开发

- [Windows PAK 打包 / 解包工具](docs/PAK_TOOL.md)：选择文件夹打包，或选择 PAK 解包，支持 KCAP/LAC。
- [Android 安装与目录授权](docs/ANDROID.md)
- [游戏语言与汉化补丁适配](docs/LANGUAGE_RESOURCES.md)
- [乌克兰语资源打包与字幕使用](docs/UKRAINIAN_SUPPORT.md)
- [外置字幕规则（中文）](docs/EXTERNAL_SUBTITLES.md) / [English](docs/EXTERNAL_SUBTITLES.en.md)
- [构建与验证](docs/BUILDING.md) / [架构](docs/ARCHITECTURE.md) / [开发记录](docs/DEVELOPMENT_HISTORY.zh-CN.md)

`main` 为静态重编译版；此前的 Godot 移植版见 [godot 分支](https://github.com/dorakyuraduang/whitealbum2-Android/tree/godot)。独立原生迁移代码保留为实验，项目仍在开发中，完整剧情兼容性与设备性能尚待实测。

公开仓库不包含原游戏 EXE、PAK、生成的游戏函数或初始化数据；完整 AOT 构建需要自行提供相应输入，详见构建文档。

## 许可

原创代码与文档采用 [MIT](LICENSE)，原游戏及第三方内容遵循各自许可，见 [NOTICE](NOTICE.md) 和 [第三方声明](THIRD_PARTY_NOTICES.md)。分发静态链接 FFmpeg 的程序时，需同时提供匹配的重新链接材料及对应源码、配置，详见构建文档。
