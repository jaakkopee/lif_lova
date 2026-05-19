#pragma once
#include "Constants.h"
#include <SFML/Graphics.hpp>
#include <TGUI/TGUI.hpp>
#include <TGUI/Backend/SFML-Graphics.hpp>
#include <TGUI/Backend/Renderer/SFML-Graphics/CanvasSFML.hpp>
#include <TGUI/Widgets/ScrollablePanel.hpp>
#include <array>
#include <functional>
#include <string>
#include <vector>

// ── AudioMidiControlWindow ──────────────────────────────────────────────────────────────
// Dedicated window for audio analysis, LIF MIDI controls, and MIDI port configuration.

class AudioMidiControlWindow {
public:
    AudioMidiControlWindow();

    void open(int displayX, int displayY, int width, int height);
    bool isOpen() const;
    void close();

    bool handleEvents();
    void update();
    void render();

    // ── State setters ───────────────────────────────────────────────────────
    // Update audio level meter (8 bands 0-1, rms 0-1). Called each frame.
    void setAudioBands(const float* bands, int count, float rms);
    void setNetworkBins(const std::array<float, 16>& audioBins,
                        const std::array<float, 16>& transientBins);

    // Update channel pressure meter (0-1).
    void setPressureNorm(float norm);

    // Update LIF MIDI status label and button.
    void setLifMidiStatus(bool enabled, int channel, int baseNote);

    // Update the LIF MIDI voicing style label.
    void setLifMidiStyle(const std::string& styleName);

    // Update the LIF modal scale label.
    void setLifMidiMode(const std::string& modeName);
    void setLifMidiModeToggleState(bool hasScene, bool autoMode);
    void setLifMidiModeEditor(const std::string& statusText, bool editable);

    // Update LIF MIDI key and note range labels.
    void setLifMidiKey(const std::string& keyName);
    void setLifMidiTonalRoot(const std::string& keyName);
    void setLifMidiRange(int minNote, int maxNote);

    // Update the LIF tone volume label.
    void setLifToneVolume(float volume);

    void setRhythmUiState(bool enabled,
                          const std::string& strategyName,
                          float bpm,
                          float gain,
                          bool audibleTransients,
                          const std::string& status);
    void setRhythmPatternSpecs(const std::string& divisive,
                               const std::string& additive,
                               const std::string& weights,
                               const std::string& lengths);

    // Update MIDI port lists and active selection indexes.
    void setMidiPortLists(const std::vector<std::string>& inPorts,
                          int inIdx,
                          const std::vector<std::string>& outPorts,
                          int outIdx);

    // Audio/MIDI control callbacks
    std::function<void()> onLIFMidiToggle;
    std::function<void()> onLIFMidiStyleCycle;
    std::function<void()> onLIFMidiModeCycle;
    std::function<void()> onLIFMidiModeToggle;
    std::function<void(int delta)> onLIFMidiKeyNudge;
    std::function<void(int delta)> onLIFMidiTonalRootNudge;
    std::function<void(int delta)> onLIFMidiRangeMinNudge;
    std::function<void(int delta)> onLIFMidiRangeMaxNudge;
    std::function<void(int delta)> onLIFMidiModeEditDegreeNudge;
    std::function<void(int delta)> onLIFMidiModeEditSemitoneNudge;
    std::function<void()> onLIFMidiModeEditReset;
    std::function<void()> onLIFToneToggle;
    std::function<void(float delta)> onLIFToneTempoNudge;
    std::function<void(float deltaHz)> onLIFToneMinFreqNudge;
    std::function<void(float deltaHz)> onLIFToneMaxFreqNudge;
    std::function<void(float delta)> onLIFToneVolumeNudge;
    std::function<void(bool bypassed)> onBKey;
    std::function<void()> onRhythmToggle;
    std::function<void()> onRhythmStrategyCycle;
    std::function<void(float delta)> onRhythmBpmNudge;
    std::function<void(float delta)> onRhythmGainNudge;
    std::function<void()> onRhythmAudibleToggle;
    std::function<void(const std::string& divisive,
                       const std::string& additive,
                       const std::string& weights,
                       const std::string& lengths)> onRhythmPatternApply;

    // Runtime MIDI port selection callbacks (selected port name).
    std::function<void(const std::string&)> onMidiInPortChanged;
    std::function<void(const std::string&)> onMidiOutPortChanged;

private:
    sf::RenderWindow window_;
    tgui::Gui        gui_;
    tgui::ScrollablePanel::Ptr contentPanel_;

    // Audio bands for meter drawing (8 bands + RMS)
    std::array<float, 8> audioBands_ = {};
    float audioRms_ = 0.0f;
    float pressureNorm_ = 0.0f;
    std::array<float, 16> networkAudioBins_ = {};
    std::array<float, 16> networkTransientBins_ = {};

    std::vector<std::array<float, 16>> networkAudioHistory_;
    std::vector<std::array<float, 16>> networkTransientHistory_;

    tgui::CanvasSFML::Ptr audioMeterCanvas_;
    tgui::CanvasSFML::Ptr pressureMeterCanvas_;
    tgui::CanvasSFML::Ptr networkBinsCanvas_;
    tgui::Label::Ptr pressureLabel_;

    tgui::Button::Ptr lifMidiToggleBtn_;
    tgui::Label::Ptr lifMidiStatusLabel_;
    tgui::Button::Ptr lifMidiStyleBtn_;
    tgui::Button::Ptr lifMidiModeBtn_;
    tgui::Button::Ptr lifMidiModeToggleBtn_;
    tgui::Label::Ptr lifMidiModeEditLabel_;
    tgui::Button::Ptr lifMidiModeDegDownBtn_;
    tgui::Button::Ptr lifMidiModeDegUpBtn_;
    tgui::Button::Ptr lifMidiModeSemiDownBtn_;
    tgui::Button::Ptr lifMidiModeSemiUpBtn_;
    tgui::Button::Ptr lifMidiModeResetBtn_;
    tgui::Label::Ptr lifMidiKeyLabel_;
    tgui::Button::Ptr lifMidiKeyDownBtn_;
    tgui::Button::Ptr lifMidiKeyUpBtn_;
    tgui::Label::Ptr lifMidiTonalRootLabel_;
    tgui::Button::Ptr lifMidiTonalRootDownBtn_;
    tgui::Button::Ptr lifMidiTonalRootUpBtn_;
    tgui::Label::Ptr lifMidiRangeLabel_;
    tgui::Button::Ptr lifMidiRangeMinDownBtn_;
    tgui::Button::Ptr lifMidiRangeMinUpBtn_;
    tgui::Button::Ptr lifMidiRangeMaxDownBtn_;
    tgui::Button::Ptr lifMidiRangeMaxUpBtn_;

    tgui::Label::Ptr lifToneVolLabel_;
    tgui::Button::Ptr lifToneVolDownBtn_;
    tgui::Button::Ptr lifToneVolUpBtn_;
    tgui::Button::Ptr lifToneToggleBtn_;

    tgui::Button::Ptr midiSettingsBtn_;
    tgui::Panel::Ptr midiSettingsPanel_;
    tgui::ComboBox::Ptr midiInPortBox_;
    tgui::ComboBox::Ptr midiOutPortBox_;
    bool midiSettingsExpanded_ = false;

    tgui::Button::Ptr audioBypPassBtn_;

    tgui::Button::Ptr rhythmToggleBtn_;
    tgui::Button::Ptr rhythmStrategyBtn_;
    tgui::Label::Ptr rhythmBpmLabel_;
    tgui::Button::Ptr rhythmBpmDownBtn_;
    tgui::Button::Ptr rhythmBpmUpBtn_;
    tgui::Label::Ptr rhythmGainLabel_;
    tgui::Button::Ptr rhythmGainDownBtn_;
    tgui::Button::Ptr rhythmGainUpBtn_;
    tgui::Button::Ptr rhythmAudibleBtn_;
    tgui::EditBox::Ptr rhythmDivisiveEdit_;
    tgui::EditBox::Ptr rhythmAdditiveEdit_;
    tgui::EditBox::Ptr rhythmWeightsEdit_;
    tgui::EditBox::Ptr rhythmLengthsEdit_;
    tgui::Button::Ptr rhythmApplyBtn_;
    tgui::Label::Ptr rhythmStatusLabel_;

    void buildGui(int width, int height);
    void drawPressureMeter();
    void drawAudioMeter();
    void drawNetworkBins();
};
