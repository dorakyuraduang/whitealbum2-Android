# Building and verification

## Independent native migration core

The user selected a shared native engine target on 2026-11-06. Run
`python scripts/verify_native_core.py` to build the existing native scene and
the new system-progress module for Windows x64 and Android ARM64. The script
uses the local Zig, CMake/Ninja, NDK r27 and previously built common media-codec
libraries. It does not generate or link any game AOT source or open the source
EXE. `--zig-cache /path/to/cache` may reuse an existing compiler cache.

The build stays hidden, below normal priority, with one job and one CPU in
this local verifier. It compares actual common source lists and checks the
Ninja link closure plus native-library symbols for AOT/guest-runtime imports.
The linked scene probe is never executed. Synthetic native progress, script,
control and camera tests run with constructed LSCR, randomized format data,
small in-memory KCAP images and fresh synthetic save files.
Its fixtures cover signed flag values, reserved flag 99, both gallery banks,
reading-bit order/bounds, command dispatch, persistent restart, narrow block
writes, I/O-failure retries, malformed formats and hard-link refusal.

Results are written to `logs/native-core-verification.json`. These are component
and link checks, not gameplay verification. They produce no APK and do not
replace the current 0.2.46 delivery. The instructions below describe the retained
AOT build, which still requires its private cached analysis inputs.

The finite movie-input check uses fake transports and constructed archives.
It covers first-view/seen skip gates, movie ownership through EOF cleanup,
frozen hidden-scene controllers, consuming the completing input frame before
accelerated story ticks, background release and transactional retirement.
Its report is `native-movie-input.json` in the Windows core build directory;
it initializes no device and executes no original script or game.

`native-legacy-script.json` checks the original script-save block converter
against owned format records and LSCR programs. It validates globals, locals,
flow/call stacks, suspended command text policy and conversion failure guards.
Only constructed continuations are executed after conversion; original saves
and scripts are not opened. The aggregate scene and slot checks below exercise
the separate common application integration.

`native-legacy-save.json` additionally checks the bounded original header and
version-2 scene audio converter. It uses constructed byte records and fake
transports to verify loop restoration, omitted one-shots/fading-out music,
current preferences and transactional failure. It opens no original save,
audio device or game; this report is not whole-scene save compatibility.

`native-legacy-scene.json` additionally checks version-2 visual, dialogue,
selection, nested-call, weather and audio reconstruction into native scene
owners. `native-legacy-slots.json` covers read-only original directory layers,
bounded full-thumbnail previews, native precedence and actual application
load/native-save/failure routes using constructed saves and uniquely owned
temporary directories. `native-save-view.json` checks both original-square and
native-wide thumbnail crops. `native-legacy-v1.json` adds the older record's
dimension/character/text layout, original reset policy, read-only source
retention and actual application load/native-save route. These tests do not
open user saves or execute an original story. Unrecoverable pending command
data remains explicitly unsupported; this is not whole-game compatibility proof.

`verify_native_android_ui.py` also compiles and runs the finite
`LegacySavePathTest` without Android services. The native Android definition
check matches the actual Java descriptors for original-save metadata/open calls
with the linked JNI literals. Original PAKs and manual saves stay external;
the Android provider itself is not run by these checks.

`native-character-palettes.json` checks automatic background/time palette
selection and the source chroma/primary/secondary order using owned TGA and AMP
bytes. It also covers preserved characters, CG/reuse state, native and original
save conversion, old control-record compatibility and composite cache reuse.
The image-style, control-state, scene-checkpoint and loading-cache checks cover
the related shared paths. No original image is decoded and no display, game or
performance test is run by this check.

`native-extra-grid.json` also checks full-CG native-resolution cropping,
four/eight-pixel edge scroll bands, bounded viewports, thumbnail expansion and
retained crops through failed/retried variant loads. All images are constructed
fixtures. Scrolling is checked with resource requests counted and denied; no
original CG pixels, window, event injection or screen access is involved.

The Windows entry link includes its actual owned-menubar adapter. The
`native-sdl-application.json` memory check covers immediate menu suspension,
a synthetic long modal gap, released keys/pointer and exactly-once selected
commands through the production pump. It never opens a Win32 menu/window or
polls OS events. Entry/source-closure checks additionally require the Windows
adapter while both hosts continue compiling identical common game sources.
The same finite checks cover the source's default edge and Alt-only activation,
without polling a keyboard. Preference checks import `menu=0/1`, preserve the
setting through ordinary option changes/reset, read the old `WAP4` format and
reject malformed `WAP5` flags. Android stores the same preference record.
`native-music-view.json` also checks the selected name/information sprites from
their separate sheets, all sixty-three row bounds, page retention and stopped
visibility using constructed atlas data only.
`native-progress-ranges.json` checks existing-file updates of one byte for a
single CG/read change and four bytes for one flag. It also covers coalesced
spans, the last byte of every block, complete missing-file initialization,
old-adapter fallback, partial-write retry and unknown-field retention. All data
and directory services are constructed in memory; no user save or device is
opened. The ordinary file adapter remains covered by the existing owned-file
progress and selected-directory checks.

Native script state checks additionally cover exact float payloads, nested call
resumption, local/global values, pending host-command continuations, transactional
restore failures and conditionally visible selections. `--resource-root res`
optionally parses the external ck-gal.pak scripts to validate every instruction
and label boundary and catalogue command reference. This static resource audit
does not execute script commands or create a game scene/window/device.
It also checks the four original calendar atlas headers and crop bounds without
decoding or presenting their pixels.
The same audit validates original INAS animation structure and texture-crop
bounds without advancing its sequences. Animation clock/loop/checkpoint tests
use constructed containers only.
The same static audit parses the two main-story AMP colour-map containers.
Colour conversion, alpha preservation and style-specific cache checks operate
on synthetic pixels and synthetic in-memory archives only.

Camera/background checks cover interrupted pans, easing, both camera banks,
timed pauses, skip completion, checkpoint continuation, pixel/scale command
conventions, preserved transforms and BC character retention. The shared SDL
scene adapter also compiles on both toolchains. For a finite composition check,
pass `--sdl-archive /path/to/an/existing/SDL2.a` pointing to a Windows x64 SDL2
build with the software renderer enabled. The optional test links the real
scene/canvas adapter but initializes no video, audio or event subsystem. It
draws synthetic solid-colour worlds into a privately allocated memory surface
and checks pixels and retired-image lifetime; no window, screen, PAK artwork,
game scene or timing/pressure loop is involved. The SDL archive, test executable
and actual native link graph are bound into the verification report. This
does not verify GLES output or device performance.
The verifier also compiles/links both native mask-shader profiles offline using
the local glslang binary. `--shader-compiler` can select another installed
glslang executable. This creates shader text in the build directory and no GL
context. The common canvas/mask adapter sources compile on x64 and ARM64;
actual desktop/GLES rendering remains unverified under the development limits.

## Public source boundary

This repository publishes authored runtime, adapters, native components,
generators and checks. Original game files, cached CFG/instruction translations,
generated game bodies, initial RAM and historical binary check inputs are not
included and are not MIT-licensed. **A fresh public checkout is not a standalone
buildable copy of the full game.** Do not download someone else's game image or
claim that the source license grants game redistribution rights.

The shared host workflow requires authorized pre-existing local build inputs at
the paths used by `scripts/generate_shared_aot.py`,
`scripts/prepare_static_data.py` and the verification scripts. It does not open,
hash or analyze the original EXE. Missing cached inputs are an explicit
prerequisite failure; do not work around this by running the old EXE-based build
entry points. A reproducible public bootstrap for these private inputs remains
unfinished.

The omitted extension callback `0x456650` additionally uses the existing
`build/source-analysis/functions-typed/00456650.c` and `.json` records plus
the cached `generated/static-data/text.inc` and `data.inc`. The bounded step in
`scripts/extend_cached_callbacks.py` checks the full signature and reuses
existing C++ templates; it neither decodes an EXE nor rewrites the input CFG.
`tests/script_wait_callback.cpp` exercises the resulting static callback,
controlled clocks, cancellation, ABI preservation and rejection guards in RAM.

The broader callback audit uses `build/cached-listing/listing.json`, exported
from the **existing** analysis database with `ExportCachedListing.java` under
`-noanalysis -readOnly` (never `-import`). It enumerates saved listing records;
it does not disassemble, decompile, modify the database or access the EXE.
`recover_cached_callbacks.py` verifies initialized pointer tables, cached code
references, complete byte signatures and the direct-call closure. Most added
instructions relocate checked C++ templates. The bounded cached-operand parser
passes the remaining forms to the original offline C++ emitter, and must
reproduce 96,614 existing instruction bodies exactly before admitting them.
The original emitter source, Zydis enum libraries and listing are private build
prerequisites; neither the emitter nor a decoder is linked into the game.

The supplement under `generated/cached-callbacks/` is hash-bound to its inputs;
stale proofs, unresolved referenced entries and compiled direct edges without
registered destinations stop generation. `tests/cached_callbacks.cpp` exercises
vtable calls, inline string/destructor behavior, floating classification and
format conversion, exception-filter declines, catch resumption and code guards
using bounded synthetic memory. These checks do not establish complete C++
exception handling or whole-game/device compatibility.

## Toolchains and dependencies

The development workspace uses Windows/PowerShell, Python 3.12, CMake/Ninja,
Zig's x64 Windows C++ compiler, JDK 21, Android NDK 27.0.12077973, Android API 35
and build tools 34.0.0. The Android host targets ARM64, API 26 and GLES 3.0, with
16 KiB ELF alignment. Existing scripts currently use workspace-local toolchain
paths; install the matching tools there or deliberately parameterize those
paths in a follow-up contribution. Do not pretend an untested host/toolchain is
supported.

SDL 2.32.10, MojoShader and stb_vorbis sources retain their notices. Prepare and
build the permitted FFmpeg codec subset with:

```powershell
python scripts/prepare_media_sources.py
python scripts/build_media_codec.py --target windows
python scripts/build_media_codec.py --target arm64
```

Run builds sequentially. Build helpers set below-normal process priority,
hide child windows and use one job. The current shared workflow is:

```powershell
python scripts/verify_shared_aot.py
python scripts/package_shared_hosts.py
```

The first command runs bounded component/state comparisons, compiles the
Android Java adapter and links the same common sources for both hosts. It does
not run the game or an emulator. The second requires a fresh successful proof,
builds the Windows host, signs the ARM64 APK with the local development identity,
verifies JNI/signature/alignment and checks matching LGPL relink materials.
Signing identities belong in ignored local build storage; never commit one.

Outputs in a complete local workspace are `out/WA2_SDL2_x64.exe`,
`out/android/WA2-arm64-dev.apk`, `out/WA2-shared-relink.zip` and
`out/build-info.json`. Generated reports bind the exact source/library hashes.
They are local artifacts and are not public source fixtures.

## Resource loading checks

The main-story caption YAML and generated 891-character bitmap coverage are
checked-in, separately licensed third-party inputs. Normal builds validate them
with `scripts/prepare_subtitles.py --check` without downloading a font or adding
Python packages. Regeneration instructions and exact source hashes are in
`third_party/wa2_subtitles/README.md`. No SpecialContents subtitle data is used.
`wa2_subtitle_tests` checks all 315 cues, source audio IDs/stops, both source-memory
layouts, caption geometry and font decoding without opening a game or device.
`scripts/verify_subtitles.py` independently compares every font RGBA byte and
checks the ten generated audio return observers. The portable shader check
assembles/translates/links the caption shader for desktop GL and GLES3 offline.
These component checks do not verify real screen appearance or audio timing.

`tests/byte_equality.cpp` checks fixed/dynamic byte ranges and words with exact
tails, unaligned starts and differing bytes just outside the requested region.
`tests/glyph_index.cpp` compares all 65,536 character codes across 26 generated
tables, including overlaps, unsorted ranges, terminators and invalid later rows,
and tests 256 mutations. `tests/glyph_geometry.cpp` changes every sprite/image/
palette/half-pixel/handle key component, checks payload ownership, replacement,
bounds and 512 single-use glyphs that must not capture complete entries.

The complete native glyph check retains 12 static-source variants, adds 16
changing-line cases and compares the enabled/disabled geometry memo within the
same binary. NaN, infinity and signed subnormal rejection is also checked in the
native sprite path. `scripts/compare_glyph_cpu.py --baseline-dir build/<saved-baseline> --baseline-release <version>`
links the same memory-test objects with saved/current shared AOT archives and
alternates five process pairs. It checks native text, changing lines, precise
static geometry and sprite paths; the old library stays in the test binaries.
Saved reports bind both archives/binaries and all changed comparison inputs.
These bounded memory checks do not establish whole-game or phone performance.

`tests/single_tile_geometry.cpp` checks all 16,777,217 signed coordinates on
the supported 1/256 grid against hardware binary32 results, plus 98,328 UV
ratios, 16 invalid inputs and all four host rounding modes. No input is
quantized: ineligible values take the existing arithmetic. The sprite test
adds 88 complete cached-source comparisons: 64 eligible power-of-two-cell
cases and 24 crossing/non-power-of-two/large-coordinate fallbacks. The dialogue
test also checks host fenv/errno preservation in the actual glyph path.
The new same-object comparison still links the saved verified 0.2.41 AOT
library for the baseline, including changing-line workloads; this measures
CPU packet generation rather than storage, GPU work or whole fast-forward.

`tests/lzss_groups.cpp` checks generated LZSS streams with all 256 literal masks,
repeated-word lengths/distances, near matches, dictionary dependencies, guard
bytes, truncated data and trailing data. The decoder and the length validator
must preserve their successful results and error text. The comparison command
is `scripts/compare_lzss_groups.py --baseline-dir build/<saved-baseline> --baseline-release <version>`.
It uses source/header bytes saved with their previous successful verification,
builds both decoders with O3 and renames only the baseline namespace. The old
decoder is confined to the test binary and is never shipped in either host.
The bounded component timings do not measure whole-game fast-forward or Android
FPS.

From `0.2.41`, the same decoder and validator can process eight consecutive
all-literal groups together: 72 stored bytes produce 64 output bytes. Every flag
and bound is checked before the block copy; mixed/short groups use the existing
token path. The copy stays outside that hot token loop, and overlapping buffers
retain the earlier sequence. Tests cover 2,993 exact streams and 420 failures,
including 2,048 flag-transition cases and 160 new literal-block error cases;
12 overlapping-buffer cases also compare complete storage against the baseline.
The shared native resource adapter also compiles with O3 on both hosts, while
unrelated static functions keep their size settings. The local 0.2.40 comparison
links the same resource-loading test object with the saved verified engine and
runtime archives, alternates process order and checks allocation callbacks,
snapshot aliases, failed publication and mapping changes. Its 2 MiB generated
loads include fixture input staging, validation, allocation, decoding and
publication. These timings do not measure real PAK I/O or whole fast-forward.

`tests/android/org/wa2/port/WidgetControlsTest.java` executes the production
`WidgetControls` and `WidgetSession` helpers on the ordinary JVM. It covers
complete-snapshot reconciliation, missing/removed/replaced tokens, signed 32-bit
handles, ordering, unchanged-view reuse, failed-factory atomicity, duplicate/count
rejection, 64 reopen cycles and nine Activity-generation cases. When the cached
Unicode fixture is present, the Java verifier also checks all 53 controls from
seven dialog definitions and binds that fixture's hash. The shared verifier
requires this coverage. These are data/ownership tests: they create no Android
Views and do not execute the NativeWidgets UI callback integration on a device.

`tests/widget_unicode.cpp` exercises the actual native widget serializer without
JNI or a window. `scripts/verify_widget_unicode.py` independently parses its JSON
and checks 416 text round-trips: all UTF-16 code units, Chinese/Japanese labels,
control characters, escaped quotes/paths, supplementary characters and isolated
surrogate units, plus cached menus/dialogs. It reproduces three previous UTF-8
byte-quoting failures and preserves 67 menu items' command/state fields and 53
controls. It does not run Android's JSON parser or prove on-device glyph/layout
rendering. The same helper is used by the Android file chooser.

Version-dialog checks also preserve both cached `SS_ICON` payloads through the
actual serializer. `tests/window_resources.cpp` checks icon-size selection,
missing resources, unsupported headers and every truncated icon prefix;
`tests/window_model.cpp` performs 64 modal open/command/close cycles without a
window, including queued messages for retired dialogs. `WidgetIconTest` runs the
same static-style classifier and BGRA32 decoder used by Android, including
alpha/mask semantics and bottom-up rows. An independent Python conversion
supplies expected cached pixels. These checks cover the missing style-3 path
that the older ownership-only tests did not run; they do not execute ImageView,
the Android JSON parser or device presentation.

`TouchGesturesTest` executes the production touch policy with synthetic pointer
IDs and clock values: 289 gesture traces, 76 two-finger taps, 69 cancel/release
cases and 288 physical-left/physical-right/touch Ctrl source traces. It checks
disabled holds, timing/slop boundaries, swapped lift order, third/replacement
fingers, delayed timers, coalesced moved releases and focus/pause cancellation.
The Activity adapter compiles against Android API 35 and forwards committed
events through Android/SDL's existing dispatch and Ctrl JNI methods. It schedules
one pending long-press deadline and sends key-state edges. Menu visibility is
kept independently of native menu refreshes. The checks create no Views and do
not execute Android's MotionEvent dispatch, SharedPreferences disk persistence
or real-device gestures.

`tests/resource_loading.cpp` checks general-resource and script decompression
with synthetic KCAP/LAC archives and both memory backends. It covers 64 exact
outputs, 22 failure cases, eight input/output/allocator aliases and two allocator
mapping changes. It checks the trailing zero, output publication, freeing an
unpublished allocation and legacy file-position effects. Its bounded timings
use three generated 2 MiB token distributions; they exclude real PAK I/O and
whole-game fast-forward. Local comparisons link the same test object to the
saved 0.2.31 function-runtime archive and the current archive.

`tests/wait_probes.cpp` checks 13 wait contracts including caller-order choice,
atomic wait-all consumption, recursive mutex ownership, duplicate/invalid
handles, 64-object waits, memory faults and a real nonzero sleep deadline.
Bounded API timings count host heap allocations: one-/three-object probes need
none, and 64-object probes need one allocation per call. The existing scheduler
test separately retains worker suspension/resume, TLS, abandoned mutex and
cross-host-thread signal checks. No game timer or scheduler slice is shortened.

`tests/image_upload.cpp` checks 320 direct 24/32-bit image rectangles, exact alpha
and crop/padding bytes, late faults without partial destination mutation,
unreadable row gaps, unsupported bounds and BGR24 overlap. Its five paired CPU
timings use generated 1920x1080 images and compare against
`tests/image_upload_before.cpp`, an O2 reference for the earlier staging and
per-pixel sequence. They do not run resource I/O or a graphics device. Actual
GPU-modified images, scaling, colour keys and other formats retain the existing
adapter path. Neither the memory check nor an APK icon-resource audit establishes
the game's real loading speed or a particular phone launcher's appearance.

`tests/native_memory.cpp` checks resource-sized REP copies/fills and the cached
compiled clear loop in owned RAM. It covers forward/backward indices, complete
CPU/FP state, padding, permission faults with partial writes, unsupported shapes
and a single preheader fallback attempt. Its optional `--bench` mode checks every
output byte and records four bounded 4 MiB CPU operations; it creates no window,
device or game process. Local before/after comparisons link the same test object
to the saved verified 0.2.29 archive and the current archive, alternate process
order and compare all recorded CPU states. These are resource-buffer component
times, not measurements of storage, full fast-forward or Android FPS.

`tests/movie_tracks.cpp` reads the external `res/mv100.pak` to owned decoder
memory without creating a device, window or game. It checks 64 PCM/YUV/timestamp
prefixes against ordinary demux order, drains the fixed audio cursor to EOF,
then checks that video starts from its own position. It also covers shared
source cursor interleaving, cancellation, excluded tracks and the actual
Player's bounded queues. It needs the user's external movie; no sample is
embedded or distributed. The earlier 8 MiB compressed backlog failure can be
reproduced by asking the default single cursor for audio until EOF; production
Player now uses fixed-track cursors instead of increasing that cache limit.

`tests/image_colours.cpp` replays all six 24/32-bit colour-table loops against
the cached metered static functions, with 324 exact state/pixel cases, 12
ordinary compiled resume checks and permission, alias, wrap and partial-page
rejections. Paired timings use generated 1920x1080 images in owned memory;
they measure image colour processing, not file I/O, GPU work, game fast-forward
or Android FPS. Both hosts compile `recompiled/image_colours.cpp` with `-O3`.

`tests/native_resource_cache.cpp` creates its own KCAP fixtures and compares raw
and compressed output, full dictionaries, trace behavior, dependency changes,
archive invalidation, rejection behavior and bounded cache eviction. It pairs
allocation reuse enabled/disabled against the same complete native image-load
chain; the linear case checks that four repeated loads need zero new native
memory commits after warmup. `tests/large_heap_memory.cpp` checks zero fill,
growth, permissions, double-free rejection, cross-heap memory limits and full
cleanup on heap destruction.

These checks establish the bounded inputs tested. They do not establish actual
Android frame rate, full gameplay, cold flash-storage performance or a tenfold
whole-game improvement.


## Test-profile update and signing identity (0.2.46)

`tests/test_profile.cpp` exercises full content unlock through the shared save
adapter and existing static save/gallery/bit readers. Both linear and paged
memory are checked, including old chapter-only profile upgrades, 2952 gallery
queries, all 122 flag values, unknown-field preservation and normal-save isolation.
This is a memory component check, not a game launch or an Android device test.

The 0.2.46 local build recovered unchanged static libraries and link inputs from
the hashed 0.2.45 LGPL relink archive after the build directory was removed.
It recompiles the same changed adapter and versioned startup on both hosts,
relinks both applications, and recompiles Android Java/resources. Its focused
report is `logs/test-content-0.2.46.json`; the historical whole-suite proof is
not relabeled as a fresh full verification. The missing historical baselines
must be restored to run the complete `verify_shared_aot.py` pipeline again.

Keep the development signing key at the ignored private location
`private/android-signing/wa2-native-dev.p12`, outside the disposable build tree,
and keep a separate secure backup. Packaging accepts the old
`build/android-signing/wa2-native-dev.p12` location as a fallback and fails if
neither key exists; it never silently creates an identity. The old local key
was lost without backup, so 0.2.46's replacement signature cannot update an
old-signature installation. A parallel test APK changes only the manifest package
to `org.wa2.port.test` and label to `WA2 全内容测试`; activity names are fully
qualified as `org.wa2.port.*`. It is verified to contain identical DEX, native
libraries and licensed assets, and keeps its private data separate. Neither keys
nor passwords belong in public source.

The native-engine migration has a separate desktop CMake root at `hosts/native`.
It builds full SDL window/audio adapters and the common native game sources;
the retained `hosts` root continues to select the AOT reference. The native
desktop wrapper accepts `--resources <UTF-8 directory>` and `--test-mode`.
Resource discovery uses external PAKs beside the program or in a `res` child
before checking the working directory. Settings/native slots use SDL's separate
private data path; no PAK is extracted or embedded.

The native desktop input adapter exposes F1 settings, F2/F3 save/load,
F5/F6 quick save/load, F7 backlog, F8 hide, F9 auto, F10 skip and F11 fullscreen.
F12 opens version information; Shift+F12 opens environment information.
Alt+1/2/3 choose the three windowed sizes, and Alt+H/L choose next-clip video
quality. These submit the same typed commands as the native Android menu;
none execute game policy in a platform-specific implementation.

`verify_native_core.py --sdl-archive <existing software-reference SDL archive>`
also links `wa2_native_windows_entry_check.exe`. This is an unlaunched GUI/x64
link artifact against a reference dependency without production device drivers.
Its entry exports and complete native source closure are checked, including the real
common frame loop. Do not distribute or run it as a complete game. The native
Android and Windows entries remain incomplete and no native game APK is released.

`verify_native_desktop_root.py` additionally configures the full desktop SDL
root without building or running it. It requires a fresh native-core proof,
then checks the production window, OpenGL and audio source closure alongside
the same common game sources. Its configuration-only report is
`logs/native-desktop-definition.json`; it does not verify a game session.

The separate `hosts/native-android` root targets Android ARM64/API 26 with full
SDL GLES/audio/HID adapters and the same common native game sources. It does not
include the retained `hosts` AOT root. Native Java must be selected with
`scripts/android_source_sets.py` (`java_sources('native')`); compiling every Java
file from both directories would select two conflicting presentation adapters.
The existing release packager still uses the retained AOT source set.

`verify_native_android_ui.py` compiles both adapter sets against the verified
Android API jar, creates only the native set's DEX, rejects legacy widget classes
and runs finite pure path/gesture tests. It cannot reverify the missing cached
original-widget transport fixture and does not load JNI or an Android app.
After fresh native-core/UI reports, `verify_native_android_definition.py` checks
the actual native-entry menu registration signatures and existing SDL JNI
exports, then configures/audits the full production Android root. Reports are
`logs/native-android-ui.json` and `logs/native-android-definition.json`. These
artifacts are verification components; no native game APK is released yet.

The real device-backend targets can also be built without launching them:

```powershell
python scripts/build_native_hosts.py --check-only
python scripts/build_native_hosts.py --host windows
python scripts/build_native_hosts.py --host android
```

This requires fresh native-core, desktop/Android definition and native-Java
reports. Each build checks source and imported-codec hashes before/after the
operation, uses one hidden below-normal job on one CPU, and audits the actual
production source/link closure. Its SDL window/OpenGL or GLES/audio/HID sources
are distinct from the software-reference dependency used in memory tests.
Reports are `logs/native-production-windows.json` and
`logs/native-production-android.json`. No resulting binary is started.

`scripts/package_native_preview.py` assembles separately named migration
artifacts under `out/native-development`, using these production libraries and
the verified native-only DEX. The development application ID is
`org.wa2.port.nativeengine`; it does not overwrite the retained AOT APK or read
its private saves. Its manifest uses fully qualified common Java entry
classes, ARM64/API 26 and GLES 2, with no legacy widget/file-dialog adapters.
Install-table alignment, signing, native load alignment and exact packaged
library/DEX bytes are checked without installing an app. Original PAKs remain
external and are selected in the existing launcher.

On its first launch without `native-settings.wa2cfg`, the native host can read
`SYSTEM.ini` and `D3D.ini` from the selected resource directory, or from its
`Documents/Leaf/WHITE ALBUM2` child if neither root file exists. Windows and
Android use the same decoder and persist the imported preferences as
`native-settings.wa2cfg` in that selected game directory. The old INI files remain untouched. Later
starts do not access those source settings. Unsupported custom pacing, wheel
mode or window dimensions produce explicit import errors; Direct3D-only device
keys do not configure SDL.

Original movie levels `mov_lv=0/1/2` map to native movie suffixes `2/1/0`.
Constructed ANSI/UTF-16 preference checks cover all three values and their
native format round trips. The owned-file bootstrap checks import from the
root/known Documents folder, send the result to both fake title and synthetic
story movie transports, reopen native settings with INI access denied, and
check that the source INI stays byte-identical. Out-of-range values still fail
before publishing native settings. These checks launch no game or device.
The separate native development package is `0.3.3-native-development`
(Android version code 4), retaining the same application ID and signing key.
The font-mask tests distinguish independent palette colour/alpha from
RGB-only grayscale coverage. The owned-memory composition test additionally
renders constructed glyphs through the actual story and confirmation paths,
checking transparent padding, white ink, black outline and antialiasing pixels.
The linked Android entry must contain its landscape-only SDL orientation hint.
The loading-cache check additionally compares sized foreground/outline
rectangles to cached integer geometry while retaining old layout records.
Confirmation checks bound the source's 656x264 request to the external
544x216 panel, including both buttons. Original version-1/version-2 scene
checks accept the completed-message page sentinel and resume its suspended
command once, without publishing progress or replaying text. No original
save/story or OS window/device is used by these constructed checks.

Native progress, configuration and manual/quick saves are stored beneath the
selected game root on both platforms: `Sys.sav`, `native-settings.wa2cfg` and
`native-saves/slot-NNN.wa2sav`. Android uses writable SAF documents, not an
internal-storage mirror. Previously read-only grants must be renewed. Native
test mode uses `native-test/` under the same selection, keeping normal PC
progress unchanged. Android launcher switches (FPS, long-press fast-forward,
test mode) are also stored in the selected root's `native-launcher.wa2cfg`.
Only the last directory URI and its Android permission grant stay in app
preferences. A verified existing selection without this file may import the
old private switch values once; a different new selection starts with defaults.
Writes run on one background queue and game start drains it before capturing
that folder's launch profile. No UI-thread provider I/O is needed. Restored
activity tasks carry the verified session profile, or return to the launcher.

PC `Sys.sav` may be copied directly to the selected root. If absent, the common
adapter imports one `Documents/Leaf/WHITE ALBUM2/Sys.sav` (combined resources)
or `Documents/Leaf/WHITE ALBUM2 -closing chapter-/Sys.sav` (closing-only resources)
within that root, validating the exact original length before publishing a
root copy. The original introductory Documents Sys.sav is then OR-merged into
that primary progress, excluding source flag 99 and preserving destination
flag 99. Every other history/gallery/flag byte follows the source merge.
Original source files stay read-only; an unchanged later launch writes nothing.
Invalid root files
and provider failures remain explicit errors; they do not trigger fresh saves.
Original manual saves retain their read-only inventory and version-1/version-2 state
converter. Native scene saves remain a different format, with no reverse export.

Desktop replacements use a closed, flushed temporary and atomic rename.
Android document replacements keep `.wa2-new`/`.wa2-old` sidecars until a
complete new file is published, with recovery of interrupted transactions on
the next open. Providers must support write, create and rename. Progress
updates retain narrow original-format block writes, not a complete rewrite
on every flag. Finite synthetic storage/provider fault checks cover these
contracts; neither an Android provider nor real gameplay has been exercised.

The assembler also supplies `WA2-native-development-relink.zip`, containing
the actual native application objects/archive order and corresponding FFmpeg
source, build configuration and license. Both standalone linker recipes are
checked without running their outputs. If disposable codec configurations were
lost, only named configuration files may be recovered from the retained relink
archive after checking its manifest and the unchanged codec-library proof;
no retained AOT application object enters the native materials. The existing
private development key is required, and no signing identity is created.

These separately labelled development artifacts are unlaunched migration
builds. Packaging does not establish whole-story, display/device compatibility
or performance, and does not change the current released backend/completion
flags in `config/porting_contract.json`.
