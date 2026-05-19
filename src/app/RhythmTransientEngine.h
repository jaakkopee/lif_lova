#pragma once

#include <array>
#include <string>

class RhythmTransientEngine {
public:
    static constexpr int NUM_BINS = 16;
    static constexpr int CYCLE_STEPS = 16;

    enum class Strategy {
        Divisive = 0,
        Additive,
        Hybrid,
    };

    RhythmTransientEngine();

    void setEnabled(bool enabled);
    bool enabled() const { return enabled_; }
    void toggleEnabled();

    void cycleStrategy();
    Strategy strategy() const { return strategy_; }
    const char* strategyName() const;

    void nudgeBpm(float delta);
    float bpm() const { return bpm_; }

    void nudgeGain(float delta);
    float gain() const { return gain_; }

    const std::string& divisiveSpec() const { return divisiveSpec_; }
    const std::string& additiveSpec() const { return additiveSpec_; }
    const std::string& weightSpec() const { return weightSpec_; }
    const std::string& lengthSpec() const { return lengthSpec_; }

    bool applyPatternSpecs(const std::string& divisive,
                           const std::string& additive,
                           const std::string& weights,
                           const std::string& lengths,
                           std::string& errorOut);

    std::array<float, NUM_BINS> advance(float dtSeconds);

private:
    bool enabled_ = false;
    Strategy strategy_ = Strategy::Hybrid;
    float bpm_ = 124.0f;
    float gain_ = 0.85f;
    float phaseSteps_ = 0.0f;

    std::string divisiveSpec_;
    std::string additiveSpec_;
    std::string weightSpec_;
    std::string lengthSpec_;

    std::array<int, 8> divisiveDivs_ = {4, 8, 0, 0, 0, 0, 0, 0};
    int divisiveCount_ = 2;

    std::array<int, 8> additiveGroups_ = {3, 3, 2, 4, 4, 0, 0, 0};
    int additiveCount_ = 5;

    std::array<float, CYCLE_STEPS> stepWeights_ = {};
    std::array<float, CYCLE_STEPS> stepLengths_ = {};

    std::array<float, NUM_BINS> envelope_ = {};
    std::array<float, NUM_BINS> holdSeconds_ = {};

    static std::string trim(const std::string& in);
    static bool parseIntCsv(const std::string& text,
                            std::array<int, 8>& out,
                            int& outCount,
                            int minValue,
                            int maxValue,
                            int minCount,
                            int maxCount,
                            std::string& errorOut);
    static bool parseFloatCsv16(const std::string& text,
                                std::array<float, CYCLE_STEPS>& out,
                                float minValue,
                                float maxValue,
                                std::string& errorOut);
    void setDefaultSpecs();
    void triggerStep(int stepIdx);
};
