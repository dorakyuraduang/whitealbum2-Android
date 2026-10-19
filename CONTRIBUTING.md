# Contributing

Read [AGENTS.md](AGENTS.md), [the porting contract](config/porting_contract.json)
and [the architecture](docs/ARCHITECTURE.md) before changing code.

- Compile the same game logic and generated static function sources on every
  platform. Keep window, input, storage, device audio and presentation adapters
  outside game logic.
- Do not introduce an instruction decoder, interpreter fallback, JIT or a second
  Android story implementation. Unknown targets and unsupported effects must
  remain explicit.
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
