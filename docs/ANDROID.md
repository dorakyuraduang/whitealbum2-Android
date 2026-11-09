# Android setup

Requirements: Android 8.0+, ARM64 and OpenGL ES 3.0. The game resources remain
external; the APK contains no PAKs and requires no whole-archive extraction.

The default delivery returned to shared AOT on 2026-11-08; the current version
is **0.2.55-shared-aot**. Use the
standard `out/android/WA2-arm64-dev.apk` for the retained AOT application, or
`out/android/WA2-0.2.55-shared-aot-replace-native.apk` to update the experimental
0.3.x app in place. The latter retains its application ID and signing identity;
both APKs have identical DEX/native game code. No app is uninstalled by the
build tools. Native-format manual saves remain present but are not AOT saves.

The AOT host now reads and writes original-format saves and INI files directly
in the selected game folder through seekable provider descriptors. Root `Sys.sav`,
`save_00.sav`..`save_99.sav`, `save_Q.sav`, `SYSTEM.ini`, `D3D.ini` and optional
`default.ini` are accepted. The original main Documents/Leaf/WHITE ALBUM2 path
aliases this root. Existing files in that full nested PC directory are a
read fallback when the root file is absent; chapter-specific Documents folders
remain distinct. There is no private save mirror or fallback to unrelated app
data. Original PAKs remain read-only. Test mode uses the selected root's
`test-mode/` and leaves normal files untouched. Disable test mode to use normal
PC saves. Launcher switches keep using the same directory-bound configuration.
Directory grants must include persistent read/write access. Synthetic file/API
and pure Java tests do not establish on-device provider or gameplay behavior.

In 0.2.49, a common CreateFile operation resolves save aliases and metadata
once before checking sharing and opening the descriptor. Android indexes each
directory once inside a storage operation, including save inventory, rather
than querying all children again for each entry. Snapshots are cleared at the
next operation. In 0.2.50 the common AOT file adapter additionally shares the
inventory across known source save-reader calls in the same save/load UI
update. This inventory expires on writes, the next source frame or after at
most 250 ms. File descriptors and payloads are always opened afresh. Ordinary
file calls retain fresh per-operation metadata. There is no private save mirror.
See [save-loading checks](SAVE_LOADING.md) for the observed code paths and limits.

In 0.2.51 the common INI adapter also shares parsed profiles within the same
known main-thread source update. Reads of different keys do not reopen the
same configuration each time. Writes remain immediate; storage mutation,
explicit flush, a new update, a changed namespace or 250 ms expires the snapshot.
Ordinary/worker calls and open writers keep fresh reads. The runtime summary
adds `profile_io` counters. A separate same-signature, Activity-free device
probe can time the actual authorized provider without game or screen access;
its file-component results do not establish complete scene-load latency.

0.2.52 also admits the known read-only save helpers outside story save/load
substates, covering title-menu reads. State changes still invalidate the
inventory; ordinary API calls and writers do not qualify. `save_read_scope`
records at most 32 caller/thread/state/writer-count contexts, without payloads,
to reveal when the actual source path declines batching.

0.2.53 includes a settled document address in read metadata. The common file
adapter uses that optional opener for existing read-only files, avoiding another
directory traversal inside Android's descriptor-open operation. Each invocation
still opens a fresh descriptor; no save contents or descriptors are retained.
Writes/creates use the original path, and frame/state/age/mutation guards still
bound metadata reuse. References must belong to the current tree/session;
retired document IDs retry normal filename resolution, and unusually long
references decline this optimization. `prepared_read_opens` records actual use.

0.2.54 applies the same update-scoped inventory to chapter-specific save
directories, including absent directories and empty slots in the first list
scan. Main/root and chapter namespaces remain distinct. Private progress
diagnostics are coalesced after file activity, at most once per 500 ms and 64
times per run. They contain bounded I/O/resource counters and no frame pixels,
save payloads or selected-directory URI. The launcher selects the newer private
progress or exit report, marking progress records `complete:false`. Android
also emits bounded `WA2IO` counter records for owner-authorized wireless
diagnosis without stopping a running game. These are diagnostics, not game
saves/configuration; the latter remain in the selected folder.

0.2.55 also samples resource activity, so skip-time resource counters can
advance without a save/config read. The same 500 ms/64-snapshot limits apply.
Image archive reads, decode work and capture-copy time have separate counters.
Compressed images whose decode takes at least 0.5 ms can enter the shared
read-only cache on first use; cheaper data keeps repeat-only admission.
Individual images up to 8 MiB now fit, while total retained bytes remain capped
at 8 MiB/32 entries. Cache entries contain owned bytes, never mutable game
buffers. No resources are extracted into the APK or written to the folder.
First-time/evicted/oversize resources still read and decode normally; component
reuse checks do not measure whole-game fast-forward speed on a phone.

The separate native migration development package now saves game data in the
currently selected game folder: root `Sys.sav`, `native-settings.wa2cfg` and
`native-saves/`. FPS, long-press and test-mode launcher switches are in the
same root's `native-launcher.wa2cfg`; only the selected directory URI and its
authorization are remembered by the app. Writes use one background queue and
complete before game start; an error is shown rather than writing to private
storage. Test mode uses `native-test/` in that same folder. Renew an
old read-only folder grant to allow read/write access. PC system progress and
supported version-1/version-2 manual saves may be copied into this selection; native
scene saves use their own format and cannot be loaded by the original PC game.
This changes the native development target, not the retained 0.2.46 AOT binary.
`0.3.1-native-development` fixes startup import of the original `mov_lv=2`
setting. Original levels `2/1/0` select movie suffixes `0/1/2`; all three values
are supported. Update the native development APK with the same signing
identity; no INI edit or save/configuration deletion is needed for this error.
`0.3.2-native-development` keeps Android's SDL window immersive even when the
portable PC preference selects windowed mode. The 1280x720 logical canvas keeps
its aspect-ratio fit. It also restores the full confirmation-panel crop,
integer glyph edges/baselines for sized text and completed-message positions
in original manual saves. These changes have finite memory/file and build
checks; their appearance and system-bar behavior still need device validation.
`0.3.3-native-development` corrects font masks: 32-bit indexed font palettes
use their stored alpha, while RGB-only grayscale palettes retain their blue
coverage. Treating both formats as blue masks exposed transparent padding and
hid black strokes, producing thick white text. Owned-memory pixel checks cover
story dialogue and confirmation labels through the actual common text/SDL
renderer. The Android entry also supplies SDL's landscape-only orientation
hint; a resizable SDL window otherwise overrides the manifest with full-user
orientation. Both landscape directions remain allowed, with aspect-ratio fit.
No device, game or screen was used to validate this development package.
When copying old Documents/Leaf folders, the native host also merges the
introductory Sys.sav into current progress using the original startup rule.
Source flag 99 is excluded and the original files remain read-only. Unchanged
repeated launches do not rewrite progress; malformed or unreadable sources
produce an explicit error while preserving the published main save.

`0.2.46-shared-aot` expands the isolated test profile to all known content:
chapters/endings, CG differences, music, replay entries, watched movies and read
text. Existing chapter-only test saves are upgraded on the next test launch.
The local APK has a **new development signing identity** because the previous
key was deleted with the build directory and has no backup. It cannot update
an installation signed with the old key. The additional `parallel-test.apk`
uses application ID `org.wa2.port.test` and launcher name **WA2 全内容测试**, so
it can be installed beside the old app without uninstalling it. Both packages
contain identical DEX/native libraries; only the installation identity and label
differ. The parallel app has separate saves and preferences: select your game folder again
and enable **测试模式（解锁全部内容）** there. It does not import old private saves.

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
or additional subtitle data needs to be placed in the game folder. Caption matching and
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

1. Place your own original `.pak` files directly in a folder of your choice and
   retain the original `IC` child folder. The folder name is unrestricted.
2. Open the app to its launch settings page and select that folder through
   Android's system directory picker. The provider must supply persistent
   read/write grants and seekable file descriptors. Local storage and SD cards
   normally provide these; opaque provider document IDs are supported.
3. Set **显示 FPS 和帧耗时**, **长按快进**, and **测试模式（解锁全部内容）** as desired. Long-press fast-forward is
   enabled by default; test mode is off by default. The folder grant and switches are remembered for
   later launches; changing a switch saves it immediately.
4. Tap **开始游戏**. The folder is checked in a background worker before the game
   initializes. Returning to the launcher allows settings to be changed.
5. If a load remains slow, exit normally through the game menu, then tap
   **复制上次运行诊断** on the launcher. It copies a bounded summary of the last
   run's I/O, resource and scheduler counters/times, plus the release/error.
   It contains no screen image or save payload and does not monitor the game.

From `0.2.46-shared-aot`, **测试模式（解锁全部内容）** uses a separate test
profile with all known content gates unlocked. Toggle it on and tap **开始游戏**.
Changing this switch takes effect on the next game start; turn it off and start
again to return to the normal profile.

Normal progress and settings remain in their existing location. Test progress,
manual/quick saves and settings use the `test-mode` child of the private game
data directory. The common adapter prepares `Documents/Leaf/WHITE ALBUM2`,
`WHITE ALBUM2 -closing chapter-` and `WHITE ALBUM2 -introductory chapter-` there.
On each test launch it upgrades existing chapter-only system saves in place:

- Set the low bit of 122 known content flags: 18 completion/second-playthrough,
  64 music, 16 watched-movie and 24 replay flags.
- Unlock all 272 valid CG variant IDs from the 149 nonempty gallery groups, in
  both saved gallery banks. Empty catalogue entries and index zero stay empty.
- Mark the 0x80000-byte reading-history block as read, permitting read-only skip
  throughout the test profile. This does not alter the game's skip timing.

The rest of the gallery block, unknown system flags, choice-state flags 99/900,
and all high bits are preserved. Manual/quick saves and settings are untouched;
an already-unlocked Sys.sav is not rewritten. The adapter does not create a
playable scene snapshot or supply content absent from the game folder. Original PAKs stay
external. Windows `--test-mode` uses the same implementation.

Memory checks load synthetic saves with the actual static readers, enumerate
all CG groups/differences with the static getters, and check reading bits,
upgrade behavior, normal-save isolation, retained manual saves and malformed
private saves. They do not exercise game UI or prove phone gameplay compatibility.

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
