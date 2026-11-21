# Project constraints from the user

- All platforms must compile the same game logic. During migration, existing
  AOT hosts must still share their generated static sources. Platform-specific
  game implementations are forbidden.
- Keep OS, window, input, device audio, resource access and presentation adapters
  separate. Different compilers/backends may compile the common sources.
- On 2026-11-08 the user switched the primary delivery back to the shared AOT
  implementation. Retain the native migration sources as experimental work;
  do not delete them or present their component builds as a complete game.
  Independent native targets must still exclude recompiled functions, guest
  CPU state and guest-memory runtime. All AOT hosts use the same static sources.
- Use existing generated C++/CFG and cached analysis. Do not open, hash, decode,
  disassemble or execute WA2_chs.exe unless the user changes this restriction.
- On 2026-11-20 the user authorized static analysis and recompilation of
  WA2_Special_cn.exe from WHITE ALBUM2 Special Contents. This exception applies
  only to the SHA-bound Special Contents profile, not WA2_chs.exe. Do not
  execute either original program or launch a game during this work.
- On 2026-11-21 the user authorized analysis of the supplied English Special
  patch ZIP and its SHA-bound WA2_Special_en.exe. Its identical code must reuse
  the existing Special static sources; localized image data are shared by hosts.
  The Main executable restriction and game-launch prohibition still apply.
- Do not monitor/read/capture the screen or operate game UI. Do not launch the
  game, an emulator or a Windows FPS/pressure test. Memory-only checks are allowed.
- Builds run below normal priority, hidden, with one job; no concurrent heavy
  builds or tests. Keep original PAK resources external, in the user-selected
  Android folder. Its name is unrestricted (updated 2026-11-08).
- Later on 2026-11-13, after the 0.2.91 regression, the user changed performance
  validation back to the authorized Android phone. Compare baseline/candidate
  in the same scene and settings using on-device FPS/frame-time counters and
  logs. PC component timings are auxiliary and cannot establish Android gains.
  Record device validation as pending when a comparable run is unavailable;
  do not turn that into a mobile performance claim. Game launch and UI remain
  user-controlled; do not read/capture the screen or operate game UI. Finite
  hidden PC component checks remain authorized as auxiliary checks, never a
  full-game pressure test.
- After each new APK build, install the verified package in place on the
  authorized Android device through ADB and verify its installed version and
  package hash. Preserve application data. If the device is unavailable, report
  installation as pending. Do not launch the app, operate game UI or read the
  screen as part of installation (updated 2026-11-09).
- Follow config/porting_contract.json. Report unknown targets and unsupported
  behavior explicitly. Component tests do not prove whole-game compatibility.
- After each completed change and its applicable validation, commit the intended
  source/documentation changes and push to the wa2-godot remote at
  https://github.com/dorakyuraduang/wa2-godot, normally on main unless the user
  specifies another branch. Verify the remote commit. If a push fails, retain
  the local commit and explicitly report the failure. Keep game resources,
  private analysis caches, credentials and build artifacts out of public source.
