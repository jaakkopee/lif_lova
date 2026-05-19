#include "RhythmTransientEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <vector>

RhythmTransientEngine::RhythmTransientEngine() {
    stepWeights_ = {
        1.00f, 0.28f, 0.45f, 0.33f,
        0.70f, 0.22f, 0.40f, 0.26f,
        0.88f, 0.24f, 0.50f, 0.30f,
        0.76f, 0.20f, 0.42f, 0.24f
    };
    stepLengths_ = {
        1.00f, 0.55f, 0.60f, 0.52f,
        0.90f, 0.50f, 0.62f, 0.54f,
        1.00f, 0.50f, 0.66f, 0.56f,
        0.94f, 0.52f, 0.60f, 0.50f
    };
    setDefaultSpecs();
}

void RhythmTransientEngine::setEnabled(bool enabled) {
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    if (!enabled_) {
        phaseSteps_ = 0.0f;
        envelope_.fill(0.0f);
        holdSeconds_.fill(0.0f);
    }
}

void RhythmTransientEngine::toggleEnabled() {
    setEnabled(!enabled_);
}

void RhythmTransientEngine::cycleStrategy() {
    strategy_ = static_cast<Strategy>((static_cast<int>(strategy_) + 1) % 3);
}

const char* RhythmTransientEngine::strategyName() const {
    switch (strategy_) {
        case Strategy::Divisive: return "Divisive";
        case Strategy::Additive: return "Additive";
        default: return "Hybrid";
    }
}

void RhythmTransientEngine::nudgeBpm(float delta) {
    bpm_ = std::clamp(bpm_ + delta, 30.0f, 280.0f);
}

void RhythmTransientEngine::nudgeGain(float delta) {
    gain_ = std::clamp(gain_ + delta, 0.0f, 2.0f);
}

bool RhythmTransientEngine::applyPatternSpecs(const std::string& divisive,
                                              const std::string& additive,
                                              const std::string& weights,
                                              const std::string& lengths,
                                              std::string& errorOut) {
    std::array<int, 8> nextDivisive = {};
    std::array<int, 8> nextAdditive = {};
    std::array<float, CYCLE_STEPS> nextWeights = {};
    std::array<float, CYCLE_STEPS> nextLengths = {};
    int nextDivisiveCount = 0;
    int nextAdditiveCount = 0;

    std::string divisiveTrimmed = trim(divisive);
    std::string additiveTrimmed = trim(additive);
    std::string weightTrimmed = trim(weights);
    std::string lengthTrimmed = trim(lengths);

    if (!parseIntCsv(divisiveTrimmed, nextDivisive, nextDivisiveCount, 1, CYCLE_STEPS, 1, 8, errorOut))
        return false;
    if (!parseIntCsv(additiveTrimmed, nextAdditive, nextAdditiveCount, 1, CYCLE_STEPS, 1, 8, errorOut))
        return false;
    if (!parseFloatCsv16(weightTrimmed, nextWeights, 0.0f, 2.0f, errorOut))
        return false;
    if (!parseFloatCsv16(lengthTrimmed, nextLengths, 0.1f, 3.0f, errorOut))
        return false;

    int additiveSum = 0;
    for (int i = 0; i < nextAdditiveCount; ++i)
        additiveSum += nextAdditive[i];
    if (additiveSum != CYCLE_STEPS) {
        errorOut = "Additive groups must sum to 16 steps.";
        return false;
    }

    divisiveDivs_ = nextDivisive;
    divisiveCount_ = nextDivisiveCount;
    additiveGroups_ = nextAdditive;
    additiveCount_ = nextAdditiveCount;
    stepWeights_ = nextWeights;
    stepLengths_ = nextLengths;

    divisiveSpec_ = divisiveTrimmed;
    additiveSpec_ = additiveTrimmed;
    weightSpec_ = weightTrimmed;
    lengthSpec_ = lengthTrimmed;

    errorOut.clear();
    return true;
}

std::array<float, RhythmTransientEngine::NUM_BINS> RhythmTransientEngine::advance(float dtSeconds) {
    std::array<float, NUM_BINS> bins = {};
    if (dtSeconds <= 0.0f)
        return bins;

    for (int b = 0; b < NUM_BINS; ++b) {
        holdSeconds_[b] = std::max(0.0f, holdSeconds_[b] - dtSeconds);
        const float decay = std::exp(-dtSeconds * 11.0f);
        envelope_[b] *= decay;
        if (holdSeconds_[b] > 0.0f)
            bins[b] = envelope_[b];
    }

    if (!enabled_)
        return bins;

    const float stepsPerSecond = (bpm_ / 60.0f) * 4.0f;
    const float prev = phaseSteps_;
    phaseSteps_ += dtSeconds * stepsPerSecond;

    int prevStep = static_cast<int>(std::floor(prev));
    int newStep = static_cast<int>(std::floor(phaseSteps_));
    int maxJump = std::max(0, newStep - prevStep);
    maxJump = std::min(maxJump, 8);
    for (int jump = 1; jump <= maxJump; ++jump) {
        int step = (prevStep + jump) % CYCLE_STEPS;
        if (step < 0)
            step += CYCLE_STEPS;
        triggerStep(step);
    }

    const float stepDur = 60.0f / bpm_ / 4.0f;
    (void)stepDur;

    for (int b = 0; b < NUM_BINS; ++b) {
        if (holdSeconds_[b] > 0.0f)
            bins[b] = std::max(bins[b], envelope_[b]);
        bins[b] = std::clamp(bins[b], 0.0f, 1.0f);
    }
    return bins;
}

void RhythmTransientEngine::triggerStep(int stepIdx) {
    bool useDivisive = (strategy_ == Strategy::Divisive || strategy_ == Strategy::Hybrid);
    bool useAdditive = (strategy_ == Strategy::Additive || strategy_ == Strategy::Hybrid);

    float baseWeight = 0.0f;
    float baseLength = 0.0f;
    bool onset = false;

    if (useDivisive) {
        for (int i = 0; i < divisiveCount_; ++i) {
            const int div = divisiveDivs_[i];
            if (div <= 0)
                continue;
            if ((stepIdx * div) % CYCLE_STEPS == 0) {
                onset = true;
                baseWeight += 0.70f + (0.60f / static_cast<float>(div));
                baseLength += std::max(1.0f, static_cast<float>(CYCLE_STEPS) / static_cast<float>(div));
            }
        }
    }

    if (useAdditive) {
        int cursor = 0;
        for (int i = 0; i < additiveCount_; ++i) {
            const int groupLen = additiveGroups_[i];
            if (groupLen <= 0)
                continue;
            if (stepIdx == cursor % CYCLE_STEPS) {
                onset = true;
                const float accent = (i % 2 == 0) ? 1.0f : 0.75f;
                baseWeight += accent;
                baseLength += static_cast<float>(groupLen);
            }
            cursor += groupLen;
        }
    }

    if (!onset)
        return;

    const float stepWeight = std::clamp(stepWeights_[stepIdx], 0.0f, 2.0f);
    const float stepLength = std::clamp(stepLengths_[stepIdx], 0.1f, 3.0f);

    const float combinedWeight = std::clamp((baseWeight * 0.58f + stepWeight * 0.85f) * gain_, 0.0f, 2.0f);
    const float lengthSteps = std::max(1.0f, baseLength * stepLength * 0.5f);
    const float stepSeconds = 60.0f / bpm_ / 4.0f;
    const float holdSeconds = lengthSteps * stepSeconds;

    const int bin = stepIdx % NUM_BINS;
    envelope_[bin] = std::max(envelope_[bin], combinedWeight);
    holdSeconds_[bin] = std::max(holdSeconds_[bin], holdSeconds);
}

std::string RhythmTransientEngine::trim(const std::string& in) {
    size_t begin = 0;
    while (begin < in.size() && std::isspace(static_cast<unsigned char>(in[begin])))
        ++begin;
    size_t end = in.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(in[end - 1])))
        --end;
    return in.substr(begin, end - begin);
}

bool RhythmTransientEngine::parseIntCsv(const std::string& text,
                                        std::array<int, 8>& out,
                                        int& outCount,
                                        int minValue,
                                        int maxValue,
                                        int minCount,
                                        int maxCount,
                                        std::string& errorOut) {
    out.fill(0);
    outCount = 0;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string token = trim(item);
        if (token.empty())
            continue;
        if (outCount >= maxCount) {
            errorOut = "Too many integer values.";
            return false;
        }
        try {
            const int value = std::stoi(token);
            if (value < minValue || value > maxValue) {
                errorOut = "Integer value out of range.";
                return false;
            }
            out[outCount++] = value;
        } catch (...) {
            errorOut = "Failed to parse integer list.";
            return false;
        }
    }

    if (outCount < minCount) {
        errorOut = "Too few integer values.";
        return false;
    }
    return true;
}

bool RhythmTransientEngine::parseFloatCsv16(const std::string& text,
                                            std::array<float, CYCLE_STEPS>& out,
                                            float minValue,
                                            float maxValue,
                                            std::string& errorOut) {
    out.fill(0.0f);
    std::stringstream ss(text);
    std::string item;
    int count = 0;
    while (std::getline(ss, item, ',')) {
        std::string token = trim(item);
        if (token.empty())
            continue;
        if (count >= CYCLE_STEPS) {
            errorOut = "Expected exactly 16 float values.";
            return false;
        }
        try {
            const float value = std::stof(token);
            if (value < minValue || value > maxValue) {
                errorOut = "Float value out of allowed range.";
                return false;
            }
            out[count++] = value;
        } catch (...) {
            errorOut = "Failed to parse float list.";
            return false;
        }
    }

    if (count != CYCLE_STEPS) {
        errorOut = "Expected exactly 16 float values.";
        return false;
    }
    return true;
}

void RhythmTransientEngine::setDefaultSpecs() {
    divisiveSpec_ = "4,8";
    additiveSpec_ = "3,3,2,4,4";
    weightSpec_ = "1.0,0.28,0.45,0.33,0.7,0.22,0.4,0.26,0.88,0.24,0.5,0.3,0.76,0.2,0.42,0.24";
    lengthSpec_ = "1.0,0.55,0.6,0.52,0.9,0.5,0.62,0.54,1.0,0.5,0.66,0.56,0.94,0.52,0.6,0.5";
}
