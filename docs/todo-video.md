# TODO: video playback (VDJ-style video decks)

**Status: not implemented. Assessed 2026-10-29 and deliberately deferred.** This
records the design, the honest cost, and what a first version would look like —
so the question does not have to be re-researched later.

## Short answer

| Question | Answer |
|---|---|
| Does Mixxx support video? | No. Not a disabled feature: there is **no video code in the tree at all**. Upstream never shipped it. |
| Can it be added? | Yes. The expensive half — decoding — is already in the build. |
| Is it cheap? | No. A sidecar video deck is roughly **1–2 weeks** of focused work plus sync tuning. VDJ parity is months. |
| Would it touch the audio engine? | Not necessarily. A read-only syncer that displays frames needs no engine change. |
| Is it worth doing on this hardware? | Only for the Windows laptop. This 2015 Air (2 cores, 8 GB, real-time Spleeter already running) will drop frames. |

## What already exists

`grep -rli video src/` returns only false positives (screensaver, vsync, track
record fields). No decoder, no output window, no library support.

What *is* usable:

```
buildenv/.../lib/libavcodec.a     FFMPEG:BOOL=ON in CMakeCache
buildenv/.../lib/libavformat.a    already linked for audio (sources/soundsourceffmpeg.cpp)
buildenv/.../lib/libswscale.a
buildenv/.../lib/libswresample.a
Qt6 + the OpenGL waveform stack
```

So H.264/HEVC/VP9 decode costs nothing in dependencies. Everything VDJ put on
top of the decoder is what would have to be written: the video clock, the output
window, deck synchronisation, and knowing that a track has a video.

Note the static FFmpeg is already bundled and LGPL, so no new licensing surface.
A `libmpv` shortcut would add GPL obligations and is not worth it.

## The three tiers

### Tier 1 — visualiser (no video files)

Audio-reactive OpenGL graphics in the place of / next to the waveform. Reads
`[ChannelN]` levels and VU meters only. No files, no sync, no decoding.

- Effort: **2–4 days**
- Risk: none. Purely additive renderer.
- This is VDJ's "visualisation" tab, not its video decks — worth naming so it is
  not mistaken for the real feature.

### Tier 2 — sidecar video deck (the thing people actually miss)

An `.mp4`/`.mkv` sitting beside the audio file (`track.mp3` → `track.mp4`) plays
in a second, fullscreen-capable window, following that deck's position, pitch
and transport.

```
src/video/videodecoder.{h,cpp}       FFmpeg packets → frames, own thread
src/video/videooutputwindow.{h,cpp}  QOpenGLWidget in a top-level QWindow,
                                     showFullScreen() on the chosen screen
src/video/videosyncer.{h,cpp}        reads [Channel1] playposition / rate /
                                     play / slip_enabled and aligns the frame clock
```

- Effort: **1–2 weeks**, plus an open-ended tail of sync polish.
- Risk: contained. The syncer only *reads* control objects; audio never waits on
  video, and nothing in `src/engine/` changes.
- Gate it behind a preference (`[Video],Enabled`, default off) in the same style
  as `[Effects],EasyBeatFx`, so an off build behaves exactly like stock Mixxx.

Why sidecar and not a library feature: making video a first-class library item
drags in the DB schema, the analyser, all importers and the track model. A
sidecar keeps the audio file as *the* track and just looks for a matching video
next to it, or takes an explicit path. This is the single biggest scope saver.

### Tier 3 — VDJ parity

Four video decks, video crossfader with transitions, video effects, text
overlays, camera input, per-deck aspect/gain controls.

- Effort: **months**. Out of scope; listed only so the gap is explicit.

## The hard parts, in order

1. **Scratching.** Steady playback holds sync easily. Scratching requires seeking
   to a keyframe and decoding forward; expect 100–300 ms of visible lag. VDJ
   spends serious engineering here. A first version should sync on
   play / pause / seek and accept that jog looks rubbery.
2. **Pitch.** Solvable: the deck `rate` already scales the audio clock, so apply
   the same rate to the video clock and the video plays slightly fast/slow, as
   VDJ does. Drift is corrected by nudging the frame clock, never by resampling
   audio.
3. **Performance.** 1080p H.264 decode plus a GL blit on a 2-core 2015 Air,
   while Spleeter separates in real time, will drop frames. Modern hardware is
   fine. Do not judge the feature by this machine.
4. **Second display.** `QWindow` + `setScreen()` + `showFullScreen()` is
   straightforward; the fiddly part is remember-restore of the chosen screen and
   reacting to displays being plugged in.
5. **Codec licensing / bundled decoders.** Already handled by the existing
   FFmpeg build. Nothing new to ship.

## What a first version would contain

Minimum useful slice, in order:

1. `[Video],Enabled` preference + Preferences → Interface checkbox (inert when off).
2. Output window that opens on a second screen and shows a placeholder.
3. FFmpeg decoder thread decoding one file, looping on EOF.
4. Sidecar auto-match on track load (`basename.mp4` beside the audio file).
5. Sync to `playposition` and `rate`; pause when the deck pauses; re-seek on
   jump.
6. Manual "load video" for the mismatch case.
7. Persist the output screen choice.

Anything beyond that — transitions, overlays, per-deck video FX — is Tier 3.

## Decision log

| Date | Decision | Reason |
|---|---|---|
| 2026-10-29 | Video deferred, documented instead of built | Effort is 1–2 weeks minimum and this hardware cannot run it well; the audio-side roadmap (Demucs, stem FX, Windows builds) matters more. |
| 2026-10-29 | Sidecar model preferred over library integration | Avoids DB schema, analyser and importer changes; keeps audio as the track. |
| 2026-10-29 | No `libmpv` | Static FFmpeg is already bundled and LGPL; mpv would add GPL surface for no gain. |
