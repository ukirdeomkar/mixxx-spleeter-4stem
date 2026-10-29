# TODO: per-stem effects (stem FX)

**Status: not implemented.** Assessed 2026-10-29. Feasible, but not "just link
the effects" — the effects engine is per channel group and our stems are summed
into the deck before any effect runs. Mixxx 2.7's native stem playback already
implements the exact pattern, so this is a port with a blueprint rather than a
design from scratch.

## Short answer

| Question | Answer |
|---|---|
| Can it be done? | Yes. One quick-effect chain per stem is the right model. |
| Is it easy? | Moderate. ~**3–5 days** for an AI-path MVP in LateNight, ~**1–2 weeks** for all skins, persistence and tests. |
| Why not a one-liner? | Effects are stateful per group; sharing one chain across four stems is wrong. Each stem needs its own group, chain, controls and reset behaviour. |
| Is there reference code? | Yes — Mixxx 2.7 native stems (`[Channel1_Stem1]`, `EffectsManager::addStem`, `EngineDeck::processStem`, `stem_channel.xml`). |

## Why the current code cannot just "link" an effect

`EngineDeck::process` today:

```
EngineBuffer::process(pOut)
  -> StemChannelProcessor::process(pOut)      // reads 4 stems, applies gain, sums to stereo
  -> pregain
  -> processPreFaderInPlace([ChannelN])       // deck EQ / standard FX
  -> mixer: processPostFaderAndMix([ChannelN]) // deck quick FX, fader, crossfader
```

By the time any effect sees audio, it is already a stereo sum of vocals, drums,
bass and instruments. There is no insertion point per stem.

An effect chain is also **stateful**: delay lines, reverb tails and filter state
belong to one chain. You cannot apply one chain to four stems one after another
in the same buffer — the second stem would inherit the first one's tail.

## The upstream blueprint (Mixxx 2.7 native stems)

Mixxx 2.7 plays Native Instruments `.stem.mp4` files as four stereo streams and
gives each stream its own group. Relevant code:

| File | What it does |
|---|---|
| `src/mixer/playermanager.cpp` | registers `[ChannelN_Stem1..4]` with the engine, calls `EngineDeck::addStemHandle` and `EffectsManager::addStem` |
| `src/engine/channels/enginedeck.{h,cpp}` | `processStem()`: splits the multichannel buffer, processes each stem through `processPostFaderInPlace`, applies cached gain/mute, sums |
| `src/effects/effectsmanager.{h,cpp}` | `addStem()`, `m_quickStemEffectChains`, `resetStemQuickFxKnob()`, `readEffectsXmlSingleDeckStem()` |
| `src/effects/presets/effectchainpresetmanager.{h,cpp}` | persists per-stem quick-effect presets in `effects.xml` |
| `res/skins/LateNight/stem_channel.xml` | preset selector + enable button + `super1` knob per stem |
| `res/skins/LateNightQML/Waveforms/StemControls.qml`, `res/qml/StemKnob.qml` | same for the QML UI |
| `src/test/stemcontrolobjecttest.cpp` | control-level tests |

Control naming that falls out of it:

```
[Channel1_Stem1] volume, mute, mute_toggle
[QuickEffectRack1_[Channel1_Stem1]] enabled, super1, preset
```

The LateNight strip already uses the generic `EffectChainPresetSelector`, so
the UI widget exists in our 2.5.6 tree too.

## What a port to this fork would involve

### 1. Give each stem a channel group (1–2 days)

- In `PlayerManager`, when a deck is created, register
  `[ChannelN_StemVocals]`, `_StemDrums`, `_StemBass`, `_StemInst` via
  `EngineMixer::registerChannelGroup` (the groups must exist in the engine
  before an effect chain can reference them).
- Pass the four `ChannelHandleAndGroup`s into `StemChannelProcessor` (or into
  `EngineDeck`, upstream style).
- `EffectsManager::registerInputChannel` for each, so the standard effect units
  can see them as routing targets if we ever want that.

### 2. Add stem quick-effect chains (2–4 days)

- Port `EffectsManager::addStem`, `m_quickStemEffectChains`,
  `resetStemQuickFxKnob` and `readEffectsXmlSingleDeckStem`.
- Port the stem sections of `EffectChainPresetManager` (`kStemQuickEffectChainPresets`)
  so presets survive a restart.
- Create the chains at deck setup, load a sensible default preset (e.g.
  Filter), and make sure `effects.xml` from older versions still loads.

### 3. Process each stem before summing (2–4 days, audio thread)

In `StemChannelProcessor::process`, for each of the four scratch buffers:

1. Interleave one stem into a preallocated stereo temp buffer.
2. Call
   `EngineEffectsManager::processPostFaderInPlace(stemHandle, mainHandle, temp,
   frames, sampleRate, featureState, gainCache, gain, false)`.
3. Accumulate with its volume/mute gain into the deck output.
4. Cache the gain for the next buffer, otherwise volume/mute changes click
   (upstream's `m_stemsGainCache`).

Keep the existing "all gains unity → return early" fast path only when every
stem chain is disabled; otherwise the bypass would skip FX.

**Feature state matters.** Tempo-synced effects (echo, delay, beat FX) need BPM
and beat distance. Pass the deck's `GroupFeatureState` from
`EngineDeck::collectFeatures` into the processor; with an empty state, delays
will not sync and the feature looks broken.

### 4. DSP fallback (2–4 days, can be deferred)

`StemDspIsolator::process` filters the mix into four bands and sums them
internally. To apply per-stem FX in fallback mode it must expose the four band
buffers instead of the sum, then the same effect loop applies.

**MVP decision:** apply stem FX only on the AI path and document that the
fallback (`DSP` source) is unaffected. This is acceptable because DSP mode is a
transient fallback, but it must be stated in the UI/docs.

### 5. Skins (1–2 days LateNight, ~1 day per other skin)

Per stem, reuse the upstream layout: preset selector, enable button, `super1`
knob. The existing stem strips already carry label + mute + volume; adding
~130 px of controls per stem is tight in a 1280×800 LateNight layout. Options:

- Full row per stem (upstream style) — best UX, needs layout work.
- One shared "stem FX focus" row: four knobs, one shared preset selector.
- Knob + enable only, preset chosen in Preferences — least space, least clear.

Four skins (`LateNight`, `Deere`, `Tango`, `Shade`) plus their 64-sampler
variants (they inherit templates) is the bulk of the cosmetic work.

### 6. Controller (2–3 days, phase 2)

The FLX4 mapping's Beat FX section targets the **standard** effect units
(`[EffectRack1_EffectUnit1]`), not per-stem quick chains. Options:

- SHIFT + FX knob adjusts the focused stem's `super1`, with the four stem pads
  selecting focus (but pads already mute/solo stems).
- A separate "STEM FX" mode on SHIFT + pads 5–8, leaving 1–4 for mute/solo.
- Leave hardware out of v1; mouse/touch only.

This needs a UX decision, not just mapping work. Do not bundle it into the
engine port.

## Work estimate

| Piece | Days |
|---|---:|
| Stem groups + handle plumbing | 1–2 |
| EffectsManager + preset persistence | 2–4 |
| Per-stem processing in `StemChannelProcessor` | 2–4 |
| LateNight UI | 1–2 |
| Deere / Tango / Shade UI | 2–3 |
| Tests, session restore, CPU check | 2–3 |
| FLX4 control (optional, phase 2) | 2–3 |
| DSP-fallback per-stem FX (optional) | 2–4 |
| **MVP (AI path + LateNight + no controller)** | **3–5** |
| **Full (4 skins + persistence + tests)** | **10–15** |

## Performance

- 4 stems × 2 decks = 8 quick chains, each with up to 4 slots. Empty slots are
  cheap; a filter or echo per stem is roughly 1–3% of one core each on this
  class of CPU.
- Expect **+5–15% of one core** with one effect per stem on both decks. Spleeter
  separation (≈0.11 RTF on two threads) remains the dominant cost, but this is
  not free on an 8 GB dual-core Air.
- Measure with both decks separated and all eight chains enabled before
  deciding whether to allow four slots per stem or only one.

## Risks and details that cause bugs

| Risk | Mitigation |
|---|---|
| Clicks on mute/volume | Per-stem gain cache, like upstream `m_stemsGainCache` |
| Effect tail survives a track change | Reset stem chains on load/seek/stop (`stem_auto_reset`) |
| Tempo FX out of sync | Pass `GroupFeatureState` from the deck, not an empty struct |
| Audio-thread allocation | Preallocate the per-stem temp buffer; no `std::vector` growth in `process` |
| `effects.xml` breaks on upgrade | Keep reading old files; default presets when stem keys are absent |
| DSP fallback silently loses FX | Either implement the 4-band output or show "FX unavailable in DSP mode" |
| UI clutter in 1280×800 | One row per stem or a focus model; test at the minimum skin size |
| Cue/record mismatch | FX runs before the sum, so PFL and recording capture it consistently |
| Standard-unit routing confusion | Do not register stems as standard-unit inputs in v1; quick chains only |

## Alternatives considered

1. **Built-in per-stem filter + echo** in `StemChannelProcessor` (3–5 days).
   No effect presets, no reverb/delay library, duplicate DSP, and a second FX
   system to maintain. Rejected: the effects framework already does this.
2. **Route stems through the four standard effect units** by registering them
   as input channels. Flexible routing, but one chain serves multiple channels
   and its state is shared; the UI would show stems next to decks in every
   routing switch. Rejected for v1.
3. **Post-sum deck FX** (what exists today). Not per-stem; a filter on the deck
   ducks everything. This is the limitation, not a solution.

## Definition of done (MVP)

- [ ] Each stem has `super1`, enable and a preset selector in LateNight.
- [ ] Soloing vocals and turning its knob changes only vocals; other stems are
      untouched.
- [ ] Presets survive a restart; a track change resets tails cleanly.
- [ ] No clicks when muting or moving a stem fader while FX is active.
- [ ] Tempo-synced presets follow deck BPM.
- [ ] Recording and PFL contain the stem FX.
- [ ] Both decks separated, eight chains on: no dropouts, CPU measured and
      documented.
- [ ] DSP fallback either applies FX or says it does not.
- [ ] `docs/ai-stems-spleeter.md` updated with the new control names.

## Decision

**Defer.** It is a contained feature, not a research project, and upstream 2.7
provides the design and most of the code. When it is picked up, do the MVP
first (AI path + LateNight, no controller, no DSP-mode FX); do not start with
four skins and hardware mapping.
