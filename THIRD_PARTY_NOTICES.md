# Third-party components

| Component | Use | License / notices |
| --- | --- | --- |
| WinRecomp runtime subset | Checked memory, ABI state and compatibility services | [MIT](tools/WindowsRecompTool-main/LICENSE), [provenance](tools/WindowsRecompTool-main/NOTICE.md) |
| SDL 2.32.10 | Window, input, audio-device and Android lifecycle adapter | [zlib](tools/SDL-release-2.32.10/LICENSE.txt) |
| MojoShader | Common shader parser and OpenGL translation adapter | [upstream license](tools/mojoshader-main/LICENSE.txt) |
| MojoShader assembler | Shader assembly support | [upstream license](tools/mojoshader_assembler-main/LICENSE.txt) |
| stb_vorbis 1.22 | Streaming Ogg/Vorbis decoding | [source and embedded MIT/public-domain terms](third_party/stb/stb_vorbis.c), [version record](third_party/stb/README.md) |
| FFmpeg 9.0.2, selected components | WMV/WMA and selected legacy media codecs | LGPL-2.1-or-later; [license](third_party/ffmpeg/COPYING.LGPLv2.1), [source/configuration record](third_party/ffmpeg/NOTICE.txt) |

SDL and MojoShader source folders may include additional file-level notices;
preserve them when modifying or redistributing those folders. Development
tools such as the Android SDK/NDK, JDK, CMake and Zig are not relicensed by this
project and are not bundled in the source repository.

FFmpeg source preparation and codec configuration are implemented in
`scripts/prepare_media_sources.py` and `scripts/build_media_codec.py`. The
configuration disables GPL, nonfree and version-3-only components. A distributed
statically linked host must be accompanied by matching source, configuration
and application link materials for relinking with a modified LGPL library.
`scripts/package_shared_hosts.py` checks the matching `WA2-shared-relink.zip`;
publishing an APK or EXE alone is not the project's complete release procedure.

Original game resources and game-derived private build inputs have separate
rights. They are not third-party open-source dependencies and must not be added
to this repository or described as MIT-licensed assets.
