# Main-story audio subtitles

Only `script/sub.yaml` from
[nxs7/WA2-CHS-Subtitle-Patch](https://github.com/nxs7/WA2-CHS-Subtitle-Patch)
at commit `bb3cbc2f459549f8afbcb56cafd4b861d763bd29` is imported. Its upstream
MIT license is reproduced verbatim in `LICENSE`. Upstream credits the
TodokanaiTL projects for the subtitle implementation and English translation;
this project does not claim authorship of the imported Chinese text.

This file contains 6 sound-effect tracks and 40 voice/interview tracks, with
315 timed captions. It adds text to audio-only passages; it is not a replacement
translation of every game script. The entire `SpecialContents/` subtitle set
(radio dramas, extra songs and other supplemental material) is excluded.
The main game's own `scene=9500` character interviews remain included because
they are part of the upstream main-story file, not the SpecialContents file.

The portable implementation preserves original audio scene/ID matching,
including voices reused in other story scenes. It observes five existing static
audio function returns; it does not ship or inject the upstream x86 DLL, patch
executable memory, or execute the upstream build scripts. Unrelated sounds do
not replace an active caption; a matching source stop clears it. Captions use
the original timing in milliseconds, white letters with a black outline at the
top of the existing game viewport, with wrapping for overlong lines. All source
game functions still execute in full. Windows and Android compile the same
event, caption, font and graphics code.

`main_story.inc` is generated only from the main YAML. `font.inc` contains
4-bit run-length encoded fill/outline coverage for exactly its 891 characters,
not an entire font file or any original game font. Source glyphs come from
Droid Sans Fallback from the Android Open Source Project under Apache 2.0;
see `FONT-NOTICE.txt` and the exact source blob and SHA-256 in `source.json`.
The generated asset is called the WA2 caption bitmap charset. The source font is
kept in the ignored build cache and is not distributed in the application.

Normal validation is `python scripts/prepare_subtitles.py --check`, which needs
only the Python standard library. To regenerate bitmap coverage, supply the
exact source font recorded in `source.json` and install Pillow, then run
`python scripts/prepare_subtitles.py --font /path/to/DroidSansFallback.ttf`.
Builds require no network, subtitle DLL, loose subtitle files or phone fonts.
Original PAK resources remain external. Memory checks and offline shader
compilation do not verify full gameplay, screen appearance or device timing.
