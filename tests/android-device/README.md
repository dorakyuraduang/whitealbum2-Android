# Android storage component test

This is an Activity-free instrumentation APK for a device explicitly connected
by its owner. It targets the installed WA2 UID using the retained development
signing certificate. It does not require root or a debuggable game package and
never launches a game Activity, touches its UI, captures a screen, or measures
gameplay FPS. Do not run it while the game is running: instrumentation owns the
target process during its test.

Build on Windows with the existing SDK/JDK and retained signing key:

```powershell
python scripts/build_storage_probe.py
```

Add `--native-build build/shared-aot-android` after configuring the normal shared
ARM64 build to include the native test. This reuses its common libraries and
unchanged generated static functions, builds with one hidden, low-priority job,
and does not change the production APK. `--target-package
org.wa2.port.nativeengine` is available only for the AOT replacement package
with that identity; do not use it against an experimental native engine.

After wireless pairing/connection, use an explicit device serial. Replace
`DEVICE` below with that serial. No pairing codes, IPs, grants or device reports
belong in the public repository.

```powershell
adb -s DEVICE install -r --no-streaming build/android-storage-probe/WA2-storage-probe.apk
adb -s DEVICE shell am instrument -w -r -e mode report org.wa2.port.storageprobe/org.wa2.probe.StorageProbe
adb -s DEVICE shell am instrument -w -r -e mode measure -e count 20 org.wa2.port.storageprobe/org.wa2.probe.StorageProbe
adb -s DEVICE shell am instrument -w -r -e mode native -e count 10 org.wa2.port.storageprobe/org.wa2.probe.StorageProbe
adb -s DEVICE uninstall org.wa2.port.storageprobe
```

Run those commands serially, hidden and below normal priority on the host. A
successful result has `passed:true` in its JSON and instrumentation result -1.
ADB's process exit code alone does not establish a successful test. Uninstall
only the probe after collecting its output; keep the game and its data intact.

`report` reads selected counters from the last saved `wa2-runtime.json`; check
its release and date because installing a new APK does not refresh that report.
`measure` compares repeated reads of the same small INI through the actual
installed adapter and a temporary private-cache fixture. It reports timings and
byte counts, never the INI contents or authorized URI. The adapter ordinarily
recovers interrupted publication on reads; the probe first rejects pending
publication sidecars in the root and legacy main-save path so it does not need
to recover them. There must be no concurrent game or external file writer.

From 0.2.53, `measure` also opens the same settled file address repeatedly through
the read-only prepared path, after one metadata request. Every iteration owns
a new descriptor and compares its bytes with the ordinary read. Invalid scheme
and tree references must fail before file access. This isolates redundant
directory resolution; it is not a whole-load speed comparison or a replacement
for the common core's frame/age/mutation snapshot guards.

`native` uses its own newly built common core, not the installed core. It calls
the unchanged static INI wrapper and the unchanged save-header reader, with
and without the batch policies. `count` defaults to 10 and is bounded to 100;
save slots start at zero. It checks identical
return sequences, counts actual provider queries/opens, and records elapsed
times. Save payloads still open and read anew. No original EXE is opened or run.

These results measure file components and a bounded source update only. They
do not measure clicking a slot, restoring an entire scene, resource decoding,
Android UI lifecycle, or whole-game compatibility.
