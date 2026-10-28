# TODO: optional High Quality (Demucs) stem engine

**Status: not implemented.** Deferred 2026-10-28 — the current Spleeter engine
is what ships. This document records the design and the measurements so the
work can be picked up without re-doing the research.

## Goal

Offer a second stem engine, like the speed/quality choice in Rekordbox:

| Engine | Model | Where it runs |
|---|---|---|
| **Fast** (default, shipped) | Spleeter 4-stem fp32, 4 x 38 MB | every machine, 9x realtime here |
| **Quality** (this TODO) | HTDemucs v4 fp32 ONNX, 302 MB | only machines that can keep up |

Quality is *not* expected to work on the 2017 MacBookAir7,2. It exists so that
anyone with a faster CPU can use it, and so the fork offers more than one
quality tier. It must be gated, never silently enabled.

## Measured behaviour (same laptop, 7.8 s HTDemucs segment)

RTF = wall seconds per audio second; 1.0 is realtime.

| Threads | Per segment | RTF | Realtime factor |
|---:|---:|---:|---:|
| 1 | 12.6 s | 1.61 | 0.62x |
| 2 | 9.2 s idle / 14.1 s warm | 1.18-1.81 | 0.55-0.85x |
| 4 | 13.8 s | 1.77 | 0.56x |
| Spleeter (reference) | 1.3 s / 11.9 s block | 0.11 | **9x** |

- Peak RSS while separating: **4.8-5.2 GB**. On an 8 GB machine this alone
  argues for keeping Quality off by default.
- With the 25% overlap every Demucs pipeline uses, whole-track RTF is 1.33x
  worse than the per-segment numbers above.
- Alternatives are worse on this hardware: `wavey-ai/stemsplits` (pure Rust)
  RTF 3.29-5.36, `nikhilunni/demucs-rs` (Burn) either panics on the Intel HD
  6000 (Metal GPUFamily macOS 1) or runs at RTF 36 on its CPU backend, and
  ORT's CoreML EP is OOM-killed. See `ai-stems-spleeter.md` for the full table.

## Packaging: on-demand download, not bundled

ONNX weights barely compress (measured on `spleeter_vocals.onnx`):

```
raw 39.4 MB -> deflate 36.5 MB (0.928) -> xz 36.2 MB (0.919)
```

So bundling HTDemucs fp32 adds ~280 MB to every artifact:

| Artifact | Today | With HQ bundled |
|---|---:|---:|
| macOS x86_64 DMG | 207.5 MB | ~490 MB |
| macOS arm64 DMG | 202.6 MB | ~485 MB |
| Windows x64 zip | 231.1 MB | ~510 MB |
| Linux deb | 166.2 MB | ~445 MB |

**Decision: do not bundle by default.** Download the model on first use:

- Installer size is unchanged.
- Enabling Quality costs **+302 MB** in the app data dir; "Remove model"
  reclaims it.
- Most users never enable it, and half of them cannot run it anyway.

fp16 would halve the download (158 MB, 147 MB compressed) but measured **16%
slower** on CPU (13.0 s vs 11.2 s) with 77 dB SNR — quality-safe, useless for
closing the speed gap. Use fp32.

## Storage: no stem cache

- Keep `StemCache` disabled. Raw float stems are ~420 MB per 5-minute track.
- If caching is ever revisited, store Opus 192k per stem (~34 MB per 5-minute
  track, 12x smaller). Make it opt-in and off by default.
- Quality therefore costs disk only for the model itself; stems are rebuilt in
  RAM each session, exactly as Spleeter does today.

## Design

1. **Setting** `stems.engine` = `fast` (default) | `quality`, plus a
   first-run probe result cached in settings as `stems.engine_rtf`.
2. **Model manager**: download `htdemucs.onnx` (302 MB) from
   `https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/main/htdemucs.onnx`
   into `.../Mixxx/models/`, verify a pinned SHA-256, show progress, allow
   deletion. Reuse the existing models directory so the app stays portable.
3. **Probe**: on first switch to Quality, separate one synthetic 7.8 s segment
   with 2 threads and store the RTF (takes ~10-30 s, once):

   | Measured RTF | Behaviour |
   |---|---|
   | <= 0.75 | "Quality (live)" |
   | 0.75 - 1.2 | selectable, warn that long sets may fall behind |
   | > 1.2 | disabled, show the measured figure |

   On this laptop that reads "0.6x realtime". Never auto-enable Quality.
4. **Engine**: restore the `HTDemucsEngine` that this fork replaced (it is in
   git history before the Spleeter work) as a second implementation behind the
   same `StemSeparator` interface. The stem buffer, mixer, skins, controller
   mapping and progressive worker are all stem-agnostic already — the Spleeter
   engine writes 4 stems into the same buffer, so nothing above the engine
   changes.
5. **UI**: a preferences entry (engine + model status + download/remove). No
   controller mapping, no per-deck switch: the engine is global.
6. **Licensing**: Demucs v4 / htdemucs is MIT; ship the model NOTICE alongside
   the Spleeter one and credit Meta Research in the README shoutouts.

## Verification before shipping

- [ ] CI benchmark on `macos-14` (arm64): RTF for `CPUExecutionProvider` and
      `CoreMLExecutionProvider`. If CoreML is 2-3x faster, Quality becomes
      genuinely live on M1-M4 and the gating thresholds may need revisiting.
- [ ] Test once on a Windows machine (planned by the maintainer).
- [ ] Confirm the probe result is not skewed by a cold page cache.
- [ ] Confirm the app still starts and plays when the model is absent or
      corrupt (fall back to Fast with a visible message).

## Deferred / optional

- A "full" installer flavour that bundles the model for offline gig laptops
  (+280 MB), for people who prep before a set and never connect.
- Cached stems, if ever wanted: Opus 192k, opt-in, off by default.
