# Project constraints from the user

- All platforms must compile the same game logic. During migration, existing
  AOT hosts must still share their generated static sources. Platform-specific
  game implementations are forbidden.
- Keep OS, window, input, device audio, resource access and presentation adapters
  separate. Different compilers/backends may compile the common sources.
- The user changed the migration target on 2026-11-06 to a complete shared
  native engine reimplementation with no recompiled game functions, guest CPU
  state or guest-memory runtime in the final application. Retain the existing
  AOT release as a reference until native replacements are ready. The native
  story host is incomplete; component builds are not complete game ports.
- Use existing generated C++/CFG and cached analysis. Do not open, hash, decode,
  disassemble or execute WA2_chs.exe unless the user changes this restriction.
- Do not monitor/read/capture the screen or operate game UI. Do not launch the
  game, an emulator or a Windows FPS/pressure test. Memory-only checks are allowed.
- Builds run below normal priority, hidden, with one job; no concurrent heavy
  builds or tests. Keep original PAK resources external, under Wa2Res on Android.
- Follow config/porting_contract.json. Report unknown targets and unsupported
  behavior explicitly. Component tests do not prove whole-game compatibility.
- After each completed change and its applicable validation, commit the intended
  source/documentation changes and push to the wa2-godot remote at
  https://github.com/dorakyuraduang/wa2-godot, normally on main unless the user
  specifies another branch. Verify the remote commit. If a push fails, retain
  the local commit and explicitly report the failure. Keep game resources,
  private analysis caches, credentials and build artifacts out of public source.
