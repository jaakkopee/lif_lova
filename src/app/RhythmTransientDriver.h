#pragma once

#include <array>
#include <cstdint>
#include <string>

class RhythmTransientDriver {
public:
    static constexpr int MAX_LANES = 3;

    enum class MixMode {
        AudioOnly = 0,
        RhythmOnly,
        Hybrid,
    };

    enum class Pattern {
        Metronome4 = 0,
        BackbeatRock,
        ReggaeThird,
        GamelanEnd,
        Bell12,
        ClaveLike,
        Triplet3Over4,
    };

    RhythmTransientDriver();

    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }

    void setMixMode(MixMode mode);
    MixMode mixMode() const { return mixMode_; }

    void setPattern(Pattern pattern);
    void requestPattern(Pattern pattern, bool quantized);
    Pattern pattern() const { return pattern_; }
    bool hasPendingPattern() const { return hasPendingPattern_; }
    Pattern pendingPattern() const { return pendingPattern_; }

    void setBpm(float bpm);
    void nudgeBpm(float delta);
    float bpm() const { return bpm_; }

    void setIntensity(float intensity);
    void nudgeIntensity(float delta);
    float intensity() const { return intensity_; }

    struct Lane {
        std::string name;
        bool enabled = true;
        int pulses = 16;
        float gain = 1.0f;
    };

    const std::array<Lane, MAX_LANES>& lanes() const { return lanes_; }
    void setLaneEnabled(int laneIdx, bool enabled);
    void toggleLaneEnabled(int laneIdx);
    void nudgeLanePulses(int laneIdx, int delta);
    void nudgeLaneGain(int laneIdx, float delta);

    // Step transport/envelopes and produce transient-derived drive.
    void tick(float dtSeconds);

    const std::array<float, 8>& bands() const { return outBands_; }
    float rms() const { return outRms_; }

    static const char* mixModeName(MixMode mode);
    static const char* patternName(Pattern pattern);

private:
    struct Trigger {
        float phase = 0.0f; // [0, 1)
        float weight = 1.0f;
        int lane = 0;
    };

    bool enabled_ = false;
    MixMode mixMode_ = MixMode::AudioOnly;
    Pattern pattern_ = Pattern::Metronome4;
    Pattern pendingPattern_ = Pattern::Metronome4;
    bool hasPendingPattern_ = false;
    float bpm_ = 112.0f;
    float intensity_ = 0.55f;

    float cyclePhase_ = 0.0f; // [0, 1)
    float env_ = 0.0f;
    std::array<float, 8> outBands_ = {};
    float outRms_ = 0.0f;

    std::array<Trigger, 16> triggers_ = {};
    int triggerCount_ = 0;
    std::array<Lane, MAX_LANES> lanes_ = {};
    std::array<float, 8> bandShape_ = {0.30f, 0.45f, 0.65f, 0.90f, 1.00f, 0.85f, 0.60f, 0.40f};

    void rebuildPattern();
    void addTrigger(float phase, float weight, int lane);
    static float wrap01(float x);
};
