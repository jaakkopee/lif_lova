#include "RhythmTransientDriver.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinBpm = 30.0f;
constexpr float kMaxBpm = 260.0f;
constexpr float kMinIntensity = 0.0f;
constexpr float kMaxIntensity = 1.0f;
constexpr int kMinLanePulses = 1;
constexpr int kMaxLanePulses = 32;
constexpr float kMinLaneGain = 0.0f;
constexpr float kMaxLaneGain = 2.0f;

constexpr float kCycleBeats = 4.0f;
constexpr float kEnvelopeDecayPerSecond = 10.0f;

int laneDefaultPulses(int laneIdx) {
    switch (laneIdx) {
        case 0: return 16;
        case 1: return 12;
        case 2: return 3;
        default: return 16;
    }
}
}

RhythmTransientDriver::RhythmTransientDriver() {
    rebuildPattern();
}

void RhythmTransientDriver::setEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        env_ = 0.0f;
        outBands_.fill(0.0f);
        outRms_ = 0.0f;
    }
}

void RhythmTransientDriver::setMixMode(MixMode mode) {
    mixMode_ = mode;
}

void RhythmTransientDriver::setPattern(Pattern pattern) {
    pattern_ = pattern;
    hasPendingPattern_ = false;
    rebuildPattern();
}

void RhythmTransientDriver::requestPattern(Pattern pattern, bool quantized) {
    if (!quantized) {
        setPattern(pattern);
        return;
    }
    pendingPattern_ = pattern;
    hasPendingPattern_ = true;
}

void RhythmTransientDriver::setBpm(float bpm) {
    bpm_ = std::clamp(bpm, kMinBpm, kMaxBpm);
}

void RhythmTransientDriver::nudgeBpm(float delta) {
    setBpm(bpm_ + delta);
}

void RhythmTransientDriver::setIntensity(float intensity) {
    intensity_ = std::clamp(intensity, kMinIntensity, kMaxIntensity);
}

void RhythmTransientDriver::nudgeIntensity(float delta) {
    setIntensity(intensity_ + delta);
}

void RhythmTransientDriver::toggleLaneEnabled(int laneIdx) {
    if (laneIdx < 0 || laneIdx >= MAX_LANES) return;
    lanes_[static_cast<size_t>(laneIdx)].enabled = !lanes_[static_cast<size_t>(laneIdx)].enabled;
}

void RhythmTransientDriver::setLaneEnabled(int laneIdx, bool enabled) {
    if (laneIdx < 0 || laneIdx >= MAX_LANES) return;
    lanes_[static_cast<size_t>(laneIdx)].enabled = enabled;
}

void RhythmTransientDriver::nudgeLanePulses(int laneIdx, int delta) {
    if (laneIdx < 0 || laneIdx >= MAX_LANES || delta == 0) return;
    auto& lane = lanes_[static_cast<size_t>(laneIdx)];
    lane.pulses = std::clamp(lane.pulses + delta, kMinLanePulses, kMaxLanePulses);
}

void RhythmTransientDriver::nudgeLaneGain(int laneIdx, float delta) {
    if (laneIdx < 0 || laneIdx >= MAX_LANES || delta == 0.0f) return;
    auto& lane = lanes_[static_cast<size_t>(laneIdx)];
    lane.gain = std::clamp(lane.gain + delta, kMinLaneGain, kMaxLaneGain);
}

float RhythmTransientDriver::wrap01(float x) {
    x = std::fmod(x, 1.0f);
    if (x < 0.0f) x += 1.0f;
    return x;
}

void RhythmTransientDriver::addTrigger(float phase, float weight, int lane) {
    if (triggerCount_ >= static_cast<int>(triggers_.size())) return;
    triggers_[static_cast<size_t>(triggerCount_)] = {wrap01(phase), std::clamp(weight, 0.0f, 1.5f), std::clamp(lane, 0, MAX_LANES - 1)};
    ++triggerCount_;
}

void RhythmTransientDriver::rebuildPattern() {
    triggerCount_ = 0;
    lanes_[0] = {"Pulse", true, 16, 1.0f};
    lanes_[1] = {"Timeline", true, 12, 0.9f};
    lanes_[2] = {"Tuplet", true, 3, 0.8f};

    switch (pattern_) {
        case Pattern::Metronome4:
            lanes_[1].enabled = false;
            lanes_[2].enabled = false;
            addTrigger(0.00f, 1.00f, 0);
            addTrigger(0.25f, 0.45f, 0);
            addTrigger(0.50f, 0.55f, 0);
            addTrigger(0.75f, 0.45f, 0);
            bandShape_ = {0.25f, 0.35f, 0.55f, 0.75f, 1.00f, 0.75f, 0.50f, 0.30f};
            break;
        case Pattern::BackbeatRock:
            lanes_[1].enabled = false;
            lanes_[2].enabled = false;
            addTrigger(0.00f, 0.60f, 0);
            addTrigger(0.25f, 1.00f, 0);
            addTrigger(0.50f, 0.60f, 0);
            addTrigger(0.75f, 1.00f, 0);
            bandShape_ = {0.45f, 0.55f, 0.70f, 0.90f, 1.00f, 0.85f, 0.65f, 0.40f};
            break;
        case Pattern::ReggaeThird:
            lanes_[1].enabled = false;
            lanes_[2].enabled = false;
            addTrigger(0.00f, 0.30f, 0);
            addTrigger(0.25f, 0.35f, 0);
            addTrigger(0.50f, 1.20f, 0); // weighted third beat
            addTrigger(0.75f, 0.35f, 0);
            bandShape_ = {0.30f, 0.40f, 0.55f, 0.80f, 1.00f, 0.90f, 0.75f, 0.55f};
            break;
        case Pattern::GamelanEnd:
            lanes_[1].enabled = false;
            lanes_[2].enabled = false;
            addTrigger(0.00f, 0.30f, 0);
            addTrigger(0.125f, 0.22f, 0);
            addTrigger(0.25f, 0.28f, 0);
            addTrigger(0.375f, 0.25f, 0);
            addTrigger(0.50f, 0.32f, 0);
            addTrigger(0.625f, 0.28f, 0);
            addTrigger(0.75f, 0.40f, 0);
            addTrigger(0.875f, 1.30f, 0); // terminal weight
            bandShape_ = {0.20f, 0.30f, 0.45f, 0.70f, 0.95f, 1.00f, 0.85f, 0.65f};
            break;
        case Pattern::Bell12:
            // 12-pulse bell-like asymmetry mapped to 4/4 cycle.
            lanes_[0].enabled = false;
            lanes_[2].enabled = false;
            lanes_[1].pulses = 12;
            addTrigger(0.00f, 1.00f, 1);   // pulse 0
            addTrigger(2.0f / 12.0f, 0.45f, 1);
            addTrigger(4.0f / 12.0f, 0.78f, 1);
            addTrigger(6.0f / 12.0f, 0.55f, 1);
            addTrigger(7.0f / 12.0f, 0.68f, 1);
            addTrigger(9.0f / 12.0f, 0.85f, 1);
            addTrigger(11.0f / 12.0f, 0.58f, 1);
            bandShape_ = {0.35f, 0.45f, 0.60f, 0.82f, 1.00f, 0.92f, 0.72f, 0.55f};
            break;
        case Pattern::ClaveLike:
            // 3-2 style over two bars compressed into one cycle.
            lanes_[0].enabled = false;
            lanes_[2].enabled = false;
            addTrigger(0.00f, 1.00f, 1);
            addTrigger(0.1875f, 0.72f, 1);
            addTrigger(0.375f, 0.80f, 1);
            addTrigger(0.625f, 0.90f, 1);
            addTrigger(0.8125f, 0.78f, 1);
            bandShape_ = {0.28f, 0.38f, 0.52f, 0.78f, 1.00f, 0.88f, 0.68f, 0.48f};
            break;
        case Pattern::Triplet3Over4:
            // 4-step grid + triplet overlay.
            lanes_[0].enabled = true;
            lanes_[1].enabled = false;
            lanes_[2].enabled = true;
            lanes_[2].pulses = 3;
            addTrigger(0.00f, 0.90f, 0);
            addTrigger(0.25f, 0.55f, 0);
            addTrigger(0.50f, 0.72f, 0);
            addTrigger(0.75f, 0.55f, 0);
            addTrigger(1.0f / 3.0f, 0.65f, 2);
            addTrigger(2.0f / 3.0f, 0.85f, 2);
            bandShape_ = {0.38f, 0.48f, 0.62f, 0.78f, 0.95f, 1.00f, 0.82f, 0.62f};
            break;
    }

    std::sort(triggers_.begin(), triggers_.begin() + triggerCount_,
              [](const Trigger& a, const Trigger& b) { return a.phase < b.phase; });
}

void RhythmTransientDriver::tick(float dtSeconds) {
    dtSeconds = std::clamp(dtSeconds, 0.0f, 0.1f);

    if (!enabled_) {
        env_ = 0.0f;
        outBands_.fill(0.0f);
        outRms_ = 0.0f;
        return;
    }

    const float prev = cyclePhase_;
    const float beatsDelta = dtSeconds * bpm_ / 60.0f;
    const float phaseDelta = beatsDelta / kCycleBeats;
    cyclePhase_ = wrap01(cyclePhase_ + phaseDelta);

    bool wrapped = cyclePhase_ < prev;
    float impulse = 0.0f;

    for (int i = 0; i < triggerCount_; ++i) {
        const Trigger& t = triggers_[static_cast<size_t>(i)];
        bool fired = false;
        if (!wrapped) {
            fired = (t.phase > prev && t.phase <= cyclePhase_);
        } else {
            fired = (t.phase > prev && t.phase <= 1.0f) || (t.phase >= 0.0f && t.phase <= cyclePhase_);
        }
        if (fired && lanes_[static_cast<size_t>(t.lane)].enabled) {
            const Lane& lane = lanes_[static_cast<size_t>(t.lane)];
            const float gain = std::clamp(lane.gain, kMinLaneGain, kMaxLaneGain);
            const float pulseScale = std::clamp(static_cast<float>(lane.pulses)
                                                / static_cast<float>(laneDefaultPulses(t.lane)),
                                                0.5f,
                                                1.5f);
            impulse += t.weight * gain * pulseScale;
        }
    }

    if (wrapped && hasPendingPattern_) {
        pattern_ = pendingPattern_;
        hasPendingPattern_ = false;
        rebuildPattern();
    }

    env_ *= std::exp(-kEnvelopeDecayPerSecond * dtSeconds);
    env_ = std::clamp(env_ + impulse * 0.42f * intensity_, 0.0f, 1.0f);

    outRms_ = env_;
    for (int i = 0; i < 8; ++i)
        outBands_[static_cast<size_t>(i)] = std::clamp(env_ * bandShape_[static_cast<size_t>(i)], 0.0f, 1.0f);
}

const char* RhythmTransientDriver::mixModeName(MixMode mode) {
    switch (mode) {
        case MixMode::AudioOnly: return "Audio";
        case MixMode::RhythmOnly: return "Rhythm";
        case MixMode::Hybrid: return "Hybrid";
        default: return "Audio";
    }
}

const char* RhythmTransientDriver::patternName(Pattern pattern) {
    switch (pattern) {
        case Pattern::Metronome4: return "Metronome";
        case Pattern::BackbeatRock: return "Rock Backbeat";
        case Pattern::ReggaeThird: return "Reggae 3rd";
        case Pattern::GamelanEnd: return "Gamelan End";
        case Pattern::Bell12: return "Bell 12";
        case Pattern::ClaveLike: return "Clave-like";
        case Pattern::Triplet3Over4: return "3 over 4";
        default: return "Metronome";
    }
}
