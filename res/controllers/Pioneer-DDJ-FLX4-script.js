// Pioneer-DDJ-FLX4-script.js
// ****************************************************************************
// * Mixxx mapping script file for the Pioneer DDJ-FLX4.
// * Mostly adapted from the DDJ-400 mapping script
// * Authors: Warker, nschloe, dj3730, jusko, Robert904
// ****************************************************************************
//
//  Implemented (as per manufacturer's manual):
//      * Mixer Section (Faders, EQ, Filter, Gain, Cue)
//      * Browsing and loading + Waveform zoom (shift)
//      * Jogwheels, Scratching, Bending, Loop adjust
//      * Cycle Temporange
//      * Beat Sync
//      * Hot Cue Mode
//      * Beat Loop Mode
//      * Beat Jump Mode
//      * Sampler Mode
//
//  Custom (Mixxx specific mappings):
//      * BeatFX: Assigned Effect Unit 1
//                v FX_SELECT Load next effect.
//                SHIFT + v FX_SELECT Load previous effect.
//                < LEFT Cycle effect focus leftward
//                > RIGHT Cycle effect focus rightward
//                ON/OFF toggles focused effect slot
//                SHIFT + ON/OFF disables all three effect slots.
//
//      * 32 beat jump forward & back (Shift + </> CUE/LOOP CALL arrows)
//      * Toggle quantize (Shift + channel cue)
//
//  Not implemented (after discussion and trial attempts):
//      * Loop Section:
//        * -4BEAT auto loop (hacky---prefer a clean way to set a 4 beat loop
//                            from a previous position on long press)
//
//        * CUE/LOOP CALL - memory & delete (complex and not useful. Hot cues are sufficient)
//
//      * Secondary pad modes (trial attempts complex and too experimental)
//        * Keyboard mode
//        * Pad FX1
//        * Pad FX2
//        * Keyshift mode
//
//  Stems (this fork):
//      * SAMPLER pad mode is repurposed for stem control
//        * PAD 1 = vocals, PAD 2 = instruments, PAD 3 = bass, PAD 4 = drums
//        * SHIFT + PAD 1-4 = solo that stem
//        * PAD 5-8 = unbound, reserved
//        * pad LEDs follow the stem enabled state
//
//  Not implemented yet (but might be in the future):
//      * Smart CFX
//      * Smart fader

var PioneerDDJFLX4 = {};

PioneerDDJFLX4.lights = {
    beatFx: {
        status: 0x94,
        data1: 0x47,
    },
    shiftBeatFx: {
        status: 0x94,
        data1: 0x43,
    },
    deck1: {
        vuMeter: {
            status: 0xB0,
            data1: 0x02,
        },
        playPause: {
            status: 0x90,
            data1: 0x0B,
        },
        shiftPlayPause: {
            status: 0x90,
            data1: 0x47,
        },
        cue: {
            status: 0x90,
            data1: 0x0C,
        },
        shiftCue: {
            status: 0x90,
            data1: 0x48,
        },
        hotcueMode: {
            status: 0x90,
            data1: 0x1B,
        },
        keyboardMode: {
            status: 0x90,
            data1: 0x69,
        },
        padFX1Mode: {
            status: 0x90,
            data1: 0x1E,
        },
        padFX2Mode: {
            status: 0x90,
            data1: 0x6B,
        },
        beatJumpMode: {
            status: 0x90,
            data1: 0x20,
        },
        beatLoopMode: {
            status: 0x90,
            data1: 0x6D,
        },
        samplerMode: {
            status: 0x90,
            data1: 0x22,
        },
        keyShiftMode: {
            status: 0x90,
            data1: 0x6F,
        },
    },
    deck2: {
        vuMeter: {
            status: 0xB0,
            data1: 0x02,
        },
        playPause: {
            status: 0x91,
            data1: 0x0B,
        },
        shiftPlayPause: {
            status: 0x91,
            data1: 0x47,
        },
        cue: {
            status: 0x91,
            data1: 0x0C,
        },
        shiftCue: {
            status: 0x91,
            data1: 0x48,
        },
        hotcueMode: {
            status: 0x91,
            data1: 0x1B,
        },
        keyboardMode: {
            status: 0x91,
            data1: 0x69,
        },
        padFX1Mode: {
            status: 0x91,
            data1: 0x1E,
        },
        padFX2Mode: {
            status: 0x91,
            data1: 0x6B,
        },
        beatJumpMode: {
            status: 0x91,
            data1: 0x20,
        },
        beatLoopMode: {
            status: 0x91,
            data1: 0x6D,
        },
        samplerMode: {
            status: 0x91,
            data1: 0x22,
        },
        keyShiftMode: {
            status: 0x91,
            data1: 0x6F,
        },
    },
};

// Store timer IDs
PioneerDDJFLX4.timers = {};

// Keep alive timer
PioneerDDJFLX4.sendKeepAlive = function() {
    midi.sendSysexMsg([0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x04, 0x05, 0x00, 0x50, 0x02, 0xf7], 12); // This was reverse engineered with Wireshark
};

// Jog wheel constants
PioneerDDJFLX4.vinylMode = true;
PioneerDDJFLX4.alpha = 1.0/8;
PioneerDDJFLX4.beta = PioneerDDJFLX4.alpha/32;

// Multiplier for fast seek through track using SHIFT+JOGWHEEL
// Fraction of the whole track moved per jog tick with SHIFT held.
PioneerDDJFLX4.searchStep = 0.0008;
PioneerDDJFLX4.bendScale = 0.8;

// Step applied by BEAT </> to the focused effect's first parameter.
PioneerDDJFLX4.beatStepSize = 0.25;

PioneerDDJFLX4.tempoRanges = [0.06, 0.10, 0.16, 0.25];

PioneerDDJFLX4.shiftButtonDown = [false, false];

// Jog wheel loop adjust
PioneerDDJFLX4.loopAdjustIn = [false, false];
PioneerDDJFLX4.loopAdjustOut = [false, false];
PioneerDDJFLX4.loopAdjustMultiply = 50;

// Beatjump pad (beatjump_size values)
PioneerDDJFLX4.beatjumpSizeForPad = {
    0x20: -1, // PAD 1
    0x21: 1,  // PAD 2
    0x22: -2, // PAD 3
    0x23: 2,  // PAD 4
    0x24: -4, // PAD 5
    0x25: 4,  // PAD 6
    0x26: -8, // PAD 7
    0x27: 8   // PAD 8
};

PioneerDDJFLX4.quickJumpSize = 32;

// Used for tempo slider
PioneerDDJFLX4.highResMSB = {
    "[Channel1]": {},
    "[Channel2]": {}
};

PioneerDDJFLX4.trackLoadedLED = function(value, group, _control) {
    midi.sendShortMsg(
        0x9F,
        group.match(script.channelRegEx)[1] - 1,
        value > 0 ? 0x7F : 0x00
    );
};

PioneerDDJFLX4.toggleLight = function(midiIn, active) {
    midi.sendShortMsg(midiIn.status, midiIn.data1, active ? 0x7F : 0);
};

//
// Init
//

PioneerDDJFLX4.init = function() {
    engine.setValue("[EffectRack1_EffectUnit1]", "show_focus", 1);

    // One Beat FX unit per deck: unit 1 only on channel 1, unit 2 only on
    // channel 2, both fully wet so ON/OFF alone makes the effect audible.
    engine.setValue("[EffectRack1_EffectUnit1]", "group_[Channel1]_enable", 1);
    engine.setValue("[EffectRack1_EffectUnit1]", "group_[Channel2]_enable", 0);
    engine.setValue("[EffectRack1_EffectUnit2]", "group_[Channel1]_enable", 0);
    engine.setValue("[EffectRack1_EffectUnit2]", "group_[Channel2]_enable", 1);

    // Unit 3 is the shared Beat FX for both decks (CH1 + CH2 on the switch).
    engine.setValue("[EffectRack1_EffectUnit3]", "group_[Channel1]_enable", 1);
    engine.setValue("[EffectRack1_EffectUnit3]", "group_[Channel2]_enable", 1);
    PioneerDDJFLX4.applyFaderLinkedMix();

    // Easy FX layout: collapse the per-effect parameter rows (Time, Feedback,
    // Ping Pong, Send) so each unit shows only ON/OFF, the super knob and the
    // effect name - the way a Rekordbox Beat FX block looks.
    const showParameters = PioneerDDJFLX4.easyBeatFx() ? 0 : 1;
    for (let unit = 1; unit <= 4; ++unit) {
        engine.setValue("[EffectRack1_EffectUnit" + unit + "]", "show_parameters", showParameters);
    }

    // Multi-slot racks hide units 3 and 4 by default. Easy mode needs unit 3
    // visible because it is the shared Beat FX for both decks.
    if (PioneerDDJFLX4.easyBeatFx()) {
        engine.setValue("[Skin]", "show_4effectunits", 1);
    }

    // React to the preference without a restart: switching easy mode off
    // restores the full parameter rows, switching it on collapses them again.
    engine.makeConnection("[Effects]", "EasyBeatFx", function(value) {
        const showParameters = value > 0.5 ? 0 : 1;
        for (let unit = 1; unit <= 4; ++unit) {
            engine.setValue("[EffectRack1_EffectUnit" + unit + "]",
                    "show_parameters", showParameters);
        }
        PioneerDDJFLX4.applyFaderLinkedMix();
    });

    engine.makeConnection("[Channel1]", "vu_meter", PioneerDDJFLX4.vuMeterUpdate);
    engine.makeConnection("[Channel2]", "vu_meter", PioneerDDJFLX4.vuMeterUpdate);

    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.deck1.vuMeter, false);
    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.deck2.vuMeter, false);

    engine.softTakeover("[Channel1]", "rate", true);
    engine.softTakeover("[Channel2]", "rate", true);

    // NOTE: the SAMPLER pad mode is repurposed for stem control (see
    // stemPadPressed below), so sampler LED feedback is not wired up here.

    engine.makeConnection("[Channel1]", "track_loaded", PioneerDDJFLX4.trackLoadedLED);
    engine.makeConnection("[Channel2]", "track_loaded", PioneerDDJFLX4.trackLoadedLED);

    // play the "track loaded" animation on both decks at startup
    midi.sendShortMsg(0x9F, 0x00, 0x7F);
    midi.sendShortMsg(0x9F, 0x01, 0x7F);

    PioneerDDJFLX4.setLoopButtonLights(0x90, 0x7F);
    PioneerDDJFLX4.setLoopButtonLights(0x91, 0x7F);

    engine.makeConnection("[Channel1]", "loop_enabled", PioneerDDJFLX4.loopToggle);
    engine.makeConnection("[Channel2]", "loop_enabled", PioneerDDJFLX4.loopToggle);

    for (i = 1; i <= 3; i++) {
        engine.makeConnection("[EffectRack1_EffectUnit1_Effect" + i +"]", "enabled", PioneerDDJFLX4.toggleFxLight);
    }
    engine.makeConnection("[EffectRack1_EffectUnit1]", "focused_effect", PioneerDDJFLX4.toggleFxLight);

    PioneerDDJFLX4.keepAliveTimer = engine.beginTimer(200, PioneerDDJFLX4.sendKeepAlive);

    // query the controller for current control positions on startup
    PioneerDDJFLX4.sendKeepAlive(); // the query seems to double as a keep alive message
};

//
// Waveform zoom
//

PioneerDDJFLX4.waveformZoom = function(midichan, control, value, status, group) {
    if (value === 0x7f) {
        script.triggerControl(group, "waveform_zoom_up", 100);
    } else {
        script.triggerControl(group, "waveform_zoom_down", 100);
    }
};

//
// Channel level lights
//

PioneerDDJFLX4.vuMeterUpdate = function(value, group) {
    const newVal = value * 127;

    switch (group) {
    case "[Channel1]":
        midi.sendShortMsg(0xB0, 0x02, newVal);
        break;

    case "[Channel2]":
        midi.sendShortMsg(0xB1, 0x02, newVal);
        break;
    }
};

//
// Effects
//

PioneerDDJFLX4.toggleFxLight = function(_value, _group, _control) {
    const enabled = engine.getValue(PioneerDDJFLX4.focusedFxGroup(), "enabled");

    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.beatFx, enabled);
    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.shiftBeatFx, enabled);
};

// Easy mode: one Beat FX unit per deck, mirroring a Rekordbox mixer.
// Unit 1 belongs to deck 1, unit 2 to deck 2. The CH SELECT buttons pick which
// unit the knobs, ON/OFF and BEAT arrows act on, so each deck keeps its own
// effect and its own beat division.
PioneerDDJFLX4.fxUnit = 1;

// Preferences > Effects > "Easy mode: one effect per deck, parameters hidden".
PioneerDDJFLX4.easyBeatFx = function() {
    return engine.getValue("[Effects]", "EasyBeatFx") > 0.5;
};

// Easy mode only: make each Beat FX unit follow its channel fader, so closing
// a fader also silences that deck's effect and its tail. Upstream gain staging
// is untouched; this just drives the unit's dry/wet control.
PioneerDDJFLX4.faderMixConnectionIds = [];

PioneerDDJFLX4.applyFaderLinkedMix = function() {
    for (let i = 0; i < PioneerDDJFLX4.faderMixConnectionIds.length; ++i) {
        engine.disconnect(PioneerDDJFLX4.faderMixConnectionIds[i]);
    }
    PioneerDDJFLX4.faderMixConnectionIds = [];

    if (!PioneerDDJFLX4.easyBeatFx()) {
        for (let unit = 1; unit <= 3; ++unit) {
            engine.setValue("[EffectRack1_EffectUnit" + unit + "]", "mix", 1.0);
        }
        return;
    }

    PioneerDDJFLX4.faderMixConnectionIds.push(
            engine.makeConnection("[Channel1]", "volume", function(value) {
                engine.setValue("[EffectRack1_EffectUnit1]", "mix", value);
            }));
    PioneerDDJFLX4.faderMixConnectionIds.push(
            engine.makeConnection("[Channel2]", "volume", function(value) {
                engine.setValue("[EffectRack1_EffectUnit2]", "mix", value);
            }));

    const updateSharedUnit = function() {
        const gain1 = engine.getValue("[Channel1]", "volume");
        const gain2 = engine.getValue("[Channel2]", "volume");
        engine.setValue("[EffectRack1_EffectUnit3]", "mix", Math.max(gain1, gain2));
    };
    PioneerDDJFLX4.faderMixConnectionIds.push(
            engine.makeConnection("[Channel1]", "volume", updateSharedUnit));
    PioneerDDJFLX4.faderMixConnectionIds.push(
            engine.makeConnection("[Channel2]", "volume", updateSharedUnit));

    engine.setValue("[EffectRack1_EffectUnit1]", "mix",
            engine.getValue("[Channel1]", "volume"));
    engine.setValue("[EffectRack1_EffectUnit2]", "mix",
            engine.getValue("[Channel2]", "volume"));
    updateSharedUnit();
};

PioneerDDJFLX4.fxUnitGroup = function() {
    return "[EffectRack1_EffectUnit" + PioneerDDJFLX4.fxUnit + "]";
};

PioneerDDJFLX4.focusedFxGroup = function() {
    // Note the bracket position: the group name is
    // [EffectRack1_EffectUnitN_EffectM], not [EffectRack1_EffectUnitN]_EffectM.
    return "[EffectRack1_EffectUnit" + PioneerDDJFLX4.fxUnit + "_Effect1]";
};

// Optional escape hatch: expose the old focus behaviour for anyone who wants
// the three-slot rack. Flip this to true to restore it.
PioneerDDJFLX4.advancedFxRack = false;

PioneerDDJFLX4.beatFxLevelDepthRotate = function(_channel, _control, value) {
    const normalized = value / 0x7F;

    if (PioneerDDJFLX4.shiftButtonDown[0] || PioneerDDJFLX4.shiftButtonDown[1]) {
        // Secondary: Echo/Delay feedback, for shaping the tail by hand.
        engine.setParameter(PioneerDDJFLX4.focusedFxGroup(), "parameter2", normalized);
    } else {
        // LEVEL/DEPTH drives the super knob of the effect slot (the knob next
        // to the effect name in the rack), exactly like Rekordbox.
        engine.setParameter(PioneerDDJFLX4.focusedFxGroup(), "meta", normalized);
    }
};

PioneerDDJFLX4.changeFocusedEffectBy = function(numberOfSteps) {
    let focusedEffect = engine.getValue(PioneerDDJFLX4.fxUnitGroup(), "focused_effect");

    // Convert to zero-based index
    focusedEffect -= 1;

    // Standard Euclidean modulo by use of two plain modulos
    const numberOfEffectsPerEffectUnit = 3;
    focusedEffect = (((focusedEffect + numberOfSteps) % numberOfEffectsPerEffectUnit) + numberOfEffectsPerEffectUnit) % numberOfEffectsPerEffectUnit;

    // Convert back to one-based index
    focusedEffect += 1;

    engine.setValue(PioneerDDJFLX4.fxUnitGroup(), "focused_effect", focusedEffect);
};

PioneerDDJFLX4.beatFxSelectPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    engine.setValue(PioneerDDJFLX4.focusedFxGroup(), "next_effect", value);
};

PioneerDDJFLX4.beatFxSelectShiftPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    engine.setValue(PioneerDDJFLX4.focusedFxGroup(), "prev_effect", value);
};

// BEAT </> adjust the focused effect's main parameter instead of moving the
// focus between effect slots. Rekordbox uses these buttons for the beat
// division of the active Beat FX, and on Mixxx that division lives in the
// effect's first knob parameter (Echo/Delay: "Time" in beats).
// Beat divisions for tempo-synced effects whose first knob parameter is a time
// in beats. Mixxx's Echo/Delay expose "Time" as 0..2 beats mapped onto the
// normalised 0..1 control value, so 1 beat == 0.5.
//   1/16, 1/8, 1/4, 1/2, 3/4, 1, 1.5, 2 beats
PioneerDDJFLX4.beatDivisions = [0.03125, 0.0625, 0.125, 0.25, 0.375, 0.5, 0.75, 1.0];

PioneerDDJFLX4.stepFocusedEffectParameter = function(steps) {
    const group = PioneerDDJFLX4.focusedFxGroup();
    const loaded = engine.getValue(group, "parameter1_loaded");
    // Mixxx's Echo rounds the delay to 1/4 beat and clamps it to 1/8 beat while
    // Quantize is on, which makes 1/16 and 1/32 impossible. The beat ladder
    // already gives exact musical values, so disable the rounding.
    engine.setValue(group, "button_parameter1", 0);
    const current = engine.getParameter(group, "parameter1");
    const divisions = PioneerDDJFLX4.beatDivisions;

    // Snap to the nearest division, then move one place.
    let nearest = 0;
    let nearestDistance = Infinity;
    for (let i = 0; i < divisions.length; ++i) {
        const distance = Math.abs(divisions[i] - current);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = i;
        }
    }

    let index = nearest + (steps > 0 ? 1 : -1);
    index = Math.min(divisions.length - 1, Math.max(0, index));
    engine.setParameter(group, "parameter1", divisions[index]);
};

PioneerDDJFLX4.beatFxLeftPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    // SHIFT keeps the original behaviour: move the focus to the previous slot.
    if (!PioneerDDJFLX4.easyBeatFx()) {
        PioneerDDJFLX4.changeFocusedEffectBy(-1);
        return;
    }

    PioneerDDJFLX4.stepFocusedEffectParameter(-1);
};

PioneerDDJFLX4.beatFxRightPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    if (!PioneerDDJFLX4.easyBeatFx()) {
        PioneerDDJFLX4.changeFocusedEffectBy(1);
        return;
    }

    PioneerDDJFLX4.stepFocusedEffectParameter(1);
};

PioneerDDJFLX4.beatFxOnOffPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    const toggleEnabled = !engine.getValue(PioneerDDJFLX4.focusedFxGroup(), "enabled");
    engine.setValue(PioneerDDJFLX4.focusedFxGroup(), "enabled", toggleEnabled);
};

PioneerDDJFLX4.beatFxOnOffShiftPressed = function(_channel, _control, value) {
    if (value === 0) { return; }

    // Kill the effect of the deck currently selected for Beat FX.
    engine.setValue(PioneerDDJFLX4.focusedFxGroup(), "enabled", 0);
    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.beatFx, false);
    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.shiftBeatFx, false);
};

// The CH SELECT switch has three positions: deck 1, deck 2 and both.
// Unit 1 is deck 1's Beat FX, unit 2 deck 2's and unit 3 the shared one.
PioneerDDJFLX4.fxChannel1Selected = false;
PioneerDDJFLX4.fxChannel2Selected = false;

PioneerDDJFLX4.applyFxChannelSelection = function() {
    let unit;
    if (PioneerDDJFLX4.fxChannel1Selected && PioneerDDJFLX4.fxChannel2Selected) {
        unit = 3; // both decks
    } else if (PioneerDDJFLX4.fxChannel2Selected) {
        unit = 2;
    } else {
        unit = 1;
    }
    PioneerDDJFLX4.fxUnit = unit;

    if (unit === 3) {
        // Switching to the shared unit takes both decks over, so the per-deck
        // effects step aside automatically.
        engine.setValue("[EffectRack1_EffectUnit1_Effect1]", "enabled", 0);
        engine.setValue("[EffectRack1_EffectUnit2_Effect1]", "enabled", 0);
    } else {
        // Moving between deck 1 and deck 2 leaves the other deck's effect
        // running - it is only disabled by hand. Leaving the shared unit turns
        // that one off, since it belongs to the "both" position.
        engine.setValue("[EffectRack1_EffectUnit3_Effect1]", "enabled", 0);
    }
};

PioneerDDJFLX4.beatFxChannel1 = function(_channel, _control, value) {
    PioneerDDJFLX4.fxChannel1Selected = value !== 0;
    PioneerDDJFLX4.applyFxChannelSelection();
};

PioneerDDJFLX4.beatFxChannel2 = function(_channel, _control, value) {
    PioneerDDJFLX4.fxChannel2Selected = value !== 0;
    PioneerDDJFLX4.applyFxChannelSelection();
};

//
// Loop IN/OUT ADJUST
//

PioneerDDJFLX4.toggleLoopAdjustIn = function(channel, _control, value, _status, group) {
    if (value === 0 || engine.getValue(group, "loop_enabled" === 0)) {
        return;
    }
    PioneerDDJFLX4.loopAdjustIn[channel] = !PioneerDDJFLX4.loopAdjustIn[channel];
    PioneerDDJFLX4.loopAdjustOut[channel] = false;
};

PioneerDDJFLX4.toggleLoopAdjustOut = function(channel, _control, value, _status, group) {
    if (value === 0 || engine.getValue(group, "loop_enabled" === 0)) {
        return;
    }
    PioneerDDJFLX4.loopAdjustOut[channel] = !PioneerDDJFLX4.loopAdjustOut[channel];
    PioneerDDJFLX4.loopAdjustIn[channel] = false;
};

// Two signals are sent here so that the light stays lit/unlit in its shift state too
PioneerDDJFLX4.setReloopLight = function(status, value) {
    midi.sendShortMsg(status, 0x4D, value);
    midi.sendShortMsg(status, 0x50, value);
};


PioneerDDJFLX4.setLoopButtonLights = function(status, value) {
    [0x10, 0x11, 0x4E, 0x4C].forEach(function(control) {
        midi.sendShortMsg(status, control, value);
    });
};

PioneerDDJFLX4.startLoopLightsBlink = function(channel, control, status, group) {
    let blink = 0x7F;

    PioneerDDJFLX4.stopLoopLightsBlink(group, control, status);

    PioneerDDJFLX4.timers[group][control] = engine.beginTimer(500, () => {
        blink = 0x7F - blink;

        // When adjusting the loop out position, turn the loop in light off
        if (PioneerDDJFLX4.loopAdjustOut[channel]) {
            midi.sendShortMsg(status, 0x10, 0x00);
            midi.sendShortMsg(status, 0x4C, 0x00);
        } else {
            midi.sendShortMsg(status, 0x10, blink);
            midi.sendShortMsg(status, 0x4C, blink);
        }

        // When adjusting the loop in position, turn the loop out light off
        if (PioneerDDJFLX4.loopAdjustIn[channel]) {
            midi.sendShortMsg(status, 0x11, 0x00);
            midi.sendShortMsg(status, 0x4E, 0x00);
        } else {
            midi.sendShortMsg(status, 0x11, blink);
            midi.sendShortMsg(status, 0x4E, blink);
        }
    });

};

PioneerDDJFLX4.stopLoopLightsBlink = function(group, control, status) {
    PioneerDDJFLX4.timers[group] = PioneerDDJFLX4.timers[group] || {};

    if (PioneerDDJFLX4.timers[group][control] !== undefined) {
        engine.stopTimer(PioneerDDJFLX4.timers[group][control]);
    }
    PioneerDDJFLX4.timers[group][control] = undefined;
    PioneerDDJFLX4.setLoopButtonLights(status, 0x7F);
};

PioneerDDJFLX4.loopToggle = function(value, group, control) {
    const status = group === "[Channel1]" ? 0x90 : 0x91,
        channel = group === "[Channel1]" ? 0 : 1;

    PioneerDDJFLX4.setReloopLight(status, value ? 0x7F : 0x00);

    if (value) {
        PioneerDDJFLX4.startLoopLightsBlink(channel, control, status, group);
    } else {
        PioneerDDJFLX4.stopLoopLightsBlink(group, control, status);
        PioneerDDJFLX4.loopAdjustIn[channel] = false;
        PioneerDDJFLX4.loopAdjustOut[channel] = false;
    }
};

//
// CUE/LOOP CALL
//

PioneerDDJFLX4.cueLoopCallLeft = function(_channel, _control, value, _status, group) {
    if (value) {
        engine.setValue(group, "loop_scale", 0.5);
    }
};

PioneerDDJFLX4.cueLoopCallRight = function(_channel, _control, value, _status, group) {
    if (value) {
        engine.setValue(group, "loop_scale", 2.0);
    }
};

//
// BEAT SYNC
//
// Note that the controller sends different signals for a short press and a long
// press of the same button.
//

PioneerDDJFLX4.syncPressed = function(channel, control, value, status, group) {
    if (engine.getValue(group, "sync_enabled") && value > 0) {
        engine.setValue(group, "sync_enabled", 0);
    } else {
        engine.setValue(group, "beatsync", value);
    }
};

PioneerDDJFLX4.syncLongPressed = function(channel, control, value, status, group) {
    if (value) {
        engine.setValue(group, "sync_enabled", 1);
    }
};

PioneerDDJFLX4.cycleTempoRange = function(_channel, _control, value, _status, group) {
    if (value === 0) { return; } // ignore release

    const currRange = engine.getValue(group, "rateRange");
    let idx = 0;

    for (let i = 0; i < this.tempoRanges.length; i++) {
        if (currRange === this.tempoRanges[i]) {
            // idx get the index of the value in tempoRanges following the currently configured one
            // or cycle back to 0 if the current is the last value of the list.
            idx = (i + 1) % this.tempoRanges.length;
            break;
        }
    }
    engine.setValue(group, "rateRange", this.tempoRanges[idx]);
};

//
// Jog wheels
//

PioneerDDJFLX4.jogTurn = function(channel, _control, value, _status, group) {
    const deckNum = channel + 1;
    // wheel center at 64; <64 rew >64 fwd
    let newVal = value - 64;

    // loop_in / out adjust
    const loopEnabled = engine.getValue(group, "loop_enabled");
    if (loopEnabled > 0) {
        if (PioneerDDJFLX4.loopAdjustIn[channel]) {
            newVal = newVal * PioneerDDJFLX4.loopAdjustMultiply + engine.getValue(group, "loop_start_position");
            engine.setValue(group, "loop_start_position", newVal);
            return;
        }
        if (PioneerDDJFLX4.loopAdjustOut[channel]) {
            newVal = newVal * PioneerDDJFLX4.loopAdjustMultiply + engine.getValue(group, "loop_end_position");
            engine.setValue(group, "loop_end_position", newVal);
            return;
        }
    }

    if (engine.isScratching(deckNum)) {
        engine.scratchTick(deckNum, newVal);
    } else { // fallback
        engine.setValue(group, "jog", newVal * this.bendScale);
    }
};


PioneerDDJFLX4.jogSearch = function(_channel, _control, value, _status, group) {
    // Move the playhead instead of bending the rate. Setting "jog" feeds
    // RateControl as a temporary pitch change, which at high gain warps the
    // audio and sounds like scratching.
    const delta = (value - 64) * PioneerDDJFLX4.searchStep;
    const position = engine.getValue(group, "playposition");
    engine.setValue(group, "playposition", Math.min(1, Math.max(0, position + delta)));
};

PioneerDDJFLX4.jogTouch = function(channel, control, value) {
    const deckNum = channel + 1;

    // skip while adjusting the loop points
    if (PioneerDDJFLX4.loopAdjustIn[channel] || PioneerDDJFLX4.loopAdjustOut[channel]) {
        return;
    }

    // SHIFT + platter is a fast seek, so never engage the scratch engine.
    // Some firmware sends a dedicated touch note (0x67) for this, others keep
    // sending 0x36, so the shift button state is the reliable signal.
    if (PioneerDDJFLX4.shiftButtonDown[channel] || control === 0x67) {
        engine.scratchDisable(deckNum);
        return;
    }

    if (value !== 0 && this.vinylMode) {
        engine.scratchEnable(deckNum, 720, 33+1/3, this.alpha, this.beta);
    } else {
        engine.scratchDisable(deckNum);
    }
};

//
// Shift button
//

PioneerDDJFLX4.shiftPressed = function(channel, _control, value, _status, _group) {
    PioneerDDJFLX4.shiftButtonDown[channel] = value === 0x7F;
    if (value === 0x7F) {
        // Drop scratch mode if the platter was already grabbed, so SHIFT always
        // means "seek", never "scratch".
        engine.scratchDisable(channel + 1);
    }
};


//
// Tempo sliders
//
// The tempo option in Mixxx's deck preferences determine whether down/up
// increases/decreases the rate. Therefore it must be inverted here so that the
// UI and the control sliders always move in the same direction.
//

PioneerDDJFLX4.tempoSliderMSB = function(channel, control, value, status, group) {
    PioneerDDJFLX4.highResMSB[group].tempoSlider = value;
};

PioneerDDJFLX4.tempoSliderLSB = function(channel, control, value, status, group) {
    const fullValue = (PioneerDDJFLX4.highResMSB[group].tempoSlider << 7) + value;

    engine.setValue(
        group,
        "rate",
        1 - (fullValue / 0x2000)
    );
};

//
// Beat Jump mode
//
// Note that when we increase/decrease the sizes on the pad buttons, we use the
// value of the first pad (0x21) as an upper/lower limit beyond which we don't
// allow further increasing/decreasing of all the values.
//

PioneerDDJFLX4.beatjumpPadPressed = function(_channel, control, value, _status, group) {
    if (value === 0) {
        return;
    }
    engine.setValue(group, "beatjump_size", Math.abs(PioneerDDJFLX4.beatjumpSizeForPad[control]));
    engine.setValue(group, "beatjump", PioneerDDJFLX4.beatjumpSizeForPad[control]);
};

PioneerDDJFLX4.increaseBeatjumpSizes = function(_channel, control, value, _status, group) {
    if (value === 0 || PioneerDDJFLX4.beatjumpSizeForPad[0x21] * 16 > 16) {
        return;
    }
    Object.keys(PioneerDDJFLX4.beatjumpSizeForPad).forEach(function(pad) {
        PioneerDDJFLX4.beatjumpSizeForPad[pad] = PioneerDDJFLX4.beatjumpSizeForPad[pad] * 16;
    });
    engine.setValue(group, "beatjump_size", PioneerDDJFLX4.beatjumpSizeForPad[0x21]);
};

PioneerDDJFLX4.decreaseBeatjumpSizes = function(_channel, control, value, _status, group) {
    if (value === 0 || PioneerDDJFLX4.beatjumpSizeForPad[0x21] / 16 < 1/16) {
        return;
    }
    Object.keys(PioneerDDJFLX4.beatjumpSizeForPad).forEach(function(pad) {
        PioneerDDJFLX4.beatjumpSizeForPad[pad] = PioneerDDJFLX4.beatjumpSizeForPad[pad] / 16;
    });
    engine.setValue(group, "beatjump_size", PioneerDDJFLX4.beatjumpSizeForPad[0x21]);
};

//
// Sampler mode
//

PioneerDDJFLX4.samplerPlayOutputCallbackFunction = function(value, group, _control) {
    if (value === 1) {
        const curPad = group.match(script.samplerRegEx)[1];
        let deckIndex = 0;
        let padIndex = 0;

        if (curPad >=1 && curPad <= 4) {
            deckIndex = 0;
            padIndex = curPad - 1;
        } else if (curPad >=5 && curPad <= 8) {
            deckIndex = 2;
            padIndex = curPad - 5;
        } else if (curPad >=9 && curPad <= 12) {
            deckIndex = 0;
            padIndex = curPad - 5;
        } else if (curPad >=13 && curPad <= 16) {
            deckIndex = 2;
            padIndex = curPad - 9;
        }

        PioneerDDJFLX4.startSamplerBlink(
            0x97 + deckIndex,
            0x30 + padIndex,
            group);
    }
};

PioneerDDJFLX4.padModeKeyPressed = function(_channel, _control, value, _status, _group) {
    const deck = (_status === 0x90 ? PioneerDDJFLX4.lights.deck1 : PioneerDDJFLX4.lights.deck2);

    if (_control === 0x1B) {
        PioneerDDJFLX4.toggleLight(deck.hotcueMode, true);
    } else if (_control === 0x69) {
        PioneerDDJFLX4.toggleLight(deck.keyboardMode, true);
    } else if (_control === 0x1E) {
        PioneerDDJFLX4.toggleLight(deck.padFX1Mode, true);
    } else if (_control === 0x6B) {
        PioneerDDJFLX4.toggleLight(deck.padFX2Mode, true);
    } else if (_control === 0x20) {
        PioneerDDJFLX4.toggleLight(deck.beatJumpMode, true);
    } else if (_control === 0x6D) {
        PioneerDDJFLX4.toggleLight(deck.beatLoopMode, true);
    } else if (_control === 0x22) {
        PioneerDDJFLX4.toggleLight(deck.samplerMode, true);
    } else if (_control === 0x6F) {
        PioneerDDJFLX4.toggleLight(deck.keyShiftMode, true);
    }
};

// Stem control on the SAMPLER pads: 1 = vocals, 2 = instruments, 3 = bass,
// 4 = drums. Pads 5-8 are deliberately unbound. SHIFT+pad solos a stem.
PioneerDDJFLX4.stemPads = {
    0x30: {enabled: "stem_vocals_enabled", solo: "stem_vocals_solo"},
    0x31: {enabled: "stem_inst_enabled", solo: "stem_inst_solo"},
    0x32: {enabled: "stem_bass_enabled", solo: "stem_bass_solo"},
    0x33: {enabled: "stem_drums_enabled", solo: "stem_drums_solo"}
};

PioneerDDJFLX4.stemPadPressed = function(_channel, control, value, _status, group) {
    if (!value) {
        return;
    }
    const stem = PioneerDDJFLX4.stemPads[control];
    if (stem) {
        script.toggleControl(group, stem.enabled);
    }
};

PioneerDDJFLX4.stemSoloPadPressed = function(_channel, control, value, _status, group) {
    if (!value) {
        return;
    }
    const stem = PioneerDDJFLX4.stemPads[control];
    if (stem) {
        engine.setValue(group, stem.solo, 1);
    }
};

PioneerDDJFLX4.samplerPadPressed = function(_channel, _control, value, _status, group) {
    if (engine.getValue(group, "track_loaded")) {
        engine.setValue(group, "cue_gotoandplay", value);
    } else {
        engine.setValue(group, "LoadSelectedTrack", value);
    }
};

PioneerDDJFLX4.samplerPadShiftPressed = function(_channel, _control, value, _status, group) {
    if (engine.getValue(group, "play")) {
        engine.setValue(group, "cue_gotoandstop", value);
    } else if (engine.getValue(group, "track_loaded")) {
        engine.setValue(group, "eject", value);
    }
};

PioneerDDJFLX4.startSamplerBlink = function(channel, control, group) {
    let val = 0x7f;

    PioneerDDJFLX4.stopSamplerBlink(channel, control);
    PioneerDDJFLX4.timers[channel][control] = engine.beginTimer(250, () => {
        val = 0x7f - val;

        // blink the appropriate pad
        midi.sendShortMsg(channel, control, val);
        // also blink the pad while SHIFT is pressed
        midi.sendShortMsg((channel+1), control, val);

        const isPlaying = engine.getValue(group, "play") === 1;

        if (!isPlaying) {
            // kill timer
            PioneerDDJFLX4.stopSamplerBlink(channel, control);
            // set the pad LED to ON
            midi.sendShortMsg(channel, control, 0x7f);
            // set the pad LED to ON while SHIFT is pressed
            midi.sendShortMsg((channel+1), control, 0x7f);
        }
    });
};

PioneerDDJFLX4.stopSamplerBlink = function(channel, control) {
    PioneerDDJFLX4.timers[channel] = PioneerDDJFLX4.timers[channel] || {};

    if (PioneerDDJFLX4.timers[channel][control] !== undefined) {
        engine.stopTimer(PioneerDDJFLX4.timers[channel][control]);
        PioneerDDJFLX4.timers[channel][control] = undefined;
    }
};


PioneerDDJFLX4.toggleQuantize = function(_channel, _control, value, _status, group) {
    if (value) {
        script.toggleControl(group, "quantize");
    }
};

PioneerDDJFLX4.quickJumpForward = function(_channel, _control, value, _status, group) {
    if (value) {
        engine.setValue(group, "beatjump", PioneerDDJFLX4.quickJumpSize);
    }
};

PioneerDDJFLX4.quickJumpBack = function(_channel, _control, value, _status, group) {
    if (value) {
        engine.setValue(group, "beatjump", -PioneerDDJFLX4.quickJumpSize);
    }
};

//
// Shutdown
//

PioneerDDJFLX4.shutdown = function() {
    // reset vumeter
    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.deck1.vuMeter, false);
    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.deck2.vuMeter, false);

    // housekeeping
    // turn off all Sampler LEDs
    for (var i = 0; i <= 7; ++i) {
        midi.sendShortMsg(0x97, 0x30 + i, 0x00);    // Deck 1 pads
        midi.sendShortMsg(0x98, 0x30 + i, 0x00);    // Deck 1 pads with SHIFT
        midi.sendShortMsg(0x99, 0x30 + i, 0x00);    // Deck 2 pads
        midi.sendShortMsg(0x9A, 0x30 + i, 0x00);    // Deck 2 pads with SHIFT
    }
    // turn off all Hotcue LEDs
    for (i = 0; i <= 7; ++i) {
        midi.sendShortMsg(0x97, 0x00 + i, 0x00);    // Deck 1 pads
        midi.sendShortMsg(0x98, 0x00 + i, 0x00);    // Deck 1 pads with SHIFT
        midi.sendShortMsg(0x99, 0x00 + i, 0x00);    // Deck 2 pads
        midi.sendShortMsg(0x9A, 0x00 + i, 0x00);    // Deck 2 pads with SHIFT
    }

    // turn off loop in and out lights
    PioneerDDJFLX4.setLoopButtonLights(0x90, 0x00);
    PioneerDDJFLX4.setLoopButtonLights(0x91, 0x00);

    // turn off reloop lights
    PioneerDDJFLX4.setReloopLight(0x90, 0x00);
    PioneerDDJFLX4.setReloopLight(0x91, 0x00);

    // stop any flashing lights
    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.beatFx, false);
    PioneerDDJFLX4.toggleLight(PioneerDDJFLX4.lights.shiftBeatFx, false);

    // stop the keepalive timer
    engine.stopTimer(PioneerDDJFLX4.keepAliveTimer);
};