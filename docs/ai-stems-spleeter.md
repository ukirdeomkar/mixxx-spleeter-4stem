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

---

# Implementation status

## Engine (`stemseparator.{h,cpp}`, new `spleeterstft.{h,cpp}`)

`HTDemucsEngine` is replaced by `SpleeterEngine`, which keeps the upstream file
names so the diff stays readable.

- Four `Ort::Session`s (`spleeter_{vocals,drums,bass,other}.onnx`), fp32,
  4 intra-op threads, one shared `Ort::Env`.
- The STFT frontend / iSTFT lives in `spleeterstft.{h,cpp}`: periodic Hann
  (4096), hop 1024, 44.1 kHz, `abs(rfft)[:1024]` fed to the nets, soft ratio mask
  `(e^2 + 1e-10/4) / (sum e^2 + 1e-10)` applied to the original complex spectrum,
  band extension `1024 -> 2049` with the per-frame **mask mean**, weighted
  overlap-add inverse.
- FFT comes from FFTW **single precision** (`FFTW3f`, already part of the Mixxx
  dependency bundle). A hand-written radix-2 FFT would have cost ~5 s per five
  minute track; `fftwf` costs well under one second.

### Block geometry (this is what makes it progressive)

The ONNX graphs have a fixed 512-frame time axis, but a frame's window also
touches its neighbours, so a 512-frame model block only *owns* 509 frames of
output:

```
kFramesPerBlock = 512 - (4096/1024 - 1) = 509
kSamplesPerBlock = 509 * 1024        = 521216 samples (~11.8 s)
block k: frames [509k, 509k + 512) -> signal samples [509k*1024, +521216)
```

Consecutive blocks therefore tile the track with no gaps and no double
counting, and every output sample is normalised only by the frames that
actually contributed to it. Blocks overlap by 3 frames (~0.6 % extra inference).

## Measured on the target machine (MacBookAir7,2, i5-5350U, 8 GB)

Per block (11.82 s of audio), after a 3.8 s first block that includes model warm-up:

| block | ms |
|---|---|
| 0 (warm-up) | 3830 |
| 1-4 | 2683 / 2623 / 3057 / 3236 |
| 5-15 (steady) | 2027 - 2328 |

=> **~5.4x real-time** in steady state; a 2:59 track is fully separated in ~37 s
of CPU work. Playback can start ~4 s after the first block lands.

The worker runs at `NormalPriority`: at `LowPriority` the GUI starved it and
blocks took 2.3-6.3 s instead (2-3x real-time).

## Validation

- `spleeterstft` reconstruction with uniform 0.25 masks over a 1.5 M sample
  stereo signal: **−137.8 dB** (float32), i.e. the STFT, mask, band extension,
  iSTFT and block tiling invert each other exactly.
- Runtime self-check in `SpleeterEngine::separateBlock`, enabled with
  `MIXXX_DEBUG_SPLEETER_STEMS=1`, logs `sum(stems) - mix` per block. Spleeter's
  masks sum to one, so this must be very negative; a positive-ish value means the
  window, the hop or the band extension is wrong.
- `src/test/stemstest.cpp` covers block tiling, the 4-way unity-sum DSP split,
  buffer round-trip and the end-to-end reconstruction (skipped when the models
  are absent).

## Progressive worker (`stemworkerthread.cpp`)

The track is decoded once, then separated block by block; each block is
published with `writeChunk()` as soon as it lands. First audio is therefore
available after ~1 block of inference (~2 s) instead of after a whole track
(~52 s for a 5 minute track at 5.8× real-time).

Until a region is ready the deck keeps using the zero-latency DSP split, so the
first seconds of a freshly loaded track are DSP stems that morph into AI stems.

## Four stems end to end

- `StemTrackBuffer`: vocals/drums/bass/other, 8 channel planes, progressive
  `framesReady` prefix.
- `StemChannelProcessor`: new `stem_bass_enabled`, `stem_bass_volume`,
  `stem_bass_solo`; the "other instruments" stem keeps the upstream
  `stem_inst_*` control names so existing mappings and the skin keep working.
- `StemDspIsolator`: 4-way split (bass = <220 Hz band, drums = transients,
  vocals = centre/high extraction, other = residual) which still sums to unity.
- LateNight skin: the stem buttons no longer live in the cramped transport row
  (only two of the four fit there). They are a dedicated strip above the track
  title, one section per stem (`res/skins/LateNight/decks/stems_strip.xml`):
  `VOC | S  DRUM | S  BASS | S  INST | S  ALL  AI`.

## Cache

Disk caching is **disabled** (`StemCache::isEnabled() == false`): raw float
stems are ~420 MB per five minute track. The cache format was bumped to
`MIXSTEM2` (4 stems) so stale 3-stem caches are ignored.

## Build notes (macOS, Mixxx 2.5 deps)

- Use the macOS **12.1** SDK. 11.3 lacks `std::bit_ceil`; 12.3+ and 13.x ship a
  `concepts` header that Qt 6.5's `moc` cannot parse (`namespace ranges::inline __cpo`).
- `-DQML=OFF` is fine: LateNight is a legacy XML skin.
- `-DBUILD_BENCH=OFF` (benchmarks require unit tests).
- `libonnxruntime.dylib` comes from `build/_deps/onnxruntime_prebuilt-src/lib`
  and must be copied into the app bundle for distribution, together with the
  four models.

## Still open

- FLX4 mapping: the Mixxx 2.6 mapping has SHIFT+KEYBOARD stem mode (pads 1-4
  mute, SHIFT+pads solo); this 2.5-based fork predates it and needs a backport.
- Recording a stem-bus mix, and per-stem FX sends.
- Optional: derive the fourth stem as `mix - (voc+drum+bass)` to save one iSTFT
  per frame, and compress a disk cache (FLAC/int16) to make reloads instant.
