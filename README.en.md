# WHITE ALBUM2 Static Recompilation

[简体中文](README.md) · **English**

A shared static-recompilation project for **Android ARM64 / Windows x64**.
Current development version: **0.2.67-shared-aot**. Both platforms compile the
same game logic. Supply your own original PAK resources; whole-archive extraction
is not required.

**[Downloads and releases](https://github.com/dorakyuraduang/whitealbum2-Android/releases)**

## Quick start

### Android

Requirements: **Android 8.0+, ARM64 and OpenGL ES 3.0**.

1. Install `WA2-arm64-<release-name>.apk`. The English game-resource profile
   requires **0.2.67 or newer**.
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

You also need the complete original graphics, audio, movie and other resources.
**Interface language** changes the launcher; **Game language** selects game
resources.

For English, retain the original `script.pak` for base script data and select
**English** in the launcher. `fon_en.pak` must contain the matching game font
atlases. If your English `en.pak` contains an `fnt` directory, generate a separate
font pack with `python scripts/prepare_english_font.py --root "your game folder"`.
This copies packed font members without decompressing them or changing `en.pak`.
See [resource and patch details](docs/LANGUAGE_RESOURCES.md).

### Windows

Place the compiled `WA2_SDL2_x64.exe` beside the original PAK files and launch it,
or specify the game folder:

```powershell
.\WA2_SDL2_x64.exe --root "D:\Games\WHITE ALBUM2" --language en-US
```

Omit `--language` for Chinese; use `--language ja-JP` for Japanese. In a prepared
local workspace, `run.cmd` uses the `res/` folder.

## Saves, settings and captions

Saves and settings are stored in the selected game folder. You can copy original
PC files such as `Sys.sav`, `save_00.sav`, `SYSTEM.ini` and `D3D.ini` there. Test
mode uses its own `test-mode/` folder; disable it when using normal saves.

Embedded main-story supplementary captions are Chinese by default, independently
of game language. Disable them or select a translated external `sub.yaml`.
External captions replace the complete embedded set; ordinary story dialogue
comes from the selected scripts and fonts.

## Documentation and development

- [Android setup](docs/ANDROID.md)
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
