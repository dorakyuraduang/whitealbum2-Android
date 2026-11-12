# 外置资源 / Loose resources

0.2.82 增加外置读取，**0.2.83 修正 PC 补丁脚本、字库目录的映射**。完整 PAK 仍放在启动器选中的游戏文件夹内；无需把整套资源解包。可以额外放入原格式资源，覆盖同名包内文件。

## 放置方式

界面、背景和立绘分别使用：

```text
<游戏目录>/grp/原文件名
<游戏目录>/bak/原文件名
<游戏目录>/char/原文件名
```

脚本和字库按启动页的**游戏语言**查找，PC 目录优先；同名文件缺失时才尝试旧版目录：

| 游戏语言 | 优先脚本目录 | 优先字库目录 | 兼容 0.2.82 的后备目录 |
| --- | --- | --- | --- |
| 简体中文 | `ck-gal/` | `fon/` | 同左 |
| 日本語 | `script/` | `fnt/` | 同左 |
| English | `script/` | `fnt/` | `en/`、`fon_en/` |
| Українська | `script/` | `fnt/` | `ua/`、`font_ua/` |

需要 IC 对应资源时，保留同样的 `IC/` 前缀，例如 `IC/grp/`、`IC/script/`。主篇和 IC 不共用同名外置脚本。外置文件名与包内成员名保持一致；脚本的 `.txt`、`.bnr` 配对文件应来自同一个补丁版本。字库仍需配套游戏字体图集，不能用 TTF/OTF 代替。

PC 布局使用补丁原有的 `en.pak`，保留原版 `script.pak` 与 IC 资源。没有独立 `fon_en.pak` / `font_ua.pak` 时，程序使用该补丁 PAK 的 `fnt` 分组，要求其中有四张可唯一识别的原格式字体图集；外置的两张大图可以覆盖其中两张，其余仍读包内。不需要重新生成字体 PAK。乌克兰语没有准备过 `ua.pak` 时使用 PC 补丁的 `en.pak`；已有的独立 PAK 继续使用原配置。语言选择、姓名及文字规则不因此改为英文。

核对用户提供的 UA 0.1.5 后确认：它与英文 PC EXE 的相关读取代码相同，先读 `<分组>/<文件名>`，再尝试 `en.pak`，随后尝试原版资源包。0.1.5 实际提供 `script/`、`IC/script/`、`fnt/`；`ua.pak` 是此前移植版的约定，不是该 PC 补丁要求的文件。移植版原有的 `patch.pak` 处理顺序仍保留。

原程序先尝试外置文件，找不到才继续其 PAK 查找流程；已有文件格式错误不会自动改读包内文件。图片需要原引擎支持的格式和布局，不会自动把 PNG/JPG 转成 TGA，也不会修正字库映射。修改资源后重新启动游戏。避免仅大小写不同的同名文件。

## 读取边界与检查

- Windows 与 Android 使用相同的资源组、优先级和语言映射。Android 直接打开授权文件夹中的文件，不复制进应用私有目录，也不改动原 PAK。
- 外置资源命名空间只读。原程序顺带尝试的 `buf` 备份写入返回访问拒绝，读取仍继续；不会为此复制一份资源。普通存档、测试存档和 INI 继续走原有的独立保存路径。
- 根目录及 IC 的目录名索引按本次游戏会话保留。其他目录元数据最多保留 250 ms，最多缓存 8 个目录、共 65,536 项；不缓存资源内容或文件描述符。文件重新打开时使用新流，损坏或无权限的文件会明确报错。
- 路径限制在所选文件夹的已知资源分组内，拒绝越界路径和有歧义的名字。文件名适配 UTF-8、GBK、Shift-JIS，不转码资源内容。
- 验证使用自建文件、PAK 和未修改的静态资源读取帮助函数，检查外置优先、回退、IC、大小写、中文资源隔离、只读保护与保存路径；同一游戏目录的 PC `script/`、`fnt/` 覆盖适用于所选的日、英、乌克兰语，不是各语言的独立文件库。Java 目录组件检查缓存期限、容量、重名与失败重试。这些检查没有启动游戏或读取屏幕，不能代替完整剧情与第三方素材兼容性测试。音频、影片沿用各自原有的加载流程。

## English

0.2.83 corrects the PC script/font routing introduced in 0.2.82. Keep the complete original PAKs in the selected game folder; bulk extraction is unnecessary. Put replacement UI images, backgrounds and character images under `grp/`, `bak/` and `char/`, using their original member filenames.

Chinese uses `ck-gal/` and `fon/`. Japanese, English and Ukrainian first use the PC `script/` and `fnt/` folders. English `en/`, `fon_en/` and Ukrainian `ua/`, `font_ua/` remain secondary fallbacks for 0.2.82 layouts. Preserve the `IC/` prefix; main and IC loose scripts remain separate. These PC folders affect the selected language within the same game folder, rather than forming separate language libraries. Matching `.txt`/`.bnr` script pairs and the correct game font atlases are required; TTF/OTF files cannot replace the atlases.

Retain the patch's `en.pak` and original `script.pak`/IC resources. Without a separate translated font PAK, the patch's `fnt` group must supply four unambiguous game atlases. Loose large atlases override their packed counterparts; missing loose atlases still come from that group. No font repacking is required. Ukrainian falls back to the PC `en.pak` if no prepared `ua.pak` exists. Existing prepared PAK pairs, Ukrainian names and text rules remain supported.

The supplied UA 0.1.5 and English PC executables have identical relevant file-loading code: a loose group/file path is tried before `en.pak` and the original archive fallbacks. The 0.1.5 payload uses `script/`, `IC/script/` and `fnt/`; `ua.pak` was an earlier port convention. The port retains its existing `patch.pak` policy.

The existing source loader tries a loose file before its archive fallback. Only a missing file falls back; an invalid replacement can still cause an error. Images must use engine-compatible formats and layouts. No image conversion or glyph-mapping changes are performed. Restart the game after editing resources, and avoid filenames differing only in ASCII case.

Both hosts share the routing policy. Android reads the authorized tree directly; resources remain external and read-only. Optional source `buf` backup writes are denied without interrupting the read. Save/configuration routing is unchanged. Root/IC name indexes last for one game session; other metadata expires after 250 ms, with eight folders and 65,536 cached entries in total. Neither payloads nor descriptors are retained. Filename lookup supports UTF-8, GBK and Shift-JIS without transcoding file contents.

Synthetic files and the unchanged static resource helper verify priority, fallback, IC paths, language isolation and write protection. Java component checks cover metadata cache bounds, ambiguity and retries. These checks launch no game and read no screen; they do not establish full-game or arbitrary replacement-asset compatibility. Audio and movies retain their existing loading paths.
