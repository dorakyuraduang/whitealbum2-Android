# WHITE ALBUM2 Static Recompilation

[简体中文](README.md) · **English**

A static-recompilation port for **Android ARM64**, using shared game logic
across platforms. Current development version: **0.2.87-shared-aot**.
Supply your own original PAK resources; whole-archive extraction is not required.

**[Downloads and releases](https://github.com/dorakyuraduang/whitealbum2-Android/releases)**

## Quick start

Requirements: **Android 8.0+, ARM64 and OpenGL ES 3.0**.

1. Install `WA2-arm64-<release-name>.apk`.
2. Copy your complete game resources to any folder on your phone. Keep the `IC`
   subfolder. **Any folder name is accepted; leave the PAK files packed.**
3. Open the app, select that folder and grant read/write access.
4. Select **Game language**, adjust FPS, hold-to-skip and extra-caption options
   as needed, then tap **Start game**.

Movies support MP4/MOV, MKV/WebM, AVI, WMV/ASF, MPEG/TS and FLV, alongside the
original and **Final Full Patch 1.0.3.0** video PAKs. Keep the game's filenames
and `IC` paths. Unreadable or undecodable movies are skipped so the story can
continue; the reason is retained in runtime diagnostics. See [movie formats and
failure handling](docs/MOVIE_FORMATS.md).

Chinese is the default. Retain the complete original resources and add the
selected language's patch:

| Game language | Script pack | Font pack |
| --- | --- | --- |
| 简体中文 / Chinese (default) | `ck-gal.pak` | `fon.pak` |
| 日本語 / Japanese | `script.pak` | `fnt.pak` |
| English | `en.pak`; loose overrides in `script/`, `IC/script/` | Loose `fnt/`; remaining atlases from the `fnt` group in `en.pak` |
| Українська / Ukrainian | PC patch's `en.pak`, `script/`, `IC/script/` | PC patch's `fnt/`; remaining atlases from `en.pak` |

You also need the complete original graphics, audio, movie and other resources.
The launcher follows the phone's primary system language, falling back to English
when unsupported. A manual **Interface language** choice is remembered and takes
priority. **Game language** independently selects game resources.

[Loose resource overrides](docs/LOOSE_RESOURCES.md) use `grp/`, `bak/` and
`char/` for graphics. Japanese, English and Ukrainian use the PC folders
`script/`, `IC/script/` and `fnt/`; Chinese keeps `ck-gal/` and `fon/`.
Keep original filenames and `.txt`/`.bnr` pairs. Loose files take priority;
missing files fall back to packed resources.

Select **English** for the English patch or **Українська** for UA **0.1.5**.
Ukrainian retains its matching names and text handling. Keep the original
`script.pak`, `IC` resources and original script encoding. Copy the patch's
`en.pak`, `script/`, `IC/script/` and `fnt/` with the same layout as on PC;
no repacking is required.

Previously prepared `fon_en.pak`, or `ua.pak` and `font_ua.pak`, remain supported,
as do their legacy loose folders. See [language resources](docs/LANGUAGE_RESOURCES.md)
and [Ukrainian support](docs/UKRAINIAN_SUPPORT.md). Patch EXE/DLL files are not run.

## Saves, settings and captions

### Normal saves

**All game saves are external files.** `<game folder>` means the folder selected
in the launcher. Use the PC directory layout for normal saves:

```text
<game folder>/Documents/Leaf/WHITE ALBUM2/
```

`Sys.sav` stores global progress and unlocks, `save_00.sav` through `save_99.sav`
are manual saves, and `save_Q.sav` is the quick save. To import PC saves, exit the
game, copy the files into this directory and disable test mode before loading.

From 0.2.76, normal saves are read and written only in this directory; it is
created on the first save. Root-level `.sav` files are ignored and are not
merged or moved automatically. Copy any saves you want to retain into this directory.

### Test-mode saves

With **Test mode (unlock all)** enabled, saves use this separate directory
inside the same external game folder:

```text
<game folder>/test-mode/
```

For example, `test-mode/Sys.sav` and `test-mode/save_00.sav`. Disable test mode
to return to normal saves.

### Settings

Android launcher options are stored in `<game folder>/native-launcher.wa2cfg`.
Game settings use `SYSTEM.ini` and `D3D.ini`; keep existing files in their current
directories.

### Supplementary captions

Embedded main-story supplementary captions are Chinese by default, controlled
by **Extra captions**. To use an external file, select a UTF-8 `sub.yaml` or
original Todokanai `subtitles` file under **Caption source** and enable
**Extra captions**. The external file replaces the complete embedded set;
Todokanai files need no YAML conversion. English letters and spaces use measured
advances for centring and wrapping.

Story dialogue comes from the selected scripts and fonts; supplementary caption
language comes from the caption file. The UA 0.1.5 captions contain English text
and can be translated before selection.

## Documentation and development

- [Windows PAK pack/unpack tool](docs/PAK_TOOL.md): folder-to-PAK and PAK-to-folder; KCAP/LAC support.
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
