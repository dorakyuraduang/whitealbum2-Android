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
scene accepts typed selection input; native selection presentation is pending.
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
Types 0/1/2/5 and the alternate snow atlas remain explicit unsupported cases.

The native story engine remains incomplete: the full title/selection/option UI,
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
