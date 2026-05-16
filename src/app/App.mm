#include "App.h"
#import  <AppKit/AppKit.h>  // NSScreen for display positions
#include <iostream>
#include <fstream>
#include <csignal>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>

// ── Signal handling: save state on Ctrl-C / kill ─────────────────────────────
static App* g_sigApp = nullptr;
static void appSigHandler(int) {
    if (g_sigApp) g_sigApp->saveState();
    std::_Exit(0);
}

// ── helpers ──────────────────────────────────────────────────────────────────

// Returns the origin (top-left in screen coords) of display N (0 = primary).
static sf::Vector2i screenOrigin(int displayIndex) {
    NSArray<NSScreen*>* screens = [NSScreen screens];
    if (displayIndex >= static_cast<int>(screens.count))
        return {0, 0};
    NSRect frame = screens[displayIndex].frame;
    // macOS Y is bottom-up; SFML Y is top-down.
    // Primary screen height for coordinate flip:
    CGFloat primaryH = screens[0].frame.size.height;
    return {
        static_cast<int>(frame.origin.x),
        static_cast<int>(primaryH - frame.origin.y - frame.size.height)
    };
}

static sf::Vector2i screenSize(int displayIndex) {
    NSArray<NSScreen*>* screens = [NSScreen screens];
    if (displayIndex >= static_cast<int>(screens.count))
        return {1920, 1080};
    NSRect frame = screens[displayIndex].frame;
    return {static_cast<int>(frame.size.width), static_cast<int>(frame.size.height)};
}

static bool isLIFPatch(FxPatchId patch) {
    return patch == FxPatchId::LIFModulate || patch == FxPatchId::LIFReplace;
}

static std::array<FxPatchId, NUM_FX_LAYERS> enforcedSceneFx(const Scene& sc) {
    std::array<FxPatchId, NUM_FX_LAYERS> fx = {sc.fx[0], sc.fx[1], sc.fx[2]};

    int lifCount = 0;
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
        if (!isLIFPatch(fx[slot])) continue;
        ++lifCount;
        if (lifCount > 2)
            fx[slot] = FxPatchId::Passthrough;
    }

    if (lifCount == 0)
        fx[NUM_FX_LAYERS - 1] = FxPatchId::LIFModulate;

    return fx;
}

static int topologyIndexFromNorm(float value) {
    return std::clamp(static_cast<int>(value * 5.0f), 0, 4);
}

static LIFNetwork::Topology topologyFromIndex(int index) {
    switch (std::clamp(index, 0, 4)) {
        case 0: return LIFNetwork::Topology::Ring;
        case 1: return LIFNetwork::Topology::FullyConnected;
        case 2: return LIFNetwork::Topology::Feedforward;
        case 3: return LIFNetwork::Topology::SparseRandom;
        default: return LIFNetwork::Topology::SmallWorld;
    }
}

static const char* topologyIndexToName(int index) {
    static const char* names[] = {"Ring", "Fully", "Feedfwd", "Random", "SmWorld"};
    return names[std::clamp(index, 0, 4)];
}

static std::filesystem::path executableImagesRoot() {
    NSString* exeDir = [NSBundle mainBundle].executablePath.stringByDeletingLastPathComponent;
    return std::filesystem::path(exeDir.UTF8String).parent_path() / "images";
}

static std::string resolveMediaPathForLoad(const std::string& originalPath) {
    if (originalPath.empty()) return originalPath;

    std::error_code ec;
    const std::filesystem::path asGiven(originalPath);
    if (std::filesystem::exists(asGiven, ec) && std::filesystem::is_regular_file(asGiven, ec))
        return asGiven.string();

    // Backward-compatible fallback: recover by basename from repo-local images/.
    const std::filesystem::path fallback = executableImagesRoot() / asGiven.filename();
    if (std::filesystem::exists(fallback, ec) && std::filesystem::is_regular_file(fallback, ec))
        return fallback.string();

    return originalPath;
}

static int findPortIndexByNameContains(const std::vector<std::string>& names,
                                       const std::string& needle) {
    std::string needleLower = needle;
    std::transform(needleLower.begin(), needleLower.end(), needleLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        std::string nameLower = names[i];
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (nameLower.find(needleLower) != std::string::npos)
            return i;
    }
    return -1;
}

static int findPortIndexByExactName(const std::vector<std::string>& names,
                                    const std::string& wanted) {
    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        if (names[i] == wanted)
            return i;
    }
    return -1;
}

static std::array<std::string, 24> pressureTargetNames() {
    return {
        "Zoom L0", "Zoom L1", "Zoom L2",
        "Rotate L0", "Rotate L1", "Rotate L2",
        "Opacity FX0", "Opacity FX1", "Opacity FX2",
        "Audio Gain FX0", "Audio Gain FX1", "Audio Gain FX2",
        "Pan L0 X", "Pan L0 Y", "Pan L1 X", "Pan L1 Y", "Pan L2 X", "Pan L2 Y",
        "FX0 Param0", "FX0 Param1", "FX1 Param0", "FX1 Param1", "FX2 Param0", "FX2 Param1"
    };
}

struct LifMidiStyleProfile {
    const char* name;
    int gematria;
};

static const LifMidiStyleProfile& lifMidiStyleProfile(int styleIndex) {
    static const LifMidiStyleProfile kPop        = {"Orpheus Black Moon", 188};
    static const LifMidiStyleProfile kRock       = {"Ares Blood Sun", 145};
    static const LifMidiStyleProfile kJazz       = {"Nostradamus Dream Sigil", 242};
    static const LifMidiStyleProfile kBlues      = {"Rasputin Mad Oracle", 190};
    static const LifMidiStyleProfile kPercussion = {"Loki Mad Clock", 109};

    switch (styleIndex) {
        case 0: return kPop;
        case 1: return kRock;
        case 2: return kJazz;
        case 3: return kBlues;
        case 4: return kPercussion;
        default:                            return kPop;
    }
}

static int wrap7(int value) {
    int out = value % 7;
    if (out < 0) out += 7;
    return out;
}

static const std::array<ModalScale, 10>& allModalScales() {
    static const std::array<ModalScale, 10> kModes = {
        ModalScale::Ionian,
        ModalScale::Dorian,
        ModalScale::Phrygian,
        ModalScale::Lydian,
        ModalScale::Mixolydian,
        ModalScale::Aeolian,
        ModalScale::Locrian,
        ModalScale::IonianPlus4,
        ModalScale::DorianPlus4,
        ModalScale::AeolianPlus7,
    };
    return kModes;
}

static bool isCustomModalScale(ModalScale scale) {
    return scale == ModalScale::IonianPlus4
        || scale == ModalScale::DorianPlus4
        || scale == ModalScale::AeolianPlus7;
}

static int customModeSlot(ModalScale scale) {
    switch (scale) {
        case ModalScale::IonianPlus4: return 0;
        case ModalScale::DorianPlus4: return 1;
        case ModalScale::AeolianPlus7: return 2;
        default: return -1;
    }
}

static std::array<std::array<int, 7>, 3> defaultCustomModeIntervals() {
    return {{{0, 2, 4, 6, 7, 9, 11},
             {0, 2, 3, 6, 7, 9, 10},
             {0, 2, 3, 5, 7, 8, 11}}};
}

static std::array<std::array<int, 7>, 3> g_customModeIntervals = defaultCustomModeIntervals();

static std::array<int, 7> autoIntervalsFromRootOffset(int semitoneOffset) {
    static constexpr std::array<int, 7> kIonian = {0, 2, 4, 5, 7, 9, 11};
    std::array<int, 7> out = {};
    std::vector<int> rel;
    rel.reserve(8);

    const int off = ((semitoneOffset % 12) + 12) % 12;
    for (int d : kIonian)
        rel.push_back((d - off + 12) % 12);

    std::sort(rel.begin(), rel.end());
    if (std::find(rel.begin(), rel.end(), 0) == rel.end()) {
        rel.insert(rel.begin(), 0);
        if (rel.size() > 7)
            rel.pop_back();
    }

    if (rel.size() < 7) {
        while (rel.size() < 7)
            rel.push_back(rel.empty() ? 0 : rel.back());
    }

    for (int i = 0; i < 7; ++i)
        out[static_cast<size_t>(i)] = rel[static_cast<size_t>(i)];
    return out;
}

static const char* autoModeNameFromRootOffset(int semitoneOffset) {
    switch (((semitoneOffset % 12) + 12) % 12) {
        case 0:  return "Ionian";
        case 2:  return "Dorian";
        case 4:  return "Phrygian";
        case 5:  return "Lydian";
        case 7:  return "Mixolydian";
        case 9:  return "Aeolian";
        case 11: return "Locrian";
        case 1:  return "Auto +1";
        case 3:  return "Auto +3";
        case 6:  return "Auto +6";
        case 8:  return "Auto +8";
        case 10: return "Auto +10";
        default: return "Auto";
    }
}

static int modalScaleOrderIndex(ModalScale scale) {
    const auto& modes = allModalScales();
    for (int i = 0; i < static_cast<int>(modes.size()); ++i)
        if (modes[static_cast<size_t>(i)] == scale)
            return i;
    return 0;
}

static std::array<int, 7> modalIntervals(ModalScale scale) {
    if (isCustomModalScale(scale)) {
        const int slot = customModeSlot(scale);
        if (slot >= 0)
            return g_customModeIntervals[static_cast<size_t>(slot)];
    }
    switch (scale) {
        case ModalScale::Ionian:     return {0, 2, 4, 5, 7, 9, 11};
        case ModalScale::Dorian:     return {0, 2, 3, 5, 7, 9, 10};
        case ModalScale::Phrygian:   return {0, 1, 3, 5, 7, 8, 10};
        case ModalScale::Lydian:     return {0, 2, 4, 6, 7, 9, 11};
        case ModalScale::Mixolydian: return {0, 2, 4, 5, 7, 9, 10};
        case ModalScale::Aeolian:    return {0, 2, 3, 5, 7, 8, 10};
        case ModalScale::Locrian:    return {0, 1, 3, 5, 6, 8, 10};
        default:                     return {0, 2, 3, 5, 7, 9, 10};
    }
}

static uint16_t modalPitchClassMask(int tonicSemitone, ModalScale scale) {
    tonicSemitone = ((tonicSemitone % 12) + 12) % 12;
    const auto intervals = modalIntervals(scale);
    uint16_t mask = 0;
    for (int d : intervals) {
        const int pc = (tonicSemitone + d) % 12;
        mask |= static_cast<uint16_t>(1u << pc);
    }
    return mask;
}

static int overlapCount12(uint16_t a, uint16_t b) {
    uint16_t v = static_cast<uint16_t>(a & b);
    int count = 0;
    for (int i = 0; i < 12; ++i)
        count += (v >> i) & 1u;
    return count;
}

static ModalScale resolveTransportedModalScale(int prevKeySemitone,
                                               ModalScale prevScale,
                                               int newKeySemitone,
                                               ModalScale fallback) {
    const uint16_t prevMask = modalPitchClassMask(prevKeySemitone, prevScale);
    int bestScore = -1;
    int bestTie = 999;
    ModalScale best = fallback;

    const int prevIdx = modalScaleOrderIndex(prevScale);
    const auto& modes = allModalScales();
    for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
        const auto candidate = modes[static_cast<size_t>(i)];
        const uint16_t candidateMask = modalPitchClassMask(newKeySemitone, candidate);
        const int score = overlapCount12(prevMask, candidateMask);
        const int tie = std::abs(i - prevIdx);
        if (score > bestScore || (score == bestScore && tie < bestTie)) {
            bestScore = score;
            bestTie = tie;
            best = candidate;
        }
    }
    return best;
}

int App::lifNeuronCountFromNorm(float v) {
    static constexpr int kCounts[4] = {512, 1024, 2048, 4096};
    int idx = std::clamp(static_cast<int>(v * 4.0f), 0, 3);
    return kCounts[idx];
}

float App::normFromLIFNeuronCount(int neuronCount) {
    if (neuronCount <= 512) return 0.0f;
    if (neuronCount <= 1024) return 1.0f / 3.0f;
    if (neuronCount <= 2048) return 2.0f / 3.0f;
    return 1.0f;
}

// ── App ───────────────────────────────────────────────────────────────────────

App::App() = default;
App::~App() {
    lifToneSynth_.stopStream();
}

bool App::sceneUsesLIF(int sceneIdx) const {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES) return false;
    const Scene& sc = SCENES[sceneIdx];
    const auto fx = enforcedSceneFx(sc);
    for (FxPatchId patch : fx)
        if (isLIFPatch(patch)) return true;
    return false;
}

void App::resetLifMidiState() {
    const int fallbackChannel = (lifMidiStyle_ == LifMidiStyle::Percussion) ? 10 : 1;
    for (int bin = 0; bin < 16; ++bin) {
        if (lifMidiNoteOn_[bin]) {
            const int channel = (lifMidiActiveChannel_[bin] > 0) ? lifMidiActiveChannel_[bin] : fallbackChannel;
            for (int note : lifMidiActiveNotes_[bin])
                midi_.sendNoteOff(channel, note);
            lifMidiNoteOn_[bin] = false;
        }
        lifMidiActiveNotes_[bin].clear();
        lifMidiActiveChannel_[bin] = 0;
        lifMidiGateFrames_[bin] = 0;
        lifMidiCooldownFrames_[bin] = 0;
    }
}

const char* App::lifMidiStyleName() const {
    return lifMidiStyleProfile(static_cast<int>(lifMidiStyle_)).name;
}

const char* App::lifMidiKeyName() const {
    static constexpr const char* kNames[12] = {
        "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
    };
    return kNames[std::clamp(lifMidiKeySemitone_, 0, 11)];
}

const char* App::lifMidiTonalRootName() const {
    static constexpr const char* kNames[12] = {
        "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
    };
    return kNames[std::clamp(lifMidiTonalRootSemitone_, 0, 11)];
}

void App::refreshLifMidiUi() {
    audioMidiWin_.setLifMidiStyle(lifMidiStyleName());
    audioMidiWin_.setLifMidiMode(lifMidiModalScaleName());
    audioMidiWin_.setLifMidiModeToggleState(currentScene_ >= 0,
                                            currentScene_ >= 0 ? (lifMidiModeUserSet_[currentScene_] == 0) : true);
    audioMidiWin_.setLifMidiKey(lifMidiKeyName());
    audioMidiWin_.setLifMidiTonalRoot(lifMidiTonalRootName());
    audioMidiWin_.setLifMidiRange(lifMidiRangeMin_, lifMidiRangeMax_);
    audioMidiWin_.setLifMidiStatus(lifMidiEnabled_,
                                   (lifMidiStyle_ == LifMidiStyle::Percussion) ? 10 : 1,
                                   lifMidiRangeMin_);
    refreshRhythmUi();
    refreshLifMidiModeEditorUi();
}

void App::refreshRhythmUi() {
    std::array<std::string, 3> laneNames = {"L1", "L2", "L3"};
    std::array<uint8_t, 3> laneEnabled = {0, 0, 0};
    std::array<int, 3> lanePulses = {0, 0, 0};
    std::array<float, 3> laneGains = {0.0f, 0.0f, 0.0f};
    const auto& lanes = rhythmDriver_.lanes();
    for (int i = 0; i < 3; ++i) {
        laneNames[static_cast<size_t>(i)] = lanes[static_cast<size_t>(i)].name;
        laneEnabled[static_cast<size_t>(i)] = lanes[static_cast<size_t>(i)].enabled ? 1 : 0;
        lanePulses[static_cast<size_t>(i)] = lanes[static_cast<size_t>(i)].pulses;
        laneGains[static_cast<size_t>(i)] = lanes[static_cast<size_t>(i)].gain;
    }

    audioMidiWin_.setRhythmStatus(
        rhythmDriver_.enabled(),
        RhythmTransientDriver::mixModeName(rhythmDriver_.mixMode()),
        RhythmTransientDriver::patternName(rhythmDriver_.pattern()),
        rhythmDriver_.bpm(),
        rhythmDriver_.intensity(),
        rhythmDriver_.hasPendingPattern());
    audioMidiWin_.setRhythmLaneScaffold(laneNames, laneEnabled, lanePulses, laneGains);
}

void App::refreshLifMidiModeEditorUi() {
    if (currentScene_ < 0) {
        audioMidiWin_.setLifMidiModeEditor("Mode Edit: no scene", false);
        return;
    }
    if (!lifMidiModeUserSet_[currentScene_]) {
        audioMidiWin_.setLifMidiModeEditor("Mode Edit: auto (tonal root)", false);
        return;
    }
    const ModalScale scale = scenes_[currentScene_].modalScale;
    const bool editable = isCustomModalScale(scale);
    if (!editable) {
        audioMidiWin_.setLifMidiModeEditor("Mode Edit: built-in mode", false);
        return;
    }

    lifMidiModeEditDegree_ = std::clamp(lifMidiModeEditDegree_, 0, 6);
    const auto ints = modalIntervals(scale);
    const int d = lifMidiModeEditDegree_;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Mode Edit: deg %d = %d st", d + 1, ints[static_cast<size_t>(d)]);
    audioMidiWin_.setLifMidiModeEditor(buf, true);
}

const char* App::lifMidiModalScaleName() const {
    // Current scene
    return lifMidiModalScaleNameForScene(currentScene_);
}

const char* App::lifMidiModalScaleNameForScene(int sceneIdx) const {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES) return "Dorian";
    if (!lifMidiModeUserSet_[sceneIdx]) {
        const int rel = (sceneKeySemitone(sceneIdx) - lifMidiTonalRootSemitone_ + 12) % 12;
        return autoModeNameFromRootOffset(rel);
    }
    ModalScale scale = scenes_[sceneIdx].modalScale;
    switch (scale) {
        case ModalScale::Ionian:     return "Ionian";
        case ModalScale::Dorian:     return "Dorian";
        case ModalScale::Phrygian:   return "Phrygian";
        case ModalScale::Lydian:     return "Lydian";
        case ModalScale::Mixolydian: return "Mixolydian";
        case ModalScale::Aeolian:    return "Aeolian";
        case ModalScale::Locrian:    return "Locrian";
        case ModalScale::IonianPlus4:return "Ionian +4";
        case ModalScale::DorianPlus4:return "Dorian +4";
        case ModalScale::AeolianPlus7:return "Aeolian +7";
        default:                          return "Dorian";
    }
}

int App::sceneKeySemitone(int sceneIdx) const {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES) return 0;
    const int midiNote = NOTE_SCENE_BASE + sceneIdx;
    return ((midiNote % 12) + 12) % 12;
}

std::array<int, 7> App::autoModalIntervalsForScene(int sceneIdx) const {
    const int rel = (sceneKeySemitone(sceneIdx) - lifMidiTonalRootSemitone_ + 12) % 12;
    return autoIntervalsFromRootOffset(rel);
}

void App::nudgeLifMidiKey(int delta) {
    if (delta == 0) return;
    resetLifMidiState();
    lifMidiKeySemitone_ = (lifMidiKeySemitone_ + delta) % 12;
    if (lifMidiKeySemitone_ < 0)
        lifMidiKeySemitone_ += 12;
    refreshLifMidiUi();
    std::cout << "[LIF MIDI] key=" << lifMidiKeyName() << std::endl;
}

void App::nudgeLifMidiTonalRoot(int delta) {
    if (delta == 0) return;
    resetLifMidiState();
    lifMidiTonalRootSemitone_ = (lifMidiTonalRootSemitone_ + delta) % 12;
    if (lifMidiTonalRootSemitone_ < 0)
        lifMidiTonalRootSemitone_ += 12;
    refreshLifMidiUi();
    saveState();
    std::cout << "[LIF MIDI] tonal root=" << lifMidiTonalRootName() << std::endl;
}

void App::nudgeLifMidiRangeMin(int delta) {
    if (delta == 0) return;
    resetLifMidiState();
    lifMidiRangeMin_ = std::clamp(lifMidiRangeMin_ + delta, 0, lifMidiRangeMax_ - 1);
    refreshLifMidiUi();
    std::cout << "[LIF MIDI] range=" << lifMidiRangeMin_ << "-" << lifMidiRangeMax_ << std::endl;
}

void App::nudgeLifMidiRangeMax(int delta) {
    if (delta == 0) return;
    resetLifMidiState();
    int oldMax = lifMidiRangeMax_;
    lifMidiRangeMax_ = std::clamp(lifMidiRangeMax_ + delta, lifMidiRangeMin_ + 1, 127);
    std::cout << "[LIF MIDI] nudgeLifMidiRangeMax: oldMax=" << oldMax << ", delta=" << delta << ", newMax=" << lifMidiRangeMax_ << ", min=" << lifMidiRangeMin_ << std::endl;
    refreshLifMidiUi();
    std::cout << "[LIF MIDI] range=" << lifMidiRangeMin_ << "-" << lifMidiRangeMax_ << std::endl;
}

void App::cycleLifMidiStyle() {
    resetLifMidiState();
    switch (lifMidiStyle_) {
        case LifMidiStyle::Pop:        lifMidiStyle_ = LifMidiStyle::Rock; break;
        case LifMidiStyle::Rock:       lifMidiStyle_ = LifMidiStyle::Jazz; break;
        case LifMidiStyle::Jazz:       lifMidiStyle_ = LifMidiStyle::Blues; break;
        case LifMidiStyle::Blues:      lifMidiStyle_ = LifMidiStyle::Percussion; break;
        case LifMidiStyle::Percussion: lifMidiStyle_ = LifMidiStyle::Pop; break;
    }
    refreshLifMidiUi();
    std::cout << "[LIF MIDI] style=" << lifMidiStyleName() << std::endl;
}

void App::toggleRhythmDriver() {
    rhythmDriver_.setEnabled(!rhythmDriver_.enabled());
    refreshRhythmUi();
    std::cout << "[Rhythm] " << (rhythmDriver_.enabled() ? "enabled" : "disabled") << std::endl;
}

void App::cycleRhythmMixMode() {
    using MixMode = RhythmTransientDriver::MixMode;
    MixMode next = MixMode::AudioOnly;
    switch (rhythmDriver_.mixMode()) {
        case MixMode::AudioOnly: next = MixMode::RhythmOnly; break;
        case MixMode::RhythmOnly: next = MixMode::Hybrid; break;
        case MixMode::Hybrid: next = MixMode::AudioOnly; break;
    }
    rhythmDriver_.setMixMode(next);
    refreshRhythmUi();
    std::cout << "[Rhythm] mix=" << RhythmTransientDriver::mixModeName(next) << std::endl;
}

void App::cycleRhythmPattern() {
    using Pattern = RhythmTransientDriver::Pattern;
    Pattern next = Pattern::Metronome4;
    switch (rhythmDriver_.pattern()) {
        case Pattern::Metronome4: next = Pattern::BackbeatRock; break;
        case Pattern::BackbeatRock: next = Pattern::ReggaeThird; break;
        case Pattern::ReggaeThird: next = Pattern::GamelanEnd; break;
        case Pattern::GamelanEnd: next = Pattern::Bell12; break;
        case Pattern::Bell12: next = Pattern::ClaveLike; break;
        case Pattern::ClaveLike: next = Pattern::Triplet3Over4; break;
        case Pattern::Triplet3Over4: next = Pattern::Metronome4; break;
    }
    rhythmDriver_.requestPattern(next, true);
    refreshRhythmUi();
    std::cout << "[Rhythm] queued pattern=" << RhythmTransientDriver::patternName(next) << std::endl;
}

void App::nudgeRhythmBpm(float delta) {
    rhythmDriver_.nudgeBpm(delta);
    refreshRhythmUi();
    std::cout << "[Rhythm] bpm=" << rhythmDriver_.bpm() << std::endl;
}

void App::nudgeRhythmIntensity(float delta) {
    rhythmDriver_.nudgeIntensity(delta);
    refreshRhythmUi();
    std::cout << "[Rhythm] intensity=" << rhythmDriver_.intensity() << std::endl;
}

void App::toggleRhythmLane(int laneIdx) {
    rhythmDriver_.toggleLaneEnabled(laneIdx);
    refreshRhythmUi();
    std::cout << "[Rhythm] lane " << laneIdx << " toggled" << std::endl;
}

void App::nudgeRhythmLanePulses(int laneIdx, int delta) {
    rhythmDriver_.nudgeLanePulses(laneIdx, delta);
    refreshRhythmUi();
    const auto& lanes = rhythmDriver_.lanes();
    if (laneIdx >= 0 && laneIdx < static_cast<int>(lanes.size()))
        std::cout << "[Rhythm] lane " << laneIdx << " pulses=" << lanes[static_cast<size_t>(laneIdx)].pulses << std::endl;
}

void App::nudgeRhythmLaneGain(int laneIdx, float delta) {
    rhythmDriver_.nudgeLaneGain(laneIdx, delta);
    refreshRhythmUi();
    const auto& lanes = rhythmDriver_.lanes();
    if (laneIdx >= 0 && laneIdx < static_cast<int>(lanes.size()))
        std::cout << "[Rhythm] lane " << laneIdx << " gain=" << lanes[static_cast<size_t>(laneIdx)].gain << std::endl;
}

void App::cycleLifMidiModalScale() {
    if (currentScene_ < 0) return;
    resetLifMidiState();
    ModalScale& scale = scenes_[currentScene_].modalScale;
    const auto& modes = allModalScales();
    int idx = modalScaleOrderIndex(scale);
    idx = (idx + 1) % static_cast<int>(modes.size());
    scale = modes[static_cast<size_t>(idx)];
    lifMidiModeUserSet_[currentScene_] = 1;
    refreshLifMidiUi();
    std::cout << "[LIF MIDI] modal scale=" << lifMidiModalScaleName() << std::endl;
    saveState();
}

void App::toggleLifMidiModeSource() {
    if (currentScene_ < 0) return;
    resetLifMidiState();

    if (lifMidiModeUserSet_[currentScene_]) {
        // Manual -> Auto: derive from tonal root + scene key on demand.
        lifMidiModeUserSet_[currentScene_] = 0;
    } else {
        // Auto -> Manual: capture the current auto mapping into a scene mode.
        const int rel = (sceneKeySemitone(currentScene_) - lifMidiTonalRootSemitone_ + 12) % 12;
        ModalScale target = ModalScale::Ionian;
        bool custom = false;
        switch (rel) {
            case 0:  target = ModalScale::Ionian; break;
            case 2:  target = ModalScale::Dorian; break;
            case 4:  target = ModalScale::Phrygian; break;
            case 5:  target = ModalScale::Lydian; break;
            case 7:  target = ModalScale::Mixolydian; break;
            case 9:  target = ModalScale::Aeolian; break;
            case 11: target = ModalScale::Locrian; break;
            default:
                target = ModalScale::IonianPlus4;
                custom = true;
                break;
        }

        scenes_[currentScene_].modalScale = target;
        if (custom) {
            const int slot = customModeSlot(target);
            if (slot >= 0)
                g_customModeIntervals[static_cast<size_t>(slot)] = autoModalIntervalsForScene(currentScene_);
        }
        lifMidiModeUserSet_[currentScene_] = 1;
    }

    refreshLifMidiUi();
    saveState();
}

void App::nudgeLifMidiModeEditDegree(int delta) {
    if (currentScene_ < 0 || delta == 0) return;
    lifMidiModeEditDegree_ = std::clamp(lifMidiModeEditDegree_ + delta, 0, 6);
    refreshLifMidiModeEditorUi();
}

void App::nudgeLifMidiModeEditSemitone(int delta) {
    if (currentScene_ < 0 || delta == 0) return;
    ModalScale scale = scenes_[currentScene_].modalScale;
    const int slot = customModeSlot(scale);
    if (slot < 0) {
        refreshLifMidiModeEditorUi();
        return;
    }
    const int degree = std::clamp(lifMidiModeEditDegree_, 0, 6);
    auto& mode = g_customModeIntervals[static_cast<size_t>(slot)];
    int candidate = std::clamp(mode[static_cast<size_t>(degree)] + delta, 0, 11);

    // Keep degree ordering non-decreasing to avoid malformed scales.
    if (degree > 0)
        candidate = std::max(candidate, mode[static_cast<size_t>(degree - 1)]);
    if (degree < 6)
        candidate = std::min(candidate, mode[static_cast<size_t>(degree + 1)]);

    mode[static_cast<size_t>(degree)] = candidate;
    lifMidiModeUserSet_[currentScene_] = 1;
    refreshLifMidiModeEditorUi();
    saveState();
}

void App::resetLifMidiModeEdits() {
    if (currentScene_ < 0) return;
    ModalScale scale = scenes_[currentScene_].modalScale;
    const int slot = customModeSlot(scale);
    if (slot < 0) {
        refreshLifMidiModeEditorUi();
        return;
    }

    g_customModeIntervals = defaultCustomModeIntervals();
    lifMidiModeUserSet_[currentScene_] = 1;
    refreshLifMidiModeEditorUi();
    saveState();
}

App::HarmonicFunction App::classifyLifFunction(int bin,
                                               float energy,
                                               float prevEnergy,
                                               float maxEnergy,
                                               bool feedforward) const {
    const auto& style = lifMidiStyleProfile(static_cast<int>(lifMidiStyle_));
    const float rel = (maxEnergy > 0.001f) ? std::clamp(energy / maxEnergy, 0.0f, 1.0f) : 0.0f;
    const float contour = energy - prevEnergy;
    const float styleBias = (static_cast<float>((style.gematria % 9) - 4)) * 0.01f; // 9 discrete values in [-0.04,+0.04]
    const float dominantRel = std::clamp(0.82f - styleBias, 0.70f, 0.92f);
    const float subRel = std::clamp(0.45f - styleBias * 0.5f, 0.30f, 0.60f);
    const float dominantContour = std::clamp(0.08f - styleBias, 0.04f, 0.12f);
    const float subContour = std::clamp(0.02f - styleBias * 0.5f, 0.00f, 0.05f);

    // De la Motte style reading: every harmony is heard as T, S, or D function.
    if (bin >= 11 || rel > dominantRel || contour > dominantContour)
        return HarmonicFunction::Dominant;
    if (bin >= 6 || rel > subRel || (feedforward && contour > subContour))
        return HarmonicFunction::Subdominant;
    return HarmonicFunction::Tonic;
}

std::vector<int> App::lifMidiNotesForFunction(HarmonicFunction function,
                                              int bin,
                                              int baseNote,
                                              float driveNorm) const {
    const auto& style = lifMidiStyleProfile(static_cast<int>(lifMidiStyle_));
    const auto clampNote = [](int n) { return std::clamp(n, 0, 127); };
    const auto fitToRange = [this](int note) {
        int n = std::clamp(note, 0, 127);
        const int lo = std::clamp(lifMidiRangeMin_, 0, 126);
        const int hi = std::clamp(lifMidiRangeMax_, lo + 1, 127);
        while (n < lo) n += 12;
        while (n > hi) n -= 12;
        if (n < lo) n = lo;
        if (n > hi) n = hi;
        return n;
    };
    const int octave = (bin / 4) - 1; // spreads bins over four octaves around base
    const int variant = bin % 4;

    if (lifMidiStyle_ == LifMidiStyle::Percussion) {
        static constexpr std::array<int, 16> drumMap = {36, 38, 42, 46, 49, 51, 45, 41, 43, 44, 47, 50, 57, 59, 60, 62};
        const int rotated = (bin + (style.gematria % 16)) % 16;
        return {drumMap[static_cast<std::size_t>(rotated)]};
    }

    int degree = 0;
    switch (function) {
        case HarmonicFunction::Tonic: {
            static constexpr std::array<int, 4> tonicDegrees = {0, 5, 2, 0};   // I, vi, iii, I (scale degrees)
            degree = wrap7(tonicDegrees[static_cast<std::size_t>(variant)] + (style.gematria % 7));
            break;
        }
        case HarmonicFunction::Subdominant: {
            static constexpr std::array<int, 4> subDegrees = {1, 3, 1, 3};     // ii / IV family
            degree = wrap7(subDegrees[static_cast<std::size_t>(variant)] + ((style.gematria / 7) % 7));
            break;
        }
        case HarmonicFunction::Dominant: {
            static constexpr std::array<int, 4> domDegrees = {4, 6, 4, 6};      // V / vii family
            degree = wrap7(domDegrees[static_cast<std::size_t>(variant)] + ((style.gematria / 49) % 7));
            break;
        }
    }

    std::array<int, 7> intervals = {0, 2, 3, 5, 7, 9, 10};
    if (currentScene_ >= 0 && currentScene_ < NUM_SCENES) {
        if (lifMidiModeUserSet_[currentScene_])
            intervals = modalIntervals(scenes_[currentScene_].modalScale);
        else
            intervals = autoModalIntervalsForScene(currentScene_);
    }
    auto noteFromDegree = [&](int rootDegree, int thirdStackOffset) {
        const int totalDegree = rootDegree + thirdStackOffset;
        const int scaleDegree = ((totalDegree % 7) + 7) % 7;
        const int degreeOctave = totalDegree / 7;
        return clampNote(baseNote + octave * 12 + intervals[scaleDegree] + degreeOctave * 12);
    };

    const int root = noteFromDegree(degree, 0);
    std::vector<int> chord;
    const int colorStack = 6 + 2 * ((style.gematria / 3) % 3);      // tertian stack height: 6/8/10
    const int extensionStack = 8 + 2 * ((style.gematria / 11) % 2); // tertian stack height: 8/10

    switch (lifMidiStyle_) {
        case LifMidiStyle::Pop:
            chord = {noteFromDegree(degree, 0), noteFromDegree(degree, 2), noteFromDegree(degree, 4)};
            if (function == HarmonicFunction::Dominant) chord.push_back(noteFromDegree(degree, colorStack));
            else chord.push_back(noteFromDegree(degree, extensionStack));
            break;
        case LifMidiStyle::Rock:
            chord = {noteFromDegree(degree, 0), noteFromDegree(degree, 4), noteFromDegree(degree, 7)};
            if (function == HarmonicFunction::Dominant) chord.push_back(noteFromDegree(degree, colorStack));
            break;
        case LifMidiStyle::Jazz:
            chord = {
                noteFromDegree(degree, 0),
                noteFromDegree(degree, 2),
                noteFromDegree(degree, 4),
                noteFromDegree(degree, colorStack),
                noteFromDegree(degree, extensionStack)
            };
            break;
        case LifMidiStyle::Blues:
            chord = {
                noteFromDegree(degree, 0),
                noteFromDegree(degree, 2),
                noteFromDegree(degree, 4),
                noteFromDegree(degree, colorStack)
            };
            if (function == HarmonicFunction::Dominant)
                chord.push_back(clampNote(root + 6 + ((style.gematria / 5) % 2))); // b5/#5 pivot
            break;
        case LifMidiStyle::Percussion:
            chord = {root};
            break;
    }

    // Thin voicings at low drive, fuller voicings when activity is strong.
    const float drive = std::clamp(driveNorm, 0.0f, 1.0f);
    if (drive < 0.40f && chord.size() > 3) chord.resize(3);
    else if (drive < 0.65f && chord.size() > 4) chord.resize(4);

    std::vector<int> deduped;
    deduped.reserve(chord.size());
    for (int note : chord) {
        const int clamped = fitToRange(clampNote(note));
        if (std::find(deduped.begin(), deduped.end(), clamped) == deduped.end())
            deduped.push_back(clamped);
    }
    return deduped;
}

bool App::init() {
    // ── Register signal handlers so Ctrl-C saves state before exit ───────
    g_sigApp = this;
    std::signal(SIGINT,  appSigHandler);
    std::signal(SIGTERM, appSigHandler);

    // ── Metal compositor ─────────────────────────────────────────────────
    if (!compositor_.init()) {
        std::cerr << "[App] Metal compositor init failed — GPU unavailable\n";
        // Continue in "software preview" mode (no compositing)
    }

    // ── Windows ──────────────────────────────────────────────────────────
    auto ctrl  = screenOrigin(0);
    auto ctrlS = screenSize(0);
    controlWin_.open(ctrl.x, ctrl.y, ctrlS.x, ctrlS.y);

    // ── Audio & MIDI Control window — positioned next to main control ────
    const int audioMidiW = 900;
    const int audioMidiH = 1150;
    const int audioMidiX = ctrl.x + std::max(10, ctrlS.x - audioMidiW - 16);
    const int audioMidiY = ctrl.y + 10;
    audioMidiWin_.open(audioMidiX, audioMidiY, audioMidiW, audioMidiH);

    auto perf  = screenOrigin(1);   // Second display
    auto perfS = screenSize(1);
    if (perfS.x == 0) {            // Fallback: no second screen — use a window
        perf  = {ctrlS.x / 2, 50};
        perfS = {960, 540};
    }
    perfWin_.open(perf.x, perf.y, perfS.x, perfS.y);

    // ── Media picker window — small panel on primary screen ──────────────
    // Position it below or beside the control window
    const std::string stashRoot = []{
        std::error_code ec;

        const std::filesystem::path imagesCandidate = executableImagesRoot();
        if (std::filesystem::exists(imagesCandidate, ec) && std::filesystem::is_directory(imagesCandidate, ec))
            return imagesCandidate.string();

        return std::string();
    }();
    // Open picker as a smaller overlay on the same screen
    mediaPickerWin_.open(ctrl.x + ctrlS.x / 2, ctrl.y,
                         ctrlS.x / 2, ctrlS.y / 2, stashRoot);

    // Pressure mapping window in lower-right quarter of the primary screen.
    const auto targetNames = pressureTargetNames();
    pressureWin_.open(ctrl.x + ctrlS.x / 2, ctrl.y + ctrlS.y / 2,
                      ctrlS.x / 2, ctrlS.y / 2,
                      std::vector<std::string>(targetNames.begin(), targetNames.end()));

    // Pre-allocate composite texture (SFML side for preview)
    if (!compositeTex_.resize({WORK_W, WORK_H}))
        std::cerr << "[App] Cannot create composite SFML texture\n";


    // ── MIDI ─────────────────────────────────────────────────────────────
    auto ports = midi_.portNames();
    int selectedInPort = -1;
    if (!ports.empty()) {
        selectedInPort = findPortIndexByNameContains(ports, "MPD218");
        if (selectedInPort < 0) selectedInPort = 0;
        if (midi_.openPort(selectedInPort))
            std::cout << "[App] MIDI IN opened: " << ports[selectedInPort] << "\n";
    } else {
        std::cout << "[App] No MIDI IN ports found\n";
    }

    auto outPorts = midi_.outputPortNames();
    int selectedOutPort = -1;
    if (!outPorts.empty()) {
        selectedOutPort = findPortIndexByNameContains(outPorts, "IAC Driver");
        if (selectedOutPort < 0) selectedOutPort = 0;
        if (midi_.openOutputPort(selectedOutPort))
            std::cout << "[App] MIDI OUT opened: " << outPorts[selectedOutPort] << "\n";
    } else {
        std::cout << "[App] No MIDI OUT ports found\n";
    }

    // Optional startup self-test for outbound MIDI path.
    // Enable with: LIF_LOVA_TEST_MIDI_OUT=1 ./lif_lova
    if (midi_.isOutputOpen()) {
        const char* midiTest = std::getenv("LIF_LOVA_TEST_MIDI_OUT");
        if (midiTest && std::strcmp(midiTest, "1") == 0) {
            midi_.sendNoteOn(1, 60, 100);
            midi_.sendNoteOff(1, 60, 0);
            std::cout << "[App] MIDI OUT self-test sent (Ch1 C4 on/off)\n";
        }
    }

    audioMidiWin_.setMidiPortLists(ports, selectedInPort, outPorts, selectedOutPort);
    audioMidiWin_.onMidiInPortChanged = [this](const std::string& name) {
        auto names = midi_.portNames();
        int idx = findPortIndexByExactName(names, name);
        if (idx < 0) idx = findPortIndexByNameContains(names, name);
        if (idx >= 0 && idx < static_cast<int>(names.size()) && midi_.openPort(idx)) {
            std::cout << "[App] MIDI IN switched: " << names[idx] << "\n";
        }
    };
    audioMidiWin_.onMidiOutPortChanged = [this](const std::string& name) {
        auto names = midi_.outputPortNames();
        int idx = findPortIndexByExactName(names, name);
        if (idx < 0) idx = findPortIndexByNameContains(names, name);
        if (idx >= 0 && idx < static_cast<int>(names.size()) && midi_.openOutputPort(idx)) {
            std::cout << "[App] MIDI OUT switched: " << names[idx] << "\n";
        }
    };

    // ── Audio capture ─────────────────────────────────────────────────
    if (!audio_.start())
        std::cerr << "[App] Audio capture unavailable (no mic/line-in?)\n";

    if (!lifToneSynth_.startStream())
        std::cerr << "[App] LIF tone synth init failed\n";
    lifToneSynth_.setFrequencyRange(lifToneMinFreqHz_, lifToneMaxFreqHz_);
    lifToneSynth_.setOutputVolume(lifToneVolume_);

    // ── Knob pickup state ────────────────────────────────────────────────
    knobLastPhys_.fill(0.5f);

    // ── Scene state objects — reset all to -1 (unvisited) ────────────────
    for (auto& s : scenes_) s.reset();
    scenePressureTargetNorm_.fill(0.0f);
    scenePressureNorm_.fill(0.0f);
    for (auto& ps : pressureSceneState_) ps.reset();

    wireCallbacks();

    // ── Restore persisted state ──────────────────────────────────────────
    loadState();  // populates scenes_ + applies last-active scene (no-op if first launch)

    // ── Default labels in control window ─────────────────────────────────
    controlWin_.setSceneName("None");
    mediaPickerWin_.setSceneName("None");
    constexpr const char* defaultNames[] = {"FX-1 P1","FX-1 P2","FX-2 P1","FX-2 P2","FX-3 P1","FX-3 P2"};
    for (int i = 0; i < NUM_KNOBS; ++i)
        controlWin_.setKnobParamName(i, defaultNames[i]);
    controlWin_.setKnobMode(knobMode_);
    refreshLifMidiUi();
    audioMidiWin_.setLifToneVolume(lifToneVolume_);
    refreshKnobDisplay();
    if (currentScene_ >= 0) refreshKnobParamNames();
    if (currentScene_ >= 0) {
        pressureWin_.setSceneName(SCENES[currentScene_].name);
        pressureWin_.setTargetStates(
            std::vector<uint8_t>(pressureSceneState_[currentScene_].enabled.begin(), pressureSceneState_[currentScene_].enabled.end()),
            std::vector<float>(pressureSceneState_[currentScene_].amount.begin(), pressureSceneState_[currentScene_].amount.end()));
        audioMidiWin_.setPressureNorm(scenePressureNorm_[currentScene_]);
    }

    return true;
}

void App::toggleLifMidi() {
    lifMidiEnabled_ = !lifMidiEnabled_;
    std::cout << "[LIF MIDI] " << (lifMidiEnabled_ ? "enabled" : "disabled") << std::endl;
    if (!lifMidiEnabled_) {
        resetLifMidiState();
        lifMidiNoSceneWarned_ = false;
    }
    refreshLifMidiUi();
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

void App::wireCallbacks() {
    midi_.onKnob = [this](int k, float v, KnobMode m){ onKnob(k, v, m); };
    midi_.onSceneSelect = [this](int idx){ onSceneSelect(idx); };
    midi_.onChannelPressure = [this](int channel, float normValue) {
        // Use channel pressure on channel 10 as scene-local FX modulation input.
        if (channel != 10 || currentScene_ < 0) return;
        scenePressureTargetNorm_[currentScene_] = std::clamp(normValue, 0.0f, 1.0f);
    };

    pressureWin_.onMappingChanged = [this](int targetIdx, bool enabled, float amount) {
        if (currentScene_ < 0) return;
        if (targetIdx < 0 || targetIdx >= NUM_PRESSURE_TARGETS) return;
        pressureSceneState_[currentScene_].enabled[targetIdx] = enabled ? 1 : 0;
        pressureSceneState_[currentScene_].amount[targetIdx] = std::clamp(amount, -1.0f, 1.0f);
        applyPressureMappings(currentScene_);
        saveState();
    };
    midi_.onModeChange = [this](KnobMode m){
        if (rKeyHeld_ || zKeyHeld_ || oKeyHeld_ || gKeyHeld_ || pKeyHeld_ || imgXfadeKeyHeld_ || sceneXfadeKeyHeld_ || globalImgXfadeKeyHeld_ || globalSceneXfadeKeyHeld_ || globalOpacityKeyHeld_ || globalAudioGainKeyHeld_ || globalRotationKeyHeld_ || globalZoomKeyHeld_) return;  // modifier key overrides; ignore while held
        knobMode_ = m;
        controlWin_.setKnobMode(m);
        static const char* opNames[]   = {"Opac L0", "-", "Opac L1", "-", "Opac L2", "-"};
        static const char* gOpNames[]  = {"GOpac L0", "-", "GOpac L1", "-", "GOpac L2", "-"};
        static const char* gainNames[] = {"Gain 0", "-", "Gain 1", "-", "Gain 2", "-"};
        static const char* gGainNames[] = {"GGain 0", "-", "GGain 1", "-", "GGain 2", "-"};
        if (m == KnobMode::LayerLevel) {
            const char** names = globalOpacityKeyHeld_ ? gOpNames : opNames;
            for (int i = 0; i < NUM_KNOBS; ++i)
                controlWin_.setKnobParamName(i, names[i]);
        } else if (m == KnobMode::FxAudio) {
            const char** names = globalAudioGainKeyHeld_ ? gGainNames : gainNames;
            for (int i = 0; i < NUM_KNOBS; ++i)
                controlWin_.setKnobParamName(i, names[i]);
        } else {
            refreshKnobParamNames();
        }
        refreshKnobDisplay();
    };
    controlWin_.onKnobDrag = [this](int knob, float v){ onKnobDrag(knob, v); };

    // ── Shared helper: update display after a modifier key press/release ──
    auto refreshModifierDisplay = [this]() {
        static const char* rotNames[]   = {"Rot L0",    "-", "Rot L1",    "-", "Rot L2",    "-"};
        static const char* gRotNames[]  = {"GRot L0",   "-", "GRot L1",   "-", "GRot L2",   "-"};
        static const char* zoomNames[]  = {"Zoom L0",   "-", "Zoom L1",   "-", "Zoom L2",   "-"};
        static const char* gZoomNames[] = {"GZoom L0",  "-", "GZoom L1",  "-", "GZoom L2",  "-"};
        static const char* opNames[]    = {"Opac L0",   "-", "Opac L1",   "-", "Opac L2",   "-"};
        static const char* gOpNames[]   = {"GOpac L0",  "-", "GOpac L1",  "-", "GOpac L2",  "-"};
        static const char* gainNames[]  = {"Gain 0",    "-", "Gain 1",    "-", "Gain 2",    "-"};
        static const char* gGainNames[] = {"GGain 0",   "-", "GGain 1",   "-", "GGain 2",   "-"};
        static const char* panNames[]   = {"Pan0 X", "Pan0 Y", "Pan1 X", "Pan1 Y", "Pan2 X", "Pan2 Y"};
        static const char* xfadeNames[]  = {"ImgFd 0",   "-", "ImgFd 1",   "-", "ImgFd 2",   "-"};
        static const char* scnFdNames[]  = {"ScnFd 0",   "-", "ScnFd 1",   "-", "ScnFd 2",   "-"};
        static const char* gImgFdNames[] = {"GImgFd 0",  "-", "GImgFd 1",  "-", "GImgFd 2",  "-"};
        static const char* gScnFdNames[] = {"GScnFd 0",  "-", "GScnFd 1",  "-", "GScnFd 2",  "-"};
        static const char* lifCountNames[] = {"LIF 512-4k", "-", "LIF 512-4k", "-", "LIF 512-4k", "-"};
        static const char* lifTopoNames[] = {"Topology", "Topology", "Topology", "Topology", "Topology", "Topology"};
        if (globalImgXfadeKeyHeld_) {
            // I key overrides: show global image-load crossfade speed mode
            controlWin_.setKnobMode(KnobMode::FxParam);
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, gImgFdNames[i]);
            refreshKnobDisplay();
            return;
        }
        if (globalSceneXfadeKeyHeld_) {
            // S key overrides: show global scene-change crossfade speed mode
            controlWin_.setKnobMode(KnobMode::FxParam);
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, gScnFdNames[i]);
            refreshKnobDisplay();
            return;
        }
        if (globalRotationKeyHeld_) {
            // Shift+R overrides: show global rotation override mode
            controlWin_.setKnobMode(KnobMode::ImgRotate);
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, gRotNames[i]);
            refreshKnobDisplay();
            return;
        }
        if (globalZoomKeyHeld_) {
            // Shift+Z overrides: show global zoom override mode
            controlWin_.setKnobMode(KnobMode::ImgZoom);
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, gZoomNames[i]);
            refreshKnobDisplay();
            return;
        }
        if (globalOpacityKeyHeld_) {
            // Shift+O overrides: show global opacity override mode
            controlWin_.setKnobMode(KnobMode::LayerLevel);
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, gOpNames[i]);
            refreshKnobDisplay();
            return;
        }
        if (globalAudioGainKeyHeld_) {
            // Shift+G overrides: show global audio gain override mode
            controlWin_.setKnobMode(KnobMode::FxAudio);
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, gGainNames[i]);
            refreshKnobDisplay();
            return;
        }
        if (imgXfadeKeyHeld_) {
            // X key overrides: show image-load crossfade speed mode
            controlWin_.setKnobMode(KnobMode::FxParam); // reuse any label; will be overridden below
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, xfadeNames[i]);
            refreshKnobDisplay();
            return;
        }
        if (sceneXfadeKeyHeld_) {
            // C key overrides: show scene-change crossfade speed mode
            controlWin_.setKnobMode(KnobMode::FxParam);
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, scnFdNames[i]);
            refreshKnobDisplay();
            return;
        }
        KnobMode eff = effectiveMode();
        controlWin_.setKnobMode(eff);
        if (eff == KnobMode::ImgRotate) {
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, rotNames[i]);
        } else if (eff == KnobMode::ImgZoom) {
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, zoomNames[i]);
        } else if (eff == KnobMode::LayerLevel) {
            const char** names = globalOpacityKeyHeld_ ? gOpNames : opNames;
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, names[i]);
        } else if (eff == KnobMode::FxAudio) {
            const char** names = globalAudioGainKeyHeld_ ? gGainNames : gainNames;
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, names[i]);
        } else if (eff == KnobMode::ImgPan) {
            for (int i = 0; i < NUM_KNOBS; ++i) controlWin_.setKnobParamName(i, panNames[i]);
        } else {
            refreshKnobParamNames();
        }
        refreshKnobDisplay();
    };

    controlWin_.onRKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == rKeyHeld_) return;
        rKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onZKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == zKeyHeld_) return;
        zKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onOKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == oKeyHeld_) return;
        oKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onGKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == gKeyHeld_) return;
        gKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onPKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == pKeyHeld_) return;
        pKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onImgXfadeKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == imgXfadeKeyHeld_) return;
        imgXfadeKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onSceneXfadeKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == sceneXfadeKeyHeld_) return;
        sceneXfadeKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onGlobalImgXfadeKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == globalImgXfadeKeyHeld_) return;
        globalImgXfadeKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onGlobalSceneXfadeKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == globalSceneXfadeKeyHeld_) return;
        globalSceneXfadeKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onGlobalOpacityKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == globalOpacityKeyHeld_) return;
        globalOpacityKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onGlobalAudioGainKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == globalAudioGainKeyHeld_) return;
        globalAudioGainKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onGlobalRotationKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == globalRotationKeyHeld_) return;
        globalRotationKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    controlWin_.onGlobalZoomKey = [this, refreshModifierDisplay](bool pressed) {
        if (pressed == globalZoomKeyHeld_) return;
        globalZoomKeyHeld_ = pressed;
        refreshModifierDisplay();
    };
    audioMidiWin_.onBKey = [this](bool bypassed) {
        audioBypassed_ = bypassed;
        lifToneSynth_.setBypass(bypassed);
        // Zero out bands in compositor immediately when bypass toggles on
        if (bypassed) {
            const float zeros[8] = {};
            compositor_.setAudioBands(zeros, 8, 0.0f);
        }
    };
    audioMidiWin_.onLIFToneToggle = [this]() {
        lifToneEnabled_ = !lifToneEnabled_;
        if (!lifToneEnabled_)
            lifToneSynth_.setColumnEnergies({});
        std::cout << "[LIF Tone] " << (lifToneEnabled_ ? "enabled" : "disabled") << std::endl;
    };
    audioMidiWin_.onLIFToneVolumeNudge = [this](float delta) {
        lifToneVolume_ = std::clamp(lifToneVolume_ + delta, 0.0f, 100.0f);
        lifToneSynth_.setOutputVolume(lifToneVolume_);
        audioMidiWin_.setLifToneVolume(lifToneVolume_);
        std::cout << "[LIF Tone] volume=" << lifToneVolume_ << "%" << std::endl;
    };
    audioMidiWin_.onLIFToneTempoNudge = [this](float delta) {
        lifToneScanTempo_ = std::clamp(lifToneScanTempo_ + delta, 0.01f, 4.0f);
        std::cout << "[LIF Tone] tempo=" << lifToneScanTempo_ << " cycles/s" << std::endl;
    };
    audioMidiWin_.onLIFToneMinFreqNudge = [this](float deltaHz) {
        lifToneMinFreqHz_ = std::clamp(lifToneMinFreqHz_ + deltaHz, 20.0f, lifToneMaxFreqHz_ - 10.0f);
        lifToneSynth_.setFrequencyRange(lifToneMinFreqHz_, lifToneMaxFreqHz_);
        std::cout << "[LIF Tone] range=" << lifToneMinFreqHz_ << "-" << lifToneMaxFreqHz_ << " Hz" << std::endl;
    };
    audioMidiWin_.onLIFToneMaxFreqNudge = [this](float deltaHz) {
        lifToneMaxFreqHz_ = std::clamp(lifToneMaxFreqHz_ + deltaHz, lifToneMinFreqHz_ + 10.0f, 12000.0f);
        lifToneSynth_.setFrequencyRange(lifToneMinFreqHz_, lifToneMaxFreqHz_);
        std::cout << "[LIF Tone] range=" << lifToneMinFreqHz_ << "-" << lifToneMaxFreqHz_ << " Hz" << std::endl;
    };
    audioMidiWin_.onRhythmToggle = [this]() { toggleRhythmDriver(); };
    audioMidiWin_.onRhythmMixCycle = [this]() { cycleRhythmMixMode(); };
    audioMidiWin_.onRhythmPatternCycle = [this]() { cycleRhythmPattern(); };
    audioMidiWin_.onRhythmBpmNudge = [this](float delta) { nudgeRhythmBpm(delta); };
    audioMidiWin_.onRhythmIntensityNudge = [this](float delta) { nudgeRhythmIntensity(delta); };
    audioMidiWin_.onRhythmLaneToggle = [this](int laneIdx) { toggleRhythmLane(laneIdx); };
    audioMidiWin_.onRhythmLanePulseNudge = [this](int laneIdx, int delta) { nudgeRhythmLanePulses(laneIdx, delta); };
    audioMidiWin_.onRhythmLaneGainNudge = [this](int laneIdx, float delta) { nudgeRhythmLaneGain(laneIdx, delta); };
    mediaPickerWin_.onFileSelected = [this](int slot, const std::string& path){
        onImageSelected(slot, path);
    };

    // Keyboard shortcut: M key toggles LIF MIDI output
    audioMidiWin_.onLIFMidiToggle = [this]() { toggleLifMidi(); };
    audioMidiWin_.onLIFMidiStyleCycle = [this]() { cycleLifMidiStyle(); };
    audioMidiWin_.onLIFMidiModeCycle = [this]() { cycleLifMidiModalScale(); };
    audioMidiWin_.onLIFMidiModeToggle = [this]() { toggleLifMidiModeSource(); };
    audioMidiWin_.onLIFMidiModeEditDegreeNudge = [this](int delta) { nudgeLifMidiModeEditDegree(delta); };
    audioMidiWin_.onLIFMidiModeEditSemitoneNudge = [this](int delta) { nudgeLifMidiModeEditSemitone(delta); };
    audioMidiWin_.onLIFMidiModeEditReset = [this]() { resetLifMidiModeEdits(); };
    audioMidiWin_.onLIFMidiKeyNudge = [this](int delta) { nudgeLifMidiKey(delta); };
    audioMidiWin_.onLIFMidiTonalRootNudge = [this](int delta) { nudgeLifMidiTonalRoot(delta); };
    audioMidiWin_.onLIFMidiRangeMinNudge = [this](int delta) { nudgeLifMidiRangeMin(delta); };
    audioMidiWin_.onLIFMidiRangeMaxNudge = [this](int delta) { nudgeLifMidiRangeMax(delta); };

    audioMidiWin_.onTransientAuditionToggle = [this](bool enabled) {
        transientAuditionEnabled_ = enabled;
        std::cout << "[Transient Audition] " << (enabled ? "enabled" : "disabled") << "\n";
    };
}

// ── Engine helper: apply one knob value to the right engine target ────────────

void App::applyKnob(int knobIdx, float v, KnobMode mode) {
    switch (mode) {
        case KnobMode::LayerLevel:
            if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_FX_LAYERS) {
                int slot    = knobIdx / 2;     // 0→1, 2→3, 4→5
                int fxLayer = slot * 2 + 1;
                layers_.setOpacity(fxLayer, v);
                compositor_.setLayerOpacity(fxLayer, v);
            }
            break;
        case KnobMode::FxAudio:
            if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_FX_LAYERS)
                compositor_.setAudioGain(knobIdx / 2, v * 8.0f);  // 0.0–8.0x gain (stronger response)
            break;
        case KnobMode::FxParam: {
            int slot = knobIdx / 2, param = knobIdx % 2;
            if (slot < NUM_FX_LAYERS) {
                fxPatches_[slot].p[param] = v;
                compositor_.setFxParams(slot, fxPatches_[slot].p[0], fxPatches_[slot].p[1]);
            }
            break;
        }
        case KnobMode::ImgRotate:
            if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS)
                compositor_.setLayerRotation(knobIdx / 2, v * 2.0f * 3.14159265f);
            break;
        case KnobMode::ImgZoom:
            if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS)
                compositor_.setLayerZoom(knobIdx / 2, std::pow(64.0f, v - 0.5f)); // 0.125x .. 8.0x
            break;
        case KnobMode::ImgPan: {
            int slot = knobIdx / 2;  // knob pair: 0/1→slot0, 2/3→slot1, 4/5→slot2
            bool isY = (knobIdx % 2 == 1);
            if (slot < NUM_SRC_LAYERS) {
                float offset = (v - 0.5f) * 2.0f;  // −1.0 (left/up) .. +1.0 (right/down)
                if (isY) compositor_.setLayerPanY(slot, offset);
                else     compositor_.setLayerPanX(slot, offset);
            }
            break;
        }
    }
}

// ── Push all stored values from one scene to the engine ───────────────────────

void App::ensureSceneTransformDefaults(int idx) {
    if (idx < 0 || idx >= NUM_SCENES) return;

    SceneState& s = scenes_[idx];
    const int rotateMi = static_cast<int>(KnobMode::ImgRotate);
    const int zoomMi   = static_cast<int>(KnobMode::ImgZoom);
    const int panMi    = static_cast<int>(KnobMode::ImgPan);

    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot) {
        const int evenKnob = slot * 2;
        if (s.knobs[rotateMi][evenKnob] < 0.0f)
            s.knobs[rotateMi][evenKnob] = 0.0f;
        if (s.knobs[zoomMi][evenKnob] < 0.0f)
            s.knobs[zoomMi][evenKnob] = 0.5f;

        // Migration guard: older state formats can carry non-neutral zoom
        // values without zoom version metadata. Treat those as uninitialized.
        if (s.zoomVersion[slot] == 0)
            s.knobs[zoomMi][evenKnob] = 0.5f;
        if (s.zoomVersion[slot] == 0)
            s.zoomVersion[slot] = globalZoomVersion_[slot];

        const int xKnob = slot * 2;
        const int yKnob = xKnob + 1;
        if (s.knobs[panMi][xKnob] < 0.0f)
            s.knobs[panMi][xKnob] = 0.5f;
        if (s.knobs[panMi][yKnob] < 0.0f)
            s.knobs[panMi][yKnob] = 0.5f;
    }
}

void App::ensureSceneOpacityDefaults(int idx) {
    if (idx < 0 || idx >= NUM_SCENES) return;

    SceneState& s = scenes_[idx];
    const int layerMi = static_cast<int>(KnobMode::LayerLevel);
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
        const int knob = slot * 2;
        // Scene 0 (Fade to Black) defaults to opacity 0; all other scenes default to 1.
        const float defaultOpacity = (idx == 0) ? 0.0f : 1.0f;
        if (s.knobs[layerMi][knob] < 0.0f)
            s.knobs[layerMi][knob] = defaultOpacity;

        // Migration guard: older state payloads can contain 0.0 opacities for
        // untouched scenes, which makes media appear to "not load". For non-fade
        // scenes, treat version-0 values as uninitialized and restore visibility.
        if (idx != 0 && s.opacityVersion[slot] == 0)
            s.knobs[layerMi][knob] = 1.0f;

        if (s.opacityVersion[slot] == 0)
            s.opacityVersion[slot] = globalOpacityVersion_[slot];
    }
}

void App::ensureSceneAudioGainDefaults(int idx) {
    if (idx < 0 || idx >= NUM_SCENES) return;

    SceneState& s = scenes_[idx];
    const int gainMi = static_cast<int>(KnobMode::FxAudio);
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
        const int knob = slot * 2;
        if (s.knobs[gainMi][knob] < 0.0f)
            s.knobs[gainMi][knob] = 0.125f; // compositor gain 1.0x
        if (s.audioGainVersion[slot] == 0)
            s.audioGainVersion[slot] = globalAudioGainVersion_[slot];
    }
}

void App::ensureSceneLIFDefaults(int idx) {
    if (idx < 0 || idx >= NUM_SCENES) return;

    SceneState& s = scenes_[idx];
    const Scene& sc = SCENES[idx];
    const auto fx = enforcedSceneFx(sc);
    if (s.lifNeuronCount <= 0) {
        int lifPatchCount = 0;
        for (FxPatchId patch : fx)
            if (isLIFPatch(patch))
                ++lifPatchCount;
        s.lifNeuronCount = (lifPatchCount >= 2) ? 2048 : 1024;
    }

    if (s.lifTopologyIndex < 0) {
        int fxMi = static_cast<int>(KnobMode::FxParam);
        for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
            if (!isLIFPatch(fx[slot])) continue;
            float stored = s.knobs[fxMi][slot * 2 + 1];
            float source = (stored >= 0.0f) ? stored : sc.params[slot][1];
            s.lifTopologyIndex = topologyIndexFromNorm(source);
            break;
        }
        if (s.lifTopologyIndex < 0)
            s.lifTopologyIndex = 0;
    }
}

void App::applySceneCrossfadeSettings(int idx) {
    if (idx < 0 || idx >= NUM_SCENES) return;
    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot)
        compositor_.setCrossfadeSpeed(slot, 0.1f + effectiveImageCrossfadeNorm(idx, slot) * 7.9f);
}

float App::effectiveImageCrossfadeNorm(int sceneIdx, int slot) const {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_SRC_LAYERS) return 0.1f;
    const SceneState& s = scenes_[sceneIdx];
    if (s.imageCrossfadeVersion[slot] < globalImageCrossfadeVersion_[slot])
        return globalImageCrossfadeNorm_[slot];
    return s.imageCrossfadeSpeedNorm[slot];
}

float App::effectiveSceneCrossfadeNorm(int sceneIdx, int slot) const {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_SRC_LAYERS) return 0.1f;
    const SceneState& s = scenes_[sceneIdx];
    if (s.sceneCrossfadeVersion[slot] < globalSceneCrossfadeVersion_[slot])
        return globalSceneCrossfadeNorm_[slot];
    return s.sceneCrossfadeSpeedNorm[slot];
}

float App::effectiveLayerOpacityNorm(int sceneIdx, int slot) const {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_FX_LAYERS) return 1.0f;
    const SceneState& s = scenes_[sceneIdx];
    if (s.opacityVersion[slot] < globalOpacityVersion_[slot])
        return globalOpacityNorm_[slot];
    const int layerMi = static_cast<int>(KnobMode::LayerLevel);
    const float local = s.knobs[layerMi][slot * 2];
    return (local >= 0.0f) ? local : 1.0f;
}

float App::effectiveAudioGainNorm(int sceneIdx, int slot) const {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_FX_LAYERS) return 0.125f;
    const SceneState& s = scenes_[sceneIdx];
    if (s.audioGainVersion[slot] < globalAudioGainVersion_[slot])
        return globalAudioGainNorm_[slot];
    const int gainMi = static_cast<int>(KnobMode::FxAudio);
    const float local = s.knobs[gainMi][slot * 2];
    return (local >= 0.0f) ? local : 0.125f;
}

void App::setLocalLayerOpacityNorm(int sceneIdx, int slot, float norm) {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_FX_LAYERS) return;
    const int layerMi = static_cast<int>(KnobMode::LayerLevel);
    scenes_[sceneIdx].knobs[layerMi][slot * 2] = norm;
    scenes_[sceneIdx].opacityVersion[slot] = globalOpacityVersion_[slot];
}

void App::setGlobalLayerOpacityNorm(int slot, float norm) {
    if (slot < 0 || slot >= NUM_FX_LAYERS) return;
    globalOpacityNorm_[slot] = norm;
    ++globalOpacityVersion_[slot];
}

void App::setLocalAudioGainNorm(int sceneIdx, int slot, float norm) {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_FX_LAYERS) return;
    const int gainMi = static_cast<int>(KnobMode::FxAudio);
    scenes_[sceneIdx].knobs[gainMi][slot * 2] = norm;
    scenes_[sceneIdx].audioGainVersion[slot] = globalAudioGainVersion_[slot];
}

void App::setGlobalAudioGainNorm(int slot, float norm) {
    if (slot < 0 || slot >= NUM_FX_LAYERS) return;
    globalAudioGainNorm_[slot] = norm;
    ++globalAudioGainVersion_[slot];
}

float App::effectiveRotationNorm(int sceneIdx, int slot) const {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_SRC_LAYERS) return 0.5f;
    const SceneState& s = scenes_[sceneIdx];
    if (s.rotationVersion[slot] < globalRotationVersion_[slot])
        return globalRotationNorm_[slot];
    const int rotMi = static_cast<int>(KnobMode::ImgRotate);
    const float local = s.knobs[rotMi][slot * 2];
    return (local >= 0.0f) ? local : 0.5f;
}

void App::setLocalRotationNorm(int sceneIdx, int slot, float norm) {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_SRC_LAYERS) return;
    const int rotMi = static_cast<int>(KnobMode::ImgRotate);
    scenes_[sceneIdx].knobs[rotMi][slot * 2] = norm;
    scenes_[sceneIdx].rotationVersion[slot] = globalRotationVersion_[slot];
}

void App::setGlobalRotationNorm(int slot, float norm) {
    if (slot < 0 || slot >= NUM_SRC_LAYERS) return;
    globalRotationNorm_[slot] = norm;
    ++globalRotationVersion_[slot];
}

float App::effectiveZoomNorm(int sceneIdx, int slot) const {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_SRC_LAYERS) return 0.5f;
    const SceneState& s = scenes_[sceneIdx];
    if (s.zoomVersion[slot] < globalZoomVersion_[slot])
        return globalZoomNorm_[slot];
    const int zoomMi = static_cast<int>(KnobMode::ImgZoom);
    const float local = s.knobs[zoomMi][slot * 2];
    return (local >= 0.0f) ? local : 0.5f;
}

void App::setLocalZoomNorm(int sceneIdx, int slot, float norm) {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_SRC_LAYERS) return;
    const int zoomMi = static_cast<int>(KnobMode::ImgZoom);
    scenes_[sceneIdx].knobs[zoomMi][slot * 2] = norm;
    scenes_[sceneIdx].zoomVersion[slot] = globalZoomVersion_[slot];
}

void App::setGlobalZoomNorm(int slot, float norm) {
    if (slot < 0 || slot >= NUM_SRC_LAYERS) return;
    globalZoomNorm_[slot] = norm;
    ++globalZoomVersion_[slot];
}

void App::setLocalImageCrossfadeNorm(int sceneIdx, int slot, float norm) {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_SRC_LAYERS) return;
    scenes_[sceneIdx].imageCrossfadeSpeedNorm[slot] = norm;
    scenes_[sceneIdx].imageCrossfadeVersion[slot] = globalImageCrossfadeVersion_[slot];
}

void App::setLocalSceneCrossfadeNorm(int sceneIdx, int slot, float norm) {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES || slot < 0 || slot >= NUM_SRC_LAYERS) return;
    scenes_[sceneIdx].sceneCrossfadeSpeedNorm[slot] = norm;
    scenes_[sceneIdx].sceneCrossfadeVersion[slot] = globalSceneCrossfadeVersion_[slot];
}

void App::setGlobalImageCrossfadeNorm(int slot, float norm) {
    if (slot < 0 || slot >= NUM_SRC_LAYERS) return;
    globalImageCrossfadeNorm_[slot] = norm;
    ++globalImageCrossfadeVersion_[slot];
}

void App::setGlobalSceneCrossfadeNorm(int slot, float norm) {
    if (slot < 0 || slot >= NUM_SRC_LAYERS) return;
    globalSceneCrossfadeNorm_[slot] = norm;
    ++globalSceneCrossfadeVersion_[slot];
}

void App::applySceneToEngine(int idx) {
    ensureSceneTransformDefaults(idx);
    ensureSceneOpacityDefaults(idx);
    ensureSceneAudioGainDefaults(idx);
    ensureSceneLIFDefaults(idx);
    applySceneCrossfadeSettings(idx);
    compositor_.setLIFTopology(topologyFromIndex(scenes_[idx].lifTopologyIndex));
    compositor_.setLIFNeuronCount(scenes_[idx].lifNeuronCount);
    const SceneState& s = scenes_[idx];

    // Layer opacity is scene-local by default and can be globally overridden.
    // Skip when a pan/zoom/opacity animation is running — it will set opacity each frame.
    if (!panZoomAnimating_) {
        for (int slot = 0; slot < NUM_FX_LAYERS; ++slot)
            applyKnob(slot * 2, effectiveLayerOpacityNorm(idx, slot), KnobMode::LayerLevel);
    }

    // Audio gain is scene-local by default and can be globally overridden.
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot)
        applyKnob(slot * 2, effectiveAudioGainNorm(idx, slot), KnobMode::FxAudio);

    // Rotation is scene-local by default and can be globally overridden.
    // Always apply on scene load; pan/zoom animation does not animate rotation.
    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot)
        applyKnob(slot * 2, effectiveRotationNorm(idx, slot), KnobMode::ImgRotate);

    // Zoom is scene-local by default and can be globally overridden.
    // Skip when a pan/zoom/opacity animation is running — it will set zoom each frame.
    if (!panZoomAnimating_) {
        for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot)
            applyKnob(slot * 2, effectiveZoomNorm(idx, slot), KnobMode::ImgZoom);
    }

    for (int mi = 0; mi < SceneState::NMODES; ++mi) {
        if (mi == static_cast<int>(KnobMode::LayerLevel) || mi == static_cast<int>(KnobMode::FxAudio) || mi == static_cast<int>(KnobMode::ImgRotate) || mi == static_cast<int>(KnobMode::ImgZoom)) continue;
        // Skip pan when animating — the animation sets it each frame.
        if (panZoomAnimating_ && mi == static_cast<int>(KnobMode::ImgPan)) continue;
        auto mode = static_cast<KnobMode>(mi);
        for (int k = 0; k < NUM_KNOBS; ++k) {
            if (s.knobs[mi][k] >= 0.0f)
                applyKnob(k, s.knobs[mi][k], mode);
        }
    }

    // Apply scene-local pressure mappings on top of stored base values.
    applyPressureMappings(idx);
}

void App::applyPressureMappings(int sceneIdx) {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES) return;

    const auto& mapping = pressureSceneState_[sceneIdx];
    const bool anyEnabled = std::any_of(mapping.enabled.begin(), mapping.enabled.end(),
                                        [](uint8_t v) { return v != 0; });
    if (!anyEnabled) return;

    const SceneState& s = scenes_[sceneIdx];
    const float pressure = std::clamp(scenePressureNorm_[sceneIdx], 0.0f, 1.0f);
    const int fxMi = static_cast<int>(KnobMode::FxParam);
    const int panMi = static_cast<int>(KnobMode::ImgPan);

    auto modulated = [&](int targetIdx, float base) {
        if (!mapping.enabled[targetIdx]) return base;
        return std::clamp(base + pressure * mapping.amount[targetIdx], 0.0f, 1.0f);
    };

    // Zoom L0..L2 targets [0..2]
    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot)
        applyKnob(slot * 2, modulated(slot, effectiveZoomNorm(sceneIdx, slot)), KnobMode::ImgZoom);

    // Rotate L0..L2 targets [3..5]
    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot)
        applyKnob(slot * 2, modulated(3 + slot, effectiveRotationNorm(sceneIdx, slot)), KnobMode::ImgRotate);

    // Opacity FX0..FX2 targets [6..8]
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot)
        applyKnob(slot * 2, modulated(6 + slot, effectiveLayerOpacityNorm(sceneIdx, slot)), KnobMode::LayerLevel);

    // Audio gain FX0..FX2 targets [9..11]
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot)
        applyKnob(slot * 2, modulated(9 + slot, effectiveAudioGainNorm(sceneIdx, slot)), KnobMode::FxAudio);

    // Pan targets [12..17]
    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot) {
        const int xKnob = slot * 2;
        const int yKnob = xKnob + 1;
        float baseX = s.knobs[panMi][xKnob] >= 0.0f ? s.knobs[panMi][xKnob] : 0.5f;
        float baseY = s.knobs[panMi][yKnob] >= 0.0f ? s.knobs[panMi][yKnob] : 0.5f;
        applyKnob(xKnob, modulated(12 + slot * 2, baseX), KnobMode::ImgPan);
        applyKnob(yKnob, modulated(13 + slot * 2, baseY), KnobMode::ImgPan);
    }

    // FX params [18..23]
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
        float p0 = s.knobs[fxMi][slot * 2];
        float p1 = s.knobs[fxMi][slot * 2 + 1];
        if (p0 < 0.0f) p0 = 0.5f;
        if (p1 < 0.0f) p1 = 0.5f;

        p0 = modulated(18 + slot * 2, p0);
        p1 = modulated(19 + slot * 2, p1);
        fxPatches_[slot].p[0] = p0;
        fxPatches_[slot].p[1] = p1;
        compositor_.setFxParams(slot, p0, p1);
    }
}

// ── Update knob param name labels from active scene's FX patches ──────────────

void App::refreshKnobParamNames() {
    if (currentScene_ < 0) return;
    const Scene& sc = SCENES[currentScene_];
    // Knobs 0-1 → FX slot 0, knobs 2-3 → FX slot 1, knobs 4-5 → FX slot 2
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
        FxPatchId patch = sc.fx[slot];
        std::string prefix = std::string(fxPatchName(patch)) + " ";
        controlWin_.setKnobParamName(slot * 2,     prefix + fxParamName(patch, 0));
        controlWin_.setKnobParamName(slot * 2 + 1, prefix + fxParamName(patch, 1));
    }
}

// ── Sync all 6 knob arc widgets for the current mode ─────────────────────────

void App::refreshKnobDisplay() {
    if (currentScene_ < 0) return;

    // I key mode: show global image-load crossfade values for even knobs.
    if (globalImgXfadeKeyHeld_) {
        for (int k = 0; k < NUM_KNOBS; ++k) {
            if (k % 2 == 1) { controlWin_.setKnobValue(k, 0); continue; }
            int slot = k / 2;
            controlWin_.setKnobValue(k, static_cast<int>(globalImageCrossfadeNorm_[slot] * 127.0f));
        }
        return;
    }

    // S key mode: show global scene-change crossfade values for even knobs.
    if (globalSceneXfadeKeyHeld_) {
        for (int k = 0; k < NUM_KNOBS; ++k) {
            if (k % 2 == 1) { controlWin_.setKnobValue(k, 0); continue; }
            int slot = k / 2;
            controlWin_.setKnobValue(k, static_cast<int>(globalSceneCrossfadeNorm_[slot] * 127.0f));
        }
        return;
    }

    // L key mode: show global opacity override values for even knobs.
    if (globalOpacityKeyHeld_) {
        for (int k = 0; k < NUM_KNOBS; ++k) {
            if (k % 2 == 1) { controlWin_.setKnobValue(k, 0); continue; }
            int slot = k / 2;
            controlWin_.setKnobValue(k, static_cast<int>(globalOpacityNorm_[slot] * 127.0f));
        }
        return;
    }

    // H key mode: show global audio gain override values for even knobs.
    if (globalAudioGainKeyHeld_) {
        for (int k = 0; k < NUM_KNOBS; ++k) {
            if (k % 2 == 1) { controlWin_.setKnobValue(k, 0); continue; }
            int slot = k / 2;
            controlWin_.setKnobValue(k, static_cast<int>(globalAudioGainNorm_[slot] * 127.0f));
        }
        return;
    }

    // Shift+R key mode: show global rotation override values for even knobs.
    if (globalRotationKeyHeld_) {
        for (int k = 0; k < NUM_KNOBS; ++k) {
            if (k % 2 == 1) { controlWin_.setKnobValue(k, 0); continue; }
            int slot = k / 2;
            controlWin_.setKnobValue(k, static_cast<int>(globalRotationNorm_[slot] * 127.0f));
        }
        return;
    }

    // Shift+Z key mode: show global zoom override values for even knobs.
    if (globalZoomKeyHeld_) {
        for (int k = 0; k < NUM_KNOBS; ++k) {
            if (k % 2 == 1) { controlWin_.setKnobValue(k, 0); continue; }
            int slot = k / 2;
            controlWin_.setKnobValue(k, static_cast<int>(globalZoomNorm_[slot] * 127.0f));
        }
        return;
    }

    // F key mode: show image-load crossfade speed values for even knobs, 0 for odd.
    if (imgXfadeKeyHeld_) {
        for (int k = 0; k < NUM_KNOBS; ++k) {
            if (k % 2 == 1) { controlWin_.setKnobValue(k, 0); continue; }
            int slot = k / 2;
            controlWin_.setKnobValue(k, static_cast<int>(scenes_[currentScene_].imageCrossfadeSpeedNorm[slot] * 127.0f));
        }
        return;
    }

    // C key mode: show scene-change crossfade speed values for even knobs, 0 for odd.
    if (sceneXfadeKeyHeld_) {
        for (int k = 0; k < NUM_KNOBS; ++k) {
            if (k % 2 == 1) { controlWin_.setKnobValue(k, 0); continue; }
            int slot = k / 2;
            controlWin_.setKnobValue(k, static_cast<int>(scenes_[currentScene_].sceneCrossfadeSpeedNorm[slot] * 127.0f));
        }
        return;
    }

    KnobMode eff = effectiveMode();
    int mi = static_cast<int>(eff);
    const SceneState& s = scenes_[currentScene_];
    // For rotate/zoom/opacity: knobs 0,2,4 active; knobs 1,3,5 inactive.
    // For pan: all 6 active. For others: all 6 active.
    const bool evenOnlyMode = (eff == KnobMode::ImgRotate  ||
                               eff == KnobMode::ImgZoom    ||
                               eff == KnobMode::LayerLevel ||
                               eff == KnobMode::FxAudio);
    for (int k = 0; k < NUM_KNOBS; ++k) {
        // Odd knobs are inactive in single-param-per-layer modes.
        if (evenOnlyMode && k % 2 == 1) {
            controlWin_.setKnobValue(k, 0);
            controlWin_.setKnobTopoName(k, "");  // Clear topology name
            continue;
        }
        float v = s.knobs[mi][k];
        if (eff == KnobMode::LayerLevel && (k % 2 == 0))
            v = effectiveLayerOpacityNorm(currentScene_, k / 2);
        if (eff == KnobMode::FxAudio && (k % 2 == 0))
            v = effectiveAudioGainNorm(currentScene_, k / 2);
        // If this knob hasn't been set in this scene yet, show physical position.
        float display = (v >= 0.0f) ? v : (evenOnlyMode ? 0.0f : knobLastPhys_[k]);
        controlWin_.setKnobValue(k, static_cast<int>(display * 127.0f));
        
        // Show topology name for odd knobs in FxParam mode when the knob is a topology parameter.
        if (eff == KnobMode::FxParam && k % 2 == 1) {
            int slot = k / 2;
            FxPatchId patch = SCENES[currentScene_].fx[slot];
            if (isLIFPatch(patch)) {
                // This knob controls the topology parameter of a LIF patch.
                int topoIdx = s.lifTopologyIndex;
                if (topoIdx < 0) topoIdx = 0;
                controlWin_.setKnobTopoName(k, topologyIndexToName(topoIdx));
            } else {
                controlWin_.setKnobTopoName(k, "");  // Clear for non-LIF patches
            }
        } else {
            controlWin_.setKnobTopoName(k, "");  // Clear topology name for all other modes
        }
    }
}

// ── MIDI knob handler ─────────────────────────────────────────────────────────

void App::onKnob(int knobIdx, float normValue, KnobMode mode) {
    knobLastPhys_[knobIdx] = normValue;

    // Ensure there is an active scene so incoming MIDI always has a target.
    if (currentScene_ < 0)
        onSceneSelect(0);

    // If a modifier key is held, override to its mode
    KnobMode effectiveMod = effectiveMode();
    // Only use MIDI-provided mode when no modifier key is held.
    KnobMode eff = (rKeyHeld_ || zKeyHeld_ || oKeyHeld_ || gKeyHeld_ || pKeyHeld_ || imgXfadeKeyHeld_ || sceneXfadeKeyHeld_ || globalImgXfadeKeyHeld_ || globalSceneXfadeKeyHeld_ || globalOpacityKeyHeld_ || globalAudioGainKeyHeld_ || globalRotationKeyHeld_ || globalZoomKeyHeld_) ? effectiveMod : mode;

    // I key intercept: set global image-load crossfade speed override for the even knob's slot.
    if (globalImgXfadeKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalImageCrossfadeNorm(slot, normValue);
            compositor_.setCrossfadeSpeed(slot, 0.1f + normValue * 7.9f); // 0.1–8.0 s
            controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        }
        return;
    }

    // S key intercept: set global scene-change crossfade speed override for the even knob's slot.
    if (globalSceneXfadeKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalSceneCrossfadeNorm(slot, normValue);
            controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        }
        return;
    }

    // L key intercept: set global opacity override for the even knob's FX slot.
    if (globalOpacityKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_FX_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalLayerOpacityNorm(slot, normValue);
            applyKnob(knobIdx, normValue, KnobMode::LayerLevel);
            controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        }
        return;
    }

    // H key intercept: set global audio gain override for the even knob's FX slot.
    if (globalAudioGainKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_FX_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalAudioGainNorm(slot, normValue);
            applyKnob(knobIdx, normValue, KnobMode::FxAudio);
            controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        }
        return;
    }

    // Only Shift+R or Caps Lock+R: set global rotation override. R alone is always local.
    if (globalRotationKeyHeld_ && !rKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalRotationNorm(slot, normValue);
            applyKnob(knobIdx, normValue, KnobMode::ImgRotate);
            controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
            return;
        }
    }
    // Always set local rotation if R is held (rKeyHeld_) or mode/eff is ImgRotate
    if ((rKeyHeld_ || mode == KnobMode::ImgRotate || eff == KnobMode::ImgRotate) && knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
        int slot = knobIdx / 2;
        setLocalRotationNorm(currentScene_, slot, normValue);
        applyKnob(knobIdx, normValue, KnobMode::ImgRotate);
        controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        return;
    }

    // Shift+Z intercept: set global zoom override for the even knob's source slot.
    if (globalZoomKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalZoomNorm(slot, normValue);
            applyKnob(knobIdx, normValue, KnobMode::ImgZoom);
            controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        }
        return;
    }

    // F key intercept: set scene-local image-load crossfade speed for the even knob's slot.
    if (imgXfadeKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setLocalImageCrossfadeNorm(currentScene_, slot, normValue);
            compositor_.setCrossfadeSpeed(slot, 0.1f + normValue * 7.9f); // 0.1–8.0 s
            controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        }
        return;
    }

    // C key intercept: set scene-local scene-change crossfade speed for the even knob's slot.
    if (sceneXfadeKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setLocalSceneCrossfadeNorm(currentScene_, slot, normValue);
            controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        }
        return;
    }
    int    mi   = static_cast<int>(eff);
    float& soft = scenes_[currentScene_].knobs[mi][knobIdx];

    // Immediate mode: always store and apply on every MIDI movement.
    if (eff == KnobMode::LayerLevel && knobIdx % 2 == 0) {
        setLocalLayerOpacityNorm(currentScene_, knobIdx / 2, normValue);
    }
    if (eff == KnobMode::FxAudio && knobIdx % 2 == 0) {
        setLocalAudioGainNorm(currentScene_, knobIdx / 2, normValue);
    }
    soft = normValue;
    if (eff == KnobMode::FxParam && (knobIdx % 2 == 1)) {
        int slot = knobIdx / 2;
        if (slot < NUM_FX_LAYERS && isLIFPatch(SCENES[currentScene_].fx[slot])) {
            scenes_[currentScene_].lifTopologyIndex = topologyIndexFromNorm(normValue);
            compositor_.setLIFTopology(topologyFromIndex(scenes_[currentScene_].lifTopologyIndex));
            controlWin_.setKnobTopoName(knobIdx, topologyIndexToName(scenes_[currentScene_].lifTopologyIndex));
        }
    }
    applyKnob(knobIdx, normValue, eff);
    applyPressureMappings(currentScene_);
    controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
}

// ── Pan/Zoom animation helpers ────────────────────────────────────────────────

void App::startPanZoomAnimation() {
    // Capture current pan/zoom values as the starting point
    const int panMi = static_cast<int>(KnobMode::ImgPan);
    const int zoomMi = static_cast<int>(KnobMode::ImgZoom);
    
    // Match pan/zoom ramp length to effective scene crossfade duration.
    float avgCrossfadeSpeedNorm = 0.0f;
    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot)
        avgCrossfadeSpeedNorm += effectiveSceneCrossfadeNorm(currentScene_, slot);
    avgCrossfadeSpeedNorm /= static_cast<float>(NUM_SRC_LAYERS);
    panZoomAnimDuration_ = 0.1f + avgCrossfadeSpeedNorm * 7.9f;  // 0.1–8.0 seconds
    
    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot) {
        // Current pan values from compositor
        compositor_.getLayerPan(slot, panXFrom_[slot], panYFrom_[slot]);
        panXTo_[slot] = (scenes_[currentScene_].knobs[panMi][slot * 2] - 0.5f) * 2.0f;
        panYTo_[slot] = (scenes_[currentScene_].knobs[panMi][slot * 2 + 1] - 0.5f) * 2.0f;
        
        // Current zoom value from compositor
        zoomFrom_[slot] = compositor_.getLayerZoom(slot);
        float zoomNorm = scenes_[currentScene_].knobs[zoomMi][slot * 2];
        zoomTo_[slot] = zoomNorm >= 0.0f ? std::pow(64.0f, zoomNorm - 0.5f) : 1.0f;
    }
    
    // Capture opacity from/to for each FX layer.
    const int layerMi = static_cast<int>(KnobMode::LayerLevel);
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
        opacityFrom_[slot] = layers_.state(slot * 2 + 1).opacity;
        float opNorm = scenes_[currentScene_].knobs[layerMi][slot * 2];
        opacityTo_[slot] = (opNorm >= 0.0f) ? opNorm : effectiveLayerOpacityNorm(currentScene_, slot);
    }
    
    panZoomAnimTime_ = 0.0f;
    panZoomAnimating_ = true;
}

void App::updatePanZoomAnimation(float deltaTime) {
    if (!panZoomAnimating_) return;
    
    panZoomAnimTime_ += deltaTime;
    float progress = panZoomAnimTime_ / panZoomAnimDuration_;
    
    if (progress >= 1.0f) {
        progress = 1.0f;
        panZoomAnimating_ = false;
    }
    
    // Easing: smooth step for a gentle start/end
    float eased = progress * progress * (3.0f - 2.0f * progress);
    
    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot) {
        float panX = panXFrom_[slot] + (panXTo_[slot] - panXFrom_[slot]) * eased;
        float panY = panYFrom_[slot] + (panYTo_[slot] - panYFrom_[slot]) * eased;
        float zoom = zoomFrom_[slot] + (zoomTo_[slot] - zoomFrom_[slot]) * eased;
        
        compositor_.setLayerPanX(slot, panX);
        compositor_.setLayerPanY(slot, panY);
        compositor_.setLayerZoom(slot, zoom);
    }
    
    // Animate FX layer opacities.
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
        float opacity = opacityFrom_[slot] + (opacityTo_[slot] - opacityFrom_[slot]) * eased;
        layers_.setOpacity(slot * 2 + 1, opacity);
        compositor_.setLayerOpacity(slot * 2 + 1, opacity);
    }
}

// ── Scene select ──────────────────────────────────────────────────────────────


void App::onSceneSelect(int sceneIdx) {
    if (sceneIdx < 0 || sceneIdx >= NUM_SCENES) return;

    const int prevScene = currentScene_;

    // Set LIF MIDI key to match the root note of the triggering scene pad.
    // Scene pads are mapped to MIDI notes starting at C2 (36), each pad is a semitone.
    // 0 = C, 1 = C#, ..., 11 = B
    lifMidiKeySemitone_ = sceneKeySemitone(sceneIdx);

    currentScene_ = sceneIdx;

    refreshLifMidiUi();
    resetLifMidiState();

    const Scene& sc = SCENES[sceneIdx];
    const auto fx = enforcedSceneFx(sc);

    compositor_.resetFeedbackBuffers();

    // 1. Apply the scene's FX patch selection.
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
        fxPatches_[slot].id = fx[slot];
        compositor_.setFxPatch(slot, fx[slot]);
        layers_.setFxPatch(slot * 2 + 1, fx[slot]);
    }
    controlWin_.setSceneName(sc.name);
    mediaPickerWin_.setSceneName(sc.name);
    pressureWin_.setSceneName(sc.name);
    pressureWin_.setTargetStates(
        std::vector<uint8_t>(pressureSceneState_[sceneIdx].enabled.begin(), pressureSceneState_[sceneIdx].enabled.end()),
        std::vector<float>(pressureSceneState_[sceneIdx].amount.begin(), pressureSceneState_[sceneIdx].amount.end()));
    audioMidiWin_.setPressureNorm(scenePressureNorm_[sceneIdx]);
    scenePressureTargetNorm_[sceneIdx] = scenePressureNorm_[sceneIdx];

    // Load this scene's image files into the 3 source layers.
    // Changed paths are crossfaded; unchanged paths are still reloaded to reset playback.
    for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot) {
        std::string path = resolveMediaPathForLoad(scenes_[sceneIdx].imgPaths[slot]);
        if (path != scenes_[sceneIdx].imgPaths[slot])
            scenes_[sceneIdx].imgPaths[slot] = path;
        const std::string prevPath =
            (prevScene >= 0 && prevScene < NUM_SCENES) ? scenes_[prevScene].imgPaths[slot] : std::string();
        const bool changed = (path != prevPath);
        if (!path.empty()) {
            if (changed) {
                // New media should start neutral: centered, 1.0x zoom, fully visible.
                const int rotateMi = static_cast<int>(KnobMode::ImgRotate);
                const int zoomMi = static_cast<int>(KnobMode::ImgZoom);
                const int panMi = static_cast<int>(KnobMode::ImgPan);
                const int layerMi = static_cast<int>(KnobMode::LayerLevel);
                const int evenKnob = slot * 2;
                scenes_[sceneIdx].knobs[rotateMi][evenKnob] = 0.0f;
                scenes_[sceneIdx].knobs[zoomMi][evenKnob] = 0.5f;
                scenes_[sceneIdx].knobs[panMi][evenKnob] = 0.5f;
                scenes_[sceneIdx].knobs[panMi][evenKnob + 1] = 0.5f;
                scenes_[sceneIdx].knobs[layerMi][evenKnob] = 1.0f;
                scenes_[sceneIdx].zoomVersion[slot] = globalZoomVersion_[slot];
                scenes_[sceneIdx].opacityVersion[slot] = globalOpacityVersion_[slot];

                // Scene-triggered image swaps use image-load crossfade (local F or global I override).
                compositor_.setCrossfadeSpeed(slot, 0.1f + effectiveImageCrossfadeNorm(sceneIdx, slot) * 7.9f);
                compositor_.beginCrossfade(slot);
            }
            // Always reload scene media so selecting a scene can reset slot playback,
            // even when the path is unchanged from the previous scene.
            layers_.loadMedia(slot * 2, path);
        }
    }
    mediaPickerWin_.setSlotPaths(scenes_[sceneIdx].imgPaths);

    int fxMi = static_cast<int>(KnobMode::FxParam);
    SceneState& s = scenes_[sceneIdx];

    ensureSceneTransformDefaults(sceneIdx);
    ensureSceneLIFDefaults(sceneIdx);

    if (!s.hasData(fxMi)) {
        // First visit: seed FxParam knobs from SCENES[] defaults.
        // Applied directly to the engine; scene object stores them so
        // subsequent visits correctly restore them.
        for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
            s.knobs[fxMi][slot * 2]     = sc.params[slot][0];
            s.knobs[fxMi][slot * 2 + 1] = sc.params[slot][1];
        }
    }

    // 2. Start pan/zoom animation before applying the new scene.
    startPanZoomAnimation();

    // 3. Apply all stored knob values to the engine.
    applySceneToEngine(sceneIdx);

    // 4. Refresh the software knob arcs to show what is actually applied.
    refreshKnobDisplay();
    // 5. Update knob param name labels to reflect the new scene's FX patches.
    refreshKnobParamNames();
    // Persist the new currentScene_ immediately so a crash won't revert it.
    saveState();

    // Reset MIDI state so a scene change starts a fresh note stream.
    if (lifMidiEnabled_)
        resetLifMidiState();
}

// ── GUI knob drag ─────────────────────────────────────────────────────────────

void App::onKnobDrag(int knobIdx, float normValue) {
    if (currentScene_ < 0) return;

    // I key intercept: set global image-load crossfade speed override.
    if (globalImgXfadeKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalImageCrossfadeNorm(slot, normValue);
            compositor_.setCrossfadeSpeed(slot, 0.1f + normValue * 7.9f); // 0.1–8.0 s
        }
        controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        return;
    }

    // S key intercept: set global scene-change crossfade speed override.
    if (globalSceneXfadeKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalSceneCrossfadeNorm(slot, normValue);
        }
        controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        return;
    }

    // L key intercept: set global opacity override.
    if (globalOpacityKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_FX_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalLayerOpacityNorm(slot, normValue);
            applyKnob(knobIdx, normValue, KnobMode::LayerLevel);
        }
        controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        return;
    }

    // H key intercept: set global audio gain override.
    if (globalAudioGainKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_FX_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalAudioGainNorm(slot, normValue);
            applyKnob(knobIdx, normValue, KnobMode::FxAudio);
        }
        controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        return;
    }

    // Shift+R intercept: set global rotation override.
    if (globalRotationKeyHeld_ && !rKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalRotationNorm(slot, normValue);
            applyKnob(knobIdx, normValue, KnobMode::ImgRotate);
        }
        controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        return;
    }

    // Shift+Z intercept: set global zoom override.
    if (globalZoomKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setGlobalZoomNorm(slot, normValue);
            applyKnob(knobIdx, normValue, KnobMode::ImgZoom);
        }
        controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        return;
    }

    // F key intercept: set scene-local image-load crossfade speed.
    if (imgXfadeKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setLocalImageCrossfadeNorm(currentScene_, slot, normValue);
            compositor_.setCrossfadeSpeed(slot, 0.1f + normValue * 7.9f); // 0.1–8.0 s
        }
        controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        return;
    }

    // C key intercept: set scene-local scene-change crossfade speed.
    if (sceneXfadeKeyHeld_) {
        if (knobIdx % 2 == 0 && knobIdx / 2 < NUM_SRC_LAYERS) {
            int slot = knobIdx / 2;
            setLocalSceneCrossfadeNorm(currentScene_, slot, normValue);
        }
        controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
        return;
    }


    KnobMode eff = effectiveMode();
    int mi = static_cast<int>(eff);
    // GUI drag bypasses pickup: write directly to scene and sync physical tracker.
    if (eff == KnobMode::LayerLevel && knobIdx % 2 == 0)
        setLocalLayerOpacityNorm(currentScene_, knobIdx / 2, normValue);
    if (eff == KnobMode::FxAudio && knobIdx % 2 == 0)
        setLocalAudioGainNorm(currentScene_, knobIdx / 2, normValue);
    if (eff == KnobMode::ImgRotate && knobIdx % 2 == 0)
        setLocalRotationNorm(currentScene_, knobIdx / 2, normValue);
    scenes_[currentScene_].knobs[mi][knobIdx] = normValue;
    if (eff == KnobMode::FxParam && (knobIdx % 2 == 1)) {
        int slot = knobIdx / 2;
        if (slot < NUM_FX_LAYERS && isLIFPatch(SCENES[currentScene_].fx[slot])) {
            scenes_[currentScene_].lifTopologyIndex = topologyIndexFromNorm(normValue);
            compositor_.setLIFTopology(topologyFromIndex(scenes_[currentScene_].lifTopologyIndex));
            controlWin_.setKnobTopoName(knobIdx, topologyIndexToName(scenes_[currentScene_].lifTopologyIndex));
        }
    }
    knobLastPhys_[knobIdx] = normValue;
    applyKnob(knobIdx, normValue, eff);
    applyPressureMappings(currentScene_);
    controlWin_.setKnobValue(knobIdx, static_cast<int>(normValue * 127.0f));
}

// ── Image selected from media picker ──────────────────────────────────────────

void App::onImageSelected(int slotIdx, const std::string& path) {
    if (slotIdx < 0 || slotIdx >= NUM_SRC_LAYERS) return;
    if (currentScene_ < 0)
        onSceneSelect(0);

    // Fade to Black is a dedicated empty scene; ignore media assignments there.
    if (currentScene_ == 0) {
        scenes_[currentScene_].imgPaths[slotIdx].clear();
        std::cout << "[MediaPicker] Ignored assignment in Fade to Black scene\n";
        saveState();
        return;
    }

    const std::string resolvedPath = resolveMediaPathForLoad(path);
    const int layerIdx = slotIdx * 2;
    const bool samePathAsLoaded = (layers_.state(layerIdx).mediaPath == resolvedPath);

    if (currentScene_ >= 0) {
        scenes_[currentScene_].imgPaths[slotIdx] = resolvedPath;
        // A newly assigned media file should appear centered and visible by default.
        const int rotateMi = static_cast<int>(KnobMode::ImgRotate);
        const int zoomMi = static_cast<int>(KnobMode::ImgZoom);
        const int panMi = static_cast<int>(KnobMode::ImgPan);
        const int layerMi = static_cast<int>(KnobMode::LayerLevel);
        const int evenKnob = slotIdx * 2;
        scenes_[currentScene_].knobs[rotateMi][evenKnob] = 0.0f;
        scenes_[currentScene_].knobs[zoomMi][evenKnob] = 0.5f;
        scenes_[currentScene_].knobs[panMi][evenKnob] = 0.5f;
        scenes_[currentScene_].knobs[panMi][evenKnob + 1] = 0.5f;
        scenes_[currentScene_].knobs[layerMi][evenKnob] = 1.0f;
        scenes_[currentScene_].zoomVersion[slotIdx] = globalZoomVersion_[slotIdx];
        scenes_[currentScene_].opacityVersion[slotIdx] = globalOpacityVersion_[slotIdx];

        compositor_.setCrossfadeSpeed(slotIdx,
                                      0.1f + effectiveImageCrossfadeNorm(currentScene_, slotIdx) * 7.9f);
    }
    // Reloading the exact same file should reset playback in that slot.
    // Only use visual crossfade when the incoming path differs.
    if (!samePathAsLoaded)
        compositor_.beginCrossfade(slotIdx);  // capture current frame before new image uploads

    if (!layers_.loadMedia(layerIdx, resolvedPath)) {
        std::cerr << "[MediaPicker] Failed to load media for scene=" << currentScene_
                  << " slot=" << slotIdx << " path=" << resolvedPath << "\n";
        return;
    }

    // Apply neutral transform/opacity immediately for this slot.
    applyKnob(slotIdx * 2, 0.0f, KnobMode::ImgRotate);
    applyKnob(slotIdx * 2, 0.5f, KnobMode::ImgZoom);
    applyKnob(slotIdx * 2, 0.5f, KnobMode::ImgPan);
    applyKnob(slotIdx * 2 + 1, 0.5f, KnobMode::ImgPan);
    applyKnob(slotIdx * 2, 1.0f, KnobMode::LayerLevel);

    saveState();  // persist immediately — don't rely on clean exit
}

// ── State persistence ─────────────────────────────────────────────────────────

std::string App::statePath() {
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/.lif_lova_state" : "/tmp/lif_lova_state";
}

static std::string legacyStatePath() {
    const char* home = getenv("HOME");
    return home ? std::string(home) + "/.vjay_ace_state" : "/tmp/vjay_ace_state";
}

void App::saveState() const {
    // Write to a temp file first, then atomically rename so a crash mid-write
    // can never leave the state file truncated/corrupt.
    const std::string tmp = statePath() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { std::cerr << "[App] Could not open temp state file for writing\n"; return; }
        // Write magic + version for future-proofing
        const uint32_t magic = 0x56414345; // 'VACE'
        const uint32_t ver   = 18;
        f.write(reinterpret_cast<const char*>(&magic), 4);
        f.write(reinterpret_cast<const char*>(&ver),   4);
        // Write all scene states (knobs + image paths)
        for (int si = 0; si < NUM_SCENES; ++si) {
            const auto& s = scenes_[si];
            for (const auto& row : s.knobs)
                for (float v : row)
                    f.write(reinterpret_cast<const char*>(&v), sizeof(float));
            for (float v : s.imageCrossfadeSpeedNorm)
                f.write(reinterpret_cast<const char*>(&v), sizeof(float));
            for (float v : s.sceneCrossfadeSpeedNorm)
                f.write(reinterpret_cast<const char*>(&v), sizeof(float));
            f.write(reinterpret_cast<const char*>(&s.lifTopologyIndex), sizeof(int));
            f.write(reinterpret_cast<const char*>(&s.lifNeuronCount), sizeof(int));
            // Write 3 image paths as length-prefixed strings.
            for (int i = 0; i < NUM_SRC_LAYERS; ++i) {
                const auto& p = s.imgPaths[i];
                uint32_t len = static_cast<uint32_t>(p.size());
                f.write(reinterpret_cast<const char*>(&len), sizeof(len));
                if (len) f.write(p.data(), len);
            }
            // Write per-scene modal scale
            int modalScale = static_cast<int>(s.modalScale);
            f.write(reinterpret_cast<const char*>(&modalScale), sizeof(int));
            const uint8_t modeUserSet = lifMidiModeUserSet_[si] ? 1u : 0u;
            f.write(reinterpret_cast<const char*>(&modeUserSet), sizeof(uint8_t));
            const auto& ps = pressureSceneState_[si];
            for (uint8_t e : ps.enabled)
                f.write(reinterpret_cast<const char*>(&e), sizeof(uint8_t));
            for (float a : ps.amount)
                f.write(reinterpret_cast<const char*>(&a), sizeof(float));
        }
        for (const auto& mode : g_customModeIntervals)
            for (int v : mode)
                f.write(reinterpret_cast<const char*>(&v), sizeof(int));
        f.write(reinterpret_cast<const char*>(&lifMidiTonalRootSemitone_), sizeof(int));
        {
            const uint8_t rhythmEnabled = rhythmDriver_.enabled() ? 1u : 0u;
            const int rhythmMixMode = static_cast<int>(rhythmDriver_.mixMode());
            const int rhythmPattern = static_cast<int>(rhythmDriver_.pattern());
            const float rhythmBpm = rhythmDriver_.bpm();
            const float rhythmIntensity = rhythmDriver_.intensity();
            f.write(reinterpret_cast<const char*>(&rhythmEnabled), sizeof(uint8_t));
            f.write(reinterpret_cast<const char*>(&rhythmMixMode), sizeof(int));
            f.write(reinterpret_cast<const char*>(&rhythmPattern), sizeof(int));
            f.write(reinterpret_cast<const char*>(&rhythmBpm), sizeof(float));
            f.write(reinterpret_cast<const char*>(&rhythmIntensity), sizeof(float));

            const auto& lanes = rhythmDriver_.lanes();
            for (int i = 0; i < RhythmTransientDriver::MAX_LANES; ++i) {
                const auto& lane = lanes[static_cast<size_t>(i)];
                const uint8_t laneEnabled = lane.enabled ? 1u : 0u;
                f.write(reinterpret_cast<const char*>(&laneEnabled), sizeof(uint8_t));
                f.write(reinterpret_cast<const char*>(&lane.pulses), sizeof(int));
                f.write(reinterpret_cast<const char*>(&lane.gain), sizeof(float));
            }
        }
        f.write(reinterpret_cast<const char*>(&currentScene_), sizeof(int));
        if (!f) { std::cerr << "[App] State write error — temp file may be incomplete\n"; return; }
    } // ofstream closes + flushes here
    if (std::rename(tmp.c_str(), statePath().c_str()) != 0) {
        std::cerr << "[App] Could not rename temp state file to " << statePath() << "\n";
        return;
    }
    std::cout << "[App] State saved (scene=" << currentScene_ << ")" << std::endl;
}

void App::loadState() {
    const std::string newPath = statePath();
    const std::string oldPath = legacyStatePath();

    std::string pathToLoad = newPath;
    {
        std::ifstream testNew(newPath, std::ios::binary);
        if (!testNew) {
            std::ifstream testOld(oldPath, std::ios::binary);
            if (!testOld) return;
            // Try to migrate old state filename to the new app name.
            if (std::rename(oldPath.c_str(), newPath.c_str()) == 0) {
                pathToLoad = newPath;
                std::cout << "[App] Migrated state file to " << newPath << std::endl;
            } else {
                pathToLoad = oldPath;
            }
        }
    }

    std::ifstream f(pathToLoad, std::ios::binary);
    if (!f) return;
    uint32_t magic = 0, ver = 0;
    if (!f.read(reinterpret_cast<char*>(&magic), 4)) return;
    if (!f.read(reinterpret_cast<char*>(&ver),   4)) return;
    if (magic != 0x56414345) {
        std::cerr << "[App] Ignoring incompatible state file\n";
        return;
    }
    // v3: 3 modes, v4: 4 modes (added ImgRotate), v5: 5 modes (added ImgZoom)
    // v6: 16 scenes (added 2 LIF scenes; O/G keys replace C2/C#2 mode-latch)
    // v7: 6 modes (added ImgPan)
    // v8: per-scene image-load and scene-change crossfade speed settings
    // v9: per-scene LIF topology and neuron count settings
    // v10: 32 scenes (added second 16-note scene bank, starts at E3)
    // v11: per-scene pressure mapping settings (21 targets)
    // v12: pressure mapping includes opacity targets (24 targets)
    // v13: persists LIF MIDI modal scale selection
    // v14: per-scene modal scale in scene payload
    // v15: custom modal scales (extended ModalScale enum)
    // v16: persists editable custom mode intervals
    // v17: persists per-scene mode auto/manual flag + tonal root
    // v18: persists rhythm driver state and lane editor settings
    const bool isV3 = (ver == 3);
    const bool isV4 = (ver == 4);
    const bool isV5 = (ver == 5);
    const bool isV6 = (ver == 6);
    const bool isV7 = (ver == 7);
    const bool isV8 = (ver == 8);
    const bool isV9 = (ver == 9);
    const bool isV10 = (ver == 10);
    const bool isV11 = (ver == 11);
    const bool isV12 = (ver == 12);
    const bool isV13 = (ver == 13);
    const bool isV16Plus = (ver >= 16);
    const bool isV17Plus = (ver >= 17);
    const bool isV18Plus = (ver >= 18);
    const bool isV14Plus = (ver >= 14);
    if (!isV3 && !isV4 && !isV5 && !isV6 && !isV7 && !isV8 && !isV9 && !isV10 && !isV11 && !isV12 && !isV13 && !isV14Plus) {
        std::cerr << "[App] Ignoring incompatible state file\n";
        return;
    }
    // Older saves have 14 or 16 scenes. v10+ saves all NUM_SCENES.
    const int savedSceneCount = (isV10 || isV11 || isV12 || isV13 || isV14Plus) ? NUM_SCENES : (isV6 || isV7 || isV8 || isV9) ? 16 : 14;
    lifMidiModeUserSet_.fill(0);
    for (int si = 0; si < savedSceneCount && si < NUM_SCENES; ++si) {
        auto& s = scenes_[si];
        // v3=3 modes, v4=4, v5/v6=5, v7=6
        int savedModes = isV3 ? 3 : isV4 ? 4 : (isV5 || isV6) ? 5 : SceneState::NMODES;
        for (int mi = 0; mi < savedModes; ++mi)
            for (float& v : s.knobs[mi])
                if (!f.read(reinterpret_cast<char*>(&v), sizeof(float))) return;
        if (isV8 || isV9 || isV10 || isV11 || isV12 || isV13 || ver >= 14) {
            for (float& v : s.imageCrossfadeSpeedNorm)
                if (!f.read(reinterpret_cast<char*>(&v), sizeof(float))) return;
            for (float& v : s.sceneCrossfadeSpeedNorm)
                if (!f.read(reinterpret_cast<char*>(&v), sizeof(float))) return;
        }
        if (isV9 || isV10 || isV11 || isV12 || isV13 || ver >= 14) {
            if (!f.read(reinterpret_cast<char*>(&s.lifTopologyIndex), sizeof(int))) return;
            if (!f.read(reinterpret_cast<char*>(&s.lifNeuronCount), sizeof(int))) return;
        }
        // Read 3 image paths
        for (int i = 0; i < NUM_SRC_LAYERS; ++i) {
            auto& p = s.imgPaths[i];
            uint32_t len = 0;
            if (!f.read(reinterpret_cast<char*>(&len), sizeof(len))) return;
            if (len > 4096) return; // sanity guard
            p.resize(len);
            if (len) { if (!f.read(p.data(), len)) return; }
            if (len > 0) std::cout << "[App] loadState: scene=" << si << " slot=" << i << " path=" << p << std::endl;
        }
        // Per-scene modal scale (v14+)
        if (ver >= 14) {
            int modalScale = static_cast<int>(ModalScale::Dorian);
            if (!f.read(reinterpret_cast<char*>(&modalScale), sizeof(int))) return;
            modalScale = std::clamp(modalScale,
                                    static_cast<int>(ModalScale::Ionian),
                                    static_cast<int>(ModalScale::AeolianPlus7));
            s.modalScale = static_cast<ModalScale>(modalScale);
            if (isV17Plus) {
                uint8_t modeUserSet = 0;
                if (!f.read(reinterpret_cast<char*>(&modeUserSet), sizeof(uint8_t))) return;
                lifMidiModeUserSet_[si] = modeUserSet ? 1 : 0;
            } else {
                // Pre-v17 did not store explicit ownership of modal scale edits.
                // Preserve custom edited modes, otherwise default to tonal-root auto mapping.
                lifMidiModeUserSet_[si] = isCustomModalScale(s.modalScale) ? 1 : 0;
            }
        }
        if (isV11 || isV12 || isV13) {
            const int savedPressureTargets = isV11 ? 21 : NUM_PRESSURE_TARGETS;
            for (int i = 0; i < savedPressureTargets; ++i) {
                uint8_t e = 0;
                if (!f.read(reinterpret_cast<char*>(&e), sizeof(uint8_t))) return;
                if (i < NUM_PRESSURE_TARGETS) pressureSceneState_[si].enabled[i] = e;
            }
            for (int i = 0; i < savedPressureTargets; ++i) {
                float a = 0.0f;
                if (!f.read(reinterpret_cast<char*>(&a), sizeof(float))) return;
                if (i < NUM_PRESSURE_TARGETS) pressureSceneState_[si].amount[i] = a;
            }
        }

        // Ensure visibility defaults are valid immediately after load.
        ensureSceneOpacityDefaults(si);
    }
    // For v13 and earlier, load the global modal scale into all scenes
    if (isV13) {
        int modalScale = static_cast<int>(ModalScale::Dorian);
        if (f.read(reinterpret_cast<char*>(&modalScale), sizeof(int))) {
            modalScale = std::clamp(modalScale,
                                    static_cast<int>(ModalScale::Ionian),
                                    static_cast<int>(ModalScale::AeolianPlus7));
            for (int si = 0; si < savedSceneCount && si < NUM_SCENES; ++si) {
                scenes_[si].modalScale = static_cast<ModalScale>(modalScale);
                lifMidiModeUserSet_[si] = 1;
            }
        }
    }

    g_customModeIntervals = defaultCustomModeIntervals();
    if (isV16Plus) {
        for (auto& mode : g_customModeIntervals) {
            for (int& v : mode) {
                if (!f.read(reinterpret_cast<char*>(&v), sizeof(int))) return;
                v = std::clamp(v, 0, 11);
            }
            // Keep monotonic ascending degrees after load.
            for (int i = 1; i < 7; ++i)
                mode[static_cast<size_t>(i)] = std::max(mode[static_cast<size_t>(i)], mode[static_cast<size_t>(i - 1)]);
        }
    }

    if (isV17Plus) {
        if (!f.read(reinterpret_cast<char*>(&lifMidiTonalRootSemitone_), sizeof(int))) return;
        lifMidiTonalRootSemitone_ = ((lifMidiTonalRootSemitone_ % 12) + 12) % 12;
    }

    if (isV18Plus) {
        uint8_t rhythmEnabled = 0;
        int rhythmMixMode = 0;
        int rhythmPattern = 0;
        float rhythmBpm = rhythmDriver_.bpm();
        float rhythmIntensity = rhythmDriver_.intensity();
        if (!f.read(reinterpret_cast<char*>(&rhythmEnabled), sizeof(uint8_t))) return;
        if (!f.read(reinterpret_cast<char*>(&rhythmMixMode), sizeof(int))) return;
        if (!f.read(reinterpret_cast<char*>(&rhythmPattern), sizeof(int))) return;
        if (!f.read(reinterpret_cast<char*>(&rhythmBpm), sizeof(float))) return;
        if (!f.read(reinterpret_cast<char*>(&rhythmIntensity), sizeof(float))) return;

        rhythmDriver_.setEnabled(rhythmEnabled != 0);
        rhythmDriver_.setMixMode(static_cast<RhythmTransientDriver::MixMode>(
            std::clamp(rhythmMixMode,
                       static_cast<int>(RhythmTransientDriver::MixMode::AudioOnly),
                       static_cast<int>(RhythmTransientDriver::MixMode::Hybrid))));
        rhythmDriver_.setPattern(static_cast<RhythmTransientDriver::Pattern>(
            std::clamp(rhythmPattern,
                       static_cast<int>(RhythmTransientDriver::Pattern::Metronome4),
                       static_cast<int>(RhythmTransientDriver::Pattern::Triplet3Over4))));
        rhythmDriver_.setBpm(rhythmBpm);
        rhythmDriver_.setIntensity(rhythmIntensity);

        for (int i = 0; i < RhythmTransientDriver::MAX_LANES; ++i) {
            uint8_t laneEnabled = 0;
            int lanePulses = 16;
            float laneGain = 1.0f;
            if (!f.read(reinterpret_cast<char*>(&laneEnabled), sizeof(uint8_t))) return;
            if (!f.read(reinterpret_cast<char*>(&lanePulses), sizeof(int))) return;
            if (!f.read(reinterpret_cast<char*>(&laneGain), sizeof(float))) return;

            rhythmDriver_.setLaneEnabled(i, laneEnabled != 0);
            rhythmDriver_.nudgeLanePulses(i, lanePulses - rhythmDriver_.lanes()[static_cast<size_t>(i)].pulses);
            rhythmDriver_.nudgeLaneGain(i, laneGain - rhythmDriver_.lanes()[static_cast<size_t>(i)].gain);
        }
    }

    int savedScene = -1;
    if (f.read(reinterpret_cast<char*>(&savedScene), sizeof(int))) {
        if (savedScene >= 0 && savedScene < NUM_SCENES)
            currentScene_ = savedScene;
        else
            std::cerr << "[App] Ignoring out-of-range savedScene=" << savedScene << "\n";
    }

    // Per-scene modal scale is now loaded with each scene (see above)

    refreshLifMidiUi();

    // Apply the last-active scene to the engine
    if (currentScene_ >= 0) {
        const Scene& sc = SCENES[currentScene_];
        const auto fx = enforcedSceneFx(sc);
        for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
            fxPatches_[slot].id = fx[slot];
            compositor_.setFxPatch(slot, fx[slot]);
            layers_.setFxPatch(slot * 2 + 1, fx[slot]);
        }
        controlWin_.setSceneName(sc.name);
        mediaPickerWin_.setSceneName(sc.name);
        // Load persisted images
        for (int slot = 0; slot < NUM_SRC_LAYERS; ++slot) {
            std::string path = resolveMediaPathForLoad(scenes_[currentScene_].imgPaths[slot]);
            if (path != scenes_[currentScene_].imgPaths[slot])
                scenes_[currentScene_].imgPaths[slot] = path;
            if (!path.empty()) {
                layers_.loadMedia(slot * 2, path);
            }
        }
        mediaPickerWin_.setSlotPaths(scenes_[currentScene_].imgPaths);
        ensureSceneTransformDefaults(currentScene_);
        applySceneToEngine(currentScene_);
    }
    std::cout << "[App] State restored from " << pathToLoad << std::endl;
}

// ── Per-frame ─────────────────────────────────────────────────────────────────

void App::uploadLayers() {
    for (int li = 0; li < NUM_LAYERS; li += 2) {
        const uint8_t* px = layers_.pixelBuffer(li);
        if (px) compositor_.uploadLayerPixels(li, px, WORK_W, WORK_H);
    }
}

void App::syncCompositorState() {
    for (int li = 0; li < NUM_LAYERS; ++li)
        compositor_.setLayerOpacity(li, layers_.state(li).opacity);
    for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
        compositor_.setFxPatch(slot, fxPatches_[slot].id);
        compositor_.setFxParams(slot, fxPatches_[slot].p[0], fxPatches_[slot].p[1]);
    }
}

void App::processFrame() {
    midi_.poll();
    layers_.update(1.0f / 60.0f);
    uploadLayers();
    syncCompositorState();
    
    // Update pan/zoom animation (60 FPS = ~0.0167s per frame)
    updatePanZoomAnimation(1.0f / 60.0f);

    // Smooth rough pressure data (low-pass + slew limit + deadband), then apply mappings.
    if (currentScene_ >= 0) {
        float cur = scenePressureNorm_[currentScene_];
        const float target = scenePressureTargetNorm_[currentScene_];
        const float diff = target - cur;
        if (std::fabs(diff) <= PRESSURE_DEADBAND) {
            cur = target;
        } else {
            const float desired = cur + diff * PRESSURE_SMOOTH_ALPHA;
            float step = desired - cur;
            step = std::clamp(step, -PRESSURE_SLEW_MAX_PER_FRAME, PRESSURE_SLEW_MAX_PER_FRAME);
            cur = std::clamp(cur + step, 0.0f, 1.0f);
        }
        scenePressureNorm_[currentScene_] = cur;
        audioMidiWin_.setPressureNorm(cur);
        applyPressureMappings(currentScene_);
    }

    // ── Audio + Rhythm drive mix for FX and LIF simulation ───────────────
    std::array<float, 8> liveBands = {};
    float liveRms = 0.0f;
    if (!audioBypassed_ && audio_.isRunning()) {
        liveBands = audio_.bands();
        liveRms = audio_.rms();
    }

    rhythmDriver_.tick(1.0f / 60.0f);

    std::array<float, 8> mixedBands = {};
    float mixedRms = 0.0f;

    switch (rhythmDriver_.mixMode()) {
        case RhythmTransientDriver::MixMode::AudioOnly:
            mixedBands = liveBands;
            mixedRms = liveRms;
            break;
        case RhythmTransientDriver::MixMode::RhythmOnly:
            if (rhythmDriver_.enabled()) {
                mixedBands = rhythmDriver_.bands();
                mixedRms = rhythmDriver_.rms();
            }
            break;
        case RhythmTransientDriver::MixMode::Hybrid:
            for (int i = 0; i < 8; ++i)
                mixedBands[static_cast<size_t>(i)] = std::clamp(
                    liveBands[static_cast<size_t>(i)] + (rhythmDriver_.enabled() ? rhythmDriver_.bands()[static_cast<size_t>(i)] : 0.0f),
                    0.0f, 1.0f);
            mixedRms = std::clamp(liveRms + (rhythmDriver_.enabled() ? rhythmDriver_.rms() : 0.0f), 0.0f, 1.0f);
            break;
    }

    compositor_.setAudioBands(mixedBands.data(), static_cast<int>(mixedBands.size()), mixedRms);
    audioMidiWin_.setAudioBands(mixedBands.data(), static_cast<int>(mixedBands.size()), mixedRms);

    // Feed transient/rhythm bands to the audio window for visualization
    {
        const auto& rhythmBands = rhythmDriver_.bands();
        const float rhythmRms   = rhythmDriver_.rms();
        audioMidiWin_.setTransientBands(rhythmBands.data(),
                                        static_cast<int>(rhythmBands.size()),
                                        rhythmRms);
    }

    // Drive LIF simulation from all active scene LIF patch params.
    if (currentScene_ >= 0) {
        std::vector<MetalCompositor::LIFDriver> drivers;
        const int fxMi = static_cast<int>(KnobMode::FxParam);
        const SceneState& s = scenes_[currentScene_];
        const Scene& sc = SCENES[currentScene_];
        for (int slot = 0; slot < NUM_FX_LAYERS; ++slot) {
            if (!isLIFPatch(sc.fx[slot])) continue;
            const float p0Stored = s.knobs[fxMi][slot * 2];
            const float p1Stored = s.knobs[fxMi][slot * 2 + 1];
            const float influence = (p0Stored >= 0.0f) ? p0Stored : sc.params[slot][0];
            const float topology = (p1Stored >= 0.0f) ? p1Stored : sc.params[slot][1];
            drivers.push_back({slot, influence, topology});
        }
        compositor_.setLIFDrivers(drivers);
    } else {
        compositor_.setLIFDrivers({});
    }

    // GPU composite → CPU readback
    if (compositor_.composite(compositePixels_)) {
        // Experimental sonification: horizontal scan = time, vertical bins = pitch.
        std::array<float, 16> lifBins = {};
        const bool lifSceneActive = sceneUsesLIF(currentScene_);
        if (lifSceneActive && (lifToneEnabled_ || lifMidiEnabled_)) {
            lifToneScanPhase_ += (1.0f / 60.0f) * lifToneScanTempo_;
            if (lifToneScanPhase_ >= 1.0f)
                lifToneScanPhase_ -= std::floor(lifToneScanPhase_);
        }
        if (lifSceneActive) {
            lifBins = compositor_.sampleLIFColumn(lifToneScanPhase_);
        }
        if (lifToneEnabled_ && !audioBypassed_ && lifSceneActive) {
            lifToneSynth_.setColumnEnergies(lifBins);
        } else if (transientAuditionEnabled_ && rhythmDriver_.enabled()) {
            // Route transient energy to the tone synth for audible feedback.
            // Up-sample 8 rhythm bands → 16 tone bins (duplicate each band).
            std::array<float, 16> rhythmToneBins = {};
            const auto& rb = rhythmDriver_.bands();
            for (int i = 0; i < 8; ++i) {
                rhythmToneBins[static_cast<size_t>(i * 2)]     = rb[static_cast<size_t>(i)];
                rhythmToneBins[static_cast<size_t>(i * 2 + 1)] = rb[static_cast<size_t>(i)];
            }
            lifToneSynth_.setColumnEnergies(rhythmToneBins);
        } else {
            lifToneSynth_.setColumnEnergies({});
        }

        // ── LIF-to-MIDI output ──
        if (lifMidiEnabled_ && lifSceneActive) {
            lifMidiNoSceneWarned_ = false;
            const int midiChannel = (lifMidiStyle_ == LifMidiStyle::Percussion) ? 10 : 1; // 1-based
            const int baseNote = 60 + lifMidiKeySemitone_;
            const bool feedforward = (currentScene_ >= 0 && scenes_[currentScene_].lifTopologyIndex == 2);

            float maxEnergy = 0.0f;
            for (float v : lifBins) maxEnergy = std::max(maxEnergy, v);

            // Adaptive thresholds prevent getting stuck on one bin when absolute
            // levels are low or compressed in a given scene/topology.
            const float noteOnThreshold = std::max(0.18f, maxEnergy * 0.55f);
            const float noteOffThreshold = std::max(0.12f, maxEnergy * 0.40f);

            for (int bin = 0; bin < 16; ++bin) {
                const float energy = lifBins[bin];
                const float prevEnergy = (bin > 0) ? lifBins[bin - 1] : energy;
                const float drive = std::clamp(0.75f * energy + 0.25f * prevEnergy, 0.0f, 1.0f);
                const HarmonicFunction function = classifyLifFunction(bin, energy, prevEnergy, maxEnergy, feedforward);

                auto sendNotesOff = [&]() {
                    const int offChannel = (lifMidiActiveChannel_[bin] > 0) ? lifMidiActiveChannel_[bin] : midiChannel;
                    for (int note : lifMidiActiveNotes_[bin])
                        midi_.sendNoteOff(offChannel, note);
                    lifMidiActiveNotes_[bin].clear();
                    lifMidiActiveChannel_[bin] = 0;
                };

                auto sendNotesOn = [&](int velocity) {
                    const auto notes = lifMidiNotesForFunction(function, bin, baseNote, drive);
                    for (int note : notes)
                        midi_.sendNoteOn(midiChannel, note, velocity);
                    lifMidiActiveNotes_[bin] = notes;
                    lifMidiActiveChannel_[bin] = midiChannel;
                };

                if (feedforward) {
                    if (lifMidiGateFrames_[bin] > 0) {
                        --lifMidiGateFrames_[bin];
                        if (lifMidiGateFrames_[bin] == 0 && lifMidiNoteOn_[bin]) {
                            sendNotesOff();
                            lifMidiNoteOn_[bin] = false;
                        }
                    }

                    if (lifMidiCooldownFrames_[bin] > 0)
                        --lifMidiCooldownFrames_[bin];

                    const float prevDrive = std::clamp(prevEnergy, 0.0f, 1.0f);
                    const bool shouldFire = (lifMidiCooldownFrames_[bin] == 0) && (drive > 0.12f);

                    if (shouldFire) {
                        const float velCurve = std::pow(drive, 0.7f);
                        const int velocity = std::clamp(static_cast<int>(30.0f + velCurve * 97.0f + prevDrive * 10.0f), 30, 127);
                        sendNotesOn(velocity);
                        lifMidiNoteOn_[bin] = true;
                        lifMidiGateFrames_[bin] = 2;
                        const int intervalFrames = std::clamp(static_cast<int>(14.0f - prevDrive * 10.0f - energy * 4.0f), 2, 14);
                        lifMidiCooldownFrames_[bin] = intervalFrames;
                    }
                } else {
                    bool active = lifMidiNoteOn_[bin]
                        ? (energy > noteOffThreshold)
                        : (energy > noteOnThreshold);

                    if (active && !lifMidiNoteOn_[bin]) {
                        const float rel = energy / std::max(maxEnergy, 0.001f);
                        const float curved = std::pow(std::clamp(rel, 0.0f, 1.0f), 0.65f);
                        const int velocity = std::clamp(static_cast<int>(20.0f + curved * 107.0f), 20, 127);
                        sendNotesOn(velocity);
                        lifMidiNoteOn_[bin] = true;
                    } else if (!active && lifMidiNoteOn_[bin]) {
                        sendNotesOff();
                        lifMidiNoteOn_[bin] = false;
                    }
                }
            }
        } else if (lifMidiEnabled_ && !lifSceneActive && !lifMidiNoSceneWarned_) {
            std::cout << "[LIF MIDI] enabled but current scene has no LIF patch; select a LIF scene to get MIDI notes." << std::endl;
            lifMidiNoSceneWarned_ = true;
        } else {
            resetLifMidiState();
        }

        perfWin_.present(compositePixels_);
        compositeTex_.update(compositePixels_.data());
    } else {
        lifToneSynth_.setColumnEnergies({});
        perfWin_.clearBlack();
        resetLifMidiState();
    }
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void App::run() {
    while (controlWin_.isOpen() && perfWin_.isOpen()) {
        if (!controlWin_.handleEvents()) break;
        if (!perfWin_.handleEvents())    break;
        if (audioMidiWin_.isOpen()) audioMidiWin_.handleEvents();
        if (mediaPickerWin_.isOpen()) mediaPickerWin_.handleEvents();
        if (pressureWin_.isOpen()) pressureWin_.handleEvents();
        controlWin_.update();
        if (audioMidiWin_.isOpen()) audioMidiWin_.update();
        processFrame();
        controlWin_.render(compositeTex_);
        if (audioMidiWin_.isOpen()) audioMidiWin_.render();
        if (mediaPickerWin_.isOpen()) mediaPickerWin_.render();
        if (pressureWin_.isOpen()) pressureWin_.render();
    }
    std::cerr << "[App] Shutdown requested; saving state...\n";
    saveState();
    std::cerr << "[App] Shutdown save completed\n";
    lifToneSynth_.stopStream();
    controlWin_.close();
    audioMidiWin_.close();
    perfWin_.close();
    pressureWin_.close();
}
