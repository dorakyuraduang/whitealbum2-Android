# 外置资源 / Loose resources

适用于 0.2.82 起的静态重编译版。完整 PAK 仍放在启动器选中的游戏文件夹内；无需把整套资源解包。可以额外放入少量原格式资源，覆盖同名包内文件。

## 放置方式

界面、背景和立绘分别使用：

```text
<游戏目录>/grp/原文件名
<游戏目录>/bak/原文件名
<游戏目录>/char/原文件名
```

脚本和字库目录随启动页的**游戏语言**选择，目录名等于对应 PAK 去掉 `.pak`：

| 游戏语言 | 脚本目录 | 字库目录 |
| --- | --- | --- |
| 简体中文 | `ck-gal/` | `fon/` |
| 日本語 | `script/` | `fnt/` |
| English | `en/` | `fon_en/` |
| Українська | `ua/` | `font_ua/` |

需要 IC 对应资源时，保留同样的 `IC/` 前缀，例如 `IC/grp/`、`IC/en/`。外置文件名与包内成员名保持一致；脚本的 `.txt`、`.bnr` 配对文件应来自同一个补丁版本。字库仍需配套游戏字体图集，不能用 TTF/OTF 代替。

原程序先尝试外置文件，找不到才继续其 PAK 查找流程；已有文件格式错误不会自动改读包内文件。图片需要原引擎支持的格式和布局，不会自动把 PNG/JPG 转成 TGA，也不会修正字库映射。修改资源后重新启动游戏。避免仅大小写不同的同名文件。

## 读取边界与检查

- Windows 与 Android 使用相同的资源组、优先级和语言映射。Android 直接打开授权文件夹中的文件，不复制进应用私有目录，也不改动原 PAK。
- 外置资源命名空间只读。原程序顺带尝试的 `buf` 备份写入返回访问拒绝，读取仍继续；不会为此复制一份资源。普通存档、测试存档和 INI 继续走原有的独立保存路径。
- 根目录及 IC 的目录名索引按本次游戏会话保留。其他目录元数据最多保留 250 ms，最多缓存 8 个目录、共 65,536 项；不缓存资源内容或文件描述符。文件重新打开时使用新流，损坏或无权限的文件会明确报错。
- 路径限制在所选文件夹的已知资源分组内，拒绝越界路径和有歧义的名字。文件名适配 UTF-8、GBK、Shift-JIS，不转码资源内容。
- 验证使用自建文件、PAK 和未修改的静态资源读取帮助函数，检查外置优先、回退、IC、大小写、语言隔离、只读保护与保存路径。Java 目录组件检查缓存期限、容量、重名与失败重试。这些检查没有启动游戏或读取屏幕，不能代替完整剧情与第三方素材兼容性测试。音频、影片沿用各自原有的加载流程。

## English

Shared-AOT builds from 0.2.82 support loose resource overrides. Keep the complete original PAKs in the selected game folder; bulk extraction is unnecessary. Put replacement UI images, backgrounds and character images under `grp/`, `bak/` and `char/`, using their original member filenames.

The script/font folders follow the selected **game language**, as shown in the table above: Chinese uses `ck-gal/` and `fon/`; Japanese `script/` and `fnt/`; English `en/` and `fon_en/`; Ukrainian `ua/` and `font_ua/`. Preserve the `IC/` prefix where the original request uses it. Matching `.txt`/`.bnr` script pairs and the correct game font atlases are required. TTF/OTF files cannot replace the atlases.

The existing source loader tries a loose file before its archive fallback. Only a missing file falls back; an invalid replacement can still cause an error. Images must use engine-compatible formats and layouts. No image conversion or glyph-mapping changes are performed. Restart the game after editing resources, and avoid filenames differing only in ASCII case.

Both hosts share the routing policy. Android reads the authorized tree directly; resources remain external and read-only. Optional source `buf` backup writes are denied without interrupting the read. Save/configuration routing is unchanged. Root/IC name indexes last for one game session; other metadata expires after 250 ms, with eight folders and 65,536 cached entries in total. Neither payloads nor descriptors are retained. Filename lookup supports UTF-8, GBK and Shift-JIS without transcoding file contents.

Synthetic files and the unchanged static resource helper verify priority, fallback, IC paths, language isolation and write protection. Java component checks cover metadata cache bounds, ambiguity and retries. These checks launch no game and read no screen; they do not establish full-game or arbitrary replacement-asset compatibility. Audio and movies retain their existing loading paths.
