# WHITE ALBUM2 静态重编译

面向 **Android ARM64 / Windows x64** 的同源静态重编译项目，两平台共用游戏逻辑。当前版本：**0.2.66-shared-aot**。游戏资源需自行准备，直接读取原版 PAK，无需批量解包。

**[下载 Android APK](https://github.com/dorakyuraduang/whitealbum2-Android/releases/download/r0.2.57/WA2-arm64-0.2.66-shared-aot.apk)** · [发行页面](https://github.com/dorakyuraduang/whitealbum2-Android/releases)

## 快速启动

### Android

要求：**Android 8.0 及以上、ARM64、OpenGL ES 3.0**。

1. 下载并安装上方的 `WA2-arm64-0.2.66-shared-aot.apk`。
2. 将完整游戏资源复制到手机的任意文件夹，保留原有 `IC` 子目录。**文件夹名称不限，PAK 不用解压。**
3. 打开应用，选择这个游戏文件夹，并授予读写权限。
4. 选择游戏语言，按需设置 FPS、长按快进和补充字幕，点击 **“开始游戏”**。

游戏语言默认中文，所选文件夹根目录需有对应的脚本包和字库包：

| 游戏语言 | 脚本包 | 字库包 |
| --- | --- | --- |
| 简体中文（默认） | `ck-gal.pak` | `fon.pak` |
| 日本語 | `script.pak` | `fnt.pak` |

这两类包还需配合完整的背景、立绘、音频、影片等原版资源。启动器界面语言与游戏语言分别设置。

### Windows

将编译包中的 `WA2_SDL2_x64.exe` 放在原版 PAK 所在目录，双击启动；也可指定资源目录：

```powershell
.\WA2_SDL2_x64.exe --root "D:\Games\WHITE ALBUM2"
```

默认中文；日语在命令末尾加 `--language ja-JP`。本地工作区也可在准备好 `res/` 资源后使用 `run.cmd`。

## 存档、配置与字幕

存档和配置读写到当前选择的游戏目录。可将 PC 的 `Sys.sav`、`save_00.sav` 等原版存档及 `SYSTEM.ini`、`D3D.ini` 复制到该目录；测试模式使用独立的 `test-mode/`，读取普通存档时请关闭测试模式。

默认内置正篇中文字幕，也可以关闭或选择自己的 `sub.yaml`。外置字幕替换整份内置补充字幕，普通剧情对白由所选脚本和字库提供。

## 文档与开发

- [Android 安装与目录授权](docs/ANDROID.md)
- [游戏语言与汉化补丁适配](docs/LANGUAGE_RESOURCES.md)
- [外置字幕规则（中文）](docs/EXTERNAL_SUBTITLES.md) / [English](docs/EXTERNAL_SUBTITLES.en.md)
- [构建与验证](docs/BUILDING.md) / [架构](docs/ARCHITECTURE.md) / [开发记录](docs/DEVELOPMENT_HISTORY.zh-CN.md)

`main` 为静态重编译版；此前的 Godot 移植版见 [godot 分支](https://github.com/dorakyuraduang/whitealbum2-Android/tree/godot)。独立原生迁移代码保留为实验，项目仍在开发中，完整剧情兼容性与设备性能尚待实测。

公开仓库不包含原游戏 EXE、PAK、生成的游戏函数或初始化数据；完整 AOT 构建需要自行提供相应输入，详见构建文档。

## 许可

原创代码与文档采用 [MIT](LICENSE)，原游戏及第三方内容遵循各自许可，见 [NOTICE](NOTICE.md) 和 [第三方声明](THIRD_PARTY_NOTICES.md)。分发静态链接 FFmpeg 的程序时，需同时提供匹配的重新链接材料及对应源码、配置，详见构建文档。
