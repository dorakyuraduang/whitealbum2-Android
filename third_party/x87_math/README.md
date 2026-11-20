# Scalar x87 arithmetic

Pinned arithmetic sources from [Bochs](https://github.com/bochs-emu/Bochs) revision
`b083c0c2515796254de31556989fe4b7b765ddc4`, including its SoftFloat 3e fork.
`UPSTREAM.json` records original paths and content hashes. The imported files
are unchanged. `config.h` supplies the integration's fixed-width integer types
and compiler features; `library.cmake` builds the same scalar sources everywhere.

Only arithmetic sources and tables are included. There is no CPU implementation,
instruction decoder, device model, or emulator executable. The production library
does not execute native x87 instructions or use the host's libm/long double for
these transcendental operations. Native instructions exist only in the finite
Windows memory oracle in `tests/transcendental.cpp`.

The x87 elementary operations always use extended precision for their results;
the caller's rounding control and sticky exception state remain observable.
See [Intel's architecture manual, volume 1](https://cdrdv2-public.intel.com/851056/253665-087-sdm-vol-1.pdf)
and the upstream implementation. The checked instruction set and measured scope
are recorded in `config/special_native_contracts.json`. The finite hardware
comparison is not an exhaustive proof for all bit patterns or processor models.

The imported code carries SoftFloat's BSD-style and Release 2b notices and LGPL
terms identified in the files. Retain those headers. `NOTICE.txt` combines the
notices and LGPL 2.1 text for the delivered binaries; `LICENSE-LGPL-2.1.txt` retains
the upstream license. The Android packager embeds the notice and verifies its
bytes; the Windows delivery places the notice beside its executable. Source and
rebuild integration are included here.
