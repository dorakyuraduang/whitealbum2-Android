# Android setup

Requirements: Android 8.0+, ARM64 and OpenGL ES 3.0. The game resources remain
external; the APK contains no PAKs and requires no whole-archive extraction.

`0.2.45-shared-aot` extends the missing-target repair to 34 additional referenced
callbacks and their supporting call chains, including virtual destructors,
exception cleanup, floating-format callbacks and a catch resumption prefix.
Both platforms compile the same recovered static source set. The generator
checks the full direct-call/branch destination inventory and refuses unresolved
targets. Install over the existing app; phone gameplay remains unverified.

`0.2.44-shared-aot` fixes the missing static callback at `4548176` (`0x456650`).
The shared AOT generator now includes this four-argument script wait command
from existing cached analysis and checked C++ templates. Its original wait
callee, timer boundaries, fast-forward and input cancellation remain intact.
All four entries in the separate extension table are checked at generation.
This fixes a known missing entrance; complete chapter/device compatibility
still requires gameplay testing. Install the update over the existing app.

From `0.2.39-shared-aot`, the main-story audio-only captions from nxs7's patch
are embedded and enabled automatically: 46 audio tracks, 315 timed captions.
The whole SpecialContents subtitle set is excluded. No subtitle DLL, font file
or additional subtitle data needs to be placed in Wa2Res. Caption matching and
rendering use the same sources on Windows and Android; captions follow the
existing game viewport instead of stretching to the phone screen. See
[scope, licenses and verification limits](SUBTITLES.md).

`0.2.41-shared-aot` shares batched literal resource decoding and an optimized
native loading adapter with Windows. First-use loads can benefit without a
larger decoded cache or extracted assets. Generated CPU comparisons cover the
decoder and the native loading boundary; whole-game fast-forward and ARM64
device performance remain unmeasured.

`0.2.42-shared-aot` adds the same exact integer glyph path and single-cell
sprite geometry used on Windows. Eligible inputs construct identical IEEE
coordinate words, without rounding positions, reducing glyph density or
changing timing. Other inputs retain the existing native floating path. The
checks compare against cached static drawing and the verified previous native
library; they do not measure Android gameplay or GPU throughput.

1. Place your own original `.pak` files directly in `Wa2Res` and retain the `IC`
   child folder. The default location is internal shared storage `/Wa2Res`.
2. Open the app to its launch settings page. Choose the local `Wa2Res` folder
   through Android's system directory picker. A nested Wa2Res or SD-card Wa2Res
   is supported; select the resource folder itself rather than its parent or IC.
3. Set **显示 FPS 和帧耗时**, **长按快进**, and **测试模式（解锁全部章节）** as desired. Long-press fast-forward is
   enabled by default; test mode is off by default. The folder grant and switches are remembered for
   later launches; changing a switch saves it immediately.
4. Tap **开始游戏**. The folder is checked in a background worker before the game
   initializes. Returning to the launcher allows settings to be changed.

From `0.2.43-shared-aot`, **测试模式（解锁全部章节）** starts a separate test profile
with the known IC, CC and CODA chapter menu gates unlocked. Toggle it on at the
launcher, then tap **开始游戏**; use the original game's chapter choices. Changing
the switch takes effect on the next game start, rather than changing a running
story. Turn it off and start again to return to the normal profile.

Normal progress and settings are left in their existing location. Test progress,
manual/quick saves and game settings use the `test-mode` child of the private game
data directory. The common save adapter initializes `Documents/Leaf/WHITE ALBUM2`,
`WHITE ALBUM2 -closing chapter-` and `WHITE ALBUM2 -introductory chapter-` there.
Existing test saves remain; known unlock bits are restored on every test launch.
The adapter changes only the 14 verified chapter/ending gate bits in `Sys.sav`,
not all global variables, choice history or gallery contents. It neither fabricates
a playable scene snapshot nor unlocks resources absent from Wa2Res. Original PAKs
stay external. Windows `--test-mode` uses this same implementation.

Memory checks invoke the actual static system-save readers/getter and check
normal-save isolation, repeated activation, retained manual saves and unsupported
private formats. They do not exercise the chapter UI or prove complete gameplay
on a phone.

With **长按快进** enabled, hold one finger still on the game canvas to hold the
original game's Ctrl input. The threshold follows Android's long-press setting
(normally about half a second); lifting, dragging, adding a finger, opening a
dialog or losing focus releases the touch hold. A physical Ctrl key remains
independently usable. With the switch disabled, a stationary touch sends one
ordinary click when lifted. Pressing the menu button never initiates a skip hold.

Tap with two fingers to hide the top-right **☰** button; repeat to show it. The
gesture also works over the button's area and is recognized before child views
split a multi-touch stream. Both fingers must form a brief tap within 300 ms and
the normal touch slop. Moving, three fingers or holding two fingers does not
toggle it. A recognized pair sends no ordinary game or button click. Menu
updates preserve the hidden state for this game session; a new Activity starts
with the button visible. Native setting dialogs and the launcher use their own
normal input. These input paths have JVM trace and API compilation checks; actual
Android touch dispatch and device behavior have not been exercised.

The app requests persistent read access to that selected directory, not blanket
storage permissions. If access is revoked or the folder is moved, choose it
again. A cancelled choice preserves the previous selection. Only root PAKs and
the known IC child are indexed; unrelated folders are not recursively scanned.

When enabled, the HUD shows successful game presents and average frame interval.
Requesting a 120 Hz display mode does not guarantee 120 FPS. When disabled, the
label is not created, and native samples are discarded before allocating display samples. The game keeps its aspect
ratio; launcher settings do not introduce a separate Android game implementation.

The top-right **☰** button opens the game's original function menu. From
`0.2.33-shared-aot`, its bounds use 48 dp with centered text rather than 48 raw
pixels and default button padding. Menu, settings-control and file-chooser text
is serialized from UTF-16 directly to JSON Unicode escapes. Earlier builds
escaped each UTF-8 byte as a separate character, corrupting Chinese/Japanese
labels and non-ASCII file paths. Game command IDs and settings behavior remain
in the shared window model. Unicode transport has memory-only regression checks;
actual device layout and Android runtime rendering have not been tested under
the current contract.

`0.2.34-shared-aot` also reconciles each complete dialog snapshot before applying
control properties. Missing/replaced controls are constructed and obsolete views
are removed; unchanged views keep their identity and keyboard state. Duplicate or
invalid definitions still fail explicitly. Queued UI work belongs to its original
Activity generation, and callbacks from retired views cannot submit commands to
a new window. Pure JVM checks exercise the production ownership/session helpers,
including 64 reopen cycles and all 53 cached setting controls. These checks do
not reproduce a player's actual Android UI interaction or establish device
compatibility.

`0.2.37-shared-aot` fixes `Unsupported source static style 3` when opening the
original version-information dialogs. Style 3 is an icon, whereas the Android
static-control factory previously accepted only text/frame styles. The shared
window model now resolves the cached group/image resource, selects its 32-pixel
image and transports the original BGRA32 DIB. Android creates an ImageView and
decodes its alpha channel or legacy transparency mask. Repeated snapshots reuse
the same bitmap. Missing resources, unsupported formats and unknown styles still
fail explicitly. The icon is separate from the launcher's `icon.ico` artwork.

Earlier ownership checks used placeholder objects and could not detect this
missing widget type. New checks execute the production type classifier and icon
decoder on the JVM, compare both cached icons' pixels, reject malformed inputs
and exercise 64 version-dialog open/close cycles in the shared memory host.
Queued messages for known destroyed windows cannot abort a new modal loop.
These checks do not launch a game or create Android Views; actual device
presentation remains unverified.

`0.2.35-shared-aot` uses the same bounded LZSS repeated-match grouping on ARM64
and Windows. Eight verified 18-byte matches of one repeating 1/2/4/8/16-byte word
can be expanded together. Other token layouts, initial dictionary references and
short buffers keep the existing checked path. Generated RAM checks cover output,
dictionary, flags, caller-owned dictionary dependencies and failure prefixes;
these do not establish Android fast-forward speed. The 8 MiB decoded cache limit
and external PAK storage policy are unchanged.

`0.2.36-shared-aot` shares font-index, glyph-geometry and exact byte-comparison
optimizations with Windows. A changing font table is fully rechecked; changed
glyph inputs, images, palettes or texture handles cannot reuse an old tile.
The two new caches allocate about 0.5 MiB together on first use. A new glyph does
not capture a complete geometry entry until repeated. These are CPU changes;
no particle count, render ordering, image scale, game timer or wait deadline is
reduced. Device FPS and whole fast-forward speed still require phone testing.

Persistent read grants use Android's [Storage Access Framework](https://developer.android.com/training/data-storage/shared/documents-files).
The launch page has been compiled against API 35 and its routing/location policy
has bounded checks. Interactive picker/lifecycle behavior and device gameplay
have not been tested under the current no-game/no-screen development contract.
