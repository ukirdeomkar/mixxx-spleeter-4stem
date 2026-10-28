# Spleeter 4-stem engine (feature branch: `feature/spleeter-4stem`)

## Goal

Replace the fork's HTDemucs 3-stem engine with a real-time-capable **Spleeter 4-stem**
engine (vocals, drums, bass, other) and expose the fourth stem end-to-end
(engine → buffer → cache → mix → skin → controller).

## Why Spleeter

Measured on this machine (MacBookAir7,2, i5-5350U, 8 GB, macOS 12.7.6):

| Model | 45 s audio | Throughput | Peak RAM |
|---|---|---|---|
| SCNet-large INT8 (4 stems) | 58 s per 11 s chunk | 0.19× real-time | 4.3 GB |
| **Spleeter 4-stem fp32 (ONNX)** | **8.18 s** | **5.5× real-time** | 2.5 GB |
| Spleeter fp16 | 10.45 s | 4.3× real-time | 3.4 GB |

fp16 is *slower* on CPU. Use fp32.

Spleeter 4-stem ONNX (4 independent U-Nets, 9.8 M params each), 38 MB per file:
`{vocals,drums,bass,other}.onnx` from `Best-Practice/spleeter-4stems-onnx`.

Models are staged at `~/Library/Application Support/Mixxx/models/spleeter_<stem>.onnx`.

## Audio contract (must match training)

- 44.1 kHz stereo
- STFT: **periodic** Hann (4096), hop 1024, `np.hanning(4097)[:-1]`
- Take `abs(stft)[..., :1024]` (bins to ~11 kHz)
- Split into 512-frame chunks (≈11.9 s); input `x: float32 [2, splits, 512, 1024]`
- Output per stem: `y: float32 [2, splits, 512, 1024]` (magnitude estimate)
- Soft ratio mask over all four stems:
  `mask = (e² + 1e-10/4) / (Σe² + 1e-10)`
- Band extension above 1024 bins: per-frame **mean** (not zeros), then apply to the
  original complex STFT and iSTFT with weighted overlap-add.

## File changes

1. `src/engine/stems/stemseparator.{h,cpp}`
   - Replace `HTDemucsEngine` with `SpleeterEngine`.
   - Four `Ort::Session`s (fp32 models), shared `Ort::Env`.
   - `separateChunk(startFrame, frames, ...)` returning 4 stems.
   - Whole-track API becomes progressive (see below).
2. `src/engine/stems/stemtrackbuffer.{h,cpp}`
   - Add bass L/R; keep `other` as the fourth stem (rename `inst` → `other`).
   - `writeChunk`/`readFrames` take 8 channel pointers.
3. `src/engine/stems/stemchannelprocessor.{h,cpp}`
   - Add `stem_bass_enabled`, `stem_bass_volume`, `stem_bass_solo`.
   - Mix 4 stems; fall back to original audio for frames not yet ready.
4. `src/engine/stems/stemcache.{h,cpp}`
   - **Do not cache raw float stems** (4 × 2 × frames × 4 B ≈ 423 MB per 5-min track;
     a 59-track library would need ~25 GB).
   - v1: disable cache (or gate behind a pref). v2: compressed (FLAC/Opus/int16).
5. `src/engine/stems/stemworkerthread.cpp`
   - Progressive: decode → separate chunk → `writeChunk` → continue, instead of
     separating the whole track before the first write.
   - Startup: first 512-frame chunk ≈ 2 s inference, then separation runs ~5.5×
     faster than playback, so it stays ahead.
   - Seeking far ahead of `framesReady` must fall back to the original mix.
6. `res/skins/LateNight/decks/stems_controls.xml`
   - Add BASS mute/fader/solo alongside VOCALS/DRUMS/INST.
7. Controller mapping (optional, follow-up)
   - Port Mixxx 2.6's FLX4 "Keyboard mode = stems" pads: 1–4 mute, Shift+1–4 solo,
     5–8 stem FX. The fork is 2.5-based, so this is a JS mapping backport.

## Memory / disk

- Whole-track float buffers: 4 stems × 2 ch × frames × 4 B
  = ~53 MB per stem-pair per 5 min → **~423 MB per deck**.
- ONNX sessions: ~2.5 GB peak. Two decks with stems is tight on 8 GB; test one deck first.
- Disk: no raw cache in v1; build + deps need ~8–10 GB.

## Verification

1. Offline unit test: run the Spleeter pipeline on a 30 s WAV, assert
   `sum(stems) - mix` ≈ −150 dB (with mean band extension).
2. Benchmarks: per-chunk inference ≤ 2.75 s (for 25% overlap) or ≤ 5.5 s (no overlap).
3. Live: load track, press play; check no dropouts, `framesReady` stays ahead.
4. FLX4: verify pad mapping once a stems mode is added.

## Risks

- Fork is unmaintained/3 days old; no tests upstream.
- Spleeter separation quality is lower than Demucs (that's the speed trade).
- Progressive mode can expose unseparated audio after seeking ahead of `framesReady`.
- 8 GB RAM is the hard ceiling for two stemmed decks.
