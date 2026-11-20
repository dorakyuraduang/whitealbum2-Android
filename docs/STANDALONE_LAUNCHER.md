# 安卓独立启动器

在 Android 8.0 及以上安装 `WA2-Launcher-<版本>.apk`，打开 **WA2 启动器**。启动器只包含界面和启动逻辑，游戏文件由玩家自行准备。

- **正篇**打开已安装的正篇安卓版 `org.wa2.port`。在正篇应用中继续使用已有的目录选择和设置。
- **Special Contents**使用独立的目录选择。选择该版本的游戏目录后，安装支持下方启动接口的 Android 运行模块，再点「打开」。目录名称不限。模块未安装时，仍可提前选好目录。

## 运行模块接入

在可启动的 Activity 上声明 `android:exported="true"`，并注册对应版本的 action。Special Contents 的示例如下。

```xml
<intent-filter>
    <action android:name="org.wa2.launcher.action.RUN_SPECIAL_CONTENTS" />
    <category android:name="android.intent.category.DEFAULT" />
</intent-filter>
```

正篇模块可注册 `org.wa2.launcher.action.RUN_MAIN`，由独立启动器直接传入为正篇所选的目录。

启动请求的 `Intent.getClipData()` 包含一项，`getItemAt(0).getUri()` 是玩家通过 Android 系统选择器授权的目录树 URI。接收方先验证 action 和树 URI，再通过 `ContentResolver` 访问。请求携带该目录树的临时读写权限。模块将资源访问、存档和游戏配置限定在该版本选中的目录内。启动器为两个版本分别保留目录授权，每次打开时再向用户所选模块传递对应目录。[Android 目录授权](https://developer.android.com/training/data-storage/shared/documents-files) · [Intent URI 权限](https://developer.android.com/reference/android/content/Intent#FLAG_GRANT_PREFIX_URI_PERMISSION)

## 构建

使用 Android SDK Platform 35、Build Tools 34.0.0 和 JDK 21 运行 `python scripts/package_launcher.py`。自备工具可用 `--sdk` 和 `--jdk` 指定路径。通过 `--keystore`、`--alias` 和环境变量 `WA2_LAUNCHER_STORE_PASSWORD` 指定自己的签名密钥。签名密钥与密钥库的密码不同时，再设置 `WA2_LAUNCHER_KEY_PASSWORD`。APK 生成在 `out/android/`。
