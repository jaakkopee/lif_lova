#pragma once
#include "Constants.h"
#include <SFML/Graphics.hpp>
#include <TGUI/TGUI.hpp>
#include <TGUI/Backend/SFML-Graphics.hpp>
#include <TGUI/Backend/Renderer/SFML-Graphics/CanvasSFML.hpp>
#include <array>
#include <deque>
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

    // Update transient/rhythm energy bands (8 bands 0-1, rms 0-1). Called each frame.
    void setTransientBands(const float* bands, int count, float rms);

    // Update channel pressure meter (0-1).
    void setPressureNorm(float norm);

    // Update LIF MIDI status label and button.
    void setLifMidiStatus(bool enabled, int channel, int baseNote);

    // Update the LIF MIDI gematria context label.
    void setLifMidiContext(const std::string& contextName);

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
    void setRhythmStatus(bool enabled,
                         const std::string& mixModeName,
                         const std::string& patternName,
                         float bpm,
                         float intensity,
                         bool quantizedPending);
    void setRhythmLaneScaffold(const std::array<std::string, 3>& names,
                               const std::array<uint8_t, 3>& enabled,
                               const std::array<int, 3>& pulses,
                               const std::array<float, 3>& gains);

    // Update MIDI port lists and active selection indexes.
    void setMidiPortLists(const std::vector<std::string>& inPorts,
                          int inIdx,
                          const std::vector<std::string>& outPorts,
                          int outIdx);

    // Audio/MIDI control callbacks
    std::function<void()> onLIFMidiToggle;
    std::function<void()> onLIFMidiContextCycle;
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
    std::function<void()> onRhythmToggle;
    std::function<void()> onRhythmMixCycle;
    std::function<void()> onRhythmPatternCycle;
    std::function<void(float delta)> onRhythmBpmNudge;
    std::function<void(float delta)> onRhythmIntensityNudge;
    std::function<void(int laneIdx)> onRhythmLaneToggle;
    std::function<void(int laneIdx, int delta)> onRhythmLanePulseNudge;
    std::function<void(int laneIdx, float delta)> onRhythmLaneGainNudge;
    std::function<void(bool bypassed)> onBKey;

    // Transient audition toggle callback (true = audition on)
    std::function<void(bool enabled)> onTransientAuditionToggle;

    // Runtime MIDI port selection callbacks (selected port name).
    std::function<void(const std::string&)> onMidiInPortChanged;
    std::function<void(const std::string&)> onMidiOutPortChanged;

private:
    sf::RenderWindow window_;
    tgui::Gui        gui_;

    // Audio bands for meter drawing (8 bands + RMS)
    std::array<float, 8> audioBands_ = {};
    float audioRms_ = 0.0f;
    float pressureNorm_ = 0.0f;

    // Transient/rhythm energy bands (8 bands + RMS)
    std::array<float, 8> transientBands_ = {};
    float transientRms_ = 0.0f;

    // Rolling history canvas: 16 rows (8 audio + 8 transient), N columns wide
    static constexpr int ROLLING_ROWS = 16;
    static constexpr int ROLLING_MAX_COLS = 860;
    std::deque<std::array<float, ROLLING_ROWS>> rollingBuf_;

    tgui::CanvasSFML::Ptr audioMeterCanvas_;
    tgui::CanvasSFML::Ptr pressureMeterCanvas_;
    tgui::Label::Ptr pressureLabel_;

    // LED circle canvases for audio and transient bands
    tgui::CanvasSFML::Ptr audioLedCanvas_;
    tgui::CanvasSFML::Ptr transientLedCanvas_;

    // Rolling 16-row history canvas
    tgui::CanvasSFML::Ptr rollingCanvas_;

    // Transient audition toggle
    tgui::Button::Ptr transientAuditionBtn_;
    bool transientAuditionEnabled_ = false;

    tgui::Button::Ptr rhythmToggleBtn_;
    tgui::Button::Ptr rhythmMixBtn_;
    tgui::Button::Ptr rhythmPatternBtn_;
    tgui::Button::Ptr rhythmBpmDownBtn_;
    tgui::Button::Ptr rhythmBpmUpBtn_;
    tgui::Button::Ptr rhythmIntDownBtn_;
    tgui::Button::Ptr rhythmIntUpBtn_;
    std::array<tgui::Button::Ptr, 3> rhythmLaneBtns_;
    std::array<tgui::Button::Ptr, 3> rhythmLanePulseDownBtns_;
    std::array<tgui::Button::Ptr, 3> rhythmLanePulseUpBtns_;
    std::array<tgui::Button::Ptr, 3> rhythmLaneGainDownBtns_;
    std::array<tgui::Button::Ptr, 3> rhythmLaneGainUpBtns_;

    tgui::Button::Ptr lifMidiToggleBtn_;
    tgui::Label::Ptr lifMidiStatusLabel_;
    tgui::Button::Ptr lifMidiContextBtn_;
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

    void buildGui(int width);
    void drawPressureMeter();
    void drawAudioMeter();
    void drawAudioLeds();
    void drawTransientLeds();
    void drawRollingCanvas();
};
