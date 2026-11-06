# Optional High Quality (Demucs) stem engine

**Status: implemented as an offline workflow (2026-11-05).** Real-time playback
still uses the Spleeter engine; HT-Demucs runs once per track from the track
context menu and the result is stored on disk.

## What shipped

| Piece | Where |
|---|---|
| Offline engine (HT-Demucs v4, single-file ONNX export) | `src/engine/stems/htdemucsengine.{h,cpp}`, 25 % overlap-add |
| Folder + index for the result files | `src/engine/stems/stemhqstore.{h,cpp}` |
| Background job, progress dialog, cancel | `src/engine/stems/stemhqservice.{h,cpp}` |
| Menu entry | `src/widget/wtrackmenu.cpp`, Analyze → **Add High Quality Stems (HT-Demucs)…** |
| Playback preference | `StemChannelProcessor` / `StemWorkerThread` load the `.stems` file before falling back to Spleeter |
| Shared model lookup and track decoding | `stemmodelpaths.{h,cpp}`, `stemdecode.{h,cpp}` |

The model is **not** bundled in release artifacts by default (see the sizes
below). The CI workflow has a `bundle_htdemucs` input for test builds.

### Flow

1. Right-click one or more tracks → Analyze → **Add High Quality Stems**.
2. The folder picker opens (the last used folder is remembered in
   `[Stems] high_quality_dir`; the default is the system music folder). Any
   writable folder works, including an external drive or USB stick.
3. A confirmation names the folder and the expected time/disk cost.
4. The job runs on its own thread with a progress dialog. One track at a time:
   peak RSS is gigabytes, and a second concurrent run on an 8 GB machine would
   swap.
5. Each track is written as `<basename>-<hash8>.stems` (the same format as the
   fast engine's cache, four stereo float stems) and registered in
   `<settings>/hq_stems_index.json`.
6. When the job finishes, any deck that currently has that track loaded reloads
   the new stems immediately. On every later load the deck checks the high
   quality store first, then the fast cache, then Spleeter.

If the `.stems` file is missing (USB stick unplugged), the deck silently falls
back to the fast engine.

## Goal

Offer a second stem quality tier, like the speed/quality choice in Rekordbox:

| Engine | Model | Where it runs |
|---|---|---|
| **Fast** (default, shipped) | Spleeter 4-stem fp32, 4 x 38 MB | every machine, 9x realtime here |
| **Quality** (offline) | HT-Demucs v4 fp32 ONNX, 316 MB | only as a background job |

Quality is *not* expected to work in real time on the 2017 MacBookAir7,2. It
never runs on the audio thread.

## Measured behaviour (same laptop, 7.8 s HT-Demucs segment)

RTF = wall seconds per audio second; 1.0 is realtime.

| Threads | Per segment | RTF | Realtime factor |
|---:|---:|---:|---:|
| 1 | 12.6 s | 1.61 | 0.62x |
| 2 | 9.2 s idle / 14.1 s warm | 1.18-1.81 | 0.55-0.85x |
| 4 | 13.8 s | 1.77 | 0.56x |
| Spleeter (reference) | 1.3 s / 11.9 s block | 0.11 | **9x** |

- Peak RSS while separating: **4.8-5.2 GB** (the FT bag of four models). The
  single-file model used here is quoted at ~1.1 GB by its publisher; either way
  keep quality off the audio path.
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

So bundling HT-Demucs fp32 adds ~316 MB to every artifact:

| Artifact | Today | With HQ bundled |
|---|---:|---:|
| macOS x86_64 DMG | 207.5 MB | ~524 MB |
| macOS arm64 DMG | 202.6 MB | ~519 MB |
| Windows x64 zip | 231.1 MB | ~547 MB |
| Linux deb | 166.2 MB | ~482 MB |

**Decision: do not bundle by default.** Download the model once:

- Installer size is unchanged.
- Enabling Quality costs **+316 MB** in the models folder; deleting the file
  reclaims it.
- Most users never use it, and half of them cannot run it anyway.

fp16 would halve the download (166 MB) but measured **16% slower** on CPU
(13.0 s vs 11.2 s) with 77 dB SNR — quality-safe, useless for closing the speed
gap. Use fp32.

## Storage

- No stem cache for the fast engine: raw float stems are ~420 MB per 5-minute
  track.
- The high quality files are exactly that size, but the user opts in per track
  and picks the destination, so an external drive is a first-class target.
- The index in the settings dir maps track location → `.stems` path. Deleting a
  file is enough to revoke it; the deck falls back to the fast engine.

## Design notes

1. **No global "engine" setting.** The quality engine is not toggled; it is a
   per-track action. That avoids a probe, a gating table and a live-vs-buffered
   decision, and it means nothing changes on the audio thread.
2. **Model manager**: none yet. Point the user at the download URL and the
   models folder. A download-with-progress dialog can be added later.
3. **Engine**: `HTDemucsEngine` behind a separate interface (not the Spleeter
   `StemSeparator`), because it is not block-progressive. It walks the track in
   7.8 s segments with a 25 % trapezoidal overlap-add and hands finished frames
   to the caller, so no second full copy of the track is needed.
4. **Licensing**: Demucs v4 / htdemucs is MIT; see `models/NOTICE.txt`.

## Verification before shipping

- [ ] CI benchmark on `macos-14` (arm64): RTF for `CPUExecutionProvider` and
      `CoreMLExecutionProvider`. If CoreML is 2-3x faster, real-time Quality
      becomes genuinely possible on M1-M4.
- [ ] Test once on a Windows machine (planned by the maintainer).
- [ ] Confirm the app still starts and plays when the model is absent or
      corrupt (it does: `isAvailable()`/`initialize()` fail, the menu explains
      where the model goes, playback stays on Spleeter).
- [ ] Measure the per-track wall time on the 2017 Intel laptop with the shipped
      build and put the real number in the confirmation dialog.

## Deferred / optional

- A "full" installer flavour that bundles the model for offline gig laptops
  (+316 MB), for people who prep before a set and never connect.
- In-app model download with SHA-256 verification and progress.
- Compressed high quality storage (Opus 192k per stem, ~34 MB per 5-minute
  track) if the 420 MB float files turn out to be too big for USB sticks.
