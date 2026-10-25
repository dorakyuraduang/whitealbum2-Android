# Shared static recompilation

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
| `game/`, `engine/script_*` | Earlier incomplete story experiments; not the production shared game entry |

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
