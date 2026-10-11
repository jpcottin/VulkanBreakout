# Vulkan Breakout

[![CI](https://github.com/jpcottin/VulkanBreakout/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/jpcottin/VulkanBreakout/actions/workflows/ci.yml)

<details>
<summary><b>CI details</b> — host tests, native tests + smoke matrix, API 34 → 37.1, plus Android CLI and Emulator Preview legs</summary>

| Legs | Image | Emulator channel | GPU | Gating |
|---|---|---|---|---|
| Host tests | host build, no emulator | — | — | ✅ blocking |
| Native tests: API 34, 36 | `default` x86_64 | stable | swiftshader / auto | ✅ blocking |
| Smoke: API 34, 36 | `default` x86_64 | stable | swiftshader / auto | ✅ blocking |
| Smoke: API 37.0 | `google_apis_ps16k` (16 KB page size) | stable | lavapipe | non-blocking |
| Smoke: API 37.0 | `google_apis_ps16k` | canary (`--channel=3`) | lavapipe, auto | non-blocking |
| Smoke: API 37.1 | `google_apis_ps16k` | canary | lavapipe, auto | non-blocking |
| Android CLI experiment | `google_apis_ps16k` 37.0 | canary | emulator default | non-blocking |
| Emulator Preview (`emulators;latest`) | `google_apis_ps16k` 37.0 | preview package | auto | non-blocking |
| Emulator Preview multi-run (snapshots) | `google_apis_ps16k` 37.0 | preview package | auto | non-blocking |
| Android CLI multi-run (snapshots) | `google_apis_ps16k` 37.0 | canary | emulator default | non-blocking |

The Android CLI leg drives the whole flow with the [`android` CLI](https://d.android.com/tools/agents/android-cli) (`android sdk install --canary`, `android emulator create/start/stop`) instead of `sdkmanager`/`avdmanager` and the emulator-runner action.

All emulator-runner legs use full diagnostics (`-verbose -show-kernel -debug-metrics -metrics-collection`) and a `cmdline-tools;latest` update so `avdmanager` writes a valid `target=android-37.x` (the runner's preinstalled version writes `android-0`, which the emulator clamps to API 3, disabling the Vulkan/GLDirectMem auto-enable the ps16k images need).

</details>

A 2D **Breakout-like** game for **Android**, rendered with **Vulkan**.

Slide the paddle left and right, keep the ball in play and break the wall of
coloured bricks, across ten increasingly difficult levels. Sibling project of
[VulkanSpaceInvaders](https://github.com/jpcottin/VulkanSpaceInvaders) and
[VulkanAsteroids](https://github.com/jpcottin/VulkanAsteroids) — same
hand-written renderer, procedural audio engine, AI-Glasses bridge and CI
architecture, with a brand-new game on top.

> **Status:** this is the project skeleton — engine, build, tests and CI, with
> a title screen and the Settings overlay. The gameplay, the Auto Play
> autopilot and the AI-Glasses mode land in the next pull requests.

## Gameplay

| Control | Action |
|---------|--------|
| Hold the **strip below the paddle** (bottom ~15% of screen) | The paddle slides toward your finger while pressed |
| Tap anywhere | Launch a ball resting on the paddle |
| Tap the **gear icon** (top-right) | Open Settings from any screen |

- **Goal:** break every brick in the wall before you run out of balls.
- **Progression:** 10 levels, easy to hard — denser walls, tougher bricks,
  unbreakable steel blocks in the way and a faster ball.
- **Bricks:** one bright colour per row so the wall reads at a glance; bricks
  that take more than one hit show an inner core per hit left.
- **Lives:** 5 balls per game.
- **Settings:** gear icon opens an overlay from any screen. Toggles: Sound
  on/off. Persists across app restarts.

## Tech

- **Pure native C++17** — Android
  [`NativeActivity`](https://developer.android.com/ndk/reference/group/native-activity)
  with `native_app_glue`; the only production JVM code is the thin AI-Glasses
  bridge (`GlassesBridge.kt`), plus the instrumented smoke test.
- **Hand-written Vulkan 1.0 2D renderer** — one graphics pipeline, one vertex
  buffer holding two shapes (a quad and a disc), per-instance transform +
  translation + RGBA + fill style, FIFO present, two frames in flight.
  Consecutive draws of the same shape batch into one instanced draw call.
- **Everything is geometry, no textures** — bricks are bevelled quads, the
  paddle a rounded bar, the ball a disc under a radial-falloff glow. Stroke
  vector font for text; 7-segment font for HUD numbers.
- **Procedural audio via [Oboe](https://github.com/google/oboe)** — an 8-voice
  synth on a low-latency mono float stream: paddle blip, wall tick, brick
  click, pitched brick-break pop, ball-lost slide, level-clear and extra-life
  jingles, pickup sparkle, and a 92 BPM ambient bed (pad + bass + arpeggio).
  No audio files.
- **GLSL → SPIR-V** compiled at build time with the NDK's `glslc` (`-mfmt=c`)
  and `#include`d directly as C arrays — no runtime shader compiler, no assets.
- **Adaptive to foldables** — the wall is laid out for the surface aspect
  ratio: fixed-size bricks, as many columns as fit. The renderer re-checks the
  surface extent every frame: when a fold/unfold resizes the window in place
  (the activity survives via `configChanges`), the swapchain is rebuilt at
  the new resolution instead of letting the compositor stretch the old one.
- **AI Glasses (Android XR projected)** — the glasses expose a secondary
  display (`ProjectionDisplay`, 30 Hz) owned by a virtual device. A ~70-line
  Kotlin bridge (`GlassesBridge.kt`) uses `androidx.xr.projected` to observe
  pairing and to launch `GlassesGameActivity` — a second `NativeActivity`
  declared with `requiredDisplayCategory="xr_projected"` — onto that display.
  The native side calls the bridge over JNI and detects its role from the
  activity class: on the glasses the clear colour is pure black (transparent
  on additive AR lenses), the swapchain is low-latency (minimal image count,
  one frame in flight, MAILBOX present when available) and the game renders
  into a centered quarter-size viewport.
- **Runtime diagnostics via Logcat** — every launch logs a full Vulkan
  extension audit (`✓ USED` / `~ PRESENT` / `✗ ABSENT` / `? UNKNOWN`) for
  instance and device extensions, and a periodic FPS line (frames/s, average
  frame time, draw calls) every 5 s. Filter with `adb logcat -s Breakout`.
- `minSdk 24` (Vulkan requires API 24+), AGP 9, NDK r29, CMake 3.22.

## Build & run

```bash
# Build
./gradlew assembleDebug

# Install and launch with the Android CLI
android run --apks app/build/outputs/apk/debug/app-debug.apk

# Or with adb
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.jpcottin.vulkanbreakout/android.app.NativeActivity
```

Requires a device with a Vulkan driver (API 24+).

## Testing

### Native unit tests (Google Test)

15 tests covering the wall layout (columns follow the screen width), the
title demo (ball stays inside the walls, huge-frame dt clamp), the settings
state machine (gear tap, sound toggle, back button, no gear inside Settings,
persistence across instances, reload-from-disk handoff), the AI-Glasses
plumbing (no gear on the glasses, pure-black clear) and render coverage of
every reachable state (output present, every shape inside the screen).

They build and run on the host in a few seconds — no device, no emulator:

```bash
scripts/host-tests.sh                              # all tests
scripts/host-tests.sh --gtest_filter='Settings.*'  # a subset
```

The same binary runs on a connected device or emulator through Gradle:

```bash
# ARM device (default)
./gradlew runNativeTests

# x86_64 emulator
./gradlew runNativeTests -PtestAbi=x86_64

# Several devices connected? Pin one:
ANDROID_SERIAL=emulator-5556 ./gradlew runNativeTests
```

### Instrumented smoke test

Launches the `NativeActivity` on a connected device, waits for the renderer
to present its first frame, asserts the activity is still `RESUMED`, and
captures a screenshot:

```bash
./gradlew connectedAndroidTest
```

## CI/CD

GitHub Actions runs on every push and pull request to `main`. The first four
jobs gate merges. The last four explore newer Android emulator tooling and are
marked `continue-on-error`, so a preview package that moves underneath us
reports its findings without ever blocking a PR.

| Job | What it does | Artifacts |
|-----|-------------|-----------|
| **Build APK** | Compiles the debug APK | `debug-apk` |
| **Host tests** | Builds `game.cpp` and the Google Test suite with the host compiler and runs it | — |
| **Native Tests** | Runs the same 15 Google Test cases on x86\_64 emulators (API 34 + API 36) | — |
| **Smoke Test** | Runs the Android instrumented test on x86\_64 emulators and captures an in-game screenshot via `UiAutomation`. The test asserts the activity is RESUMED **and** that the renderer logged `First frame presented`, so a dead Vulkan path fails loudly. Blocking on API 34 + 36; non-blocking legs on API 37.0 and 37.1 (`google_apis_ps16k`, 16 KB pages) across the lavapipe and auto GPU backends, from both the stable and canary channels | `smoke-screenshot-api*`, `smoke-test-results-api*`, `smoke-logcat-api*` (suffixed per leg) |
| **Android CLI experiment** | Drives the same instrumented test through the `android` CLI — SDK install, AVD creation, boot and teardown — instead of `sdkmanager`/`avdmanager` plus the emulator-runner action | `cli-smoke-*` |
| **Emulator Preview** | Boots the Android Emulator Preview package (`emulators;latest`, which installs alongside the stable emulator under `emulators/latest/`) and runs the instrumented test against it | `preview-smoke-*` |
| **Emulator Preview multi-run** | Four boot cycles against the same AVD with quickboot snapshots enabled: each cycle plays the game briefly, screenshots it, then shuts down so the emulator saves its snapshot. Checks whether a live Vulkan app survives snapshot save/restore | `preview-multirun-screenshots`, `preview-multirun-emulator-logs` |
| **Android CLI multi-run** | The same four-cycle snapshot experiment driven entirely by the `android` CLI (`emulator start` / `stop`, `run`, `screen capture`, `layout`) against the canary emulator | `cli-multirun-screenshots`, `cli-multirun-logs` |

The preview jobs share their setup through the composite action in
`.github/actions/preview-emulator`, which installs the system image, creates
the AVD, installs the preview emulator and its host dependencies.

### Replaying the Emulator Preview job locally

Pushing to see what a preview emulator does is a slow way to iterate, so the
multi-run job can be replayed on a local machine in a few minutes:

```bash
scripts/replay-preview-multirun.sh            # 4 cycles, throwaway AVD, cleaned up after
scripts/replay-preview-multirun.sh -n 2 -k    # 2 cycles, keep the AVD for inspection
scripts/replay-preview-multirun.sh -h         # options
```

It needs `emulators;latest`, the API 37.0 `google_apis_ps16k` system image and
KVM. Every `adb` call is pinned to the emulator it launches and shutdown is
scoped to that emulator's process group, so it is safe to run while other
devices or emulators are attached.

## License

[Apache License 2.0](LICENSE).
