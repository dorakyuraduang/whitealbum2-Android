# Shared native migration and retained AOT release

On 2026-11-06 the user changed the target to a complete native engine
reimplementation. Windows and Android continue to share game logic, resources
stay external, and the existing executable-analysis/UI restrictions remain.
The final native application must not link generated game functions, source
CPU state, checked source-address memory, or the Windows compatibility runtime.
The current released APK still uses AOT; a linkable component is not a full port.

`native/CMakeLists.txt` is an independent build root for the common native scene,
script engine, resource/media components and native progress adapter. It never
includes `hosts/`, `recompiled/`, `generated/` or `port/`. Its link-check library
forces all existing native scene dependencies to resolve on Windows x64 and
Android ARM64. It has no launchable application and is never loaded by tests.
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
file adapter opens only Sys.sav in a host-supplied private directory. It preserves
manual saves/settings and writes only dirty original-format blocks (4 KiB for
system flags). Failed blocks remain dirty for retry. Whole-file/multi-block
atomicity and a complete scene-save snapshot are not claimed. A scene without
progress storage fails explicitly if it encounters a progress command.

The LSCR runtime/session now has bounded versioned checkpoints for globals,
locals, value/flow stacks, nested scene calls and suspended commands. Restore
checks resource content identity, catalogue signatures and instruction
boundaries before committing any state. These script checkpoints do not yet
include a complete rendered scene or audio transport snapshot.

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
kept separate. Device presentation and full scene snapshots remain pending.

SetWeather2/ChangeWeather2 now drive native type-6 particle growth and sway.
Snow density-selection flags 0x200/0x400 retain the cached RNG call sequence,
including overwritten values. Live particle count is tracked separately from
fixed slot indices, so density reductions cannot strand later active slots.
Native type-1 sakura uses three original flipbook atlases, distinct prefilled/
new-particle RNG order, original wrapping ranges and per-size animation timing.
Its curve/atlas phase is bounded by their joint period to avoid signed overflow.
Types 2/5 and the alternate snow atlas remain explicit unsupported cases.

The native Calendar command computes date/weekday state, retains the 2010..2013
holiday facts and selects the original three calendar image atlases. Its shared
state machine handles the background, moving digit/weekday crops and line
transitions, including the source's one-third skip timing. Display requests
outside the retained year table fail explicitly; date-only assignments remain
valid. Its overlay has a separate scene accessor for presentation after dialogue.
Calendar checkpoints reload resource handles without executing script commands.

`engine/sprite_animation.*` parses the original INAS sprite-animation containers
into native frame rectangles, texture names and timed sequences. The native
player supports nested repetition, finite/continuous passes, rational clock
rates, empty frames and bounded playback checkpoints. Unimplemented transforms,
blend overrides and events fail explicitly. Static checks parse the eight local
animation definitions and their texture headers; only synthetic sequences run.
LoadBmpAnime now owns native players/textures inside the common bitmap slots,
with asynchronous texture readiness, source-bound checks, placement, uniform
zoom, alpha/additive fades and independent-layer lifetime. Arbitrary destination
rectangle edits on animated slots still fail explicitly. Rain now uses the same
player, original trimmed frame offsets, a scale of two about the screen centre
and a foreground layer independent of its scope flag. Bitmap moves update the
source's automatic pivot before subsequent scale/rotation commands. Complete
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
when needed; hosts pass `Scene::dialogue_transform()` to dialogue sprite draws.
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

The native SDL canvas selects OpenGL on desktop and GLES2 on Android. Both use
one common mask-shader equation, with GLSL 120/100 preambles and the appropriate
precision declarations. This makes native mask transitions available through
the desktop GL renderer as well. The scene, canvas and mask adapters compile
on both toolchains; generated shader pairs are compiled/linked offline for
both profiles. A software reference still checks owned-memory pixel equations.
No actual GL context, window or hardware rendering is exercised by these checks.

The native story engine remains incomplete: the full title/option UI,
backlog integration, full scene snapshots and restore, lifecycle integration and
unsupported render/animation/weather modes still need native implementations
or integration. Reading-history APIs exist but are not yet connected to every
dialogue transition. No whole-game or device/performance claim follows from
these component checks. Existing generated C++/CFG is a development reference,
never a native-runtime dependency.

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
| `platform/progress_storage*` | Native private-save filesystem adapter |

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
