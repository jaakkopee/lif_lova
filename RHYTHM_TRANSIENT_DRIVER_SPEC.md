# Rhythm Transient Driver Spec (Silent LIF Excitation)

Date: 2026-05-16
Status: Draft for approval before implementation

## 1. Goal
Add a silent rhythm engine that generates synthetic transients and injects them into the LIF simulation input path.

The rhythm engine must support:
- Simple metronome pulses
- International rhythm timelines (African bell-like, Cuban clave-like, reggae off/accent patterns, gamelan end-weighted cycles, western rock/blues/art-music inspired forms)
- Tuplets and cross-rhythms (for example 3 against 4)
- Per-scene and live user selection

The generated transients are not sent to audio output. They only modulate LIF drive.

## 2. Non-goals (phase 1)
- No full DAW-style sequencer
- No sample playback
- No audio rendering of rhythm clicks
- No network sync / external clock sync in first version

## 3. Integration points in current codebase
Primary integration targets:
- App frame loop and scene state orchestration: src/app/App.mm
- LIF driver ingestion and compositor handoff: src/app/MetalCompositor.h, src/app/MetalCompositor.mm
- User controls panel (initial controls): src/app/AudioMidiControlWindow.h, src/app/AudioMidiControlWindow.cpp

Potential future dedicated editor window:
- New files (planned): src/app/RhythmPatternWindow.h, src/app/RhythmPatternWindow.cpp

## 4. Architecture overview
Add a new runtime subsystem:
- RhythmTransientDriver

Responsibilities:
1. Maintain transport phase (BPM, cycle position, step/tick timing)
2. Evaluate one or more rhythm lanes
3. Emit transient events and per-frame envelope energy
4. Produce a compact excitation vector for LIF input

High-level data flow each frame:
1. App ticks RhythmTransientDriver with dt
2. Driver updates lane phases and triggers events
3. Driver computes current envelope energy per lane and merged output
4. App mixes excitation with existing real-audio-derived drive
5. Mixed drive is passed to LIF simulation path

Mix mode options:
- AudioOnly
- RhythmOnly
- AudioPlusRhythm

## 5. Rhythm data model

### 5.1 Pattern container
RhythmPattern:
- name: string
- basePulses: int (for UI quantization reference, for example 16)
- lanes: vector<RhythmLane>
- defaultBpm: float
- description: string
- tags: vector<string>

### 5.2 Lane model
RhythmLane:
- laneName: string
- pulsesPerCycle: int (for example 12, 16, 24)
- events: vector<RhythmEvent>
- gain: float (0..1)
- probabilityBias: float (0..1)
- microTimingMs: float (global lane shift)
- swingAmount: float (-1..1)
- enabled: bool
- targetMask: bitmask or enum routing to LIF bins/regions

### 5.3 Event model
RhythmEvent:
- pulseIndex: int
- weight: float (accent strength 0..1)
- probability: float (0..1)
- durationPulses: float (usually short, but allows sustained impulses)
- shape: enum (ExpDecay, LinearDecay, Gaussian)
- microOffsetMs: float

## 6. Timing and math

### 6.1 Transport
Given:
- bpm
- pulsesPerQuarter (PPQ) set by lane resolution mapping

Per frame:
- beatDelta = dt * bpm / 60
- pulseDeltaLane = beatDelta * lanePulsesPerBeat
- lanePhase += pulseDeltaLane

When lanePhase crosses an event pulse boundary (with wrap), trigger event.

### 6.2 Tuplets and 3:4 support
Tuplets are represented by lane resolution, not special-case logic.
Examples:
- 4-step pulse lane over quarter-note beat grid
- 3-step triplet lane running over same cycle length

3:4 cross-rhythm is achieved by running two active lanes:
- lane A: 4 subdivision pattern
- lane B: 3 subdivision pattern

### 6.3 Swing and microtiming
Apply timing offsets before trigger checks:
- Swing shifts off-beat pulses by +/- swing fraction
- microOffsetMs shifts event-specific trigger time

### 6.4 Envelope generation
On trigger, create a transient envelope instance:
- peak = event.weight * lane.gain * globalIntensity
- envelope(t) based on selected shape and decay constants

Frame output:
- sum active envelopes (clamped 0..1)
- optionally per-target vectors for spatial/tonal routing

## 7. LIF injection strategy
Add rhythm excitation as an additional drive signal entering LIF update.

Recommended first injection model:
- Global scalar excitation value E(t) mixed into existing LIF drive term
- Optional lane routing to selected LIF tone bins (phase 2)

Mix equation (conceptual):
- drive = (audioGain * audioDrive) + (rhythmGain * E(t))
- clamp drive to valid range before LIF step

Scene/local control:
- Each scene can override rhythmGain and selected pattern

## 8. Preset families (initial)
Provide preset templates with editable lanes/events.

1. Metronome 4
- Single lane, pulsesPerCycle 4
- Strong on 1, equal or weaker on others

2. Backbeat Rock
- 8 or 16 pulse lane
- Accents on 2 and 4

3. Blues Shuffle feel
- Triplet-influenced lane with uneven long/short spacing

4. Reggae third-weight
- Stronger weight on 3rd beat position in cycle

5. Clave-like 3-2 / 2-3
- Two timeline variants in 8 or 16 pulse abstraction

6. West African bell-like timeline
- 12 pulse timeline with signature asymmetric accents

7. Gamelan end-weighted cycle
- Strong terminal accent near cycle end, lighter interior punctuation

8. Art-music asymmetric meter
- Example 7 or 11 pulse structures with evolving accents

Note: names should be descriptive and respectful; avoid claiming strict ethnomusicological authenticity unless exact canonical forms are implemented and documented.

## 9. UI/UX specification

## 9.1 Initial controls (in Audio/MIDI window)
Add compact controls:
- Rhythm Driver On/Off
- Mix Mode: AudioOnly / RhythmOnly / Hybrid
- Pattern Select
- BPM
- Global Intensity
- Lane Density/Complexity macro (phase 1 shortcut)

## 9.2 Advanced editor (new Rhythm Pattern window)
Planned advanced window features:
- Lane list with enable/mute/solo
- Step grid per lane (variable pulses per lane)
- Accent editing (weight per hit)
- Probability per event
- Swing/microtiming per lane
- Tuplet helper (create 3-over-4 lane quickly)
- Preset load/save/duplicate

## 9.3 Live performance behavior
- Pattern changes quantized at next cycle boundary (toggleable)
- Optional immediate mode for aggressive live switching
- Scene recall can load pattern and rhythm params

## 10. State persistence
State file extension (next version bump) should include:
- global rhythm enabled/mix mode/intensity/bpm
- per-scene selected pattern ID
- per-scene rhythm gain
- custom pattern library payload (if user-edited patterns are supported in phase 2)

## 11. Safety and performance
- No dynamic allocations in hot path after initialization
- Cap active envelopes per lane (for example 32)
- Deterministic RNG for probability events (seeded, optionally scene-seeded)
- Clamp all outputs to valid ranges

## 12. Implementation phases

Phase 1: Core silent driver
- Add RhythmTransientDriver class
- One global pattern at a time
- Metronome + few fixed presets
- Global scalar excitation injection
- Minimal controls in Audio/MIDI window

Phase 2: Multi-lane + tuplets
- Lane editor model in memory
- 3:4 and arbitrary cross-rhythms through lane resolutions
- Event probability and microtiming

Phase 3: Dedicated pattern editor window
- Create RhythmPatternWindow
- Full editing UI and pattern library management

Phase 4: Scene integration and morphing
- Scene-level rhythm assignment
- Pattern morph and evolution macros

## 13. Open decisions for approval
1. Should BPM be global only or scene-local capable from phase 1?
2. Should pattern switching default to cycle-quantized or immediate?
3. Should phase 1 include a dedicated window, or only controls in Audio/MIDI panel?
4. Should non-diatonic presets be neutral names first, with optional culturally specific labels later?

## 14. Proposed immediate next coding task
Implement Phase 1 minimal vertical slice:
- New RhythmTransientDriver runtime class
- Fixed preset enum (Metronome, Backbeat, Reggae3, ClaveLike, Bell12, EndWeighted, Triplet34)
- App integration with RhythmOnly/Hybrid mix
- 5-6 UI controls in Audio/MIDI window
- Build and runtime sanity test
