# Active shared AOT and experimental native migration

On 2026-11-08 the user restored shared AOT as the primary delivery. The native
migration sections below describe retained experimental work; native targets
still exclude AOT/guest state, while current AOT hosts compile the same static
game sources. Neither implementation is declared a fully verified game port.

The AOT process now mounts a separate writable selected-directory namespace,
beside its read-only PAK mount. Shared `platform/compat/save_files.*` owns path
aliases and test-profile selection; local C++ and Android provider adapters own
OS operations. Source save bytes are passed through unchanged. Main Documents
paths map to root saves/INI, nested main files are read fallback, and separate
chapter directories retain their identities. Normal and test-mode mounts never
fall through to private app files. Source file handles retain independent seek,
read/write/truncate/flush and access-sharing semantics, including main-path
aliases. Picker-authorized paths outside this namespace retain their existing
host access. Cross-provider atomic moves remain explicitly unsupported.

The profile adapter and movie-quality selection use the same mounted files.
Android opens seekable local-provider descriptors for regular source writes;
INI publication uses the existing staged/backup recovery protocol. Directory
metadata does not scan/decompress PAK payloads. The linked Android entry audits
the exact Java/JNI method descriptors and landscape-only SDL hint. External
original-format files, not native scene-save conversion, drive this AOT path.

On 2026-11-06 the user changed the target to a complete native engine
reimplementation. Windows and Android continue to share game logic, resources
stay external, and the existing executable-analysis/UI restrictions remain.
The final native application must not link generated game functions, source
CPU state, checked source-address memory, or the Windows compatibility runtime.
The current released APK still uses AOT; a linkable component is not a full port.

The current native application targets are `hosts/native` (Windows x64) and
`hosts/native-android` (Android ARM64). Both link the same 91 common game/engine
sources through `platform/native_host.cpp`; OS paths, input, resources, device
audio and SDL presentation stay outside game policy. Actual production builds
include window/render/audio backends, and Android includes its native-only Java
adapters and complete SDL HID/JNI exports. They exclude the AOT and guest-state
runtime rather than falling back to it for an unsupported command. Separate
unlaunched development packages and matching LGPL link materials can be made
with the source-bound build/package tools documented in `BUILDING.md`.

Remaining source-specific UI/renderer options and complete story/device
compatibility are not established. The retained AOT
release and contract completion flags stay unchanged. The component notes
below record migration increments; later integration sections supersede the
earlier component-only integration boundaries.

First native startup can import the original integer preferences from
`SYSTEM.ini` / `D3D.ini`. Both hosts supply read-only Sources for the same
common parser and directory selection; no Windows profile API is involved.
It first checks the selected resource directory, then its known
`Documents/Leaf/WHITE ALBUM2` child, and never mixes the two directories.
Existing native preferences take precedence, including a corrupt native file
which must fail rather than silently reset. Valid imported settings are
persisted atomically before publication and the source is not read on later
starts. The parser maps original volume/mute/character switches, skip/voice
policy, opacity, skin, text/transition timing, confirmation, mouse policy,
movie quality, and window mode/three known window sizes.
The original `mov_lv` values are levels, not native filename suffixes: levels
`2`, `1`, `0` map to suffixes `0`, `1`, `2` respectively. All three levels are
accepted and persist in the existing native preference format. An absent key
keeps the native high-quality default. Imports leave the INI untouched.
Source-only Direct3D device/debug keys are not portable preferences. Nonstandard timing, wheel
mode and window dimensions without native
support fail explicitly. Import is
bounded to 64 KiB per file and supports ANSI/UTF-8 and BOM UTF-16 integer keys.
Synthetic memory/file, bootstrap and Java path/JNI-declaration checks exercise
this integration; original user settings and devices are not opened by tests.

Android's SDL display adapter keeps the native window in immersive fullscreen
independently of the retained PC window-mode preference. It no longer clears
SDL's fullscreen/system-bar state when first applying portable preferences.
Desktop display policy and aspect-ratio fitting of the shared logical canvas
are retained. The Android entry supplies SDL's `LandscapeLeft LandscapeRight`
orientation hint before creating the window: SDL's resizable-window default
otherwise overrides the manifest's sensor-landscape request with full-user
orientation. The linked-entry audit requires this hint in the Android library.
Actual Android system-bar and orientation behavior are not device-verified.

Text presentation now applies the cached integer glyph-edge truncation and
line baseline for sized glyphs. Existing layout records keep their format;
their historical centre-adjusted Y is compensated only while building glyph
sprites. Foreground and outline colours/coverage are retained. Constructed
font sheets check normal, F20 and F14 rectangles, paired passes and fractional
outer positions against cached generated-code results for owned ASCII strings.
The confirmation panel uses the source's 656x264 rectangle, bounded to the
full 544x216 external atlas rather than cutting it to 128 rows. A completed
source message can store a page counter equal to its number of segments;
conversion retains all visible text, normalizes to the final native segment
and resumes the suspended command once. Greater page values still fail with
the actual counter and segment count. Constructed old saves and native
round trips check this boundary; no original story or device is executed.

`native/CMakeLists.txt` is an independent build root for the common native scene,
script engine, resource/media components and native progress adapter. It never
includes the retained `hosts/CMakeLists.txt`, `recompiled/`, `generated/` or
`port/` runtime. Its link-check library
forces all existing native scene dependencies to resolve on Windows x64 and
Android ARM64. Check libraries are never loaded by tests. An optional Android
SDL/JNI entry link uses an existing SDL2 dependency; it does not create an APK
or replace the released application.
The verification checks actual compile commands, Ninja link dependency closure
and native symbols; the game does not fall back to AOT when a command is missing.

The first new replacement is system progress. `engine/progress.*` owns ordinary
save-format data, with explicit little-endian flag serialization and MSB-first
reading bits. `engine/scene_progress.*` handles LSCR commands 198/199/201/202
(GetGameFlag, SetGameFlag, V_Flag, H_Flag). Flag 99 writes become zero as in the
cached original setter; gallery writes use group*10+difference and truncate to a
byte. Invalid indices/lengths fail before mutation. Existing unknown fields are
round-tripped, and the full-content test profile uses the shared unlock list.

An injected Storage adapter separates persistence from game state. The ordinary
file adapter opens Sys.sav in the selected game directory. It preserves
manual saves/settings and writes changed spans inside the original-format
blocks, including the 4 KiB system-flags region. Failed spans remain dirty for retry. Whole-file/multi-block
atomicity and a complete scene-save snapshot are not claimed. A scene without
progress storage fails explicitly if it encounters a progress command.

The common scene also records CGs when V/H backgrounds (including their scaled
variants) or ordinary V/H-prefixed bitmap files are loaded. Normal backgrounds,
animations and background reuse do not trigger this rule. Bitmap suffixes follow
the cached source's decimal parser, including its optional minus and 32-bit
wrapping, followed by native gallery bounds checks. After parameter validation,
the seen flag is persisted before the asset request. A failed write retains the
old visual owner and leaves progress retryable; a failed asset request retains
the already-persisted flag. Continuations, repeated seen images and checkpoint
restoration do not cause redundant gallery writes. Thirty-four synthetic cases
and eighteen guards check this ordering, storage failures, native controller
integration and scene restore without original assets or a rendering device.

The LSCR runtime/session now has bounded versioned checkpoints for globals,
locals, value/flow stacks, nested scene calls and suspended commands. Restore
checks resource content identity, catalogue signatures and instruction
boundaries before committing any state. These script checkpoints do not yet
include rendered scene/audio state by themselves; the aggregate described
below binds them to the native controllers.

`engine/script_legacy.*` translates the original manual save's appended script
and global blocks into an ordinary native LSSN checkpoint. The converter checks
the serialized block sizes, label tables, running positions, pending command
sites, value/flow bounds and global schema before returning anything. Globals
are matched by name and storage type, with saved global stack references remapped
to the current catalogue. Integers, float payloads, local arrays, conditional
flows, switch cases and nested scene owners become native values. Obsolete
allocation/callback pointers are neither resolved nor retained. Conversion does
not advance a script, publish globals or replace the current session.

The original format omits suspended command string contents. Translation
therefore requires an explicit scene-state resolver for each such parameter;
it rejects unrecoverable heap text rather than guessing. The aggregate scene
factory described below supplies this resolver; the script-data converter is
not a scene loader on its own. Thirty-one constructed-data cases and
seventy-seven guards exercise native resumption, exact real bits, call ownership,
reference remapping, malformed files and conversion without execution or
mutation. No original save, original script or device is used in those checks.

`game/legacy_save.*` inspects the original 88-byte header and translates its
version-2 scene audio records into the existing LSMR/LSER/LSVR formats. Header
inspection checks the stored version, story classification, local SYSTEMTIME
fields and original-byte description, without interpreting the thumbnail or
requiring the scene payload. Audio translation restarts one retained music
owner and looping effect/voice channels; idle/fading-out music, ordinary speech,
one-shot effects and transport cursors are omitted as in the source loader.
Ambiguous simultaneous music owners and unsupported phases fail explicitly.
Conversion does not open transports or mutate a current scene. The common
audio restorers continue to apply current volume, mute and speaker preferences.
Twenty-nine constructed-data cases and thirty-nine guards check these records
with fake playback. This audio/header converter alone does not establish
complete original scene or device compatibility.

`Scene::restore_legacy` translates version-1 and version-2 manual scenes into ordinary native
owners. It restores script/global/call state, settled background/camera and
character views, static/INAS bitmap state, dialogue text/page/reveal, source
choice-history identity, calendar, continuous shake, prefilled weather and
retained loop audio. Construction resolves assets through the new owner's
catalogue without replaying LSCR, reading or gallery callbacks. Current
preferences apply before playback creation; the unsaved RNG continues from the
application. The existing application swap publishes the candidate only after
successful preparation. Source load quirks are isolated to this converter:
ordinary backgrounds reapply source centering, while bitmap rotation and the
stored destination rectangle are omitted by the original loader. Native
checkpoints retain their full geometry. Twenty-seven constructed-data cases
and forty-three guards cover native checkpoint round trips, page continuation,
nested return, choice history and failed replacement with fake transports.
Unrecoverable saved text/heap values, pending commands other than
message/selection waits and ambiguous simultaneous music remain explicit errors.

The version-1 path has its own fixed-prefix/record bounds and converts only
serialized fields to the version-2 schema before creating these same native
owners. Old integer background destination dimensions are divided by 1280/720;
rotation is zero. Character positions use the old integer placement, while the
speaker-name field keeps its original 32-byte bound. Choice restore supplies
the source's zero history group/script identity and disabled-display flag,
using the same fallback history entry as its old loader. Old weather ignores
extended flags/amplitude, and the old voice-chapter override is retained while
voice offset/adult mode reset. The source does not restore its old voice loops,
calendar, novel flag or clock; the native candidate starts those unsaved fields
fresh. These are file-format translations, with no recompiled functions or
original objects. Eighteen synthetic cases and nineteen guards cover background,
character/bitmap geometry, source defaults, thumbnail rows, full native application
load/save, read-only source retention and failed candidates. No original save,
original story, physical font or rendering/audio device is opened.

`game/legacy_slots.*` adds a read-only source layer below native slots. Native
files, including damaged ones, take precedence. Original `save_00.sav` through
`save_99.sav` and `save_Q.sav` can be read from the selected resource directory;
if absent as a bank, the known Documents/Leaf combined/closing directory is
selected. The separate introductory directory supplies the alternate bank.
Directories are never merged piecemeal. Every replacement writes a native file,
leaving original saves untouched. Browsing reads at most 65,624 bytes per source
slot: header plus its top-to-bottom 128-square BGRA thumbnail. The menu scales
the full thumbnail into its 16:9 slot, including quick previews. It does not
crop it to native thumbnail dimensions or reserialize an original preview as a
native document. Thirty-four own-file/memory cases and thirty-four guards cover
the actual application load/save route, precedence, limits and failed loads.
Both hosts supply metadata/open adapters for this same policy. Android uses
the authorized Wa2Res tree and one bounded directory scan, with no payload copy;
406 pure Java path cases and seventeen guards accompany compiled JNI-signature
checks. These checks do not exercise an Android document provider or establish
compatibility with user-created original saves or an entire original story.

Selection commands 208/209 now retain original insertion ordinals, conditional
hidden/disabled entries, eight-entry limits, and persistent choice-history
bits. Pending choices and completed results can be checkpointed. The common
scene accepts typed selection input; its shared presentation/input controller
uses the original row sprites, visible-row geometry and logical coordinates.
Static resource checks parsed all 187 local scripts and their 1157 labels and
299575 command sites without executing them or inspecting their rendered output.

The common scene now records dialogue reads using the cached 185-name persisted
row order, marks chapter-boundary sentinels, and uses next-line read status for
skip policy. Native rand/math builtins share the engine RNG; waiting and auto/demo
state can be checkpointed, including four-argument Wait2's read-gated interrupt.
GoTitle produces an explicit scene completion request. Music and completed/stopped
movies update their ordinary persistent unlock flags. These are core behaviors;
they do not implement the remaining title/options/replay presentation by themselves.

Native backgrounds now accept both pixel-sized B/BC/V/H and scaled B2/BC2/V2/H2
parameters, retained-transform sentinels and optional nonblocking transitions.
BC variants preserve character layers. A shared two-bank camera handles pan,
zoom, rotation, easing, timed pauses and WSZ completion, with bounded native
checkpoints. The SDL presentation adapter applies the resulting rotation and
invalidates masked-transition caches when the pose changes. Both toolchains
compile this adapter; these checks do not exercise a rendering device.
The background reuse selector -2 shares the prior immutable raster instead of
forming a resource filename or decoding it again. Mask shader mode arguments
are consumed only by masked transitions, as in the cached renderer.
Camera pan now changes source sampling independently of the fixed destination
rectangle. Ordinary backgrounds centre that source offset; CG commands use
their offsets directly. The shared native quad builder clips available source
pixels before reflection, clockwise-in-mathematical-coordinates rotation and
independent screen-axis scaling. Reflection acts about the pivot, rather than
only swapping UVs. Radians retain the source's binary32 conversion. SDL pixel
edges need no D3D9 half-pixel subtraction. Native textures omit unused source
allocation padding; edge-filter parity still needs presentation verification.
Constructed geometry checks and both compilation targets cover these changes
without opening a renderer or launching the game.
Retained background transforms also preserve the appropriate native animation
clocks. A retained pan samples integer source coordinates before continuing the
donor's curve; mixed X/Y parameters follow the original shared activity-flag
order. Retained scale/angle pivots and inactive-donor wait clocks stay with the
receiving bank. These cases are covered by continuation/checkpoint fixtures.

The native speech controller owns the original four bounded channels and handles
VXV gain fades separately from VS fades that stop and lock a channel. Fades
retain the source counter order, reserved duration presets and volume mixing.
EroMode state is checkpointed and supplied to the configurable speech policy.
Synthetic voices validate these transitions without opening an audio device.

NovelMode/NB/NBR use the same native dialogue/font system as ordinary dialogue:
40 columns by 15 rows at (75,26), a configurable full-screen black backing,
separate text visibility and the original fade-counter order. A bounded novel
checkpoint preserves suspended backing fades. ADV message-window animation is
kept separate. Device presentation and application save-menu integration remain
pending; aggregate snapshots now include the native dialogue/novel state.

SetWeather2/ChangeWeather2 now drive native type-6 particle growth and sway.
Snow density-selection flags 0x200/0x400 retain the cached RNG call sequence,
including overwritten values. Live particle count is tracked separately from
fixed slot indices, so density reductions cannot strand later active slots.
Native type-1 sakura uses three original flipbook atlases, distinct prefilled/
new-particle RNG order, original wrapping ranges and per-size animation timing.
Its curve/atlas phase is bounded by their joint period to avoid signed overflow.
Snow flag 0x800 selects the title's perspective motion: each spawn retains its
horizontal origin, smaller particles drift by depth, and side exits respawn
instead of wrapping. The cached creation/update paths also retain the extra
variant RNG draw, constant alpha 192 and depth-scaled size. The apparent
alternate atlas crop is overwritten in the source update before drawing, so
these particles use the same weather.tga snow cell. Synthetic cases cover the
title's 0xf03 flags, RNG consumption, geometry, side respawn and checkpoint
continuation. Types 2/5 and 0x800 on non-snow types remain explicit unsupported
cases; no title animation or device frame rate has been visually tested.

The native Calendar command computes date/weekday state, retains the 2010..2013
holiday facts and selects the original three calendar image atlases. Its shared
state machine handles the background, moving digit/weekday crops and line
transitions, including the source's one-third skip timing. Display requests
outside recovered display cases fail explicitly; date-only assignments remain
valid. The original `2029` scene's literal year `201` is a recovered compatibility
case: retain `201-01-29`, its computed weekday and no holiday. Cached lookup data
contains no valid month/day pair for that source access, so the native engine
does not recreate its off-table memory lookup or silently change the year to
2011. Several closing-story records also request `2013-02-29`; source Calendar
treats month/day as labels and computes a weekday without civil-date validation.
The native labels retain that value and still reject a day outside 1..31.
Active checkpoints preserve these states. Its overlay has a separate scene accessor for presentation after dialogue.
Calendar checkpoints reload resource handles without executing script commands.

`engine/sprite_animation.*` parses the original INAS sprite-animation containers
into native frame rectangles, texture names and timed sequences. The native
player supports nested repetition, finite/continuous passes, rational clock
rates, empty frames and bounded playback checkpoints. Unimplemented transforms,
blend overrides and events fail explicitly. Static checks parse the eight local
animation definitions and their texture headers; only synthetic sequences run.
LoadBmpAnime now owns native players/textures inside the common bitmap slots,
with asynchronous texture readiness, source-bound checks, placement, uniform
zoom, alpha/additive fades and independent-layer lifetime. SetBmpPos and
SetBmpZoom retain the source's animation semantics: the first destination point
positions the INAS pieces and moves their automatic pivot, while each piece
keeps its own dimensions and texture crop. The outer rectangle does not stretch
or clip the animation. Ordinary bitmap edits retain an explicitly assigned
pivot; coordinates reach the renderer as full integers rather than truncated
16-bit save-record fields. Synthetic checks cover these edits together with
checkpoint restoration, negative/large coordinates and subsequent zoom/rotation.
Rain now uses the same
player, original trimmed frame offsets, a scale of two about the screen centre
and a foreground layer independent of its scope flag. Animated bitmap moves
update the source's automatic pivot before subsequent scale/rotation commands. Complete
scene playback and device presentation are not verified.
Background completion clears weather only for a normal background in scope 0;
CG transitions and foreground scopes retain their weather, including scope 2.

SetShake/SetShake2 now use a native frame-counted state machine for sine,
square, alternating random, turn, rotation-sine and periodic zoom modes
(0/1/2/9/12/13/15). Direction signs, integer envelope truncation, source counter
order, finite/reserved/continuous durations and both command wait policies
are retained. Random direction selection shares the engine RNG and preserves
its two-frame phase across new effects. Other effect codes fail explicitly.
Background-only, dialogue-only and combined scopes have separate typed
transforms. Presentation composites the world into one bounded reusable target
when needed; the shared frame below assigns dialogue transforms per sprite,
while leaving novel backing and the fixed bottom strip in screen space.
The shake checkpoint contains its parameters, counter and scopes; the common
RNG state remains the enclosing scene's responsibility. Constructed state and
vertex checks do not verify whole-scene rendering or device performance.

Native background transitions now carry separate old/new world transforms.
Effect 12 enlarges the old world by `2-(1-t)^2` while the new world dissolves;
effect 17 retains its integer-alpha vertical push and both composite opacities.
Their synthetic state vectors agree with bounded memory-only cached-function
calls. Bitmap/character replacements retain bounded retiring snapshots until
the old bank is released, and sprite membership distinguishes the previous,
current and shared worlds. A BC-preserved character can participate in both
sides of the dissolve instead of being covered by the new background.
The renderer uses at most two reusable transition targets and an optional
shake target; steady backgrounds bypass these composites. Image-only targets
are reused, while participating live sprites invalidate their bank. Optional
software-reference checks use only constructed solid-colour layers and owned
memory pixels. They do not verify original whole-scene pixels or frame rates.

SetChromaMode/SetEffctMode now affect subsequent native background/CG/character
loads. Chroma uses the original integer luminance coefficients, separate
truncations and distinct 0/255/256 behavior; alpha is preserved. AMP colour maps
support the original 256/768/1280-byte formats, including composed channel
lookups. Shared image workers apply the style once before upload. Request and
decoded-image cache keys include the style so grey, normal and tinted variants
cannot alias or mutate each other. AMP maps resolve from the default grp bank,
with existing patch/root/IC precedence. Character style changes invalidate
same-filename reuse for subsequent character commands. Static resource checks
parse the main-story negative/sepia maps; only constructed pixels are converted
by tests. Native backdrop/audio settings and full application UI remain pending.

Character image loads also follow the cached automatic palette selection. A
present background-specific `Bxxxxxx.amp` takes precedence; otherwise time modes
0/1/2/3 choose no table, evening, night or indoor. A new B/BC/B2/BC2 changes the
automatic filename, while CGs, background reuse and a time assignment retain it.
Preserved character pixels remain unchanged until a different image is loaded,
matching the source's same-image reuse. Pixel conversion applies chroma, the
automatic table and then the explicit effect, with alpha retained. The catalogue
composes the two tables once during image selection and interns the effective
style. A warm set of at most 32 input tables (24 KiB of lookup bytes) avoids
repeated AMP reads when only composite styles remain active. No additional
per-sprite or per-frame pixel conversion is introduced. Existing patch/root/IC
selection and provider/corruption errors remain explicit.

An optional marked LSCT extension preserves the automatic filename. Older
native control records without it still load; unknown markers, malformed names
and trailing data fail before state publication. Original version-2 scene
conversion reads the corresponding saved filename as ordinary format data.
Twenty-four constructed-pixel/script cases and twelve guards check all time
defaults, both palette stages with noncommuting tables, CG/reuse/BC retention,
native/original save state, source precedence and warm composite reuse without
AMP reads. These are memory checks; original pixels, physical colour output and
device performance are not inspected or claimed.

The native scene's embedded main-story captions now receive typed speech and
effect lifecycle events. Pending captions wait for their asynchronous voice to
be ready; prepared effects do not start captions before playback. Stop events
clear borrowed transport references before voices are destroyed. Caption
arrival order, overlap policy and pause-adjusted timing are common engine code.
One lazily decoded font atlas and per-cue sprite geometry are reused; the scene
exposes a final caption overlay independent of camera/dialogue shake. The
existing main-story-only data scope is unchanged. Synthetic transports cover
all 46 embedded tracks, preparation/stop/readiness and subtitle layout reuse.
Audio-device synchronization, interview-menu integration and application
presentation remain separate incomplete migration boundaries.

The native selection view uses the cached 784x72 three-state background crops,
history marker, original visible-row spacing/position and narrower text-area
hit boxes. Its shared input controller retains insertion ordinals while skipping
hidden/disabled rows, supports pointer focus and keyboard navigation, and gates
activation during the original 30-tick fade. Labels use prepared native font
atlases with foreground tint separate from their shadows. Input/layout tests
use synthetic atlases and stub text preparation; real selection atlas checks
read headers only. Visible unavailable/replay-unchosen options can receive focus;
activation retains the selection and emits the original rejection cue. Hover,
confirmation and rejection use system effects 9211/9212/9214. Their typed events
and blocked selections are checked without opening an audio device. Actual
rendering and sound synchronization remain unverified.

Native system sounds preload the five original focus/accept/cancel/reject/message
clips into independent, bounded PCM buffers. Selection feedback restarts its
own cue buffer rather than reading a PAK, starting a decoder thread, or taking
one of the eight story one-shot channels. Root Ogg/WAV and IC Ogg/WAV retain
the existing lookup precedence. Master/system sliders and mute apply to this
bank independently; failed preparation publishes no partial bank. Synthetic
PCM checks cover restart, overlap, silence, volume and failed-load recovery
without an audio device. Original cue playback is not device-verified.

Native background effects 11/12/13/14 now reproduce the cached zoom-in,
zoom-out, combined zoom and quadratic overshoot curves. Effects 16/17/18/19
push both complete worlds vertically/horizontally with the original integer
distance and alpha counters. Constructed cached-state samples cover the curves
and four directions; zero-duration/skip completion releases the old world.
Other unknown effect codes retain explicit unsupported errors.

Native scene suspension now freezes one shared monotonic gameplay timeline,
including millisecond script timers and frame-counted scene updates. The mixer
services callbacks with silence while leaving buffered and streamed PCM cursors,
notifications and underrun counters untouched. Movies/captions also receive
their existing pause notifications; resuming excludes time spent suspended.
Constructed clocks, native timer commands and PCM buffers check this policy.
Platform lifecycle delivery and actual device audio remain unverified.

Bitmap draw modes 4 and 6, present in the statically inspected story scripts,
now retain their source RGB equations: source times source alpha plus destination
times inverse source colour, and destination times source colour. The GL/GLES
adapter uses a custom blend state for mode 4 and MOD for mode 6, preserving the
opaque world's coverage. Both ordinary and INAS bitmaps retain these modes at
zero alpha; alpha/additive zero-alpha sprites remain culled. Synthetic state
checks cover all four supported bitmap modes and the owned-memory renderer
checks multiplication. SDL's software renderer cannot exercise the custom mode
4 blend; actual GL/GLES pixel output is not yet device-verified.

Streamed native audio now constructs its WAV/Ogg reader and bounded PCM queue
on the producer thread as well. No audio source size/header read occurs in the
voice factory on the gameplay thread; settings applied before initialization
are retained when PCM becomes available. Ready/failure remain explicit, and
stop joins the worker. Synthetic provider tests check pending pause/gain,
startup cancellation, looping, initial silence and asynchronous provider/codec
errors. Archive lookup/member opening still occurs at the scene resource
boundary; this change does not claim to remove all synchronous resource I/O.

EndMessage is now bound to the native shake controller: a live nonzero-duration
shake stops and restores world/dialogue transforms, while a duration-zero
continuous shake remains active. StopShake always resets both scopes. The
EndMessage argument remains ignored as in the cached wrapper. A missing binding
is an explicit error instead of a silent callback omission. Synthetic checks
dispatch the actual dialogue command, including both reserved finite durations.

Native dialogue checkpoints now retain the current prepared text layouts,
typing budget/page, opening/closing/name-fade counters, window settings,
visibility and novel backing state. Layouts store bounded ordinary glyph
metadata and exact float bits; font/UI textures are reconstructed from external
resources. Restore validates every section before replacing live state and
does not replay history callbacks. Changing the skin during an active message
also reloads its assets on the following update rather than leaving it waiting
with no pending request. Synthetic checks cover metadata layouts and empty-text
dialogue continuation with fake UI atlases; restored glyph pixel output and
whole-scene save/load integration remain unverified/incomplete.

The scene compositor now prepares previous/current world targets before entering
the optional outer shake target. A background fade or mask combined with shake
therefore no longer requests a nested render target from the canvas. The same
bounded targets and caches are reused. Owned-memory pixel checks cover fade,
mask and zoom while an outer transform is active; no hardware context is used.
Fully opaque world passes also use a copy instead of an additional alpha blend.

Native image references identify the exact external root/IC/patch PAK bank,
member filename and load-time chroma/AMP settings. Both pending requests and
live decoded images can be described; checkpoint data contains only these
identifiers and the small colour tables, never image pixels. Reload validates
bank/member names and recreates an asynchronous request through the catalogue.
Ownership tokens reject requests from other stores. Synthetic checks cover
fresh-catalogue reload, IC routing, styled pixel preservation and invalid paths.
These references are a prerequisite for full scene snapshots, not an integrated
manual-save implementation.

Native sakura particles now optionally share one load-time atlas for all three
original textures. Replicated one-pixel gutters retain clamped linear sampling;
particle order, simulation, source pixels, colours and layering stay intact.
Packing is bounded to 2048 per side and retains the original multi-texture path
if it cannot fit. Duplicate texture handles alias without copying. The atlas
is transient and is never added to an APK/resource package. Owned-memory checks
preserve sampled colours and reduce six alternating-texture draws to one;
this finite submission check is not a game FPS or device performance test.

Dialogue now defers glyph raster construction until its text is actually drawn.
Logic updates and restored dialogue retain complete layout/page/typewriter
metadata, so undisplayed fast-forward messages allocate no glyph atlas. The
visible message is materialized once and reuses its immutable pixels on later
frames. Resource failures preserve the logical text state. Synthetic checks
now resume nonempty dialogue without reading a font atlas and check that a
failed first materialization does not change its checkpoint. Actual restored
font pixels still require original-resource/device presentation validation.
Constructed constant-coverage font atlases additionally exercise visible
body/name layer tagging and mid-name-transition checkpoint continuation.

Visible glyphs copy stored palette alpha for 32-bit indexed font sources, or
blue coverage for RGB-only grayscale sources, into their final white-RGB/alpha
tiles. Local external-font palette metadata establishes that colour and alpha
are independent; transparent white padding and opaque black ink must not use
blue as coverage. The earlier blue-only conversion and its tests incorrectly
ignored this distinction. The former temporary
RGBA region allocation and second pixel copy for every glyph/shadow are gone.
Source origin, palette bounds and destination bounds remain checked. Synthetic
24/32-bit palettes in both TGA row orientations check their respective channels.
Constructed font atlases also pass through FontBook, story dialogue,
confirmation labels and SDL's software renderer into an owned RGBA buffer.
Transparent padding, opaque ink, black outline and partial-alpha antialiasing
have independent expected pixel values; normal/sized text, a two-byte glyph,
ruby banks and warm texture reuse are covered. No original glyph pixels,
window, screen, video/audio/event subsystem or game execution enters this check.

The native scene exposes a bounded skip-frame entry. It advances multiple logic
ticks without presenting intermediate states, with a default 2 ms/256-tick
budget, and stops extra work at asynchronous image/audio waits, choices, movies,
calendar animation, script termination or an exhausted script slice. Held skip
and pointer position persist; click/confirm/navigation/window-toggle edges and
direct choices are consumed once. Normal updates remain one logic tick. A
constant/broken budget clock cannot create an unbounded loop. Fake-step/clock
tests cover this scheduling policy without instantiating a scene or measuring
FPS. The released AOT application does not use this native entry yet.

The shared native scene now records a bounded newest-first dialogue/choice
backlog, retaining the source's 255-entry/ten-voice limits without shifting
every stored text/voice table for each message. Typed voice metadata is emitted
before skip/mute playback suppression, so skipped speech remains identifiable.
LogOut suppresses voice attachment; ordinary text/name/novel-mode records remain.
Raw text is bounded by the native language's 4095-byte limit, names by 63 bytes.
Checkpoints retain ordered entries and the pending voice descriptor and validate
before replacing live state. Synthetic checks cover capacity, order, metadata
while skipping, and corruption recovery. Backlog presentation/audio replay now
has the common application binding described below. Source UI edge cases and
whole-game compatibility remain incomplete.

Background checkpoints now retain both world layers, pending/reused image
references, masks, camera tween state and every transition counter/parameter.
A restore worker supplies immutable catalogue-owned pixels; unknown/mismatched
resources and dimensions fail before controller replacement. Native colour
fade/flash counters are checkpointed separately. Synthetic continuation checks
cover all supported zoom/push transitions, flipped/inverted masks, camera pan,
reuse during loading and skip completion. Restore does not replay background
change callbacks or dump texture pixels. Full scene/save-menu integration and
actual presentation after restore remain pending.

Character checkpoints now retain all eight active slots and bounded retired
copies, previous/current poses and external image references, pending loads,
style generations, drawing order, removal flags and transition progress.
Immutable image records verify catalogue identity and original dimensions.
Restore does not call character-set/resource-selection callbacks; saved styles
remain independent of current settings while loading. Equal restored AMP tables
are interned with existing named tables, so a restored image can reuse its
existing decode/texture identity rather than creating pointer-key duplicates.
Synthetic checks cover supported transitions, pending styles, replacement,
retired worlds, removal and failed restore. Whole-scene and device validation
remain separate.

Bitmap checkpoints retain all 32 active slots and bounded retired copies,
source/destination rectangles, pivots, scale/angle, reflection, colour, blend,
fade counters and exact pending/loaded image references. INAS definitions are
reopened by saved member/group and matched through the playback fingerprint;
texture requests and sequence progress are restored without ticking the player.
Failed restore leaves existing logical slots intact. Synthetic checks cover
static/animated loads, fades and transforms, independent/retired layers and
changed definitions. These remain component checks, not whole-game saves.

Weather checkpoints preserve all 400 particle slots, population, flags, spawn
clock, target/current motion, perspective spawn origins, loading references and
rain INAS playback. The internal particle schema is versioned and rejects the
earlier unreleased native layout explicitly. The
last drawn sprite geometry is retained separately: several source effects draw
before incrementing phase/growth, so advancing again at load would change that
frame. Restored sakura atlases are regenerated from their three external source
images; RGBA texture data is not serialized. Restore consumes no random values;
the enclosing scene must restore its single shared RNG state as well. Invalid
resources/geometry/populations leave the live controller unchanged. Replacing
a scope-2 effect now clears stale sprites while its new textures load. Finite
synthetic continuations cover all implemented weather types and pending loads.

Native audio saves follow the cached original collector/load path
`0x423650`/`0x423ea0`: music and looping SE/voices restart from their resources;
ordinary dialogue speech and one-shot sounds do not resume. Music retains its
requested song, loop mode and target volume. Looping SE retain channel, sound
and target gain and use the original -2/30-tick resume fade, after preparing all
new channels silently. Looping speech retains canonical speaker/chapter/line
and the source fade's starting gain; restore does not apply the current chapter
offset a second time or issue backlog/history events. Current mixer settings
and adult-voice muting remain in force. Parsing and factory/preparation failures
leave the old logical controller state intact and stop partial new transports.
No decoder/PCM queue is serialized. Tests use fake voices only; this does not
prove device playback or complete-scene load integration.

The common scene now binds 17 subsystem checkpoints into one bounded versioned
save envelope. It retains script/global/call stacks, reading position, control
timers, choices and selection focus/fade, calendar, shared RNG, shake/weather,
background/camera, bitmaps, characters, colour, dialogue/novel, backlog and
audio resume specifications. Every envelope is checked before opening any
resource. Restore builds a fresh owner with callbacks bound to its final
address and an unattached mixer; the application can retain its old scene
until all validation and resource preparation succeeds. Held/latched skip is
cleared. The saved logical clock is rebased to the current monotonic clock,
so elapsed waits do not include time outside the saved scene. Window/calendar
assets are prepared without ticking counters or materializing glyphs.

Selection restore preserves visible insertion ordinals, focus, opacity, asset
references and readiness. It checks the exact saved choice list. Choice-history
progress remains persistent: if a choice was selected after this save, restore
can load its newly required history marker without rolling those bits back.
Save during a movie transport is an explicit unsupported boundary; no decoder
is serialized. This is a new native format, not original manual-save import.
Finite synthetic scripts and in-memory PAKs check scene continuation, dialogue
pages, choices, independent clocks and failed-load isolation. No original
story, game window, rendering device or original save is used. Application
file/save-menu integration and whole-game compatibility remain unverified.

The native save-slot repository now validates snapshot envelopes before writes
and after reads. Its file adapter keeps `native-saves/slot-NNN.wa2sav` beneath a
selected game directory, apart from original manual saves and Sys.sav.
Writes use an exclusively created temporary, flush its contents, then replace
the named native slot atomically (with directory flushing on Android/POSIX).
Slot enumeration sorts numeric IDs and reports damaged entries individually;
one damaged slot does not hide healthy saves. A damaged ordinary native slot
can be overwritten, while links/directories are rejected. The Windows adapter
uses wide paths, and the same save model/file adapter compile for Android.
Tests use memory storage and newly created synthetic directories, preserve
sentinel original-format filenames, and check bounded slots, failed writes,
hard links and malformed data. The application save menu is still pending.

Native application preferences have a separate versioned `WAP2` format (with
read compatibility for the earlier native `WAPR` format) and
injected storage. Unchanged edits do not write; invalid values and failed
writes cannot publish a new current value. Current audio gains, character voice
switches, text budget, window skin/opacity, auto delay, skip-read policy and
caption visibility can be applied to a scene. Restore applies current
preferences before preparing UI images or restarting looping audio, preserving
script-controlled opening durations and scene progress. These are new native
preferences; importing original registry/INI settings is not implemented.

The voice switches follow the cached original ten-character group order and
supporting-role mapping. Disabled/fast-skipped new lines do not replace an
already playing channel. Turning speech off lowers existing gains through the
original zero-duration volume operation; turning it back on permits future
lines rather than replaying an old line. Looping-voice restore observes the
current policy without creating a decoder for excluded speech. Synthetic checks
use fake voices and memory settings storage. The file adapter persists only
`native-settings.wa2cfg` in the selected game directory, through the same
exclusive temporary/flush/atomic replacement helper as native save slots.
It rejects links, directories and excessive lengths; it never imports or
modifies original settings or system/manual saves. Separate synthetic-directory
checks cover reopen, unchanged edits, damaged files and wide Windows paths.
Application option controls still need integration.

`game/application.*` now owns one shared native scene, current preferences,
native slots and bounded page navigation. New stories and loads prepare a new
owner before replacing the old one; failed loads retain the old scene, mixer,
preferences and menu path. Menu suspension freezes script/visual logic while
audio continues; background suspension also freezes PCM transport, movie and
caption clocks. Returning from a menu/load clears the first transient story
input so the selecting click cannot also advance dialogue. The application uses
the bounded skip scheduler and reports unsupported modal movie use or a script
ending without a title request. Its synthetic integration checks use constructed
PAKs/scripts and memory PCM/storage, with no window, device or original story.
Hosts must retain a shared mixer owner until their old audio output is detached.
These navigation transitions do not yet connect all page views or provide a
launchable native application.

`game/title_view.*` implements the original root/chapter/extras title view with
ordinary shared state and external image requests. Cached menu tables supply
the normal/highlight atlas cells, exact hit widths and content unlock rules;
button focus uses logical 1280x720 input, independently of display scaling.
The background advances by the source's binary32 1.2 promoted to binary64,
then stored back as binary32. Background/heading/button fades, staggered scale
easing, copyright, help crops, perspective snow, chapter entries 1001/2001/3001,
special-story entries 5000/5100 and exit timings remain in the common code.
The combined title's eight atlases are loaded before its first tick; unsupported
smaller atlas layouts fail explicitly. Injected music/cue services and typed
commands keep device audio and application navigation outside the view.
Twenty-one constructed-image cases and six guards check the fades, boundary
hits, modal resume, chapter gating, menu back paths and delayed scene/quit
commands. These are owned-memory component checks, without original pixels,
game/window launch or visual verification.

`game/title_scene.*` connects that view to a native mixer, the persistent five
UI cues, common music controller, system progress and bounded movie player.
Its startup states preserve the cached company/opening movie selection, three
delay ticks, movie-quality suffix and IC opening music flag. Preferences apply
to menu music/cues and movie gain; background suspension freezes title logic,
PCM consumption and movie time. `engine/audio_resources.*` now supplies the
same root/IC naming and fallback rules to title audio and story audio. A failed
archive/provider access propagates instead of selecting another bank.

`Application::prepare_title` opts the host into this owner. Typed title actions
open common menus, enter the selected original LSCR scene, return from a modal
page or close the application after the source fade. Title and story handoffs
carry the one native RNG state forward instead of reseeding each scene. New
owners are prepared before replacing the old owner; failed loads retain the
title, navigation and mixer. A device adapter must compare the returned shared
mixer when ownership changes and retain the old mixer until detaching output.
Twenty synthetic cases, thirteen guards and twelve archive-lookup cases cover
these transitions, startup branches and audio/resource policy. Music/movie
services are fakes; system cue PCM and all scripts/images/PAKs are constructed
in memory. This does not verify a movie decoder on a device or visual parity.
The integrated options/save/load/extras/backlog pages and production entries
are described below. Their component checks do not establish whole-game parity.

Story `SetMovie` now uses the cached pre-play audio sequence: stop speech
channel 0, then every one-shot, looping and prepared effect. Other speech
channels, BGM and all scene/UI state remain owned while movie presentation
temporarily replaces the picture. This is distinct from the cached full-reset
path. Movie amplitude follows the same quantized master/music preferences as
title movies, including live edits. Explicit scene retirement first flushes
reading progress and records a tracked movie as seen before stopping it, as in
the cached exit path. A failed replacement preparation retains the current
scene and movie. Destruction following a playback failure still stops the
transport without inventing a successful viewing.

`SceneServices` injects common music/effect/speech/movie transports and survives
application save/load replacement. Defaults still use the common resource and
codec components; platform adapters do not implement game policy. Twenty-eight
constructed cases and fourteen guards cover pre-play ordering, retained visual
owners, preference changes, background clock suspension, movie EOF, failed
providers and transport ownership after load. They use fake transports and
constructed scripts/archives only. Movie completion intentionally yields one
frame before the following script command. No original story, audio/video
device, game window or screen is used by these checks.

Story movies own input until the suspended SetMovie command releases the
player, including the frame after the decoder reports EOF. Left/right mouse,
Enter, Space and Escape request skip; tracked records from 200 upward require
an existing seen flag, while untracked clips do not. Held Ctrl does not skip a
movie. The shared application routes these events before toolbar, right-click
menu, backlog and choice handling. The hidden scene's frame controllers and
statistics freeze during playback, while device PCM, wall-derived time,
movie updates and captions continue. Completion consumes one normal frame
before story execution resumes, even with latched accelerated skip enabled.
Startup title movies also accept cancel/right-click input through the common
title controller. Forty-three finite synthetic cases and seven guards check
these policies, background input release, failed storage/replacement retention
and explicit-exit viewing flags, using fake transports and owned archives only.

`game/render_frame.*` assembles an owned shared presentation snapshot for title
and story. Background/environment membership remains separate from overlays;
foreground bitmaps, dialogue, selection, calendar and captions merge by the
source layer and sprite slot. Equal keys retain piece/glyph/pass ordering.
Copies retain immutable image and YUV owners without copying their pixels,
so a completed frame remains valid after the application replaces its scene.
Twenty-three synthetic cases and three guards check this assembly, including
visible generated-font metadata and name-slot restore. Capturing a frame does
not advance logic, typewriter fades, audio or RNG.
Unsupported modal views and an unprepared title fail explicitly.

Dialogue now supplies the source backing/frame/button/text/name priorities.
Its alternating name slots are saved in LSDI payload version 2; earlier native
component snapshots fail explicitly rather than losing fade ordering. This is
an unreleased native checkpoint change, not an original manual-save conversion
or a change to the retained AOT application. Choice passes now retain their
source slot numbers as well, so panels sharing the body-text layer sort after
the body. INAS pieces remain stable through bitmap and environment sorting.

`platform/sdl_presentation.*` consumes the same frame on both toolchains and
groups only adjacent sprites with equal transforms. It routes the movie path
through the common YUV presenter, clears to black before the first decoded
frame, preserves display aspect ratio, retains post-video captions and resets
both image/movie uploads after renderer reset. Hosts still own window/input,
device audio and the final present. Finite software composition checks use a
64x36 owned memory surface, generated solid colours and small constructed YUV
planes in forty-five cases; they create no window or SDL video/audio/event device. These checks do
not validate the original game's full visual output or hardware performance.

`game/frame_driver.*` provides one wall-clock scheduler for all native hosts.
Normal logic remains exactly 60 Hz across 30/60/120/240 Hz presentation schedules.
Integer remainder clocks avoid long-term rounding drift and large timestamp
multiplication overflow. A stall permits at most eight normal catch-up updates
and drops excess work; only the final due update may use the remaining shared
skip budget. Eight catch-up updates cannot each obtain a fresh 2 ms skip budget.
Background suspension clears pending edges and catch-up debt; resuming makes
one immediate normal update without replaying stale gestures. Queued clicks
retain their press coordinates while subsequent motion changes hover position.
Constructed clocks/scripts check these rules without measuring device FPS.

`platform/audio_binding.*` retains each mixer until its device callback is
detached. Unchanged ownership keeps the existing output; title/story/load
handoffs close the old output before opening a new one, with no overlapping
devices. Factory/open/close failures preserve ownership through cleanup and
report errors. The destructor also detaches safely if teardown reports failure.
Fake outputs check close/destruction ordering and that at most one is active.

`platform/sdl_application.*` connects the common driver, SDL input translation,
frame snapshot, presentation and audio binding. Hosts supply the application,
canvas, event collector, monotonic clock, wait adapter and successful-present
callback. Mouse/touch hover is tracked explicitly; existing Android gesture
adaptation supplies Ctrl for enabled long-press skip instead of a second
unconditional native timer. The common pump uses host-provided resource/storage
adapters and never creates a window or selects a platform-specific script engine. Its owned-memory checks
exercise rendering, actual-present counting, cache reset, background suspension
and scene/mixer replacement using fake device outputs and constructed events.
The full loop is compiled, not run, during development; tests never poll an OS
event queue. Complete modal pages and final application packaging remain
pending, so this is still not a launchable complete game release.

`platform/native_application.*` supplies shared startup policy and private
storage integration. It prepares the title instead of jumping straight into
1001, reopens native preferences/slots, applies test-mode progress before title
construction and persists that profile only after preparation succeeds. The
default content probe uses the cached `IC/bak.pak` existence rule, not the
presence of story scripts or a platform installation registry. The resource
opener returns null only for absence; authorization/provider errors propagate.
An explicit content descriptor can be supplied by another installation adapter.
Unknown/duplicate startup arguments and invalid timeouts fail explicitly.

Saved-image restoration now waits on `ImageRequest` publication notifications.
Completed requests and decoded-image cache hits return the same immutable
owner immediately; multiple waiters share publication, timeout returns no
partial image, and decoding/worker/shutdown errors retain their diagnostics.
Closing a loader wakes its queued requests even while its current request is
finishing. Ordinary asynchronous rendering continues to use nonblocking get().
Constructed gated sources check these cases without original data or pixels.
Bootstrap checks reopen private preferences and a complete native scene slot
using constructed scripts, fake movies and owned temporary files.

`android/game/game_host.cpp` is now a thin migration entry around the same
bootstrap and SDL application loop. It creates the Android window/canvas,
supplies authorized PAK descriptors and the selected directory's SAF storage,
clock/wait adapters and the successful-present callback. It no longer owns
another story scheduler, skip timer, manual draw ordering or raw mixer device.
The NDK compiles this entry without launching it. With an existing Android
SDL2 library, the verifier also links the actual SDL_main/JNI entry, resource
descriptor adapter, common SDL drawing/audio loop, native bootstrap and complete
common scene into one ARM64 library. It checks the real link graph, defined
symbols and dynamic dependencies, including the HUD-reset JNI export. The new
entry does not initialize the Win32 widget/shell bridges or mount a compiled
game image. Shared HUD registration and presentation counters serve both native
and retained hosts. JNI service registration releases partially initialized
owners in reverse order on failure; constructed service checks cover every
failure position and retry for both service sets, without creating a JVM.
The retained JNI entry is separately compiled with the NDK after this split.
Android's retained release root still selects the AOT host; final APK selection,
remaining full page views and device behavior remain migration work. The native
entry library is linked but never loaded, started or copied into a release APK.

The native SDL canvas selects OpenGL on desktop and GLES2 on Android. Both use
one common mask-shader equation, with GLSL 120/100 preambles and the appropriate
precision declarations. This makes native mask transitions available through
the desktop GL renderer as well. The scene, canvas and mask adapters compile
on both toolchains; generated shader pairs are compiled/linked offline for
both profiles. A software reference still checks owned-memory pixel equations.
No actual GL context, window or hardware rendering is exercised by these checks.

`game/options_view.*` now supplies the common three-page settings view as a
component: source-resource atlas geometry, selected cells, hover/help regions,
volume mute cells, the ten character voice switches, wrapping tabs/arrows,
quantized sliders and pointer capture outside a slider while dragging. It uses
ordinary typed fields and shared immutable images. Page loading retains the
displayed page until incoming atlases are complete; one outstanding image
request and at most two new requests per tick bound this component's queue use.
Opening/closing use 15 logic ticks and page fades use 7 by default; the owner
can supply source-adjusted durations. Applying an edit calls the supplied
writer before publishing the new value, so a failed write preserves the model.
Reset confirmation, sound previews and return are explicit owner commands.
The view checks use constructed blank atlas images and inspect geometry and
state only; no original image pixels are decoded or displayed.

`game/options_preferences.*` maps all 28 visible fields, the four mute controls
and their inverse atlas-cell conventions to common preferences. The native
text increments now use the cached 60 Hz source calculation `2 / text pace`:
instant, 4, 2 and 1 layout time units. Arbitrary native text timings remain
readable from old settings but have no source UI cell and fail explicitly when
converted to this view; they are not rounded to a different setting. Settings
retain source opacity values through 256; drawing saturates to 255 instead of
wrapping to transparent. The extended codec retains previous fields and adds
three independent mute switches, display/confirmation/right-click policies,
voice cut on manual message advance, and the transition-wait switch. Legacy
settings retain their stored custom text speed and receive extension defaults.
Music/effect/master muting keeps the slider gain and live playback; movie gain
uses the same mute policy. The common scene applies transition-wait without
making text instantaneous. Voice cut applies only to channel zero when a
completed message is manually advanced, preserving other channels and a voice
whose text is still being revealed. Constructed scene/voice checks exercise
these bindings and failed preference writes without opening a device.

`Application` now owns this options view when the common bootstrap wires its
loader. It prepares the incoming model/images before changing navigation,
suspends story logic, keeps the same mixer and ambient audio, applies/persists
edits, and returns after the closing transition without forwarding that click
to the story. Background suspension freezes the view as well as device audio;
input release clears slider capture on return. SDL's held-pointer state passes
through the common input queue, so a captured slider can keep moving outside
its hit rectangle. `RenderFrame` composes the same parent scene and options
sprites in their common layer order, retaining immutable image owners through
closure. Leaving for a title/story replacement or shutdown releases the view.
Constructed application checks cover those ownership/persistence boundaries,
including failed page preparation and failed settings writes. The Android
native entry now actually links these view paths through the shared bootstrap.

The common default owner now handles reset and audio preview commands. Reset
uses `game/confirmation_view.*`, the original theme/background and two-button
atlas, original font indices, logical hit rectangles and bounded asynchronous
image loading. It accepts keyboard/pointer input after opening, consumes its
answer once, and blocks edits underneath it during the closing fade. Disabling
confirmation applies defaults directly. Persistence occurs before publishing
defaults to the scene/view. Source reset restores speech volume to 256 (its
static initialization was 195), theme zero and all ten character switches;
display mode and the native caption preference are retained. Host window-size
reset is not implemented. The bootstrap lazily prepares prompt glyphs with the
same native FontBook; the constructed action checks use deferred glyph metrics
and verify geometry/state, not font pixels or appearance on a device.

`game/options_audio.*` shares the five source voice samples, random selection
and reserved effect channel between story/title owners. Effect preview replaces
channel 15 with sound 8101; speech preview replaces channel zero, plays once,
and does not append a story/backlog voice descriptor. Their existing mixers,
gain/mute/character policies, pause and lifetime remain in force. Repeated
previews keep the channel count bounded and do not restart music. Speech retains
the source voice offset and ordinary skip policy; suspending story logic disables
story acceleration, so a stale held-skip input cannot suppress a menu preview. The title
owner now also services its native effects and speech while a menu is open.
Reset's confirmation sound uses the existing ordinary effect pool. Fake voice
checks cover replacement, muting, cleanup and the common application route;
no decoder or audio device is opened by those checks.

The right-click policy now distinguishes hiding the message window from opening
the shared settings view, matching the cached source's 0/1 branches. The latter
persists from its actual option cell, suspends story logic before processing that
click, and consumes the closing click before resuming. Missing settings services
and missing save services (source policy 2) fail before changing story/navigation.
Policy 2 now opens the common save view when its services are prepared.
Display-mode changes require the window adapter described below; options over
another modal page fail explicitly. The current compositor retains the frozen
parent scene beneath the options overlay. Cached 4222ed..42237c and
422c38..422ce7 now supply its missing left header: chapter themes 0/1/2 select
sys_05010/05011/05012, while title and later themes select sys_04010. The first
three use source/destination Y 16 and require 184 atlas rows for their 168-row
crop; the full header starts at zero. Header/chrome zoom is about 640/360 and
travels between 1.1 and 1 during the fifteen-tick fade. The header remains
owned across tab changes. The cached options path uses press edges and drag
state; speculative held-arrow or captured-parent zoom features are not added.

The native story engine remains unverified as a complete game. Shared page
views, production host builds and original INI import are described below;
unsupported render/animation/weather modes still fail explicitly. Original
manual-save conversion is unfinished. No whole-game or device/performance
claim follows from these component checks. Existing generated C++/CFG is a
development reference, never a native-runtime dependency.

The native manual-save browser now has a separate `game/save_document.*`
storage boundary. Its small WSP2 preview (local date, ordering timestamp, raw
font description and optional 128x72 RGBA thumbnail) and LSGS scene payload have
independent checksums in a WASF container. `SaveSlots::preview` reads at most
131084 bytes; the production private-file adapter reads only that prefix rather
than copying the full scene. Browsing metadata does not establish that the scene
body is valid: actual loading still checks every scene envelope before owner
replacement. Earlier LSGS-only native files remain loadable and are identified
as legacy entries without invented preview details. Slot I/O is serialized for
the forthcoming asynchronous browser. Writes retain atomic publication and
original-save isolation. The shared checkpoint CRC now uses a constant table
with the same polynomial/output; finite reference/known-vector checks cover file
compatibility. Earlier WASP previews remain readable; WSP2 additionally records
the source calendar/chapter classification. Source descriptions retain their
first-line, 24-byte character boundary, formatting-scope closure and ellipsis.

`game/save_view.*` supplies the native save/load view component with the source
two columns of five rows, ten wrapping page tabs, previous/next arrows, title
badge, latest-slot marker, quick-load control and alternate-bank selector. The
100 manual slots use 0..99, the quick slot uses 100, and the optional second
read-only view uses native slots 200..299. Those are private native names, not a
conversion of an original save directory. One joined `SavePreviewRequest` reads
metadata at a time, while the displayed page retains its image owners until the
incoming page is ready. Atlas preparation has one pending image and at most two
new image requests per logic tick. Metadata/decoder failures remain explicit;
invalid slots cannot emit a load command. Empty saves, overwrite confirmation,
load confirmation, cancellation, pending-operation input gating, result display
and failed-operation retry are shared game state. Newly saved empty slots remain
discoverable when paging away and back.

Menu numbers/dates use the cached sixteen-column ASCII bitmap grid in
`sys_01013.tga` with 12x16 cells. The source's neutral brightness 128 maps to
RGB 255, rather than changing alpha. Chapter badges retain the source type/sub
rows and calendar month/day cells. Original atlas headers supply dimensions;
view tests allocate blank pixels and deferred glyph metrics, so they verify
geometry and state, not original font pixels, appearance or device compatibility.
The common Application now binds this view to real native slot writes and scene
restoration. Opening it freezes story logic while retaining the same mixer;
backgrounding also pauses the audio and menu clock. Successful loads prepare a
complete new scene before releasing the active one. Failed writes/restores keep
the current scene, navigation and slot data and expose their diagnostic. Slot
errors use the host's operation-error adapter when attached, preserving the
view for retry. A missing error adapter propagates the exception explicitly.
Right-click save routing and one-shot PageUp/PageDown input use the same code on
both hosts. A closing click is consumed before story logic resumes.

The platform bootstrap supplies the local wall-clock date independently of game
logic. The SDL adapter creates a separate owned 128x72 CPU surface and renders
the retained scene into it for a save thumbnail. It does not read a window,
framebuffer, screen, existing renderer target or device pixels. Checks use only
constructed images/scripts and verify thumbnail pixels without an SDL window,
video subsystem, event polling, GL context or audio output. These checks do not
verify an original-resource scene or a complete playable port.

The bootstrap now prepares menu descriptions through its current story/title
owner. Story menus reuse the existing FontBook; the title creates a lazy one.
An options confirmation no longer creates a second long-lived copy of the
same source font pages alongside an active story. Glyph images still own their
pixels independently and can outlive scene/view replacement in a retained frame.

`game/backlog_view.*` now binds the native history deque to the common
Application. Cached layout constants supply four normal rows in chronological
display order, one novel entry, name/body metrics, speaker controls, scrollbar,
top/bottom strips and return control. Only the visible entries prepare text;
scrolling retains the old rows if preparing an incoming page fails. Image
preparation retains one pending request and starts at most two per logic tick.
Normal/novel row limits and retained-frame ownership use ordinary game objects.
The application suppresses current dialogue/choice overlays under the history
page and freezes story logic while continuing its existing mixer. Backgrounding
freezes the page as well. Sequential replay retains at most ten descriptors,
uses the same speech channel and character/mute settings, and never creates a
new history event. Recorded line numbers are absolute: replay does not add the
current script's speech offset again. Closing or replacing the view releases its
replay owner before replacing the scene.

Continued message commands now follow cached 40e680..40e72e instead of
creating another history row. The live dialogue inserts its pause marker,
while the existing history text concatenates the continuation directly.
The first name/identity stays attached and each changed adjacent voice
descriptor joins its bounded replay sequence. Append does not apply the
fresh-row log-output filter, matching that separate source branch. Overlong
text or an eleventh distinct descriptor fails before publishing history.
Native checkpoints retain the combined row and pending latest voice. Normal
history reveals all pause portions; the current novel row includes every
portion through its reveal ordinal, clamped when removing appended pauses
reduces the history layout's portion count. Checks exercise actual common
message commands and sequential replay, and render glyph sprites from
uniform synthetic indexed font sheets. No original glyph pixels are read.

The active script's reading context now follows cached 4187ab..4187dc before
each unpaused story update. Waiting for the next dialogue callback left a
replacement's prelude or a returning caller on the previous script's read
row. Read-only fast-forward could then use an unrelated chapter's next-line
bit. This sync also records the departing line marker before entering another
catalogue row, including child-return paths with no immediate parent dialogue.
An unchanged name returns before searching the catalogue. Paused logic does
not move this state. Constructed scene-load/call/return scripts check the
read flags, persisted end markers and unread waits, without game playback.

Native movie lookup also adapts to installations containing one quality only.
For the known mvNN1/mvNN2 PAK names, root and IC locations for the requested
variant both take priority; only their explicit absence permits the matching
mvNN0 default. Both names then share the same owned source cache entry. This
is a native resource-availability policy, rather than a claim that the old
loader made that fallback. It serves title and story movies through the same
catalogue. Permission/provider errors and an existing but corrupt movie are
never hidden by switching variants. Other filename conventions and a missing
default remain explicit errors; original PAKs stay external and unmodified.

The story toolbar also has its source hover/press atlas rows, moving 208x16
help strip and sixteen-step bottom-proximity alpha. Cached 4112a3..411575
supplies the integer truncation, crop rows and fade geometry; 4131b1..
413301 supplies demo visibility and replay brightness. The footer/help strip
stays in screen space while the buttons retain the dialogue transform.
Application input frames tick those transient timers once, independently of
accelerated story updates and repeated render capture. Menus/backgrounding
freeze them; loading a checkpoint clears them without changing the save format.
Replay save/quick/load actions and hidden demo actions share one availability
policy with host menus/shortcuts. A disabled visible button consumes its click
before dialogue advancement. Manual story advance, right-click and non-auto
toolbar clicks clear ordinary auto-play, while demo timing remains separate.
Constructed blank-image/script checks cover these bindings and a sixty-four
tick accelerated frame; they do not establish original artwork appearance.

The nine bottom controls now share their cached hit rectangles with the
dialogue renderer. Their quick-save/load, settings, save/load, history, skip,
auto and window actions are processed before story advancement. Input is
unprojected through the dialogue transform before hit testing. The scroll and
page edges are queued once by the shared frame driver; held pointer state is
reserved for scrollbar dragging. Quick-save writes the isolated native quick
slot directly and displays the common completion view; quick-load uses the
confirmation setting before preparing its replacement scene. Source 40a19f..
40a1ad writes before calling 41ef80 with zero, whereas 40a281..40a2ca checks
the quick slot and calls it with one. A save confirmation would add behavior
which that source path does not have. The notification retains its original
sys_*0110 atlas (480 by 112), 400/304 position, 450/345 text position, seven
tick fade and sixty tick minimum display. The load prompt closes before its
result appears. Modal loading, closing and result timers freeze in the
background; the original scene's mixer continues in the foreground. Failed
loads keep the old scene; read/write failures clear the operation, report the
error and rebase input/time before resuming. Manual-save completion uses the
same result atlas instead of cropping a dialogue background. Menu chrome uses
its source centre-based zoom; the backlog itself enters and leaves directly
(403850 and 403e00). Cached source
41247c..41250d gates
each recorded voice control using its first speaker and the existing ten
character switches (406410); master/global speech mute does not disable that
control. Its icon atlas rows are disabled 0, ordinary 28 and hovered 56. The
source consumes single-click edges for arrows and held input for dragging,
without a separate playing-state icon or replay-level meter. Scrolling clears
remaining replay descriptors before a same-tick next-line decision, lets the
current voice finish and retains its owner for closing the page. Permission
changes refresh visible controls without rebuilding text. The view checks
use constructed PAKs and deferred font geometry, not original glyph pixels,
game playback, a screen or device input. No native game package is released by
these checks.

The common application now binds fullscreen edits through a reversible display
adapter. A failed display switch leaves settings unchanged; a failed preference
write restores the previous display mode before returning the error. Game and
menu settings apply only after the file commit succeeds. The SDL implementation
uses its own window's fullscreen/message-box services, separate from game logic,
and captures only a weak renderer owner so a stale callback cannot keep or use a
closed window. A software renderer with no window exposes neither service.

Manual and quick slot failures can report their diagnostic without ending the
application. The common recovery path freezes logical time before the host
reports the error, releases stale input, and rebases the frame driver on return.
Time spent in a blocking dialog cannot become a story timer jump or eight
catch-up updates. Finite checks inject display/write failures and a simulated
sixty-second dialog interval; no actual window, message box or game is opened.
Real fullscreen switching and message-box appearance remain untested on devices.

`platform/native_host.*` now owns SDL initialization, window/canvas creation,
the native application, shared frame/input policy and device-audio loop for both
hosts. The Android wrapper supplies SAF resources, private storage and frame-HUD
submission; the desktop wrapper supplies external-directory resources and an SDL
private-storage path. Neither entry links generated functions or guest memory.

`platform/native_desktop.*` accepts an explicit UTF-8 `--resources` directory or
discovers PAKs beside the program, in its `res` child, or in the working directory
and its `res` child, in that order. It examines directory metadata, never an
original executable. Optional PAK absence returns null; permission/provider errors
remain errors. Native files stay in their separate private storage. The desktop
GUI entry uses SDL's own UTF-8 Windows main shim and the same native host as Android.

The core verifier now links the real Windows entry against its existing
software-reference SDL archive, audits the GUI/x64 PE, entry exports, linked
native-host archive symbols, source closure
and absence of recompilation dependencies, and never starts that executable.
That dependency has no production window/audio driver and its artifact must not
be delivered as a game. `hosts/native/CMakeLists.txt` defines the separate full
SDL Windows build; it is not selected as the released host. Android's retained
Java/widget startup still needs its native-only adapter selection before native
APK packaging. Extras views and remaining source UI behavior remain incomplete.

The title's music extra now owns `game/music_view.*`, with the cached 63-track
order, two pages, closing-only 27-track first page, bonus flag 134, alternate
21/35 rule and source hit/atlas coordinates. Opening and leaving stop the title
track with the source 120-tick fade; selection toggles stop (60 ticks) and uses
0/60 ticks for first/subsequent previews. Commands acknowledge playback only
after the shared title audio owner accepts it, so an open/decoder failure does
not select a new row or discard an existing playing voice. The page keeps the
same mixer and preference gains, and freezes input while backgrounded. Loading
has one outstanding atlas request and starts at most two new requests per tick;
it copies only needed flag booleans, not the multi-megabyte progress blocks.
Synthetic atlas/input/voice tests cover the application route and retained-frame
composition without opening a device. Animated transitions, level meters and
device appearance remain unverified. Cached source 0x42a6d0 is a voice-comment
menu using `9500_...` speech,
not a movie gallery. No native release or whole-game compatibility is claimed.

The selected music entry uses both source label sheets: the 344-by-24 name at
(160,552) and the 512-by-24 information row at (536,552). Both retain the playing
entry's absolute catalogue row when the browsing page changes; stopping removes
both. Constructed atlases check all sixty-three rows, the alternate-track entry,
page retention and stopped visibility. These are layout/data checks, not device
or original-image appearance verification.

`game/extra_grid.*` now shares native gallery/replay navigation, immutable visible
images and presentation. Its content-only catalogue has 168 gallery groups and
24 replay entries from the existing cached tables. Unlock tests use gallery
bank zero and exact replay flags; unavailable IC cards cannot start a story.
Visible `tv...` thumbnails and full `v...` variants come from the external `grp`
catalogue, including its authorized IC fallback. Page loading commits only when
all requested pictures are ready, retaining the previous visible page on a
provider or decoder error. Full gallery display loads only seen variants and
retains its current image on a failed replacement. There is one outstanding
image request and at most two new page requests per update.

Replay starts the common `9999` resource script with the original 1-based entry
number. `GetReplayMode` and native aggregate snapshots preserve 0..24 instead of
collapsing every entry to a boolean. The existing four-byte field keeps old 0/1
snapshots byte-compatible; numbers above 24 remain errors. While a replay runs,
its title/grid owner is retained with audio paused. Returning or loading within
that session restores the same page/mixer without startup films or atlas
reloads. Synthetic LSCR checks explicitly assign entry 24 to a global, round-trip
its saved scene and return to the retained page. Importing original manual
slots, full source
page transitions/counters and visual/device verification remain pending.

Native host menus use `game/user_command.*`: one bounded action set and one
availability/checked-state policy for both hosts. Settings, slots, quick slots,
backlog, dialogue visibility, auto, skip, display mode, title and quit enter the
same Application methods as in-game controls. A whole queued batch is validated
before any action runs; a disabled/stale action is rejected and a blocking error
ends the batch. Actions do not also advance the current dialogue. SDL function
keys and pointer-free, tagged SDL user events feed this common path. The frame
driver retains explicit menu selections through popup focus loss but releases
ordinary keys/pointer clicks, applying selections once on resumption. Finite
synthetic event/clock tests cover the focus gap and multi-update catch-up.

Android's common activity and touch gestures now depend on a presentation-only
`PortUi` adapter. The retained AOT source set uses its old widget/file-dialog
transport; the native source set replaces that class and excludes NativeWidgets,
WidgetSession/Controls/Icon and FileChooser. Native Java displays labels and
enabled/checked masks supplied by C++, forwards a selected action through its
three registered JNI methods, and never reads a scene. Its state comes from an
atomic snapshot; the game thread alone validates and executes actions. The same
launcher, SAF resources, FPS HUD, long-press control and two-finger toggle remain.
`android_source_sets.py` audits the source selection. Java/API compilation and
native-only DEX checks cover both adapter sets without loading an Activity; the
unavailable retained original-widget fixture is explicitly not reverified.

`hosts/native-android` is a separate full SDL/ARM64 CMake root, including GLES,
audio and Android HID JNI sources required by SDLActivity. Definition checks
require fresh native-core and Java reports, compare the compiled native-menu
Java signatures with actual linked registration literals, verify all SDL Java
native exports against the existing SDL library, and audit the production
source closure. This is a configuration/interface check, not an Android game
session, production-SDL build or native APK publication. Remaining original UI
behavior and unverified full-story compatibility prevent a complete-game claim.

The voice-comment extra now uses `game/commentary_view.*` and a dedicated native
title audio transport. Its five source columns map to speakers 4,2,1,3,5 and
external clips `9500_%04d_%02d.ogg` with `(speaker-1, speaker)` fields;
`sys_07200`..`sys_07204` remain
external page images. The cached raw-comment path uses master/speech gain,
independently of dialogue/character voice switches. Cancellation, natural finish,
factory/decoder failure, backgrounding and leaving the owner release or pause
the correct transport. One pending image and at most two new atlas requests per
update bound loading. Finite synthetic tests cover all five identities, exact
gain, late-load cancellation and recovery without playing device audio.

An imported native replay snapshot can now reconstruct its title/extra owner
and selected page on exit, including closing the extra while its title pictures
are still loading. Startup movies run only for application startup, not every
return from a story. Returning from extras restores title music through the
shared owner. The common SDL entry explicitly stops text input because no game
page requests an OS edit field; startup must not raise an unsolicited IME.
Source transitions/meters/counters and remaining original settings UI still
need completion, and physical presentation remains unverified.

The shared user menu now exposes source high/low video quality. Cached movie
name selection maps high quality to suffix `0.pak` and low quality to `1.pak`;
the current decoder is not restarted when that preference changes. Title and
story owners both apply it to the next external clip. Imported original
`mov_lv=0` retains suffix `2.pak`, with neither high/low menu item checked,
following the cached original menu comparisons. An explicit bootstrap
suffix is reserved for controlled integrations. Missing quality variants now
follow the explicitly bounded default-file policy described above; an existing
variant with a provider or decoder failure remains an error.
`WAP5` preferences retain `WAPR`, `WAP2`, `WAP3` and `WAP4` decoding, with high quality as their
compatible default. The original window-management dialog contains four
opacity sliders and a visibility switch, already covered by the common native
options view; it is not a separate skin/colour picker. The three original
windowed sizes now share application policy and transaction rollback with
fullscreen. Android does not enable desktop resizing. Version/environment
information uses common command policy and an SDL presentation service, pausing
the active owner and rebasing its clock/input after a blocking native dialog.
The desktop entry embeds the provided root icon. Legacy D3D-only renderer
options, remaining source-menu options and exact extra-page animation remain migration
work; this does not establish full-story or physical presentation compatibility.

Music-browser transitions now use common `MenuTiming` values (15/7/15 source
logic ticks, or zero when transition waits are disabled). Headers/footer controls
slide 32 pixels, content fades/scales about the source centre, and the old page
remains until its fade-out completes. Opening and page transitions consume no
selection input. Closing starts the existing 120-tick audio fade before the
visual owner returns to the title; backgrounding freezes both progress and
transport. Row focus selects its background cell before its label is drawn.
Finite tests cover gradual/zero-delay paths, repeated transition clicks, exact
page commit boundaries, early cancellation and background suspension. Other
extra-page transitions, counters and physical presentation remain unverified.

Gallery/replay grids use the same bounded opening, page and closing timing.
Their next page must load completely before the old page starts to fade, and
its rows commit only at the fade-out boundary. CG replacement retains the old
raster for the cached 16-tick alpha transition, with no pixel/screen capture.
An exit publishes its audio fade before leaving. Replay entry preparation keeps
the return grid usable if a resource provider fails; a successful replay retains
that same page and owner. Constructed tests cover alpha steps, suppressed
transition clicks, failed preparation/retry, backgrounding and save restoration.
Remaining counters/chrome and full physical game compatibility are still
migration work. Native desktop SDL windows now request
the provided embedded icon for both large and small window icons.

The first full CG expands from its selected 256-by-144 thumbnail rectangle over
sixteen logic ticks using the cached quadratic geometry/alpha curve. Later
variants retain their separate sixteen-tick linear crossfade. The common view
uses a native-resolution 1280-by-720 source window rather than shrinking an
oversized CG into the screen. Pointer bands within 200/100 logical pixels of
each edge scroll by four/eight pixels per tick, bounded by the actual raster.
Panning issues no image request or pixel copy. A failed variant request retains
the previous image and crop; successful variants and reopening a card reset the
new viewport, while the crossfade keeps the old crop. Constructed images check
edge thresholds, both axes, clamping, geometry milestones, retry and no-I/O
scrolling. No original rendered image, device or screen is used in these checks.
Gallery/replay arrow turns additionally move outgoing/incoming cards in opposite
32-pixel directions without the tab-change zoom. The loading transaction retains
its requested direction; selecting a page tab clears that direction. Finite
checks cover both turn directions, the page commit and a subsequent tab change.

The native Windows entry now supplies a separate owned menubar window at the
game client's top edge. Its four groups forward all nineteen common user
commands and use the same enabled/checked masks as Android. The game SDL window
never receives a Win32 menu or a changed client rectangle. State and position
updates are suppressed when unchanged. All HWND/menu handles and UTF-16 label
conversion remain in `platform/windows/native_menu.*`; no Windows menu policy
or callback enters the common game/engine implementation.

A native menu may run its own nested message loop inside SDL event pumping.
The adapter suspends the common owner immediately and marks one background pump
after the loop. The shared frame driver then resumes without consuming elapsed
menu time, releases ordinary input and applies the selected explicit command
once. Finite owned-memory SDL tests cover a thirty-second synthetic gap,
immediate mixer pause, no catch-up, command retention, failure and input release.
The real Windows entry is compiled/linked but no HWND or menu is opened during
verification. The common preferences retain original `menu=0/1`: default mode
follows the top edge, while Alt-only mode opens on a fresh Alt press and stays
available until the pointer enters and leaves, the menu closes, or focus leaves
the owner. Held Alt does not repeatedly reopen a dismissed menu. Android retains
this adapter preference when the selected directory moves between platforms;
it does not turn it into an Android game-input setting. `WAP5` adds the boolean,
older formats default to edge activation and ordinary options reset preserves
this separate desktop preference. Finite checks cover the activation state,
old-format decoding, malformed flags, import and persisted retry. This does not
establish actual desktop/device presentation or whole-game compatibility.

Voice-comment pages also share the source menu timing. Their opening/closing
opacity, centred content scale and header/footer motion stay in the common
view, with no control activation during opening. Global close stops commentary
immediately, then completes the visual transition before returning; cancellation
from a loading text page cannot later start its clip. Finite tests cover partial
opening cancellation, zero delay, backgrounded closing and active voice release.

Original-resource verification additionally checks 80 menu atlas header/crop
contracts, without decoding artwork pixels or presenting them. Every original
Calendar call is examined through literal records and the pure date-support
policy; unresolved arguments are reported explicitly. This static audit does
not execute LSCR branches, a scene, UI or a game session.

Extra grids now choose the source IC/CC/CODA locked-card image by row theme,
include the original page-heading strip, and place page tabs from x=428 relative
to the first available page. Page transitions alias already-owned immutable
chrome/thumbnail rasters. A page with no new thumbnails does not request any
resource again; requests for new content remain bounded and a failed first or
later thumbnail keeps the old page. Synthetic marker pixels and actual header
metadata validate the three placeholders without viewing original artwork.

The static corpus audit walks literal argument records for dialogue and choice
text, consuming unknown flag-expression shapes without evaluating variables or
calling functions. All 71,243 installed calls expose literal text: 1,644,117
letter instances parse through the common control/encoding rules. Four actual
font-sheet headers and both foreground/shadow crops of 3,937 unique glyph/size
pairs are checked. The source lookup's fallback slot is retained and its codes
are listed in the report. Syntax uses a large bounded test window because a
static walk does not know normal/novel geometry; full-scene pagination, dialogue
appearance and story execution are explicitly not established by this audit.

Native image loading also retains a bounded recent set of immutable decoded
rasters after their last request leaves a scene. Each opened archive defaults
to 8 MiB/eight entries, with a 4 MiB per-image admission limit. This is a
per-archive retention budget, separate from pictures still owned by active
scenes; it is not a bound on total process or GPU memory. Colour/chroma variants
and archive identities remain separate. Oversized pictures still load normally,
failed/abandoned work is not retained, and eviction cannot invalidate active
picture owners. Cache statistics expose retained pixel bytes and entries,
without reading a device or screen. Explicit cache clearing releases its pixel
owners while leaving active scene/request owners and their metadata intact;
later requests or pending completions may warm it again.

Replay grids include the original `sys_05300` description strip and
`sys_05301` sixteen-pixel rows. Hover selects `page*12+ordinal` at logical
position (104,608). The original hit selector excludes disabled rectangles, so
locked rows receive neither pointer focus nor a scene description. Leaving the
card or beginning a page change hides its row. The static strip sits at (104,624). Gallery pages do not
request these replay-only pictures. A locked CODA replay uses `sys_05903` until
clear flag 5 is set, then `sys_05902` for an unseen individual scene. Marker
pixels and exact crop/input checks use constructed images; the three external
atlas headers additionally enter the static metadata audit.

The native music browser, gallery/replay grids, settings, save/load and backlog
pages share the small BGM caption's 63-song name order. Their provider reads
the current story/title audio owner, independently of a hovered or last
acknowledged browser row. `sys_00300` supplies 264x16 names at (24,696).
Stopping music hides its name; unknown/sentinel name rows stay blank.
`sys_00301` is an eight-pixel-high level bar at (24,688), not a fixed marker.
The native PCM queue supplies mean absolute alternating signed-16 values from
at most 1024 bytes at its software play cursor, preserving original mono
even/odd pairing. Reading the meter consumes no PCM, performs no provider I/O,
and allocates no sample history. Original voice-volume and master/category
integer truncation stages precede four-pixel display steps. With no selected
music, the bar uses the loudest active ordinary effect; muted/prepared effects
and a paused application do not produce a bar. Saturated bars are clipped to
their 192-pixel source atlas. Opening/closing transitions and full CG views
hide these footers. Source pictures stay external and share the bounded image
cache. Constructed PCM, voice and menu/application checks cover these rules;
physical-device latency and actual meter appearance remain unverified.

Native script and font members now use that same catalogue's patch/root/IC
lookup, rather than opening a single root archive behind it. Original cached
437820 supplies the precedence used by script binary/text and font-sheet
reads. A small bounded member-reader interface retains the existing 16 MiB
per-member limit before archive reads or decompression. The single-archive
adapter still owns its source for callers that intentionally use an isolated
archive; catalogue readers borrow their scene/title's longer-lived catalogue.
Selected corrupt data and permission/read errors remain errors, with no
fallback to an older script or font. Missing members alone continue lookup.
Constructed script/PAK checks exercise globals, function catalogues, code,
text, IC-only scripts and a real common Runtime invocation. Generated uniform
font sheets with different palette markers check mixed-layer foreground and
shadow coverage, failed preparation/retry and warm reads with I/O denied.
These checks neither execute original story scripts nor read original pixels.

The native script library reuses an immutable parsed LSCR Program while an
existing scene/call owns it. The cache contains weak program references, never
Runtime objects, locals, globals, flow stacks or an old scene kept alive for
caching. Each invocation retains its own runtime state. Re-entering a local
script therefore avoids another archive read and parse, while unowned programs
can be released. Constructed archive tests deny all further source reads on
warm paths and check byte identity, style/archive isolation, bounded retention,
eviction/lifetime and global-state preservation. These checks measure avoided
component I/O; they do not establish game loading times or phone frame rates.

## Selected game-directory persistence

The native development entries use `platform/game_directory.*` for all game
progress, preferences and scene slots. The same common storage wrappers select
root `Sys.sav`, `native-settings.wa2cfg` and `native-saves/slot-NNN.wa2sav` on
Windows and Android. The former AppData/internal `native-game` locations are
not the active native storage root. Selecting another resource directory also
selects another save/configuration set. No original PAK payload is modified.

Android supplies seekable authorized document descriptors and bounded metadata,
while Windows supplies ordinary files with wide paths. Java receives direct
byte buffers only during synchronous writes; it does not retain native buffer
addresses or mirror game data into internal storage. A generation token rejects
an old host after its directory binding changes. Directory scans are bounded,
and each SAF publication reuses its own short-lived metadata index. The strict
file allowlist admits no PAK, original manual save, INI write or parent escape.

Native test mode scopes this same service to `native-test/` beneath the selected
root. It never imports or writes normal Sys.sav. Normal root progress has first
priority, including explicit errors for invalid sizes/provider failures. Only
when absent may the common adapter import one matching combined/closing
Documents Sys.sav; it validates exact size and publishes the root copy before
returning. The source's separate introductory Documents bank is then merged:
history, both gallery banks and all flags are ORed, excluding source flag 99.
Destination flag 99 and unrelated bits remain intact. Changed results are
published through one complete replacement, while unchanged later launches
write nothing. Source files stay read-only. Missing main progress can be
initialized from this introductory bank; the isolated test profile reads neither
normal bank. Later ordinary flag updates remain inside the 4 KiB flags region.

Twenty-seven constructed-data cases and sixteen failure guards check randomized
opaque-block merges, exact flag masking, dirtiness of individual blocks, root
and copied-Documents precedence, source retention, repeat-start write suppression
and failure before publication. These checks open no user save or game.

Filesystem replacements use the existing exclusive flushed temporary/rename.
SAF replacements use staged publication with a retained old document and
interruption recovery before the next read. Synthetic provider faults cover
write, rename, cleanup, failed rollback and retry. These are finite memory/file
checks, not a provider/device or whole-game compatibility claim. The retained
AOT release binary remains unchanged.

System progress additionally retains a half-open changed-byte span for each
block. On an existing valid Sys.sav, one changed CG/read byte writes one byte,
and one changed integer flag writes four bytes. Multiple pending edits coalesce
within their own block; successful spans are acknowledged independently and
failed spans remain retryable. This removes the former synchronous full-gallery
write of about 2 MB for a single new CG. No previous file payload is read during
these writes, and the original file size/layout and untouched bytes are retained.
Missing files still receive complete initialization. Older Storage adapters can
use the full-block fallback. Twenty-two own-memory cases and fifteen guards
check boundary bytes, range union, partial failures/retry, randomized final-byte
equality, unknown fields and isolated test directories. These are byte/I/O-policy
checks, not measured startup, frame rate or Android-provider performance.

The Android launcher separately stores its adapter settings in
`native-launcher.wa2cfg` under the selected root. Its small versioned/checksummed
record is distinct from game preferences. Queue serialization prevents an older
toggle write from overtaking a newer one, and failed writes retain dirty state
for retry before launch. An edit made during a pending write cannot be cleared
by that older acknowledgement. Existing root files beat the one-time private
switch fallback, while newly selected roots start with defaults. The launcher
passes a folder-bound immutable profile to the SDL activity; a restored task
cannot start SDL_main with a missing/mismatched launch profile. Only directory
selection/permission metadata stays in app preferences. Pure JVM checks cover
record corruption, narrow defaults, write retry and interleaved edits, with no
Android provider/activity launch. The retained AOT library does not link the
native-only game-directory implementation.

## Current released implementation

The current host is a mixed static recompilation and native subsystem port.
It is not a complete high-level source rewrite. Existing cached instruction C++
and CFGs generate shared native function bodies with direct control flow.
The same bodies are compiled for Windows x64 and Android ARM64. Production
hosts do not link a runtime instruction decoder or JIT.

Source ABI registers, the original 32-bit address layout, checked memory and
some exact floating-point state remain. They incur costs that a conventional
native engine need not have. Native resource, audio, numerical and selected
sprite/glyph functions replace measured work while preserving explicit input
contracts; unsupported inputs execute the existing checked static function or
raise an explicit unsupported error.

| Directory | Responsibility |
| --- | --- |
| `recompiled/` | Static function dispatch, ABI and native function entries |
| `generated/shared-aot/` | Private generated game function sources shared by both hosts |
| `generated/static-data/` | Private original initial memory, never host-executed code |
| `engine/` | Common resource, image, audio, math and text components |
| `platform/` | File, clock, window, input, device audio and presentation adapters |
| `hosts/` | Entry points using the same shared runtime |
| `android/app/` | Launcher, folder authorization and optional frame HUD |
| `native/`, `game/`, `engine/script_*` | Independent incomplete native migration core; not yet the released game entry |
| `engine/progress*`, `engine/scene_progress*` | Native system progress and script command handling |
| `platform/progress_storage*`, `platform/game_directory*` | Shared selected-directory persistence policy with filesystem/SAF adapters |

The admitted set currently contains 1,750 entries, 1,424 owning bodies and
122,950 instruction locations. There are 181 deferred entries and 13 remaining
unsupported transcendental sites. Complete gameplay is not verified. Consult
the locally generated manifest and verification reports for exact boundaries;
these numbers do not constitute a whole-game compatibility claim.

Callback discovery also checks the existing analysis database's references
against initialized pointer tables and active code. Missing virtual, cleanup
and formatting callbacks are recovered through exact cached templates or
already-decoded operands passed to the original offline emitter. A generated
supplement and its complete direct-call closure enter the same AOT inventory;
there is no host-specific callback implementation or runtime fallback. Catch
continuations must have an explicit cached control-flow proof as well.

The Android launcher initializes no game or SDL native library. It first checks
the selected folder in a background worker, persists a durable read grant and
passes the bounded PAK index to the game activity. The optional FPS HUD counts
successful game presents rather than Android UI refreshes.

The shared runtime observes the returns of five cached source audio functions
for the embedded main-story audio-only captions. Original bodies and source
ABI results remain intact; no generic instruction hook or executable patch is
added. Immutable caption data, timer selection, compact font decoding and
geometry are common engine code. The common graphics adapter overlays captions
after presentation into the existing letterboxed viewport. Only upstream
`script/sub.yaml` is imported; all SpecialContents subtitle data is excluded.
See `docs/SUBTITLES.md` for source scope and validation boundaries.

Decoded image caching retains up to 8 MiB/32 entries, with a 4 MiB per-entry
limit and repeat-use admission. Reused resources receive new mutable game
buffers; cached bytes remain private. The allocator can separately retain at
most two freed large blocks totaling 4 MiB. Cache hits preserve dictionary-tail
dependencies, final dictionary state and source diagnostics. Neither cache
stores an entire rendered frame or removes particles.
