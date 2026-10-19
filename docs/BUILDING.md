# Building and verification

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
