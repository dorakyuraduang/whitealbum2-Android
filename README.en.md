# WHITE ALBUM2 Static Recompilation

[简体中文](README.md) · **English**

A static-recompilation port for **Android ARM64**, using shared game logic
across platforms. Current release: **0.2.126-special-aot** (version code 129).
One APK provides **Main / Special Contents (SC)** entries with separate resource
folders and launcher settings. SC supports **Chinese, Japanese and English**.
Supply your own original PAK resources; whole-archive extraction is not required.

**[Download 0.2.126](https://github.com/dorakyuraduang/whitealbum2-Android/releases/tag/r0.2.126)** · [All releases](https://github.com/dorakyuraduang/whitealbum2-Android/releases)

The integrated APK retains the Main `0.2.108-shared-aot` runtime and runs SC in
a separate process. English SC uses the **Special Contents full patch v1.0**.
Complete English gameplay and on-phone caption synchronization still need manual
verification. See the [release notes](docs/releases/0.2.126.md) and
[SC details](docs/SPECIAL_CONTENTS.md).

## Quick start

Requirements: **Android 8.0+, ARM64 and OpenGL ES 3.0**.

1. Install `WA2-arm64-0.2.126-special-aot.apk`. Update an existing installation
   in place to retain its application data.
2. Copy the corresponding game's complete resources to any folder on your phone.
   Keep Main's `IC` subfolder and use separate folders for Main and SC.
   **Any folder name is accepted; leave the PAK files packed.**
3. Open the app, choose **Main** or **SC**, select its folder and grant read/write access.
4. Select **Game language**, choose your FPS display, 60 FPS limit (off by default),
   hold-to-skip and extra-caption settings, then tap **Start game**.

Movies support MP4/MOV, MKV/WebM, AVI, WMV/ASF, MPEG/TS and FLV, alongside the
original and **Final Full Patch 1.0.3.0** video PAKs. Keep the game's filenames
and `IC` paths. Unreadable or undecodable movies are skipped so the story can
continue; the reason is retained in runtime diagnostics. See [movie formats and
failure handling](docs/MOVIE_FORMATS.md).

### Main languages and resources

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

### SC languages and English resource preparation

Keep SC's complete original graphics, audio and movie resources alongside the
selected language's files:

| SC language | Interface | Scripts | Fonts |
| --- | --- | --- | --- |
| Chinese | `gcn.pak` | `script_cn.pak`, `script_cn2.pak` | `fon.pak` |
| Japanese | `grp.pak` | `script.pak` | `fnt.pak` |
| English | `grp` / `bak` groups in `en.pak`, original graphics as fallback | `script` group in `en.pak`; retain original `script.pak` | `fnt` group in `en.pak` |

Download and extract **`WA2-SC-English-resource-tools.zip`** from this release.
With Python 3.9+ and its standard library, run this in the extracted directory,
supplying your own full English patch ZIP and original SC resource folder:

```powershell
python scripts/prepare_special_english_resources.py "path/to/White Album 2 Special Contents full patch (v1.0).zip" --base-root "path/to/original SC folder"
```

Copy **`en.pak`, `subtext_en.dat`, `mv000_en.pak`, `credits_en.pak`** from
`out/special-english-resources/` into the SC folder selected on your phone,
then choose **SC → English**. No compiler or private analysis cache is required.
The tool never runs the original EXE and preserves the original Chinese/Japanese
movies. The tool archive and APK do not include these game resources.

**SC selects supplementary captions automatically by game language; there is no
external-caption picker.** Chinese reads `subtext.dat`, `subfont.tga` and
`songfont.tga`; English reads `subtext_en.dat`. Japanese uses `subtext_ja.dat`,
which is not supplied by this English patch. Supply matching files or disable
extra captions when unavailable. See [SC details](docs/SPECIAL_CONTENTS.md).

## Saves, settings and captions

### Normal saves

**All game saves are external files.** `<game folder>` means the folder selected
for the corresponding title in the launcher. Main uses the PC directory layout:

```text
<game folder>/Documents/Leaf/WHITE ALBUM2/
```

`Sys.sav` stores global progress and unlocks, `save_00.sav` through `save_99.sav`
are manual saves, and `save_Q.sav` is the quick save. To import PC saves, exit the
game, copy the files into this directory and disable test mode before loading.

From 0.2.76, normal saves are read and written only in this directory; it is
created on the first save. Root-level `.sav` files are ignored and are not
merged or moved automatically. Copy any saves you want to retain into this directory.

SC uses `Documents/Leaf/WHITE ALBUM2 Special Contents/` inside its own resource
folder. Main and SC saves remain separate and are not automatically merged.

### Test-mode saves

With Main's **Test mode (unlock all)** enabled, saves use this separate directory
inside the same external game folder:

```text
<game folder>/test-mode/
```

For example, `test-mode/Sys.sav` and `test-mode/save_00.sav`. Disable test mode
to return to normal saves.
SC does not provide unlock-all test mode.

### Settings

Main launcher options use `<Main folder>/native-launcher.wa2cfg`; SC uses
`<SC folder>/native-launcher-special.wa2cfg`. Languages and switches are saved
independently for each title.
Game settings use `SYSTEM.ini` and `D3D.ini`; keep existing files in their current
directories.

### Supplementary captions

Embedded Main supplementary captions are Chinese by default, controlled
by **Extra captions**. To use an external file for Main, select a UTF-8 `sub.yaml` or
original Todokanai `subtitles` file under **Caption source** and enable
**Extra captions**. The external file replaces the complete embedded set;
Todokanai files need no YAML conversion. English letters and spaces use measured
advances for centring and wrapping.
SC follows the automatic version-specific caption rules above.

Story dialogue comes from the selected scripts and fonts; supplementary caption
language comes from the caption file. The UA 0.1.5 captions contain English text
and can be translated before selection.

## Documentation and development

- [Windows-to-Android static recompilation and optimization workflow (Chinese)](docs/WINDOWS_TO_ANDROID_AOT.md)
- [Windows PAK pack/unpack tool](docs/PAK_TOOL.md): folder-to-PAK and PAK-to-folder; KCAP/LAC support.
- [Android setup](docs/ANDROID.md)
- [Android launcher and runtime integration (Chinese)](docs/STANDALONE_LAUNCHER.md)
- [SC languages, captions and builds (Chinese)](docs/SPECIAL_CONTENTS.md)
- [0.2.126 release notes](docs/releases/0.2.126.md)
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
This release includes `WA2-0.2.126-special-aot-relink.zip` for both Main and SC;
phone installation needs only the APK. Asset hashes are in `SHA256SUMS.txt`.
