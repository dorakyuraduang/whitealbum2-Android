# 安卓启动入口

## 一个 APK

合包启动页提供 **本体 / SC** 配置切换按钮。先选择程序，再设置对应的资源目录、游戏语言、FPS、60 帧限制、长按快进和菜单开关，点击 **开始游戏** 启动当前选择。两边分别记住目录和配置；本体继续沿用原有设置。

日文模式使用 `grp.pak`（UI）、`script.pak`（脚本）和 `fnt.pak`（字体）。SC 中文模式继续使用原有 `gcn.pak`、两个中文脚本包和 `fon.pak`。资源始终保存在玩家授权的外部目录。SC 和本体使用相同的 FPS、60 帧限制、长按快进、补充字幕及菜单开关，各自保存。SC 默认字幕读取该目录中的 `subtext.dat`、`subfont.tga`、`songfont.tga`，也可选择统一格式的外置字幕。SC 游戏语言目前提供中文和日文；本体原有语言和测试模式选项保留。

Special Contents 的安卓运行模块需要打包在同一个 APK 内。启动页在找到包内对应模块并确认目录授权后才启用「开始游戏」。普通启动页更新包只包含正篇现有运行模块；使用 `--integrated --with-special` 可加入已核验的 Special AOT 模块，保留正篇运行库并在独立进程运行 Special。构建、资源和验证边界见 [Special Contents](SPECIAL_CONTENTS.md)。

已有通过核验的正篇 APK 和本地构建记录时，运行 `python scripts/package_launcher.py --integrated` 可生成启动页更新包。它使用正篇的包名和签名，输出文件按 APK 版本命名，例如 `WA2-arm64-0.2.108-launcher.1.apk`。正篇运行库和游戏内诊断版本仍对应原构建。

## 独立启动器

在 Android 8.0 及以上安装 `WA2-Launcher-<版本>.apk`，打开 **WA2 启动器**。启动器只包含界面和启动逻辑，游戏文件由玩家自行准备。

- **正篇**打开已安装的正篇安卓版 `org.wa2.port`。在正篇应用中继续使用已有的目录选择和设置。
- **Special Contents**使用独立的目录选择。选择该版本的游戏目录后，安装支持下方启动接口的 Android 运行模块，再点「打开」。目录名称不限。模块未安装时，仍可提前选好目录。

## 运行模块接入

在可启动的 Activity 上注册对应版本的 action。合包的 Special Contents Activity 可设置 `android:exported="false"`。独立启动器接收的模块需声明 `android:exported="true"`。

```xml
<intent-filter>
    <action android:name="org.wa2.launcher.action.RUN_SPECIAL_CONTENTS" />
    <category android:name="android.intent.category.DEFAULT" />
</intent-filter>
```

正篇模块可注册 `org.wa2.launcher.action.RUN_MAIN`，由独立启动器直接传入为正篇所选的目录。

启动请求的 `Intent.getClipData()` 包含一项，`getItemAt(0).getUri()` 是玩家通过 Android 系统选择器授权的目录树 URI。接收方先验证 action 和树 URI，再通过 `ContentResolver` 访问。合包由应用持有授权，独立启动器会通过请求为所选模块授予临时读写权限。模块使用该版本所选的目录，并按版本区分存档和游戏配置。[Android 目录授权](https://developer.android.com/training/data-storage/shared/documents-files) · [Intent URI 权限](https://developer.android.com/reference/android/content/Intent#FLAG_GRANT_PREFIX_URI_PERMISSION)

## 构建

使用 Android SDK Platform 35、Build Tools 34.0.0 和 JDK 21 运行 `python scripts/package_launcher.py`。自备工具可用 `--sdk` 和 `--jdk` 指定路径。通过 `--keystore`、`--alias` 和环境变量 `WA2_LAUNCHER_STORE_PASSWORD` 指定自己的签名密钥。签名密钥与密钥库的密码不同时，再设置 `WA2_LAUNCHER_KEY_PASSWORD`。APK 生成在 `out/android/`。
