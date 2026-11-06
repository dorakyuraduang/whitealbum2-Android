# Contributing

Read [AGENTS.md](AGENTS.md), [the porting contract](config/porting_contract.json)
and [the architecture](docs/ARCHITECTURE.md) before changing code.

- The migration target is one native engine for all platforms. Compile the
  same game logic on Windows and Android; keep OS and device adapters outside
  game logic. The retained AOT release must still share its generated sources.
- Native migration targets must not link recompiled functions, guest CPU state,
  guest memory, machine-instruction interpreters or JITs. The original LSCR game
  script language is handled by the common native engine. Unknown commands and
  unsupported effects remain explicit; never add a second Android game engine.
- Use existing generated C++/CFG and authorized cached inputs. Opening, hashing,
  decoding, disassembling or running the original executable is outside the
  current development contract.
- Run one hidden, below-normal-priority build or bounded memory check at a time.
  Do not launch the game, an emulator, UI automation, screen capture or an FPS
  pressure test under the current contract.
- Add a meaningful bounded correctness comparison for behavioral or numerical
  changes. Report cold/repeated inputs, allocation/copy costs, fallback coverage
  and memory limits when reporting a performance improvement.
- Preserve full particle counts, ordering, aspect ratio, audio ownership and
  source function contracts. A component speedup does not establish phone FPS.
- Submit source and synthetic fixtures only. Never submit PAKs, EXEs, initial
  game RAM, cached code translations, private logs, signing keys, credentials or
  player saves. Check the staged files before committing.

Describe the trigger, resulting behavior and validation in a pull request.
State device/gameplay verification separately from compilation and memory
checks. Contributions to newly authored project files use the root MIT license;
preserve dependency licenses and do not submit code you cannot license.
