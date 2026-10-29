# TODO: Android (APK) build

**Status: not implemented. Deferred 2026-10-29.** There is no Android build in
this repository, and one cannot be produced from the current 2.5.6 tree. This
document records why, so the question does not need re-investigating.

## The claim to check

`dromangdev/mixxx-stems` (the repo this project was forked from) advertises
Android in its README: a Platform badge, a "Building for Android" section with
a `-DCMAKE_SYSTEM_NAME=Android` command, NNAPI support and "APK Bundling".

What that repository actually contains:

| Claim | Reality |
|---|---|
| Android build | **0** Android files in the tree: no `AndroidManifest.xml`, no `packaging/android`, no Gradle |
| APK | **0** releases, so no APK was ever published |
| Native code | Only the ONNX Runtime AAR download and `__ANDROID__` branches in `stemseparator.cpp` (NNAPI include, `assets:/models` unpack) |
| Build command | Would fail at configure: the base (Mixxx 2.5.6) requires PortAudio and PortMidi, neither of which builds for Android |

So the README section is aspirational, copied from ONNX Runtime's cross-build
notes. The platform layer was never written.

## Where Android support actually lives

Android support was merged into **Mixxx itself**, not into the fork:

| Fact | Value |
|---|---|
| PR | [mixxxdj/mixxx#15565](https://github.com/mixxxdj/mixxx/pull/15565) "feat: add support for Android", merged **2025-11-22** |
| Size | 41 files, +1977/−93 |
| Branch | `main` only, version **2.7.0** (unreleased) |
| `2.6` branch | No `packaging/android`, no QML skin — Android is not there |
| Latest release | 2.5.6 (2026-03-27); **no released Mixxx has Android** |
| Artifact | CI matrix entry "Android 15 arm64" on `ubuntu-24.04`, vcpkg `arm64-android`, `tools/android_buildenv.sh`, APK at `build/android-build/build/outputs/apk/release/*.apk` |
| Audio | Oboe / OpenSLES (`PA_USE_OBOE`) instead of PortAudio's desktop APIs |
| UI | The new **QML skin** (`LateNightQML`), launched with `--qml` / `--new-ui` |
| Maturity | Open issue [#17059](https://github.com/mixxxdj/mixxx/issues/17059) "Android: make the tablet/Chromebook build usable" (2026-09-10) — installability, storage permissions, screen handling still being fixed. Target is tablets/Chromebooks, landscape |

That PR depends on 2.6/2.7 refactors (QML UI, `core services`, new controller
enumeration), which is why it cannot simply be cherry-picked onto 2.5.6.

## It is not a skin problem

The natural idea — "just add a LateNight variant for Android" — does not work
in either direction:

- **2.5.6 has no QML skin.** All skins in this tree (`LateNight`, `Deere`,
  `Tango`, `Shade`) are QWidget XML. Android's supported UI is the QML skin,
  which first appears on `main` (2.7).
- **A skin needs an app.** There is no Android platform layer in 2.5.6 to load
  a skin: no manifest, no permissions, no storage/asset paths, no Android audio
  backend. A skin port would sit on top of a port that does not exist.

So Android requires, in order: platform support → QML UI → stems UI in QML.
Our four XML stem strips are the last step, not the first.

## The FLX4 blocker

Even a successful APK would not control a DDJ-FLX4 today:

```cmake
default_option(PORTMIDI "Enable the PortMidi backend for MIDI controllers" "NOT ANDROID")
```

MIDI is **disabled by default on Android**. The Android PR added USB HID/bulk
support (`src/controllers/android.cpp`, HID/bulk enumerators), but the FLX4 is
a MIDI-class device. A libremidi backend is an open WIP pull request
([#16135](https://github.com/mixxxdj/mixxx/pull/16135), updated 2026-09-01).

Until that lands, Android means touchscreen DJing only — which defeats the
purpose of this fork's controller integration.

## What a real APK would cost

1. **Rebase the stems work onto `main` 2.7-dev.** Our integration touches ~54
   files (engine buffer, channel mixer, controls, worker, preferences, skins,
   controllers). The 2.7 engine/QML APIs have moved since 2.5.6, so this is a
   re-apply, not a merge.
2. **Recreate the stem strips in the QML skin.** The XML work in `LateNight`,
   `Deere`, `Tango` and `Shade` does not carry over to `LateNightQML`.
3. **Ship the models in the APK** (or download them, as the HQ TODO proposes).
   Four Spleeter models are 152 MB raw and ONNX barely compresses
   (0.928 measured), so with Qt and ORT the APK lands around
   **200–240 MB** — at or over Google Play's 200 MB base limit, so a GitHub
   side-load is the realistic distribution; Play would need asset packs.
4. **Mobile build deps:** FFTW3f and ONNX Runtime for `arm64-v8a` (the AAR
   exists), 2–4 thread limits, thermal throttling.
5. **Memory:** four float stem buffers are ~420 MB per deck at 44.1 kHz, plus
   ORT. Fine on an 8 GB flagship, not on a 4 GB phone.
6. **Performance is unmeasured.** Spleeter does 9x realtime on the 2017 Air;
   a modern phone SoC is plausibly faster, but big.LITTLE scheduling and
   thermal limits make that a guess until benchmarked on a device.

## CI plan, when it is time

CI cannot supply the missing platform layer, but once the port exists the
build is straightforward and belongs in the existing release workflow:

- Job: `Android 15 arm64`, `ubuntu-24.04`, `-DCMAKE_SYSTEM_NAME=Android`,
  `-DQT6=ON -DQML=ON -DHID=ON`, vcpkg triplet `arm64-android`, NDK via
  `tools/android_buildenv.sh`.
- For this fork: `workflow_dispatch` only, unsigned/debug-signed APK uploaded
  as an artifact. Signing secrets (`ANDROID_SIGNING_KEYSTORE_BASE64`,
  `ANDROID_SIGNING_PASSWORD`) only if it ever becomes a published asset.
- Do **not** add the job before the port: it would fail at configure on
  `find_package(PortAudio REQUIRED)` and pollute the workflow.

## Verification checklist (after a port)

- [ ] APK installs on a phone/tablet and starts with the QML UI.
- [ ] Audio output through Oboe at usable latency.
- [ ] Library scan finds music (READ_MEDIA_AUDIO on Android 13+).
- [ ] FLX4 recognised and mapped — **blocked on libremidi**.
- [ ] Spleeter separation RTF measured on device; confirm it beats realtime.
- [ ] Memory stays under the device limit with one separated deck.
- [ ] App falls back to the DSP split when the models are absent.

## Decision

Defer. Revisit when all three hold:

1. Mixxx **2.7 is released** with Android support (so the rebase target is
   stable, and #17059's usability fixes are in).
2. **libremidi lands**, so the FLX4 works on Android.
3. A phone benchmark shows Spleeter keeping up, which makes the APK worth the
   200 MB and the port.

Until then the product stays desktop: Spleeter live, FLX4 on decks 1–2.
