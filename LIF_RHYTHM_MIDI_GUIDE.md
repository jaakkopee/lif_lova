# LIF Neuron, Rhythm Driver, and MIDI Guide

This guide explains, step by step, how the LIF system in `lif_lova` works, how rhythm transients drive it, and how MIDI is generated from LIF activity.

## 1) Big picture signal flow

Each frame (`App::processFrame`):

1. Live audio analysis is read (`liveBands[8]`, `liveRms`) if audio is enabled.
2. `RhythmTransientDriver::tick()` advances synthetic rhythm transients.
3. Audio + rhythm are mixed to `mixedBands[8]`, `mixedRms` using selected mix mode:
   - `AudioOnly`
   - `RhythmOnly`
   - `Hybrid` (sum and clamp)
4. Mixed values are sent to the compositor and LIF path.
5. LIF output is sampled as 16 vertical bins.
6. Those bins feed:
   - tone synth (`LIFToneSynth`) and/or
   - LIF-to-MIDI note/chord generation.

Primary code:
- `/home/runner/work/lif_lova/lif_lova/src/app/App.mm` (frame loop, mixing, MIDI)
- `/home/runner/work/lif_lova/lif_lova/src/app/RhythmTransientDriver.cpp` (rhythm excitation)
- `/home/runner/work/lif_lova/lif_lova/src/app/LIFNetwork.mm` and `.../shaders/vjay_shaders.metal` (LIF simulation)

## 2) How one LIF neuron works in this project

The GPU kernel `lif_step` stores per-neuron state as:

- `x`: membrane potential
- `y`: spike flag (1 when fired this step)
- `z`: refractory time left
- `w`: last spike time

Update logic per step (`lif_step`):

1. Read previous state.
2. Compute synaptic input from all neurons:
   - `synaptic = Σ_j weights[i,j] * prevSpike[j]`
3. Build external drive:
   - `drive = inputCurrent[i] + luminanceTerm + noise`
4. Dynamic threshold:
   - `thresholdEff = max(0.12, threshold - rms*0.12)`
5. If not refractory:
   - `membrane = membrane*(1 - leak*dt) + (drive + synaptic)*dt`
   - clamp membrane to `[0, 2]`
   - if `membrane >= thresholdEff`: spike, reset membrane, set refractory timer
6. If refractory:
   - membrane relaxes toward reset.

In plain terms: neurons integrate drive over time, leak energy away, fire when threshold is reached, then briefly rest (refractory).

## 3) How rhythm transient energy is generated

`RhythmTransientDriver` creates impulses from pattern triggers, then converts them to an envelope.

### 3.1 Trigger impulse

When a trigger fires:

`impulse += weight * laneGain * pulseScale`

Where:
- `weight` = per-hit accent (pattern-defined)
- `laneGain` = user lane gain (0..2)
- `pulseScale = clamp(lanePulses / laneDefaultPulses, 0.5, 1.5)`

### 3.2 Envelope update

Per frame:

- decay: `env *= exp(-10 * dt)`
- injection: `env = clamp(env + impulse * 0.42 * intensity, 0, 1)`

At 60 FPS:
- `dt ≈ 1/60`
- per-frame decay factor `exp(-10/60) ≈ 0.846`
- envelope half-life is about `ln(2)/10 ≈ 69 ms`

### 3.3 Output to rhythm bands

- `outRms = env`
- `outBands[i] = clamp(env * bandShape[i], 0, 1)`

So rhythm transients become the same kind of 8-band + RMS drive format as live audio.

## 4) How much transient energy is needed to drive the network

This section gives practical math from current code.

## 4.1 Immediate envelope jump from one hit

For one trigger in one frame (ignoring decay and previous env):

`Δenv ≈ 0.42 * intensity * weight * laneGain * pulseScale`

With current defaults (`intensity=0.55`, `laneGain=1`, `pulseScale=1`):
- weight 1.00 hit -> `Δenv ≈ 0.231`
- weight 0.45 hit -> `Δenv ≈ 0.104`
- weight 1.20 hit -> `Δenv ≈ 0.277`

This tells you how hard a single transient pushes rhythm RMS/bands.

## 4.2 From envelope to LIF input current

In `LIFNetwork::step`, for neuron group `g`:

- `influenceDrive = 0.35 + 1.65*influence`
- `bandDrive = band[g] * (0.30 + 1.70*rms)`
- `cross = band[(g+3)%8] * 0.15`
- `inputCurrent = (bandDrive + cross) * influenceDrive`

Under `RhythmOnly` mode:
- `rms = env`
- `band[g] ≈ env * bandShape[g]`

So a useful approximation is:

`inputCurrent ≈ env * bandShape[g] * (0.30 + 1.70*env) * (0.35 + 1.65*influence) + crossTerm`

(`crossTerm` is smaller and positive.)

## 4.3 Practical target ranges for reliable activation

Because membrane integrates with leak/refractory and also receives luminance + recurrent synaptic input, there is no single hard threshold on `env`.  
In practice from current equations:

- **low influence (~0.2):** aim `env` roughly `0.5+` for strong driving
- **mid influence (~0.5):** aim `env` roughly `0.3+`
- **high influence (~0.8-1.0):** `env` around `0.2-0.3` can already drive activity well

If network appears too quiet, increase in this order:
1. rhythm `intensity`
2. lane gain of active lane(s)
3. LIF patch `influence` parameter

## 5) Rhythm driver controls and concepts

Current rhythm features:

- enable/disable driver
- mix mode (Audio / Rhythm / Hybrid)
- pattern cycling
- BPM
- global intensity
- per-lane enable
- per-lane pulse count
- per-lane gain

Patterns currently implemented:
- Metronome
- Rock Backbeat
- Reggae 3rd
- Gamelan End
- Bell 12
- Clave-like
- 3 over 4

Pattern switching is quantized to cycle wrap when requested quantized (`requestPattern(..., true)`).

## 6) How MIDI is generated from LIF bins

After compositing, app samples one LIF column:

- `lifBins = compositor_.sampleLIFColumn(scanPhase)` (16 bins, 0..1)

For each bin:

1. Compute energy and derived drive:
   - `drive = clamp(0.75*energy + 0.25*prevEnergy, 0, 1)`
2. Classify harmonic function (`Tonic`, `Subdominant`, `Dominant`) from:
   - bin index
   - relative energy vs max bin energy
   - contour (`energy - prevEnergy`)
   - feedforward topology flag
3. Build notes/chords (`lifMidiNotesForFunction`) using:
   - current key + tonal root + modal scale
   - selected style (`Pop`, `Rock`, `Jazz`, `Blues`, `Percussion`)
   - configured MIDI range clamp (`lifMidiRangeMin_..lifMidiRangeMax_`)
4. Compute velocity and send NoteOn/NoteOff.

## 6.1 Note gating thresholds (non-feedforward)

Per frame:

- `noteOnThreshold = max(0.18, maxEnergy*0.55)`
- `noteOffThreshold = max(0.12, maxEnergy*0.40)`

This hysteresis avoids chatter and adapts to low/high overall activity.

## 6.2 Feedforward special behavior

If scene LIF topology is feedforward:

- fire condition: cooldown is zero and `drive > 0.12`
- gate length: 2 frames
- cooldown interval:
  - `intervalFrames = clamp(14 - prevDrive*10 - energy*4, 2, 14)`
- velocity:
  - `velocity = clamp(30 + pow(drive,0.7)*97 + prevDrive*10, 30, 127)`

## 6.3 Non-feedforward velocity

When crossing into active state:

- `rel = energy / max(maxEnergy, 0.001)`
- `curved = pow(rel, 0.65)`
- `velocity = clamp(20 + curved*107, 20, 127)`

## 7) Step-by-step debugging checklist

If you want to verify end-to-end behavior in performance:

1. Confirm scene uses `LIF Modulate` or `LIF Replace`.
2. Enable rhythm driver and set `RhythmOnly`.
3. Start with `Metronome`, BPM ~100, intensity ~0.7.
4. Raise lane 0 gain if needed.
5. Raise LIF influence (FX param 1 on LIF patch).
6. Enable LIF MIDI output.
7. Watch for note activity and adjust:
   - if sparse: increase intensity/gain/influence
   - if dense: lower intensity or increase rhythmic sparsity.

## 8) Important implementation references

- Rhythm generation:
  - `/home/runner/work/lif_lova/lif_lova/src/app/RhythmTransientDriver.cpp`
- Audio/rhythm mixing + LIF bin -> MIDI:
  - `/home/runner/work/lif_lova/lif_lova/src/app/App.mm`
- LIF CPU-side parameterization:
  - `/home/runner/work/lif_lova/lif_lova/src/app/LIFNetwork.mm`
- LIF GPU update kernels:
  - `/home/runner/work/lif_lova/lif_lova/src/app/shaders/vjay_shaders.metal`

