# WHITE ALBUM2 Static Recompilation

[简体中文](README.md) · **English**

A shared static-recompilation project for **Android ARM64 / Windows x64**.
Current development version: **0.2.71-shared-aot**. Both platforms compile the
same game logic. Supply your own original PAK resources; whole-archive extraction
is not required.

**[Downloads and releases](https://github.com/dorakyuraduang/whitealbum2-Android/releases)**

## Quick start

### Android

Requirements: **Android 8.0+, ARM64 and OpenGL ES 3.0**.

1. Install `WA2-arm64-<release-name>.apk`. English resources require **0.2.67+**;
   Ukrainian resources and external Todokanai captions require **0.2.70+**.
2. Copy your complete game resources to any folder on your phone. Keep the `IC`
   subfolder. **Any folder name is accepted; leave the PAK files packed.**
3. Open the app, select that folder and grant read/write access.
4. Select **Game language**, adjust FPS, hold-to-skip and extra-caption options
   as needed, then tap **Start game**.

Chinese is the default. Put the selected language's script and font packs in
the root of the game folder:

| Game language | Script pack | Font pack |
| --- | --- | --- |
| 简体中文 / Chinese (default) | `ck-gal.pak` | `fon.pak` |
| 日本語 / Japanese | `script.pak` | `fnt.pak` |
| English | `en.pak` | `fon_en.pak` |
| Українська / Ukrainian | `ua.pak` | `font_ua.pak` |

You also need the complete original graphics, audio, movie and other resources.
**Interface language** changes the launcher; **Game language** selects game
resources.

For English, retain the original `script.pak` for base script data and select
**English** in the launcher. `fon_en.pak` must contain the matching game font
atlases. If your English `en.pak` contains an `fnt` directory, generate a separate
font pack with `python scripts/prepare_english_font.py --root "your game folder"`.
This copies packed font members without decompressing them or changing `en.pak`.
Back up an existing `fon_en.pak` before generation. Renaming the Chinese font
pack does not produce the matching English fonts.
See [resource and patch details](docs/LANGUAGE_RESOURCES.md).

Ukrainian support targets the UA **0.1.5** patch. Select **Українська** under
**Game language**. Prepare these two archives using the game's **KCAP format**:

- **`ua.pak`**: Keep the `bak` and `grp` groups from the patch's `en.pak`.
  Use its `script` group as the base and overlay the Ukrainian `script/` files.
  Store `IC/script/` in a separate **`ic/script`** group to preserve scenes with
  identical filenames.
- **`font_ua.pak`**: Combine the two large font atlases from the patch's `fnt/`
  folder with the two small atlases from the `fnt` group in its `en.pak`.
  Put all four directly inside the font archive, preserving their filenames.

Keep the original script encoding and `.txt`/`.bnr` pairs. The
[resource preparation tool](scripts/prepare_ukrainian_resources.py) generates
both packs from the original 0.1.5 ZIP. Put them in the selected game folder's
root and retain the original `script.pak`, `IC` subfolder and other game resources. See the
[preparation guide](docs/UKRAINIAN_SUPPORT.md) for the procedure.

### Windows

Place the compiled `WA2_SDL2_x64.exe` beside the original PAK files and launch it,
or specify the game folder:

```powershell
.\WA2_SDL2_x64.exe --root "D:\Games\WHITE ALBUM2" --language en-US
```

Omit `--language` for Chinese; use `--language ja-JP` for Japanese or
`--language uk-UA` for Ukrainian. In a prepared local workspace, `run.cmd`
uses the `res/` folder.

## Saves, settings and captions

**Normal saves default to the root of the selected game-resource folder.**
`<game folder>` below means the folder selected in the launcher; its name is unrestricted.

| Purpose | Default location in normal mode |
| --- | --- |
| Global progress, read history and content unlocks | `<game folder>/Sys.sav` |
| Manual saves (100 slots) | `<game folder>/save_00.sav` through `<game folder>/save_99.sav` |
| Quick save | `<game folder>/save_Q.sav` |
| Game and display settings | `<game folder>/SYSTEM.ini`, `<game folder>/D3D.ini`; an existing `default.ini` is also read here |
| Android launcher options (language, FPS, caption source, etc.) | `<game folder>/native-launcher.wa2cfg` |

For example, selecting **Internal storage/Games/WA2** on Android puts the first
manual save at **`/storage/emulated/0/Games/WA2/save_00.sav`**. Windows uses the same
layout: starting with `--root "D:\Games\WHITE ALBUM2"` puts it at
**`D:\Games\WHITE ALBUM2\save_00.sav`**. Some files only appear after saving;
filename capitalization may differ.

With **Test mode (unlock all)** enabled, game saves and INI settings use
**`<game folder>/test-mode/`**, such as `test-mode/Sys.sav` and
`test-mode/save_00.sav`. The Android launcher's `native-launcher.wa2cfg` remains
in the game folder's root.

To import normal PC saves, exit the game and copy `Sys.sav`, numbered saves such
as `save_00.sav`, and `save_Q.sav` directly into the game folder's root. Copy the
INI files too if you want the corresponding settings, and disable test mode
before loading. The legacy layout is also supported: when a root file is absent,
the adapter checks `<game folder>/Documents/Leaf/WHITE ALBUM2/`. Separate chapter
directories, `WHITE ALBUM2 -introductory chapter-` and
`WHITE ALBUM2 -closing chapter-`, remain alongside it under `Documents/Leaf/`.

Embedded main-story supplementary captions are Chinese by default. The
**Extra captions** switch controls their display. External captions support
**UTF-8 `sub.yaml` and the original Todokanai format**: select a file under
**Caption source**, such as the patch's `todokanai/subtitles`, and enable
**Extra captions**. Todokanai files work directly without YAML conversion.

Caption files are separate from the script packs; their text determines their
language. The UA 0.1.5 caption file contains English text and can be translated
before selection. External captions replace the complete embedded set and are
never selected automatically. Ordinary story dialogue comes from the selected
scripts and fonts.

## Documentation and development

- [Android setup](docs/ANDROID.md)
- [Ukrainian resource preparation and captions](docs/UKRAINIAN_SUPPORT.md)
- [External caption format](docs/EXTERNAL_SUBTITLES.en.md)
- [Building](docs/BUILDING.md) / [Architecture](docs/ARCHITECTURE.md)

`main` contains static recompilation; the earlier Godot port is on the
[godot branch](https://github.com/dorakyuraduang/whitealbum2-Android/tree/godot).
Independent native migration remains experimental. Full-story compatibility and
device performance are still under development and require gameplay testing.

The public repository excludes original EXEs/PAKs, generated game functions and
initialization data. Complete AOT builds require those additional authorized
inputs; see the build documentation.

## License

Original project code and documentation use [MIT](LICENSE). Original game and
third-party content retain their own terms: see [NOTICE](NOTICE.md) and
[third-party notices](THIRD_PARTY_NOTICES.md). Distributions statically linking
FFmpeg must include matching relink materials, corresponding source and build
configuration as described in the build documentation.
