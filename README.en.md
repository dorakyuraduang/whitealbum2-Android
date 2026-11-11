# WHITE ALBUM2 Static Recompilation

[简体中文](README.md) · **English**

A static-recompilation port for **Android ARM64**, using shared game logic
across platforms. Current development version: **0.2.78-shared-aot**.
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

Chinese is the default. Put the selected language's script and font packs in
the root of the game folder:

| Game language | Script pack | Font pack |
| --- | --- | --- |
| 简体中文 / Chinese (default) | `ck-gal.pak` | `fon.pak` |
| 日本語 / Japanese | `script.pak` | `fnt.pak` |
| English | `en.pak` | `fon_en.pak` |
| Українська / Ukrainian | `ua.pak` | `font_ua.pak` |

You also need the complete original graphics, audio, movie and other resources.
The launcher follows the phone's primary system language, falling back to English
when unsupported. A manual **Interface language** choice is remembered and takes
priority. **Game language** independently selects game resources.

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
