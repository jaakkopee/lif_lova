#include <SFML/Window/Keyboard.hpp>
#include "AudioMidiControlWindow.h"
#include <cmath>
#include <algorithm>

AudioMidiControlWindow::AudioMidiControlWindow() = default;

void AudioMidiControlWindow::open(int displayX, int displayY, int width, int height) {
    window_.create(sf::VideoMode({static_cast<unsigned>(width),
                                  static_cast<unsigned>(height)}),
                   "lif_lova - Audio & MIDI");
    window_.setPosition({displayX, displayY});
    window_.setFramerateLimit(60);
    gui_.setWindow(window_);
    buildGui(width);
}

bool AudioMidiControlWindow::isOpen() const { return window_.isOpen(); }
void AudioMidiControlWindow::close()        { window_.close(); }

const tgui::Color BG_DARK   {16,  16,  20 };
const tgui::Color TEXT_DIM  {150, 165, 195};
const tgui::Color TEXT_VAL  {215, 225, 255};

void AudioMidiControlWindow::buildGui(int width) {
    const int PAD = 18;
    const int colW = width - PAD * 2;

    int y = PAD;
    const int SEC_GAP  = 28;  // gap between sections
    const int ELEM_GAP = 14;  // gap between elements within a section

    // ── Section helper lambdas ───────────────────────────────────────────────
    auto addSectionLabel = [&](const std::string& text, tgui::Color col) {
        auto lbl = tgui::Label::create(text);
        lbl->setPosition(PAD, y);
        lbl->setTextSize(14);
        lbl->getRenderer()->setTextColor(col);
        gui_.add(lbl);
        y += 22;
    };

    auto addSubLabel = [&](const std::string& text) {
        auto lbl = tgui::Label::create(text);
        lbl->setPosition(PAD, y);
        lbl->setTextSize(12);
        lbl->getRenderer()->setTextColor(TEXT_DIM);
        gui_.add(lbl);
        y += 18;
    };

    // ══════════════════════════════════════════════════════════════════════════
    // 1. PRESSURE METER
    // ══════════════════════════════════════════════════════════════════════════
    pressureLabel_ = tgui::Label::create("Pressure CH10: 0%");
    pressureLabel_->setPosition(PAD, y);
    pressureLabel_->setTextSize(12);
    pressureLabel_->getRenderer()->setTextColor(tgui::Color(170, 220, 255));
    gui_.add(pressureLabel_);
    y += 20;

    pressureMeterCanvas_ = tgui::CanvasSFML::create({static_cast<float>(colW), 16.0f});
    pressureMeterCanvas_->setPosition(PAD, y);
    gui_.add(pressureMeterCanvas_);
    y += 16 + SEC_GAP;

    // ══════════════════════════════════════════════════════════════════════════
    // 2. AUDIO & TRANSIENT VISUALIZATIONS
    // ══════════════════════════════════════════════════════════════════════════
    addSectionLabel("Audio & Transient Analysis", tgui::Color(180, 220, 255));

    // ── Audio spectrum bar meter ─────────────────────────────────────────
    addSubLabel("Audio Spectrum:");

    audioMeterCanvas_ = tgui::CanvasSFML::create({static_cast<float>(colW), 50.0f});
    audioMeterCanvas_->setPosition(PAD, y);
    gui_.add(audioMeterCanvas_);
    y += 50 + ELEM_GAP;

    // ── Audio LED circles ────────────────────────────────────────────────
    addSubLabel("Audio Band LEDs:");

    audioLedCanvas_ = tgui::CanvasSFML::create({static_cast<float>(colW), 56.0f});
    audioLedCanvas_->setPosition(PAD, y);
    gui_.add(audioLedCanvas_);
    y += 56 + ELEM_GAP;

    // ── Transient energy LED circles ─────────────────────────────────────
    addSubLabel("Transient Energy LEDs:");

    transientLedCanvas_ = tgui::CanvasSFML::create({static_cast<float>(colW), 56.0f});
    transientLedCanvas_->setPosition(PAD, y);
    gui_.add(transientLedCanvas_);
    y += 56 + ELEM_GAP;

    // ── Rolling 16-row canvas ────────────────────────────────────────────
    addSubLabel("Rolling History  (top 8 rows = audio  |  bottom 8 = transient):");

    rollingCanvas_ = tgui::CanvasSFML::create({static_cast<float>(colW), 192.0f});
    rollingCanvas_->setPosition(PAD, y);
    gui_.add(rollingCanvas_);
    y += 192 + SEC_GAP;

    // ══════════════════════════════════════════════════════════════════════════
    // 3. RHYTHM CONTROLS
    // ══════════════════════════════════════════════════════════════════════════
    addSectionLabel("Rhythm Engine", tgui::Color(255, 210, 80));

    rhythmToggleBtn_ = tgui::Button::create("Rhythm Off");
    rhythmToggleBtn_->setPosition(PAD, y);
    rhythmToggleBtn_->setSize(110, 30);
    rhythmToggleBtn_->setTextSize(12);
    rhythmToggleBtn_->onPress([this] { if (onRhythmToggle) onRhythmToggle(); });
    gui_.add(rhythmToggleBtn_);

    rhythmMixBtn_ = tgui::Button::create("Mix: Audio");
    rhythmMixBtn_->setPosition(PAD + 120, y);
    rhythmMixBtn_->setSize(110, 30);
    rhythmMixBtn_->setTextSize(12);
    rhythmMixBtn_->onPress([this] { if (onRhythmMixCycle) onRhythmMixCycle(); });
    gui_.add(rhythmMixBtn_);

    rhythmPatternBtn_ = tgui::Button::create("Pat: Metronome");
    rhythmPatternBtn_->setPosition(PAD + 240, y);
    rhythmPatternBtn_->setSize(210, 30);
    rhythmPatternBtn_->setTextSize(12);
    rhythmPatternBtn_->onPress([this] { if (onRhythmPatternCycle) onRhythmPatternCycle(); });
    gui_.add(rhythmPatternBtn_);
    y += 30 + ELEM_GAP;

    // BPM / Intensity nudge row
    auto addNudgePair = [&](tgui::Button::Ptr& dn, tgui::Button::Ptr& up,
                            const std::string& lbl,
                            int x, int w,
                            std::function<void()> onDn, std::function<void()> onUp) {
        dn = tgui::Button::create(lbl + " -");
        dn->setPosition(x, y);
        dn->setSize(w, 26);
        dn->setTextSize(11);
        dn->onPress(onDn);
        gui_.add(dn);

        up = tgui::Button::create(lbl + " +");
        up->setPosition(x + w + 6, y);
        up->setSize(w, 26);
        up->setTextSize(11);
        up->onPress(onUp);
        gui_.add(up);
    };

    addNudgePair(rhythmBpmDownBtn_, rhythmBpmUpBtn_, "BPM", PAD, 60,
                 [this] { if (onRhythmBpmNudge) onRhythmBpmNudge(-2.0f); },
                 [this] { if (onRhythmBpmNudge) onRhythmBpmNudge(2.0f); });

    addNudgePair(rhythmIntDownBtn_, rhythmIntUpBtn_, "Int", PAD + 140, 60,
                 [this] { if (onRhythmIntensityNudge) onRhythmIntensityNudge(-0.05f); },
                 [this] { if (onRhythmIntensityNudge) onRhythmIntensityNudge(0.05f); });
    y += 26 + ELEM_GAP;

    // Lane controls
    auto laneLabel = tgui::Label::create("Lanes (toggle / pulse / gain):");
    laneLabel->setPosition(PAD, y);
    laneLabel->setTextSize(11);
    laneLabel->getRenderer()->setTextColor(TEXT_DIM);
    gui_.add(laneLabel);
    y += 18;

    const int laneW   = (colW - 2 * ELEM_GAP) / 3;
    const int laneX0  = PAD;

    for (int i = 0; i < 3; ++i) {
        int lx = laneX0 + i * (laneW + ELEM_GAP);

        rhythmLaneBtns_[i] = tgui::Button::create("Lane");
        rhythmLaneBtns_[i]->setPosition(lx, y);
        rhythmLaneBtns_[i]->setSize(laneW, 30);
        rhythmLaneBtns_[i]->setTextSize(12);
        rhythmLaneBtns_[i]->onPress([this, i] { if (onRhythmLaneToggle) onRhythmLaneToggle(i); });
        gui_.add(rhythmLaneBtns_[i]);

        int btnW = laneW / 2 - 3;

        rhythmLanePulseDownBtns_[i] = tgui::Button::create("Pulse-");
        rhythmLanePulseDownBtns_[i]->setPosition(lx, y + 34);
        rhythmLanePulseDownBtns_[i]->setSize(btnW, 24);
        rhythmLanePulseDownBtns_[i]->setTextSize(11);
        rhythmLanePulseDownBtns_[i]->onPress([this, i] { if (onRhythmLanePulseNudge) onRhythmLanePulseNudge(i, -1); });
        gui_.add(rhythmLanePulseDownBtns_[i]);

        rhythmLanePulseUpBtns_[i] = tgui::Button::create("Pulse+");
        rhythmLanePulseUpBtns_[i]->setPosition(lx + btnW + 6, y + 34);
        rhythmLanePulseUpBtns_[i]->setSize(btnW, 24);
        rhythmLanePulseUpBtns_[i]->setTextSize(11);
        rhythmLanePulseUpBtns_[i]->onPress([this, i] { if (onRhythmLanePulseNudge) onRhythmLanePulseNudge(i, 1); });
        gui_.add(rhythmLanePulseUpBtns_[i]);

        rhythmLaneGainDownBtns_[i] = tgui::Button::create("Gain-");
        rhythmLaneGainDownBtns_[i]->setPosition(lx, y + 62);
        rhythmLaneGainDownBtns_[i]->setSize(btnW, 24);
        rhythmLaneGainDownBtns_[i]->setTextSize(11);
        rhythmLaneGainDownBtns_[i]->onPress([this, i] { if (onRhythmLaneGainNudge) onRhythmLaneGainNudge(i, -0.05f); });
        gui_.add(rhythmLaneGainDownBtns_[i]);

        rhythmLaneGainUpBtns_[i] = tgui::Button::create("Gain+");
        rhythmLaneGainUpBtns_[i]->setPosition(lx + btnW + 6, y + 62);
        rhythmLaneGainUpBtns_[i]->setSize(btnW, 24);
        rhythmLaneGainUpBtns_[i]->setTextSize(11);
        rhythmLaneGainUpBtns_[i]->onPress([this, i] { if (onRhythmLaneGainNudge) onRhythmLaneGainNudge(i, 0.05f); });
        gui_.add(rhythmLaneGainUpBtns_[i]);
    }
    y += 30 + 24 + 24 + 8 + SEC_GAP;  // lane btn + pulse row + gain row + padding

    // ══════════════════════════════════════════════════════════════════════════
    // 4. LIF MIDI CONTROLS
    // ══════════════════════════════════════════════════════════════════════════
    addSectionLabel("LIF MIDI Control", tgui::Color(255, 210, 80));

    lifMidiToggleBtn_ = tgui::Button::create("Enable LIF MIDI (M)");
    lifMidiToggleBtn_->setPosition(PAD, y);
    lifMidiToggleBtn_->setSize(colW, 30);
    lifMidiToggleBtn_->onPress([this] { if (onLIFMidiToggle) onLIFMidiToggle(); });
    gui_.add(lifMidiToggleBtn_);
    y += 30 + ELEM_GAP;

    lifMidiStatusLabel_ = tgui::Label::create("LIF MIDI: Off");
    lifMidiStatusLabel_->setPosition(PAD, y);
    lifMidiStatusLabel_->setTextSize(12);
    lifMidiStatusLabel_->getRenderer()->setTextColor(TEXT_DIM);
    gui_.add(lifMidiStatusLabel_);
    y += 20 + ELEM_GAP;

    lifMidiStyleBtn_ = tgui::Button::create("LIF MIDI Style: Orpheus Black Moon");
    lifMidiStyleBtn_->setPosition(PAD, y);
    lifMidiStyleBtn_->setSize(colW, 28);
    lifMidiStyleBtn_->onPress([this] { if (onLIFMidiStyleCycle) onLIFMidiStyleCycle(); });
    gui_.add(lifMidiStyleBtn_);
    y += 28 + ELEM_GAP;

    lifMidiModeBtn_ = tgui::Button::create("LIF MIDI Mode: Dorian");
    lifMidiModeBtn_->setPosition(PAD, y);
    lifMidiModeBtn_->setSize(colW, 28);
    lifMidiModeBtn_->onPress([this] { if (onLIFMidiModeCycle) onLIFMidiModeCycle(); });
    gui_.add(lifMidiModeBtn_);
    y += 28 + ELEM_GAP;

    lifMidiModeEditLabel_ = tgui::Label::create("Mode Edit: n/a");
    lifMidiModeEditLabel_->setPosition(PAD, y);
    lifMidiModeEditLabel_->setTextSize(11);
    lifMidiModeEditLabel_->getRenderer()->setTextColor(TEXT_DIM);
    gui_.add(lifMidiModeEditLabel_);
    y += 18 + 4;

    // Mode edit buttons
    {
        const int bw = (colW - 4 * 6) / 5;
        lifMidiModeDegDownBtn_ = tgui::Button::create("Deg-");
        lifMidiModeDegDownBtn_->setPosition(PAD, y);
        lifMidiModeDegDownBtn_->setSize(bw, 26);
        lifMidiModeDegDownBtn_->onPress([this] { if (onLIFMidiModeEditDegreeNudge) onLIFMidiModeEditDegreeNudge(-1); });
        gui_.add(lifMidiModeDegDownBtn_);

        lifMidiModeDegUpBtn_ = tgui::Button::create("Deg+");
        lifMidiModeDegUpBtn_->setPosition(PAD + bw + 6, y);
        lifMidiModeDegUpBtn_->setSize(bw, 26);
        lifMidiModeDegUpBtn_->onPress([this] { if (onLIFMidiModeEditDegreeNudge) onLIFMidiModeEditDegreeNudge(1); });
        gui_.add(lifMidiModeDegUpBtn_);

        lifMidiModeSemiDownBtn_ = tgui::Button::create("Semi-");
        lifMidiModeSemiDownBtn_->setPosition(PAD + 2 * (bw + 6), y);
        lifMidiModeSemiDownBtn_->setSize(bw, 26);
        lifMidiModeSemiDownBtn_->onPress([this] { if (onLIFMidiModeEditSemitoneNudge) onLIFMidiModeEditSemitoneNudge(-1); });
        gui_.add(lifMidiModeSemiDownBtn_);

        lifMidiModeSemiUpBtn_ = tgui::Button::create("Semi+");
        lifMidiModeSemiUpBtn_->setPosition(PAD + 3 * (bw + 6), y);
        lifMidiModeSemiUpBtn_->setSize(bw, 26);
        lifMidiModeSemiUpBtn_->onPress([this] { if (onLIFMidiModeEditSemitoneNudge) onLIFMidiModeEditSemitoneNudge(1); });
        gui_.add(lifMidiModeSemiUpBtn_);

        lifMidiModeResetBtn_ = tgui::Button::create("Reset");
        lifMidiModeResetBtn_->setPosition(PAD + 4 * (bw + 6), y);
        lifMidiModeResetBtn_->setSize(bw, 26);
        lifMidiModeResetBtn_->onPress([this] { if (onLIFMidiModeEditReset) onLIFMidiModeEditReset(); });
        gui_.add(lifMidiModeResetBtn_);
    }
    y += 26 + SEC_GAP;

    // ══════════════════════════════════════════════════════════════════════════
    // 5. LIF TONE SYNTH
    // ══════════════════════════════════════════════════════════════════════════
    addSectionLabel("LIF Tone Synth", tgui::Color(255, 210, 80));

    lifToneToggleBtn_ = tgui::Button::create("Enable LIF Tone (K)");
    lifToneToggleBtn_->setPosition(PAD, y);
    lifToneToggleBtn_->setSize(colW, 30);
    lifToneToggleBtn_->onPress([this] { if (onLIFToneToggle) onLIFToneToggle(); });
    gui_.add(lifToneToggleBtn_);
    y += 30 + ELEM_GAP;

    lifToneVolLabel_ = tgui::Label::create("Tone Vol: 85%");
    lifToneVolLabel_->setPosition(PAD, y);
    lifToneVolLabel_->setTextSize(12);
    lifToneVolLabel_->getRenderer()->setTextColor(TEXT_DIM);
    gui_.add(lifToneVolLabel_);
    y += 20 + 4;

    lifToneVolDownBtn_ = tgui::Button::create("Vol -");
    lifToneVolDownBtn_->setPosition(PAD, y);
    lifToneVolDownBtn_->setSize(90, 28);
    lifToneVolDownBtn_->onPress([this] { if (onLIFToneVolumeNudge) onLIFToneVolumeNudge(-5.0f); });
    gui_.add(lifToneVolDownBtn_);

    lifToneVolUpBtn_ = tgui::Button::create("Vol +");
    lifToneVolUpBtn_->setPosition(PAD + 100, y);
    lifToneVolUpBtn_->setSize(90, 28);
    lifToneVolUpBtn_->onPress([this] { if (onLIFToneVolumeNudge) onLIFToneVolumeNudge(5.0f); });
    gui_.add(lifToneVolUpBtn_);
    y += 28 + SEC_GAP;

    // ══════════════════════════════════════════════════════════════════════════
    // 6. AUDIO BYPASS + TRANSIENT AUDITION
    // ══════════════════════════════════════════════════════════════════════════
    audioBypPassBtn_ = tgui::Button::create("Audio Bypass (B)");
    audioBypPassBtn_->setPosition(PAD, y);
    audioBypPassBtn_->setSize((colW - ELEM_GAP) / 2, 30);
    audioBypPassBtn_->getRenderer()->setBackgroundColor(tgui::Color(60, 40, 40));
    audioBypPassBtn_->onPress([this] {
        auto bgColor = audioBypPassBtn_->getRenderer()->getBackgroundColor();
        if (bgColor == tgui::Color(60, 40, 40)) {
            audioBypPassBtn_->getRenderer()->setBackgroundColor(tgui::Color(100, 60, 60));
            if (onBKey) onBKey(true);
        } else {
            audioBypPassBtn_->getRenderer()->setBackgroundColor(tgui::Color(60, 40, 40));
            if (onBKey) onBKey(false);
        }
    });
    gui_.add(audioBypPassBtn_);

    transientAuditionBtn_ = tgui::Button::create("Transient Audition: Off");
    transientAuditionBtn_->setPosition(PAD + (colW - ELEM_GAP) / 2 + ELEM_GAP, y);
    transientAuditionBtn_->setSize((colW - ELEM_GAP) / 2, 30);
    transientAuditionBtn_->getRenderer()->setBackgroundColor(tgui::Color(30, 50, 30));
    transientAuditionBtn_->onPress([this] {
        transientAuditionEnabled_ = !transientAuditionEnabled_;
        transientAuditionBtn_->setText(transientAuditionEnabled_
                                       ? "Transient Audition: On"
                                       : "Transient Audition: Off");
        transientAuditionBtn_->getRenderer()->setBackgroundColor(
            transientAuditionEnabled_ ? tgui::Color(40, 100, 40) : tgui::Color(30, 50, 30));
        if (onTransientAuditionToggle)
            onTransientAuditionToggle(transientAuditionEnabled_);
    });
    gui_.add(transientAuditionBtn_);
    y += 30 + SEC_GAP;

    // ══════════════════════════════════════════════════════════════════════════
    // 7. MIDI SETTINGS (collapsible)
    // ══════════════════════════════════════════════════════════════════════════
    midiSettingsBtn_ = tgui::Button::create("MIDI Settings ▼");
    midiSettingsBtn_->setPosition(PAD, y);
    midiSettingsBtn_->setSize(colW, 28);
    midiSettingsBtn_->onPress([this] {
        midiSettingsExpanded_ = !midiSettingsExpanded_;
        if (midiSettingsPanel_)
            midiSettingsPanel_->setVisible(midiSettingsExpanded_);
        if (midiSettingsBtn_)
            midiSettingsBtn_->setText(midiSettingsExpanded_ ? "MIDI Settings ▲" : "MIDI Settings ▼");
    });
    gui_.add(midiSettingsBtn_);
    y += 28 + 8;

    const int panelW = colW;
    midiSettingsPanel_ = tgui::Panel::create({static_cast<float>(panelW), 700.0f});
    midiSettingsPanel_->setPosition(PAD, y);
    midiSettingsPanel_->getRenderer()->setBackgroundColor(tgui::Color(24, 24, 32));
    midiSettingsPanel_->setVisible(false);
    gui_.add(midiSettingsPanel_);

    int py = 8;
    const int panelColW = panelW - 12;

    auto addPanelLabel = [&](const std::string& text) {
        auto lbl = tgui::Label::create(text);
        lbl->setPosition(6, py);
        lbl->setTextSize(11);
        lbl->getRenderer()->setTextColor(TEXT_DIM);
        midiSettingsPanel_->add(lbl);
        py += 18;
    };

    addPanelLabel("MIDI Input:");
    midiInPortBox_ = tgui::ComboBox::create();
    midiInPortBox_->setPosition(6, py);
    midiInPortBox_->setSize(panelColW, 26);
    midiInPortBox_->setDefaultText("Select MIDI input");
    midiInPortBox_->onItemSelect([this] {
        if (onMidiInPortChanged)
            onMidiInPortChanged(midiInPortBox_->getSelectedItem().toStdString());
    });
    midiSettingsPanel_->add(midiInPortBox_);
    py += 26 + 12;

    addPanelLabel("MIDI Output:");
    midiOutPortBox_ = tgui::ComboBox::create();
    midiOutPortBox_->setPosition(6, py);
    midiOutPortBox_->setSize(panelColW, 26);
    midiOutPortBox_->setDefaultText("Select MIDI output");
    midiOutPortBox_->onItemSelect([this] {
        if (onMidiOutPortChanged)
            onMidiOutPortChanged(midiOutPortBox_->getSelectedItem().toStdString());
    });
    midiSettingsPanel_->add(midiOutPortBox_);
    py += 26 + 18;

    // Key / Tonal Root / Mode Source
    addPanelLabel("MIDI Key:");
    lifMidiKeyLabel_ = tgui::Label::create("Key: C");
    lifMidiKeyLabel_->setPosition(6, py);
    lifMidiKeyLabel_->setTextSize(11);
    lifMidiKeyLabel_->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(lifMidiKeyLabel_);
    py += 18;

    lifMidiKeyDownBtn_ = tgui::Button::create("Key -");
    lifMidiKeyDownBtn_->setPosition(6, py);
    lifMidiKeyDownBtn_->setSize(70, 26);
    lifMidiKeyDownBtn_->onPress([this] { if (onLIFMidiKeyNudge) onLIFMidiKeyNudge(-1); });
    midiSettingsPanel_->add(lifMidiKeyDownBtn_);

    lifMidiKeyUpBtn_ = tgui::Button::create("Key +");
    lifMidiKeyUpBtn_->setPosition(84, py);
    lifMidiKeyUpBtn_->setSize(70, 26);
    lifMidiKeyUpBtn_->onPress([this] { if (onLIFMidiKeyNudge) onLIFMidiKeyNudge(1); });
    midiSettingsPanel_->add(lifMidiKeyUpBtn_);
    py += 26 + 14;

    addPanelLabel("Tonal Root:");
    lifMidiTonalRootLabel_ = tgui::Label::create("Tonal Root: C");
    lifMidiTonalRootLabel_->setPosition(6, py);
    lifMidiTonalRootLabel_->setTextSize(11);
    lifMidiTonalRootLabel_->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(lifMidiTonalRootLabel_);
    py += 18;

    lifMidiTonalRootDownBtn_ = tgui::Button::create("Root -");
    lifMidiTonalRootDownBtn_->setPosition(6, py);
    lifMidiTonalRootDownBtn_->setSize(70, 26);
    lifMidiTonalRootDownBtn_->onPress([this] { if (onLIFMidiTonalRootNudge) onLIFMidiTonalRootNudge(-1); });
    midiSettingsPanel_->add(lifMidiTonalRootDownBtn_);

    lifMidiTonalRootUpBtn_ = tgui::Button::create("Root +");
    lifMidiTonalRootUpBtn_->setPosition(84, py);
    lifMidiTonalRootUpBtn_->setSize(70, 26);
    lifMidiTonalRootUpBtn_->onPress([this] { if (onLIFMidiTonalRootNudge) onLIFMidiTonalRootNudge(1); });
    midiSettingsPanel_->add(lifMidiTonalRootUpBtn_);
    py += 26 + 14;

    addPanelLabel("Mode Source:");
    {
        auto modeSourceBtn = tgui::Button::create("Auto/Manual");
        modeSourceBtn->setPosition(6, py);
        modeSourceBtn->setSize(panelColW / 2, 26);
        modeSourceBtn->onPress([this] { if (onLIFMidiModeToggle) onLIFMidiModeToggle(); });
        midiSettingsPanel_->add(modeSourceBtn);
        lifMidiModeToggleBtn_ = modeSourceBtn;
    }
    py += 26 + 18;

    addPanelLabel("Note Range:");
    lifMidiRangeLabel_ = tgui::Label::create("Range: 36-96");
    lifMidiRangeLabel_->setPosition(6, py);
    lifMidiRangeLabel_->setTextSize(11);
    lifMidiRangeLabel_->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(lifMidiRangeLabel_);
    py += 18;

    lifMidiRangeMinDownBtn_ = tgui::Button::create("Lo -");
    lifMidiRangeMinDownBtn_->setPosition(6, py);
    lifMidiRangeMinDownBtn_->setSize(50, 26);
    lifMidiRangeMinDownBtn_->onPress([this] { if (onLIFMidiRangeMinNudge) onLIFMidiRangeMinNudge(-1); });
    midiSettingsPanel_->add(lifMidiRangeMinDownBtn_);

    lifMidiRangeMinUpBtn_ = tgui::Button::create("Lo +");
    lifMidiRangeMinUpBtn_->setPosition(60, py);
    lifMidiRangeMinUpBtn_->setSize(50, 26);
    lifMidiRangeMinUpBtn_->onPress([this] { if (onLIFMidiRangeMinNudge) onLIFMidiRangeMinNudge(1); });
    midiSettingsPanel_->add(lifMidiRangeMinUpBtn_);

    lifMidiRangeMaxDownBtn_ = tgui::Button::create("Hi -");
    lifMidiRangeMaxDownBtn_->setPosition(120, py);
    lifMidiRangeMaxDownBtn_->setSize(50, 26);
    lifMidiRangeMaxDownBtn_->onPress([this] { if (onLIFMidiRangeMaxNudge) onLIFMidiRangeMaxNudge(-1); });
    midiSettingsPanel_->add(lifMidiRangeMaxDownBtn_);

    lifMidiRangeMaxUpBtn_ = tgui::Button::create("Hi +");
    lifMidiRangeMaxUpBtn_->setPosition(178, py);
    lifMidiRangeMaxUpBtn_->setSize(50, 26);
    lifMidiRangeMaxUpBtn_->onPress([this] { if (onLIFMidiRangeMaxNudge) onLIFMidiRangeMaxNudge(1); });
    midiSettingsPanel_->add(lifMidiRangeMaxUpBtn_);
}

bool AudioMidiControlWindow::handleEvents() {
    while (const auto event = window_.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window_.close();
            return false;
        }

        gui_.handleEvent(*event);
    }

    // Use isKeyPressed for continuous key polling
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::M)) {
        if (onLIFMidiToggle) onLIFMidiToggle();
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::K)) {
        if (onLIFToneToggle) onLIFToneToggle();
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::B)) {
        auto bgColor = audioBypPassBtn_->getRenderer()->getBackgroundColor();
        if (bgColor == tgui::Color(60, 40, 40)) {
            audioBypPassBtn_->getRenderer()->setBackgroundColor(tgui::Color(100, 60, 60));
            if (onBKey) onBKey(true);
        } else {
            audioBypPassBtn_->getRenderer()->setBackgroundColor(tgui::Color(60, 40, 40));
            if (onBKey) onBKey(false);
        }
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Hyphen)) {
        if (onLIFToneVolumeNudge) onLIFToneVolumeNudge(-5.0f);
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Equal)) {
        if (onLIFToneVolumeNudge) onLIFToneVolumeNudge(5.0f);
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LBracket)) {
        if (onLIFMidiKeyNudge) onLIFMidiKeyNudge(-1);
    }
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RBracket)) {
        if (onLIFMidiKeyNudge) onLIFMidiKeyNudge(1);
    }

    return window_.isOpen();
}

void AudioMidiControlWindow::update() {
    // Append one column to the rolling buffer each frame
    std::array<float, ROLLING_ROWS> col = {};
    for (int i = 0; i < 8; ++i) {
        col[static_cast<size_t>(i)]     = audioBands_[static_cast<size_t>(i)];
        col[static_cast<size_t>(8 + i)] = transientBands_[static_cast<size_t>(i)];
    }
    rollingBuf_.push_back(col);
    while (static_cast<int>(rollingBuf_.size()) > ROLLING_MAX_COLS)
        rollingBuf_.pop_front();

    drawAudioMeter();
    drawPressureMeter();
    drawAudioLeds();
    drawTransientLeds();
    drawRollingCanvas();
}

void AudioMidiControlWindow::render() {
    window_.clear(BG_DARK);
    gui_.draw();
    window_.display();
}

void AudioMidiControlWindow::setAudioBands(const float* bands, int count, float rms) {
    int n = std::min(count, static_cast<int>(audioBands_.size()));
    for (int i = 0; i < n; ++i) audioBands_[i] = bands[i];
    audioRms_ = rms;
}

void AudioMidiControlWindow::setTransientBands(const float* bands, int count, float rms) {
    int n = std::min(count, static_cast<int>(transientBands_.size()));
    for (int i = 0; i < n; ++i) transientBands_[i] = bands[i];
    transientRms_ = rms;
}

void AudioMidiControlWindow::setPressureNorm(float norm) {
    pressureNorm_ = std::clamp(norm, 0.0f, 1.0f);
    int pct = static_cast<int>(pressureNorm_ * 100.0f);
    pressureLabel_->setText("Pressure CH10: " + std::to_string(pct) + "%");
}

void AudioMidiControlWindow::setLifMidiStatus(bool enabled, int channel, int baseNote) {
    std::string status = enabled ? "LIF MIDI: On (Ch " + std::to_string(channel) + ")" : "LIF MIDI: Off";
    lifMidiStatusLabel_->setText(status);
    lifMidiToggleBtn_->setText(enabled ? "Disable LIF MIDI (M)" : "Enable LIF MIDI (M)");
}

void AudioMidiControlWindow::setLifMidiStyle(const std::string& styleName) {
    lifMidiStyleBtn_->setText("LIF MIDI Style: " + styleName);
}

void AudioMidiControlWindow::setLifMidiMode(const std::string& modeName) {
    lifMidiModeBtn_->setText("LIF MIDI Mode: " + modeName);
}

void AudioMidiControlWindow::setLifMidiModeToggleState(bool hasScene, bool autoMode) {
    const std::string text = hasScene
        ? (autoMode ? "Mode Source: Auto" : "Mode Source: Manual")
        : "Mode Source: n/a";
    if (lifMidiModeToggleBtn_) {
        lifMidiModeToggleBtn_->setText(text);
        lifMidiModeToggleBtn_->setEnabled(hasScene);
    }
}

void AudioMidiControlWindow::setLifMidiModeEditor(const std::string& statusText, bool editable) {
    if (lifMidiModeEditLabel_)
        lifMidiModeEditLabel_->setText(statusText);
    if (lifMidiModeDegDownBtn_) lifMidiModeDegDownBtn_->setEnabled(editable);
    if (lifMidiModeDegUpBtn_) lifMidiModeDegUpBtn_->setEnabled(editable);
    if (lifMidiModeSemiDownBtn_) lifMidiModeSemiDownBtn_->setEnabled(editable);
    if (lifMidiModeSemiUpBtn_) lifMidiModeSemiUpBtn_->setEnabled(editable);
    if (lifMidiModeResetBtn_) lifMidiModeResetBtn_->setEnabled(editable);
}

void AudioMidiControlWindow::setLifMidiKey(const std::string& keyName) {
    lifMidiKeyLabel_->setText("Key: " + keyName);
}

void AudioMidiControlWindow::setLifMidiTonalRoot(const std::string& keyName) {
    lifMidiTonalRootLabel_->setText("Tonal Root: " + keyName);
}

void AudioMidiControlWindow::setLifMidiRange(int minNote, int maxNote) {
    lifMidiRangeLabel_->setText("Range: " + std::to_string(minNote) + "-" + std::to_string(maxNote));
}

void AudioMidiControlWindow::setLifToneVolume(float volume) {
    int pct = static_cast<int>(volume * 100.0f);
    lifToneVolLabel_->setText("Tone Vol: " + std::to_string(pct) + "%");
}

void AudioMidiControlWindow::setRhythmStatus(bool enabled,
                                             const std::string& mixModeName,
                                             const std::string& patternName,
                                             float bpm,
                                             float intensity,
                                             bool quantizedPending) {
    if (rhythmToggleBtn_)
        rhythmToggleBtn_->setText(enabled ? "Rhythm On" : "Rhythm Off");
    if (rhythmMixBtn_)
        rhythmMixBtn_->setText("Mix: " + mixModeName);
    if (rhythmPatternBtn_) {
        const int bpmInt = static_cast<int>(bpm + 0.5f);
        const int pct = static_cast<int>(std::clamp(intensity, 0.0f, 1.0f) * 100.0f + 0.5f);
        const std::string q = quantizedPending ? " Q" : "";
        rhythmPatternBtn_->setText("Pat: " + patternName + q + " " + std::to_string(bpmInt) + " " + std::to_string(pct) + "%");
    }
}

void AudioMidiControlWindow::setRhythmLaneScaffold(const std::array<std::string, 3>& names,
                                                   const std::array<uint8_t, 3>& enabled,
                                                   const std::array<int, 3>& pulses,
                                                   const std::array<float, 3>& gains) {
    for (int i = 0; i < 3; ++i) {
        auto btn = rhythmLaneBtns_[static_cast<size_t>(i)];
        if (!btn) continue;
        const bool on = enabled[static_cast<size_t>(i)] != 0;
        const int gainPct = static_cast<int>(std::clamp(gains[static_cast<size_t>(i)], 0.0f, 2.0f) * 100.0f + 0.5f);
        const std::string label = names[static_cast<size_t>(i)] + " " + std::to_string(pulses[static_cast<size_t>(i)])
                                  + " " + std::to_string(gainPct) + "%"
                                  + (on ? " On" : " Off");
        btn->setText(label);
    }
}

void AudioMidiControlWindow::setMidiPortLists(const std::vector<std::string>& inPorts,
                                               int inIdx,
                                               const std::vector<std::string>& outPorts,
                                               int outIdx) {
    midiInPortBox_->removeAllItems();
    for (const auto& p : inPorts) {
        midiInPortBox_->addItem(p);
    }
    if (inIdx >= 0 && inIdx < static_cast<int>(inPorts.size()))
        midiInPortBox_->setSelectedItemByIndex(inIdx);

    midiOutPortBox_->removeAllItems();
    for (const auto& p : outPorts) {
        midiOutPortBox_->addItem(p);
    }
    if (outIdx >= 0 && outIdx < static_cast<int>(outPorts.size()))
        midiOutPortBox_->setSelectedItemByIndex(outIdx);
}

void AudioMidiControlWindow::drawAudioMeter() {
    if (!audioMeterCanvas_) return;
    auto& rt = audioMeterCanvas_->getRenderTexture();
    audioMeterCanvas_->clear(tgui::Color(12, 12, 24));

    const float width  = audioMeterCanvas_->getSize().x;
    const float height = audioMeterCanvas_->getSize().y;
    const float barW = width / 8.0f;
    const float barGap = 3.0f;

    for (int i = 0; i < 8; ++i) {
        float energy = std::clamp(audioBands_[static_cast<size_t>(i)], 0.0f, 1.0f);
        float barH = energy * (height - 4.0f);
        float x = i * barW + barGap;
        float y = height - barH - 2.0f;

        // Gradient: blue → purple → magenta at high energy
        uint8_t r = static_cast<uint8_t>(60  + energy * 195);
        uint8_t g = static_cast<uint8_t>(80  - energy * 60);
        uint8_t b = static_cast<uint8_t>(200 - energy * 80);

        sf::RectangleShape bar({barW - barGap * 2, barH});
        bar.setPosition({x, y});
        bar.setFillColor(sf::Color(r, g, b));
        rt.draw(bar);

        // RMS indicator tick on the RMS bar (band 0 carries overall RMS hint)
        if (i == 0 && audioRms_ > 0.01f) {
            float rmsTick = height - audioRms_ * (height - 4.0f) - 2.0f;
            sf::RectangleShape tick({barW - barGap * 2, 2.0f});
            tick.setPosition({x, rmsTick});
            tick.setFillColor(sf::Color(255, 255, 255, 180));
            rt.draw(tick);
        }
    }

    audioMeterCanvas_->display();
}

// Helper: draw 8 LED-like glowing circles on a canvas
static void drawLedRow(tgui::CanvasSFML& canvas,
                       const std::array<float, 8>& values,
                       sf::Color baseOff, sf::Color baseOn)
{
    auto& rt = canvas.getRenderTexture();
    canvas.clear(tgui::Color(10, 10, 18));

    const float W = canvas.getSize().x;
    const float H = canvas.getSize().y;
    const float cellW = W / 8.0f;
    const float cx0 = cellW * 0.5f;
    const float cy = H * 0.42f;
    const float rOuter = std::min(cellW * 0.38f, H * 0.40f);
    const float rInner = rOuter * 0.65f;

    for (int i = 0; i < 8; ++i) {
        float energy = std::clamp(values[static_cast<size_t>(i)], 0.0f, 1.0f);
        float cx = cx0 + i * cellW;

        // Outer glow ring (semi-transparent, grows with energy)
        {
            float glowR = rOuter + energy * rOuter * 0.5f;
            sf::CircleShape glow(glowR);
            glow.setOrigin({glowR, glowR});
            glow.setPosition({cx, cy});
            uint8_t alpha = static_cast<uint8_t>(energy * 80);
            glow.setFillColor(sf::Color(baseOn.r, baseOn.g, baseOn.b, alpha));
            rt.draw(glow);
        }

        // LED circle body
        {
            sf::CircleShape led(rInner);
            led.setOrigin({rInner, rInner});
            led.setPosition({cx, cy});
            uint8_t r = static_cast<uint8_t>(baseOff.r + energy * (baseOn.r - baseOff.r));
            uint8_t g = static_cast<uint8_t>(baseOff.g + energy * (baseOn.g - baseOff.g));
            uint8_t b = static_cast<uint8_t>(baseOff.b + energy * (baseOn.b - baseOff.b));
            led.setFillColor(sf::Color(r, g, b));
            // Bright spec highlight
            led.setOutlineThickness(1.5f);
            led.setOutlineColor(sf::Color(
                static_cast<uint8_t>(std::min(255, static_cast<int>(r) + 60)),
                static_cast<uint8_t>(std::min(255, static_cast<int>(g) + 60)),
                static_cast<uint8_t>(std::min(255, static_cast<int>(b) + 60)),
                180));
            rt.draw(led);
        }
    }
    canvas.display();
}

void AudioMidiControlWindow::drawAudioLeds() {
    if (!audioLedCanvas_) return;
    drawLedRow(*audioLedCanvas_, audioBands_,
               sf::Color(20, 20, 60), sf::Color(80, 160, 255));
}

void AudioMidiControlWindow::drawTransientLeds() {
    if (!transientLedCanvas_) return;
    drawLedRow(*transientLedCanvas_, transientBands_,
               sf::Color(40, 20, 10), sf::Color(255, 140, 40));
}

void AudioMidiControlWindow::drawRollingCanvas() {
    if (!rollingCanvas_) return;
    auto& rt = rollingCanvas_->getRenderTexture();
    rollingCanvas_->clear(tgui::Color(8, 8, 14));

    const float W  = rollingCanvas_->getSize().x;
    const float H  = rollingCanvas_->getSize().y;
    const float rowH = H / static_cast<float>(ROLLING_ROWS);

    // Draw divider between audio (rows 0-7) and transient (rows 8-15)
    sf::RectangleShape divider({W, 1.0f});
    divider.setPosition({0.0f, rowH * 8.0f});
    divider.setFillColor(sf::Color(60, 60, 80, 120));
    rt.draw(divider);

    int numCols = static_cast<int>(rollingBuf_.size());
    if (numCols == 0) {
        rollingCanvas_->display();
        return;
    }

    // Draw one vertical pixel column per history entry, right-aligned
    // (newest column on the right edge)
    float pixelW = std::max(1.0f, W / static_cast<float>(ROLLING_MAX_COLS));
    float startX = W - numCols * pixelW;

    for (int col = 0; col < numCols; ++col) {
        const auto& frame = rollingBuf_[static_cast<size_t>(col)];
        float xPixel = startX + col * pixelW;

        for (int row = 0; row < ROLLING_ROWS; ++row) {
            float energy = std::clamp(frame[static_cast<size_t>(row)], 0.0f, 1.0f);
            if (energy < 0.02f) continue;

            float yPixel = row * rowH;
            sf::RectangleShape cell({pixelW, rowH});
            cell.setPosition({xPixel, yPixel});

            // Top 8 rows = audio (cool blue-purple), bottom 8 = transient (warm amber-red)
            if (row < 8) {
                uint8_t r = static_cast<uint8_t>(30  + energy * 180);
                uint8_t g = static_cast<uint8_t>(60  + energy * 100);
                uint8_t b = static_cast<uint8_t>(180 + energy * 75);
                cell.setFillColor(sf::Color(r, g, b));
            } else {
                uint8_t r = static_cast<uint8_t>(180 + energy * 75);
                uint8_t g = static_cast<uint8_t>(80  + energy * 80);
                uint8_t b = static_cast<uint8_t>(20  + energy * 30);
                cell.setFillColor(sf::Color(r, g, b));
            }
            rt.draw(cell);
        }
    }

    rollingCanvas_->display();
}

void AudioMidiControlWindow::drawPressureMeter() {
    if (!pressureMeterCanvas_) return;
    auto& rt = pressureMeterCanvas_->getRenderTexture();
    pressureMeterCanvas_->clear(tgui::Color(12, 12, 24));

    float width  = pressureMeterCanvas_->getSize().x;
    float height = pressureMeterCanvas_->getSize().y;
    float barW   = pressureNorm_ * width;

    sf::RectangleShape bar({barW, height});
    bar.setFillColor(sf::Color(100, 150, 220));
    rt.draw(bar);

    pressureMeterCanvas_->display();
}
