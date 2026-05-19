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
    buildGui(width, height);
}

bool AudioMidiControlWindow::isOpen() const { return window_.isOpen(); }
void AudioMidiControlWindow::close()        { window_.close(); }

const tgui::Color BG_DARK   {16,  16,  20 };
const tgui::Color TEXT_DIM  {150, 165, 195};
const tgui::Color TEXT_VAL  {215, 225, 255};

void AudioMidiControlWindow::buildGui(int width, int height) {
    const int leftColW = width - 28;

    contentPanel_ = tgui::ScrollablePanel::create({static_cast<float>(width), static_cast<float>(height)});
    contentPanel_->setPosition(0, 0);
    contentPanel_->getRenderer()->setBackgroundColor(BG_DARK);
    gui_.add(contentPanel_);

    // ── Pressure meter label ─────────────────────────────────────────────────────
    pressureLabel_ = tgui::Label::create("Pressure CH10: 0%");
    pressureLabel_->setPosition(14, 12);
    pressureLabel_->setTextSize(12);
    pressureLabel_->getRenderer()->setTextColor(tgui::Color(170, 220, 255));
    contentPanel_->add(pressureLabel_);

    pressureMeterCanvas_ = tgui::CanvasSFML::create({static_cast<float>(leftColW), 14.0f});
    pressureMeterCanvas_->setPosition(14, 30);
    contentPanel_->add(pressureMeterCanvas_);

    // ── Audio meter label ─────────────────────────────────────────────────────
    auto audioLabel = tgui::Label::create("Audio Spectrum:");
    audioLabel->setPosition(14, 50);
    audioLabel->setTextSize(12);
    audioLabel->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(audioLabel);

    audioMeterCanvas_ = tgui::CanvasSFML::create({static_cast<float>(leftColW), 40.0f});
    audioMeterCanvas_->setPosition(14, 68);
    contentPanel_->add(audioMeterCanvas_);

    // ── LIF MIDI output controls ─────────────────────────────────────────
    auto lifMidiLabel = tgui::Label::create("LIF MIDI Control");
    lifMidiLabel->setPosition(14, 114);
    lifMidiLabel->setTextSize(13);
    lifMidiLabel->getRenderer()->setTextColor(tgui::Color(255, 210, 80));
    contentPanel_->add(lifMidiLabel);

    lifMidiToggleBtn_ = tgui::Button::create("Enable LIF MIDI (M)");
    lifMidiToggleBtn_->setPosition(14, 134);
    lifMidiToggleBtn_->setSize(leftColW, 26);
    lifMidiToggleBtn_->onPress([this] {
        if (onLIFMidiToggle) onLIFMidiToggle();
    });
    contentPanel_->add(lifMidiToggleBtn_);

    lifMidiStatusLabel_ = tgui::Label::create("LIF MIDI: Off");
    lifMidiStatusLabel_->setPosition(14, 162);
    lifMidiStatusLabel_->setTextSize(12);
    lifMidiStatusLabel_->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(lifMidiStatusLabel_);

    lifMidiStyleBtn_ = tgui::Button::create("LIF MIDI Style: Pop");
    lifMidiStyleBtn_->setPosition(14, 182);
    lifMidiStyleBtn_->setSize(leftColW, 22);
    lifMidiStyleBtn_->onPress([this] {
        if (onLIFMidiStyleCycle)
            onLIFMidiStyleCycle();
    });
    contentPanel_->add(lifMidiStyleBtn_);

    lifMidiModeBtn_ = tgui::Button::create("LIF MIDI Mode: Dorian");
    lifMidiModeBtn_->setPosition(14, 208);
    lifMidiModeBtn_->setSize(leftColW, 22);
    lifMidiModeBtn_->onPress([this] {
        if (onLIFMidiModeCycle)
            onLIFMidiModeCycle();
    });
    contentPanel_->add(lifMidiModeBtn_);

    lifMidiModeEditLabel_ = tgui::Label::create("Mode Edit: n/a");
    lifMidiModeEditLabel_->setPosition(14, 258);
    lifMidiModeEditLabel_->setTextSize(11);
    lifMidiModeEditLabel_->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(lifMidiModeEditLabel_);

    lifMidiModeDegDownBtn_ = tgui::Button::create("Deg-");
    lifMidiModeDegDownBtn_->setPosition(14, 276);
    lifMidiModeDegDownBtn_->setSize(42, 20);
    lifMidiModeDegDownBtn_->onPress([this] {
        if (onLIFMidiModeEditDegreeNudge)
            onLIFMidiModeEditDegreeNudge(-1);
    });
    contentPanel_->add(lifMidiModeDegDownBtn_);

    lifMidiModeDegUpBtn_ = tgui::Button::create("Deg+");
    lifMidiModeDegUpBtn_->setPosition(60, 276);
    lifMidiModeDegUpBtn_->setSize(42, 20);
    lifMidiModeDegUpBtn_->onPress([this] {
        if (onLIFMidiModeEditDegreeNudge)
            onLIFMidiModeEditDegreeNudge(1);
    });
    contentPanel_->add(lifMidiModeDegUpBtn_);

    lifMidiModeSemiDownBtn_ = tgui::Button::create("Sem-");
    lifMidiModeSemiDownBtn_->setPosition(106, 276);
    lifMidiModeSemiDownBtn_->setSize(44, 20);
    lifMidiModeSemiDownBtn_->onPress([this] {
        if (onLIFMidiModeEditSemitoneNudge)
            onLIFMidiModeEditSemitoneNudge(-1);
    });
    contentPanel_->add(lifMidiModeSemiDownBtn_);

    lifMidiModeSemiUpBtn_ = tgui::Button::create("Sem+");
    lifMidiModeSemiUpBtn_->setPosition(154, 276);
    lifMidiModeSemiUpBtn_->setSize(44, 20);
    lifMidiModeSemiUpBtn_->onPress([this] {
        if (onLIFMidiModeEditSemitoneNudge)
            onLIFMidiModeEditSemitoneNudge(1);
    });
    contentPanel_->add(lifMidiModeSemiUpBtn_);

    lifMidiModeResetBtn_ = tgui::Button::create("Reset");
    lifMidiModeResetBtn_->setPosition(202, 276);
    lifMidiModeResetBtn_->setSize(54, 20);
    lifMidiModeResetBtn_->onPress([this] {
        if (onLIFMidiModeEditReset)
            onLIFMidiModeEditReset();
    });
    contentPanel_->add(lifMidiModeResetBtn_);

    // ── LIF Tone volume controls ────────────────────────────────────────
    auto lifToneLabel = tgui::Label::create("LIF Tone Synth");
    lifToneLabel->setPosition(14, 302);
    lifToneLabel->setTextSize(13);
    lifToneLabel->getRenderer()->setTextColor(tgui::Color(255, 210, 80));
    contentPanel_->add(lifToneLabel);

    lifToneToggleBtn_ = tgui::Button::create("Enable LIF Tone (K)");
    lifToneToggleBtn_->setPosition(14, 322);
    lifToneToggleBtn_->setSize(leftColW, 22);
    lifToneToggleBtn_->onPress([this] {
        if (onLIFToneToggle)
            onLIFToneToggle();
    });
    contentPanel_->add(lifToneToggleBtn_);

    lifToneVolLabel_ = tgui::Label::create("Tone Vol: 85%");
    lifToneVolLabel_->setPosition(14, 348);
    lifToneVolLabel_->setTextSize(11);
    lifToneVolLabel_->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(lifToneVolLabel_);

    lifToneVolDownBtn_ = tgui::Button::create("Vol -");
    lifToneVolDownBtn_->setPosition(14, 366);
    lifToneVolDownBtn_->setSize(70, 22);
    lifToneVolDownBtn_->onPress([this] {
        if (onLIFToneVolumeNudge)
            onLIFToneVolumeNudge(-5.0f);
    });
    contentPanel_->add(lifToneVolDownBtn_);

    lifToneVolUpBtn_ = tgui::Button::create("Vol +");
    lifToneVolUpBtn_->setPosition(90, 366);
    lifToneVolUpBtn_->setSize(70, 22);
    lifToneVolUpBtn_->onPress([this] {
        if (onLIFToneVolumeNudge)
            onLIFToneVolumeNudge(5.0f);
    });
    contentPanel_->add(lifToneVolUpBtn_);

    // ── Audio bypass toggle ─────────────────────────────────────────────
    audioBypPassBtn_ = tgui::Button::create("Audio Bypass (B)");
    audioBypPassBtn_->setPosition(14, 394);
    audioBypPassBtn_->setSize(leftColW, 22);
    audioBypPassBtn_->getRenderer()->setBackgroundColor(tgui::Color(60, 40, 40));
    audioBypPassBtn_->onPress([this] {
        // Toggle visual feedback
        auto bgColor = audioBypPassBtn_->getRenderer()->getBackgroundColor();
        if (bgColor == tgui::Color(60, 40, 40)) {
            audioBypPassBtn_->getRenderer()->setBackgroundColor(tgui::Color(100, 60, 60));
            if (onBKey) onBKey(true);
        } else {
            audioBypPassBtn_->getRenderer()->setBackgroundColor(tgui::Color(60, 40, 40));
            if (onBKey) onBKey(false);
        }
    });
    contentPanel_->add(audioBypPassBtn_);

    // ── MIDI Settings dropdown section ──────────────────────────────────
    midiSettingsBtn_ = tgui::Button::create("MIDI Settings ▼");
    midiSettingsBtn_->setPosition(14, 422);
    midiSettingsBtn_->setSize(leftColW, 24);
    midiSettingsBtn_->onPress([this] {
        midiSettingsExpanded_ = !midiSettingsExpanded_;
        if (midiSettingsPanel_)
            midiSettingsPanel_->setVisible(midiSettingsExpanded_);
        if (midiSettingsBtn_)
            midiSettingsBtn_->setText(midiSettingsExpanded_ ? "MIDI Settings ▲" : "MIDI Settings ▼");
    });
    contentPanel_->add(midiSettingsBtn_);

    midiSettingsPanel_ = tgui::Panel::create({static_cast<float>(leftColW), 260.0f});
    midiSettingsPanel_->setPosition(14, 448);
    midiSettingsPanel_->getRenderer()->setBackgroundColor(tgui::Color(24, 24, 32));
    midiSettingsPanel_->setVisible(false);
    contentPanel_->add(midiSettingsPanel_);

    auto midiInLabel = tgui::Label::create("Input:");
    midiInLabel->setPosition(6, 4);
    midiInLabel->setTextSize(11);
    midiInLabel->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(midiInLabel);

    midiInPortBox_ = tgui::ComboBox::create();
    midiInPortBox_->setPosition(6, 20);
    midiInPortBox_->setSize(leftColW - 12, 22);
    midiInPortBox_->setDefaultText("Select MIDI input");
    midiInPortBox_->onItemSelect([this] {
        if (onMidiInPortChanged)
            onMidiInPortChanged(midiInPortBox_->getSelectedItem().toStdString());
    });
    midiSettingsPanel_->add(midiInPortBox_);

    auto midiOutLabel = tgui::Label::create("Output:");
    midiOutLabel->setPosition(6, 46);
    midiOutLabel->setTextSize(11);
    midiOutLabel->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(midiOutLabel);

    midiOutPortBox_ = tgui::ComboBox::create();
    midiOutPortBox_->setPosition(6, 62);
    midiOutPortBox_->setSize(leftColW - 12, 22);
    midiOutPortBox_->setDefaultText("Select MIDI output");
    midiOutPortBox_->onItemSelect([this] {
        if (onMidiOutPortChanged)
            onMidiOutPortChanged(midiOutPortBox_->getSelectedItem().toStdString());
    });
    midiSettingsPanel_->add(midiOutPortBox_);

    lifMidiKeyLabel_ = tgui::Label::create("Key: C");
    lifMidiKeyLabel_->setPosition(6, 92);
    lifMidiKeyLabel_->setTextSize(11);
    lifMidiKeyLabel_->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(lifMidiKeyLabel_);

    lifMidiKeyDownBtn_ = tgui::Button::create("Key -");
    lifMidiKeyDownBtn_->setPosition(6, 112);
    lifMidiKeyDownBtn_->setSize(56, 22);
    lifMidiKeyDownBtn_->onPress([this] {
        if (onLIFMidiKeyNudge)
            onLIFMidiKeyNudge(-1);
    });
    midiSettingsPanel_->add(lifMidiKeyDownBtn_);

    lifMidiKeyUpBtn_ = tgui::Button::create("Key +");
    lifMidiKeyUpBtn_->setPosition(66, 112);
    lifMidiKeyUpBtn_->setSize(56, 22);
    lifMidiKeyUpBtn_->onPress([this] {
        if (onLIFMidiKeyNudge)
            onLIFMidiKeyNudge(1);
    });
    midiSettingsPanel_->add(lifMidiKeyUpBtn_);

    lifMidiTonalRootLabel_ = tgui::Label::create("Tonal Root: C");
    lifMidiTonalRootLabel_->setPosition(6, 140);
    lifMidiTonalRootLabel_->setTextSize(11);
    lifMidiTonalRootLabel_->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(lifMidiTonalRootLabel_);

    lifMidiTonalRootDownBtn_ = tgui::Button::create("Root-");
    lifMidiTonalRootDownBtn_->setPosition(6, 160);
    lifMidiTonalRootDownBtn_->setSize(56, 22);
    lifMidiTonalRootDownBtn_->onPress([this] {
        if (onLIFMidiTonalRootNudge)
            onLIFMidiTonalRootNudge(-1);
    });
    midiSettingsPanel_->add(lifMidiTonalRootDownBtn_);

    lifMidiTonalRootUpBtn_ = tgui::Button::create("Root+");
    lifMidiTonalRootUpBtn_->setPosition(66, 160);
    lifMidiTonalRootUpBtn_->setSize(56, 22);
    lifMidiTonalRootUpBtn_->onPress([this] {
        if (onLIFMidiTonalRootNudge)
            onLIFMidiTonalRootNudge(1);
    });
    midiSettingsPanel_->add(lifMidiTonalRootUpBtn_);

    auto modeSourceLabel = tgui::Label::create("Mode Source:");
    modeSourceLabel->setPosition(130, 140);
    modeSourceLabel->setTextSize(11);
    modeSourceLabel->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(modeSourceLabel);

    auto modeSourceBtn = tgui::Button::create("Auto/Manual");
    modeSourceBtn->setPosition(130, 160);
    modeSourceBtn->setSize(leftColW - 136, 22);
    modeSourceBtn->onPress([this] {
        if (onLIFMidiModeToggle)
            onLIFMidiModeToggle();
    });
    midiSettingsPanel_->add(modeSourceBtn);

    // Mirror the same shared pointer so both locations trigger identical behavior.
    lifMidiModeToggleBtn_ = modeSourceBtn;

    lifMidiRangeLabel_ = tgui::Label::create("Range: 36-96");
    lifMidiRangeLabel_->setPosition(6, 214);
    lifMidiRangeLabel_->setTextSize(11);
    lifMidiRangeLabel_->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(lifMidiRangeLabel_);

    lifMidiRangeMinDownBtn_ = tgui::Button::create("Lo-");
    lifMidiRangeMinDownBtn_->setPosition(6, 234);
    lifMidiRangeMinDownBtn_->setSize(32, 22);
    lifMidiRangeMinDownBtn_->onPress([this] {
        if (onLIFMidiRangeMinNudge)
            onLIFMidiRangeMinNudge(-1);
    });
    midiSettingsPanel_->add(lifMidiRangeMinDownBtn_);

    lifMidiRangeMinUpBtn_ = tgui::Button::create("Lo+");
    lifMidiRangeMinUpBtn_->setPosition(42, 234);
    lifMidiRangeMinUpBtn_->setSize(32, 22);
    lifMidiRangeMinUpBtn_->onPress([this] {
        if (onLIFMidiRangeMinNudge)
            onLIFMidiRangeMinNudge(1);
    });
    midiSettingsPanel_->add(lifMidiRangeMinUpBtn_);

    lifMidiRangeMaxDownBtn_ = tgui::Button::create("Hi-");
    lifMidiRangeMaxDownBtn_->setPosition(78, 234);
    lifMidiRangeMaxDownBtn_->setSize(32, 22);
    lifMidiRangeMaxDownBtn_->onPress([this] {
        if (onLIFMidiRangeMaxNudge)
            onLIFMidiRangeMaxNudge(-1);
    });
    midiSettingsPanel_->add(lifMidiRangeMaxDownBtn_);

    lifMidiRangeMaxUpBtn_ = tgui::Button::create("Hi+");
    lifMidiRangeMaxUpBtn_->setPosition(114, 234);
    lifMidiRangeMaxUpBtn_->setSize(32, 22);
    lifMidiRangeMaxUpBtn_->onPress([this] {
        if (onLIFMidiRangeMaxNudge)
            onLIFMidiRangeMaxNudge(1);
    });
    midiSettingsPanel_->add(lifMidiRangeMaxUpBtn_);

    const int rhythmTop = 724;
    auto rhythmLabel = tgui::Label::create("Rhythm Transient Engine");
    rhythmLabel->setPosition(14, rhythmTop);
    rhythmLabel->setTextSize(13);
    rhythmLabel->getRenderer()->setTextColor(tgui::Color(255, 210, 80));
    contentPanel_->add(rhythmLabel);

    rhythmToggleBtn_ = tgui::Button::create("Rhythm: Off");
    rhythmToggleBtn_->setPosition(14, rhythmTop + 20);
    rhythmToggleBtn_->setSize(leftColW, 24);
    rhythmToggleBtn_->onPress([this] {
        if (onRhythmToggle)
            onRhythmToggle();
    });
    contentPanel_->add(rhythmToggleBtn_);

    rhythmStrategyBtn_ = tgui::Button::create("Strategy: Hybrid");
    rhythmStrategyBtn_->setPosition(14, rhythmTop + 48);
    rhythmStrategyBtn_->setSize(leftColW, 22);
    rhythmStrategyBtn_->onPress([this] {
        if (onRhythmStrategyCycle)
            onRhythmStrategyCycle();
    });
    contentPanel_->add(rhythmStrategyBtn_);

    rhythmBpmLabel_ = tgui::Label::create("BPM: 124.0");
    rhythmBpmLabel_->setPosition(14, rhythmTop + 74);
    rhythmBpmLabel_->setTextSize(11);
    rhythmBpmLabel_->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(rhythmBpmLabel_);

    rhythmBpmDownBtn_ = tgui::Button::create("BPM-");
    rhythmBpmDownBtn_->setPosition(14, rhythmTop + 92);
    rhythmBpmDownBtn_->setSize(64, 22);
    rhythmBpmDownBtn_->onPress([this] {
        if (onRhythmBpmNudge)
            onRhythmBpmNudge(-2.0f);
    });
    contentPanel_->add(rhythmBpmDownBtn_);

    rhythmBpmUpBtn_ = tgui::Button::create("BPM+");
    rhythmBpmUpBtn_->setPosition(82, rhythmTop + 92);
    rhythmBpmUpBtn_->setSize(64, 22);
    rhythmBpmUpBtn_->onPress([this] {
        if (onRhythmBpmNudge)
            onRhythmBpmNudge(2.0f);
    });
    contentPanel_->add(rhythmBpmUpBtn_);

    rhythmGainLabel_ = tgui::Label::create("Pattern Gain: 0.85");
    rhythmGainLabel_->setPosition(154, rhythmTop + 74);
    rhythmGainLabel_->setTextSize(11);
    rhythmGainLabel_->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(rhythmGainLabel_);

    rhythmGainDownBtn_ = tgui::Button::create("Gain-");
    rhythmGainDownBtn_->setPosition(154, rhythmTop + 92);
    rhythmGainDownBtn_->setSize(64, 22);
    rhythmGainDownBtn_->onPress([this] {
        if (onRhythmGainNudge)
            onRhythmGainNudge(-0.05f);
    });
    contentPanel_->add(rhythmGainDownBtn_);

    rhythmGainUpBtn_ = tgui::Button::create("Gain+");
    rhythmGainUpBtn_->setPosition(222, rhythmTop + 92);
    rhythmGainUpBtn_->setSize(64, 22);
    rhythmGainUpBtn_->onPress([this] {
        if (onRhythmGainNudge)
            onRhythmGainNudge(0.05f);
    });
    contentPanel_->add(rhythmGainUpBtn_);

    rhythmAudibleBtn_ = tgui::Button::create("Audible Transients: Off");
    rhythmAudibleBtn_->setPosition(290, rhythmTop + 92);
    rhythmAudibleBtn_->setSize(leftColW - 290, 22);
    rhythmAudibleBtn_->onPress([this] {
        if (onRhythmAudibleToggle)
            onRhythmAudibleToggle();
    });
    contentPanel_->add(rhythmAudibleBtn_);

    auto divisiveLabel = tgui::Label::create("Divisive (ints, sum-free):");
    divisiveLabel->setPosition(14, rhythmTop + 122);
    divisiveLabel->setTextSize(11);
    divisiveLabel->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(divisiveLabel);

    rhythmDivisiveEdit_ = tgui::EditBox::create();
    rhythmDivisiveEdit_->setPosition(14, rhythmTop + 138);
    rhythmDivisiveEdit_->setSize(leftColW, 22);
    rhythmDivisiveEdit_->setDefaultText("4,8");
    contentPanel_->add(rhythmDivisiveEdit_);

    auto additiveLabel = tgui::Label::create("Additive groups (must sum to 16):");
    additiveLabel->setPosition(14, rhythmTop + 164);
    additiveLabel->setTextSize(11);
    additiveLabel->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(additiveLabel);

    rhythmAdditiveEdit_ = tgui::EditBox::create();
    rhythmAdditiveEdit_->setPosition(14, rhythmTop + 180);
    rhythmAdditiveEdit_->setSize(leftColW, 22);
    rhythmAdditiveEdit_->setDefaultText("3,3,2,4,4");
    contentPanel_->add(rhythmAdditiveEdit_);

    auto weightsLabel = tgui::Label::create("Weights (16 floats):");
    weightsLabel->setPosition(14, rhythmTop + 206);
    weightsLabel->setTextSize(11);
    weightsLabel->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(weightsLabel);

    rhythmWeightsEdit_ = tgui::EditBox::create();
    rhythmWeightsEdit_->setPosition(14, rhythmTop + 222);
    rhythmWeightsEdit_->setSize(leftColW, 22);
    rhythmWeightsEdit_->setDefaultText("1.0,0.28,0.45,0.33,0.7,0.22,0.4,0.26,0.88,0.24,0.5,0.3,0.76,0.2,0.42,0.24");
    contentPanel_->add(rhythmWeightsEdit_);

    auto lengthsLabel = tgui::Label::create("Lengths (16 floats):");
    lengthsLabel->setPosition(14, rhythmTop + 248);
    lengthsLabel->setTextSize(11);
    lengthsLabel->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(lengthsLabel);

    rhythmLengthsEdit_ = tgui::EditBox::create();
    rhythmLengthsEdit_->setPosition(14, rhythmTop + 264);
    rhythmLengthsEdit_->setSize(leftColW, 22);
    rhythmLengthsEdit_->setDefaultText("1.0,0.55,0.6,0.52,0.9,0.5,0.62,0.54,1.0,0.5,0.66,0.56,0.94,0.52,0.6,0.5");
    contentPanel_->add(rhythmLengthsEdit_);

    rhythmApplyBtn_ = tgui::Button::create("Apply Rhythm Pattern");
    rhythmApplyBtn_->setPosition(14, rhythmTop + 292);
    rhythmApplyBtn_->setSize(leftColW, 24);
    rhythmApplyBtn_->onPress([this] {
        if (onRhythmPatternApply && rhythmDivisiveEdit_ && rhythmAdditiveEdit_ && rhythmWeightsEdit_ && rhythmLengthsEdit_) {
            onRhythmPatternApply(rhythmDivisiveEdit_->getText().toStdString(),
                                 rhythmAdditiveEdit_->getText().toStdString(),
                                 rhythmWeightsEdit_->getText().toStdString(),
                                 rhythmLengthsEdit_->getText().toStdString());
        }
    });
    contentPanel_->add(rhythmApplyBtn_);

    rhythmStatusLabel_ = tgui::Label::create("Rhythm: ready");
    rhythmStatusLabel_->setPosition(14, rhythmTop + 320);
    rhythmStatusLabel_->setTextSize(11);
    rhythmStatusLabel_->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(rhythmStatusLabel_);

    auto binsLabel = tgui::Label::create("Network Bins (Audio=blue, Transient=orange)");
    binsLabel->setPosition(14, rhythmTop + 344);
    binsLabel->setTextSize(11);
    binsLabel->getRenderer()->setTextColor(TEXT_DIM);
    contentPanel_->add(binsLabel);

    const float scopeH = 220.0f;
    networkBinsCanvas_ = tgui::CanvasSFML::create({static_cast<float>(leftColW), scopeH});
    networkBinsCanvas_->setPosition(14, static_cast<float>(rhythmTop + 362));
    contentPanel_->add(networkBinsCanvas_);

    const float contentHeight = static_cast<float>(rhythmTop + 362) + scopeH + 14.0f;
    contentPanel_->setContentSize({static_cast<float>(width), contentHeight});
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
    drawAudioMeter();
    drawPressureMeter();
    drawNetworkBins();
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

void AudioMidiControlWindow::setNetworkBins(const std::array<float, 16>& audioBins,
                                            const std::array<float, 16>& transientBins) {
    networkAudioBins_ = audioBins;
    networkTransientBins_ = transientBins;

    if (!networkBinsCanvas_)
        return;

    const std::size_t maxCols = static_cast<std::size_t>(std::max(16.0f, networkBinsCanvas_->getSize().x));
    networkAudioHistory_.push_back(networkAudioBins_);
    networkTransientHistory_.push_back(networkTransientBins_);
    if (networkAudioHistory_.size() > maxCols)
        networkAudioHistory_.erase(networkAudioHistory_.begin());
    if (networkTransientHistory_.size() > maxCols)
        networkTransientHistory_.erase(networkTransientHistory_.begin());
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

void AudioMidiControlWindow::setRhythmUiState(bool enabled,
                                              const std::string& strategyName,
                                              float bpm,
                                              float gain,
                                              bool audibleTransients,
                                              const std::string& status) {
    if (rhythmToggleBtn_)
        rhythmToggleBtn_->setText(enabled ? "Rhythm: On" : "Rhythm: Off");
    if (rhythmStrategyBtn_)
        rhythmStrategyBtn_->setText("Strategy: " + strategyName);
    if (rhythmBpmLabel_)
        rhythmBpmLabel_->setText("BPM: " + std::to_string(static_cast<int>(std::round(bpm))));
    if (rhythmGainLabel_) {
        int pct = static_cast<int>(std::round(gain * 100.0f));
        rhythmGainLabel_->setText("Pattern Gain: " + std::to_string(pct) + "%");
    }
    if (rhythmAudibleBtn_)
        rhythmAudibleBtn_->setText(audibleTransients ? "Audible Transients: On" : "Audible Transients: Off");
    if (rhythmStatusLabel_)
        rhythmStatusLabel_->setText("Rhythm: " + status);
}

void AudioMidiControlWindow::setRhythmPatternSpecs(const std::string& divisive,
                                                   const std::string& additive,
                                                   const std::string& weights,
                                                   const std::string& lengths) {
    if (rhythmDivisiveEdit_) rhythmDivisiveEdit_->setText(divisive);
    if (rhythmAdditiveEdit_) rhythmAdditiveEdit_->setText(additive);
    if (rhythmWeightsEdit_) rhythmWeightsEdit_->setText(weights);
    if (rhythmLengthsEdit_) rhythmLengthsEdit_->setText(lengths);
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
    const float barGap = 2.0f;

    for (int i = 0; i < 8; ++i) {
        float energy = std::clamp(audioBands_[i], 0.0f, 1.0f);
        float barH = energy * (height - 4.0f);
        float x = i * barW + barGap;
        float y = height - barH - 2.0f;

        sf::RectangleShape bar({barW - barGap * 2, barH});
        bar.setPosition({x, y});
        bar.setFillColor(sf::Color(100 + static_cast<int>(energy * 155), 80, 150));
        rt.draw(bar);
    }

    audioMeterCanvas_->display();
}

void AudioMidiControlWindow::drawPressureMeter() {
    if (!pressureMeterCanvas_) return;
    auto& rt = pressureMeterCanvas_->getRenderTexture();
    pressureMeterCanvas_->clear(tgui::Color(12, 12, 24));

    float width = pressureMeterCanvas_->getSize().x;
    float height = pressureMeterCanvas_->getSize().y;
    float barW = pressureNorm_ * width;

    sf::RectangleShape bar({barW, height});
    bar.setFillColor(sf::Color(100, 150, 220));
    rt.draw(bar);

    pressureMeterCanvas_->display();
}

void AudioMidiControlWindow::drawNetworkBins() {
    if (!networkBinsCanvas_)
        return;

    auto& rt = networkBinsCanvas_->getRenderTexture();
    networkBinsCanvas_->clear(tgui::Color(10, 10, 18));

    const float width = networkBinsCanvas_->getSize().x;
    const float height = networkBinsCanvas_->getSize().y;
    const float bandH = height / 16.0f;

    if (bandH <= 1.0f) {
        networkBinsCanvas_->display();
        return;
    }

    for (std::size_t i = 0; i < networkAudioHistory_.size(); ++i) {
        const float x = width - static_cast<float>(networkAudioHistory_.size() - i);
        if (x < 0.0f || x >= width)
            continue;

        const auto& aCol = networkAudioHistory_[i];
        const auto& tCol = networkTransientHistory_[i];
        for (int bin = 0; bin < 16; ++bin) {
            const float a = std::clamp(aCol[bin], 0.0f, 1.0f);
            const float t = std::clamp(tCol[bin], 0.0f, 1.0f);
            const float y = static_cast<float>(bin) * bandH;

            sf::RectangleShape bg({1.0f, bandH - 1.0f});
            bg.setPosition({x, y});
            bg.setFillColor(sf::Color(12, 14, 24));
            rt.draw(bg);

            sf::RectangleShape audioPx({1.0f, (bandH - 1.0f) * a});
            audioPx.setPosition({x, y + (bandH - 1.0f) * (1.0f - a)});
            audioPx.setFillColor(sf::Color(70, 130, 240, 200));
            rt.draw(audioPx);

            sf::RectangleShape transientPx({1.0f, (bandH - 1.0f) * t});
            transientPx.setPosition({x, y + (bandH - 1.0f) * (1.0f - t)});
            transientPx.setFillColor(sf::Color(255, 150, 40, 180));
            rt.draw(transientPx);
        }
    }

    networkBinsCanvas_->display();
}
