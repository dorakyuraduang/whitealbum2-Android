# License scope and provenance

The root MIT license covers newly authored project code and documentation only.
It does not replace notices supplied by dependencies and does not grant rights
to WHITE ALBUM2, its executable, translation patches, scripts, music, artwork,
fonts, videos, save files or trademarks. This is an unofficial development
project; it is not endorsed by the game's rights holders.

Generated translations of game functions, cached analysis, initial executable
memory and raw instruction snapshots are separate game-derived inputs. They
are intentionally omitted from the public source repository and are not
relicensed under MIT. A local development checkout may contain these inputs;
their presence does not make them part of the public license grant. You must
supply authorized inputs and game resources yourself to build or run a host.

The checked native replacements preserve observed function contracts. The
project does not claim authorship of the original game's code or algorithms.
No new executable analysis is performed by the shared-source build workflow.

The selected runtime files under `tools/WindowsRecompTool-main/` retain
`Copyright (c) 2026 WinRecomp contributors` and their own MIT license. Local
changes to the allocator and compatibility runtime are provided under the same
license; see the upstream notice alongside those files.

SDL, MojoShader, stb_vorbis and FFmpeg retain their own licenses. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). In particular, the MIT license
of this project does not remove LGPL obligations for a linked FFmpeg build.

The separately licensed main-story caption data from nxs7's
WA2-CHS-Subtitle-Patch is included under its upstream MIT terms, preserved in
`third_party/wa2_subtitles/LICENSE`. This explicit grant covers that imported
caption file only; it does not grant rights to other translation patches or
game assets. SpecialContents subtitle data is excluded. The small caption
bitmap charset uses AOSP Droid Sans Fallback under its separately preserved
Apache 2.0 font notices.
