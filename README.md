# Mixxx Stems — real-time 4-stem separation

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-macOS%20(Intel)%20%7C%20Linux%20%7C%20Windows-lightgrey.svg)](https://mixxx.org)
[![Engine](https://img.shields.io/badge/AI%20Engine-Spleeter%204stems%20(ONNX)-orange.svg)](https://github.com/deezer/spleeter)
[![ONNX](https://img.shields.io/badge/Inference-ONNX%20Runtime%20C%2B%2B-blueviolet.svg)](https://github.com/microsoft/onnxruntime)

A fork of **[Mixxx](https://mixxx.org) 2.5.6** that separates a playing track into
**four stems in real time** — **vocals, instruments, bass, drums** — and lets you
mute, solo and re-mix them from the UI or from a Pioneer **DDJ-FLX4**.

Separation runs in the background, block by block, so you can press play a couple
of seconds after loading a track instead of waiting for the whole file.

---

## Download

<!-- downloads:start -->
| Platform | Build | Status |
|---|---|---|
| **macOS** (Intel, 10.15+) | [Mixxx-Stems-2.5.6-macOS-x86_64.dmg](https://github.com/ukirdeomkar/mixxx-spleeter-4stem/releases/download/v2.5.6-stems.1/Mixxx-Stems-2.5.6-macOS-x86_64.dmg) | tested |
| **Windows 10/11** (x64) | [Mixxx-Stems-2.5.6-Windows-x64.zip](https://github.com/ukirdeomkar/mixxx-spleeter-4stem/releases/download/v2.5.6-stems.1/Mixxx-Stems-2.5.6-Windows-x64.zip) | built by CI, untested |
<!-- downloads:end -->

All builds live under [Releases](https://github.com/ukirdeomkar/mixxx-spleeter-4stem/releases).
The four Spleeter models are bundled in every build - nothing to configure.

### macOS installation

1. Pick the build for your Mac - **Apple Silicon** (`arm64`) for M1/M2/M3/M4,
   **Intel** (`x86_64`) for older Macs, which also runs on Apple Silicon under
   Rosetta 2. Both require macOS 11 or newer.
2. Open the `.dmg` and drag **Mixxx** into **Applications**.
3. First launch: **right-click the app -> Open** (it is ad-hoc signed, not notarised).
   If macOS refuses, use *System Settings -> Privacy & Security -> Open Anyway*,
   or clear the quarantine flag:

   ```bash
   xattr -dr com.apple.quarantine /Applications/Mixxx.app
   ```

4. Launch. Models ship inside `Mixxx.app/Contents/Resources/models`, so the
   project folder is not needed.

### Windows installation

1. Download the `.zip` and **right-click -> Extract All** (do not run anything
   from inside the archive - Windows needs the DLLs and `models/` folder
   alongside `mixxx.exe`).
2. Open the extracted folder and run **`mixxx.exe`**.
3. If SmartScreen appears: **More info -> Run anyway** (the build is unsigned).
4. Keep the folder contents together: `mixxx.exe`, `onnxruntime.dll`,
   `fdk-aac.dll`, `models/`, `controllers/`, `skins/`, `platforms/`, ...

### Linux installation

1. Install the `.deb` (Ubuntu 22.04+ / Debian 12+):

   ```bash
   sudo apt install ./Mixxx-Stems-*-Linux.deb
   ```

2. The models are installed to `/usr/share/mixxx/models`, so the app finds them
   automatically. Launch `mixxx` from your application menu or a terminal.
3. This build has FFmpeg disabled (licensing), so exotic container/video formats
   may not load. Audio formats (MP3/AAC/FLAC/OGG/WAV) are fine.
4. The package's dependency list is inherited from Mixxx and may need a
   `sudo apt --fix-broken install` on minimal systems.

### First run (all platforms)

1. **Sound Hardware**: pick your controller/output device (e.g. `DDJ-FLX4`) as
   output, and its inputs for the headphone cue.
2. **Controllers**: the `Pioneer DDJ-FLX4` mapping should be detected; enable it
   and load the built-in `Pioneer DDJ-FLX4` mapping if nothing is selected.
3. Load a track - separation starts immediately. You can press play after a
   couple of seconds; the first region switches from the DSP split to real stems
   as soon as it is ready.
4. **FLX4 pads**: press the **SAMPLER** pad-mode button. Pad 1 vocals, 2
   instruments, 3 bass, 4 drums; `SHIFT`+pad solos; pads 5-8 are free.

---

## Shoutouts

This project is standing on a lot of other people's work. Go star these:

- **[Mixxx](https://github.com/mixxxdj/mixxx)** — the DJ software this is built on.
  Mixxx is GPL v2+, and so is this fork. None of this exists without the Mixxx
  team's engine, audio pipeline, skins and controller framework.
- **[dromangdev/mixxx-stems](https://github.com/dromangdev/mixxx-stems)** — the fork
  this one started from. It brought stem separation into Mixxx (HTDemucs v4 /
  HTDemucs-FT, ONNX Runtime), plus the whole-track stem buffer, the per-stem
  controls, the DSP isolator and the first stems UI. This fork replaces its
  engine and extends it to four stems.
- **[Deezer Spleeter](https://github.com/deezer/spleeter)** — the model architecture
  and the 4stems weights (MIT). Fast and small enough to run on a 2017 dual-core
  laptop, which is the entire reason this fork exists.
- **[Best-Practice/spleeter-4stems-onnx](https://huggingface.co/Best-Practice/spleeter-4stems-onnx)**
  — the ONNX export of Spleeter 4stems used here (Apache-2.0). Their model card is
  also the reference for the STFT / mask / band-extension contract, including the
  ELU gotcha and the `average` vs `zeros` band extension.
- **[sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx)** — the original PyTorch
  port of Spleeter's U-Net that the ONNX export is derived from.
- **[ONNX Runtime](https://github.com/microsoft/onnxruntime)** (MIT) and
  **[FFTW](https://www.fftw.org/)** (GPL) — inference and the FFT behind the
  STFT/iSTFT frontend.

---

## Features

- **Four stems**: vocals, instruments ("other"), bass, drums.
- **Progressive separation**: the first model block lands after ~2–4 s; after that
  separation runs about **5× faster than playback**, so it stays ahead of the
  playhead while you mix.
- **Honest fallback**: regions that are not separated yet use a zero-latency
  4-way Linkwitz-Riley DSP split (unity-sum), and the deck switches to the real
  AI stems as soon as the region is ready.
- **FLX4 pads**: pad mode **SAMPLER** is repurposed for stems —
  pad 1 vocals, 2 instruments, 3 bass, 4 drums, `SHIFT`+pad solos,
  pads 5–8 unbound and reserved. Pad LEDs follow the stem state.
- **On-screen control strip** above the track title, same order as the pads.
- **No disk cache**: raw float stems would be ~420 MB per five-minute track;
  separation is fast enough that caching is not worth the disk.

Measured on the development machine (MacBookAir7,2 — i5-5350U 2C/4T, 8 GB,
macOS 12.7.6), per 11.82 s block: **2.0–2.3 s** in steady state, 3.8 s for the
first block including warm-up.

---

## Requirements

- macOS 10.15+ (developed on 12.7.6, Intel). Linux/Windows build steps are
  inherited from Mixxx and untested here.
- The four ONNX models, ~38 MB each:
  `spleeter_{vocals,drums,bass,other}.onnx` from
  [Best-Practice/spleeter-4stems-onnx](https://huggingface.co/Best-Practice/spleeter-4stems-onnx).
  Place them in `~/Library/Application Support/Mixxx/models/`, next to the
  executable in `models/`, or in the app bundle's `Resources/models/`.

## Building (macOS)

Mixxx's prebuilt dependency bundle is used, so only a matching toolchain is needed.

```bash
# dependencies (x64-osx-min1100 bundle for Mixxx 2.5)
source tools/macos_buildenv.sh setup

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$MIXXX_VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DQT6=ON \
  -DQML=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_BENCH=OFF \
  -DOPTIMIZE=native \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX12.1.sdk
cmake --build build --parallel 2
./build/mixxx
```

Notes that cost real time to discover:

- **Use the macOS 12.1 SDK.** 11.3 lacks `std::bit_ceil`; 12.3+ and 13.x ship a
  `<concepts>` header that Qt 6.5's `moc` cannot parse
  (`namespace ranges::inline __cpo`).
- `-DQML=OFF` is fine — the LateNight skin is legacy XML.
- `-DBUILD_BENCH=OFF`, otherwise CMake demands unit tests.
- `-DOPTIMIZE=native` matters for local use: the i5-5350U is Broadwell. Do
  **not** publish a `native` build - it bakes in AVX2 and crashes on older
  Intel Macs and on Rosetta without AVX2. Release builds use `portable`.

Set `MIXXX_DEBUG_SPLEETER_STEMS=1` to log per-block residual and timing; the four
masks sum to one, so `sum(stems) - mix` must be very negative (~−135 dB measured).

## Stem controls

| Control | Meaning |
|---|---|
| `[ChannelN],stem_vocals_enabled` / `stem_inst_enabled` / `stem_bass_enabled` / `stem_drums_enabled` | mute/unmute |
| `[ChannelN],stem_vocals_volume` / `stem_inst_volume` / `stem_bass_volume` / `stem_drums_volume` | per-stem gain |
| `[ChannelN],stem_vocals_solo` / `stem_inst_solo` / `stem_bass_solo` / `stem_drums_solo` | solo |
| `[ChannelN],stem_reset` | unmute everything |
| `[ChannelN],stems_source` | 1 = AI stems, 0 = DSP split only |

The "instruments" stem keeps the upstream `stem_inst_*` names so existing skins
and mappings keep working.

Every bundled skin — LateNight, Deere, Tango and Shade, including the
"(64 Samplers)" variants — carries the stem strip above the deck transport:
`VOC | S  INST | S  BASS | S  DRUM | S  ALL  AI`. Click a stem to mute/unmute
it, hold `S` (or right-click the stem button) to solo, `ALL` unmutes everything
and the last button switches between `DSP` (instant) and `AI` (Spleeter).

## Known limitations

- Only one separated deck is comfortable on 8 GB of RAM (~420 MB per deck plus
  ~1.5 GB of ONNX Runtime).
- Seeking far ahead of the separation front falls back to the DSP split for a few
  seconds while the worker catches up. For a clean recording take, let the track
  load for a moment first — the recording captures whatever the deck is
  currently outputting.
- The FLX4 mapping covers decks 1 and 2 (the two physical deck sides).
- Separation quality is Spleeter's: well below Demucs, but it runs on this
  hardware and Demucs does not.

## License

GPL v2 or later, inherited from [Mixxx](https://github.com/mixxxdj/mixxx). See
`LICENSE`. The Spleeter weights are MIT (Deezer); the ONNX export is Apache-2.0.
