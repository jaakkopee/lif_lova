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

    // ── Pressure meter label ─────────────────────────────────────────────────────
    pressureLabel_ = tgui::Label::create("Pressure CH10: 0%");
    pressureLabel_->setPosition(14, 12);
    pressureLabel_->setTextSize(12);
    pressureLabel_->getRenderer()->setTextColor(tgui::Color(170, 220, 255));
    gui_.add(pressureLabel_);

    pressureMeterCanvas_ = tgui::CanvasSFML::create({static_cast<float>(leftColW), 14.0f});
    pressureMeterCanvas_->setPosition(14, 30);
    gui_.add(pressureMeterCanvas_);

    // ── Audio meter label ─────────────────────────────────────────────────────
    auto audioLabel = tgui::Label::create("Audio Spectrum:");
    audioLabel->setPosition(14, 50);
    audioLabel->setTextSize(12);
    audioLabel->getRenderer()->setTextColor(TEXT_DIM);
    gui_.add(audioLabel);

    rhythmToggleBtn_ = tgui::Button::create("Rhythm Off");
    rhythmToggleBtn_->setPosition(108, 48);
    rhythmToggleBtn_->setSize(76, 18);
    rhythmToggleBtn_->setTextSize(11);
    rhythmToggleBtn_->onPress([this] {
        if (onRhythmToggle)
            onRhythmToggle();
    });
    gui_.add(rhythmToggleBtn_);

    rhythmMixBtn_ = tgui::Button::create("Mix: Audio");
    rhythmMixBtn_->setPosition(188, 48);
    rhythmMixBtn_->setSize(74, 18);
    rhythmMixBtn_->setTextSize(11);
    rhythmMixBtn_->onPress([this] {
        if (onRhythmMixCycle)
            onRhythmMixCycle();
    });
    gui_.add(rhythmMixBtn_);

    rhythmPatternBtn_ = tgui::Button::create("Pat: Metronome");
    rhythmPatternBtn_->setPosition(108, 88);
    rhythmPatternBtn_->setSize(154, 18);
    rhythmPatternBtn_->setTextSize(11);
    rhythmPatternBtn_->onPress([this] {
        if (onRhythmPatternCycle)
            onRhythmPatternCycle();
    });
    gui_.add(rhythmPatternBtn_);

    audioMeterCanvas_ = tgui::CanvasSFML::create({static_cast<float>(leftColW), 40.0f});
    audioMeterCanvas_->setPosition(14, 68);
    gui_.add(audioMeterCanvas_);

    // ── LIF MIDI output controls ─────────────────────────────────────────
    auto lifMidiLabel = tgui::Label::create("LIF MIDI Control");
    lifMidiLabel->setPosition(14, 114);
    lifMidiLabel->setTextSize(13);
    lifMidiLabel->getRenderer()->setTextColor(tgui::Color(255, 210, 80));
    gui_.add(lifMidiLabel);

    lifMidiToggleBtn_ = tgui::Button::create("Enable LIF MIDI (M)");
    lifMidiToggleBtn_->setPosition(14, 134);
    lifMidiToggleBtn_->setSize(leftColW, 26);
    lifMidiToggleBtn_->onPress([this] {
        if (onLIFMidiToggle) onLIFMidiToggle();
    });
    gui_.add(lifMidiToggleBtn_);

    lifMidiStatusLabel_ = tgui::Label::create("LIF MIDI: Off");
    lifMidiStatusLabel_->setPosition(14, 162);
    lifMidiStatusLabel_->setTextSize(12);
    lifMidiStatusLabel_->getRenderer()->setTextColor(TEXT_DIM);
    gui_.add(lifMidiStatusLabel_);

    lifMidiStyleBtn_ = tgui::Button::create("LIF MIDI Style: Pop");
    lifMidiStyleBtn_->setPosition(14, 182);
    lifMidiStyleBtn_->setSize(leftColW, 22);
    lifMidiStyleBtn_->onPress([this] {
        if (onLIFMidiStyleCycle)
            onLIFMidiStyleCycle();
    });
    gui_.add(lifMidiStyleBtn_);

    lifMidiModeBtn_ = tgui::Button::create("LIF MIDI Mode: Dorian");
    lifMidiModeBtn_->setPosition(14, 208);
    lifMidiModeBtn_->setSize(leftColW, 22);
    lifMidiModeBtn_->onPress([this] {
        if (onLIFMidiModeCycle)
            onLIFMidiModeCycle();
    });
    gui_.add(lifMidiModeBtn_);

    lifMidiModeEditLabel_ = tgui::Label::create("Mode Edit: n/a");
    lifMidiModeEditLabel_->setPosition(14, 258);
    lifMidiModeEditLabel_->setTextSize(11);
    lifMidiModeEditLabel_->getRenderer()->setTextColor(TEXT_DIM);
    gui_.add(lifMidiModeEditLabel_);

    lifMidiModeDegDownBtn_ = tgui::Button::create("Deg-");
    lifMidiModeDegDownBtn_->setPosition(14, 276);
    lifMidiModeDegDownBtn_->setSize(42, 20);
    lifMidiModeDegDownBtn_->onPress([this] {
        if (onLIFMidiModeEditDegreeNudge)
            onLIFMidiModeEditDegreeNudge(-1);
    });
    gui_.add(lifMidiModeDegDownBtn_);

    lifMidiModeDegUpBtn_ = tgui::Button::create("Deg+");
    lifMidiModeDegUpBtn_->setPosition(60, 276);
    lifMidiModeDegUpBtn_->setSize(42, 20);
    lifMidiModeDegUpBtn_->onPress([this] {
        if (onLIFMidiModeEditDegreeNudge)
            onLIFMidiModeEditDegreeNudge(1);
    });
    gui_.add(lifMidiModeDegUpBtn_);

    lifMidiModeSemiDownBtn_ = tgui::Button::create("Sem-");
    lifMidiModeSemiDownBtn_->setPosition(106, 276);
    lifMidiModeSemiDownBtn_->setSize(44, 20);
    lifMidiModeSemiDownBtn_->onPress([this] {
        if (onLIFMidiModeEditSemitoneNudge)
            onLIFMidiModeEditSemitoneNudge(-1);
    });
    gui_.add(lifMidiModeSemiDownBtn_);

    lifMidiModeSemiUpBtn_ = tgui::Button::create("Sem+");
    lifMidiModeSemiUpBtn_->setPosition(154, 276);
    lifMidiModeSemiUpBtn_->setSize(44, 20);
    lifMidiModeSemiUpBtn_->onPress([this] {
        if (onLIFMidiModeEditSemitoneNudge)
            onLIFMidiModeEditSemitoneNudge(1);
    });
    gui_.add(lifMidiModeSemiUpBtn_);

    lifMidiModeResetBtn_ = tgui::Button::create("Reset");
    lifMidiModeResetBtn_->setPosition(202, 276);
    lifMidiModeResetBtn_->setSize(54, 20);
    lifMidiModeResetBtn_->onPress([this] {
        if (onLIFMidiModeEditReset)
            onLIFMidiModeEditReset();
    });
    gui_.add(lifMidiModeResetBtn_);

    // ── LIF Tone volume controls ────────────────────────────────────────
    auto lifToneLabel = tgui::Label::create("LIF Tone Synth");
    lifToneLabel->setPosition(14, 302);
    lifToneLabel->setTextSize(13);
    lifToneLabel->getRenderer()->setTextColor(tgui::Color(255, 210, 80));
    gui_.add(lifToneLabel);

    lifToneToggleBtn_ = tgui::Button::create("Enable LIF Tone (K)");
    lifToneToggleBtn_->setPosition(14, 322);
    lifToneToggleBtn_->setSize(leftColW, 22);
    lifToneToggleBtn_->onPress([this] {
        if (onLIFToneToggle)
            onLIFToneToggle();
    });
    gui_.add(lifToneToggleBtn_);

    lifToneVolLabel_ = tgui::Label::create("Tone Vol: 85%");
    lifToneVolLabel_->setPosition(14, 348);
    lifToneVolLabel_->setTextSize(11);
    lifToneVolLabel_->getRenderer()->setTextColor(TEXT_DIM);
    gui_.add(lifToneVolLabel_);

    lifToneVolDownBtn_ = tgui::Button::create("Vol -");
    lifToneVolDownBtn_->setPosition(14, 366);
    lifToneVolDownBtn_->setSize(70, 22);
    lifToneVolDownBtn_->onPress([this] {
        if (onLIFToneVolumeNudge)
            onLIFToneVolumeNudge(-5.0f);
    });
    gui_.add(lifToneVolDownBtn_);

    lifToneVolUpBtn_ = tgui::Button::create("Vol +");
    lifToneVolUpBtn_->setPosition(90, 366);
    lifToneVolUpBtn_->setSize(70, 22);
    lifToneVolUpBtn_->onPress([this] {
        if (onLIFToneVolumeNudge)
            onLIFToneVolumeNudge(5.0f);
    });
    gui_.add(lifToneVolUpBtn_);

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
    gui_.add(audioBypPassBtn_);

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
    gui_.add(midiSettingsBtn_);

    midiSettingsPanel_ = tgui::Panel::create({static_cast<float>(leftColW), 600.0f});
    midiSettingsPanel_->setPosition(14, 464);
    midiSettingsPanel_->getRenderer()->setBackgroundColor(tgui::Color(24, 24, 32));
    midiSettingsPanel_->setVisible(false);
    gui_.add(midiSettingsPanel_);

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

    auto rhythmLabel = tgui::Label::create("Rhythm:");
    rhythmLabel->setPosition(6, 188);
    rhythmLabel->setTextSize(11);
    rhythmLabel->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(rhythmLabel);
    std::cout << "[DEBUG] Added Rhythm label to MIDI Settings panel" << std::endl;

    rhythmBpmDownBtn_ = tgui::Button::create("BPM-");
    rhythmBpmDownBtn_->setPosition(6, 206);
    rhythmBpmDownBtn_->setSize(32, 22);
    rhythmBpmDownBtn_->onPress([this] {
        if (onRhythmBpmNudge)
            onRhythmBpmNudge(-2.0f);
    });
    midiSettingsPanel_->add(rhythmBpmDownBtn_);

    rhythmBpmUpBtn_ = tgui::Button::create("BPM+");
    rhythmBpmUpBtn_->setPosition(42, 206);
    rhythmBpmUpBtn_->setSize(32, 22);
    rhythmBpmUpBtn_->onPress([this] {
        if (onRhythmBpmNudge)
            onRhythmBpmNudge(2.0f);
    });
    midiSettingsPanel_->add(rhythmBpmUpBtn_);

    rhythmIntDownBtn_ = tgui::Button::create("Int-");
    rhythmIntDownBtn_->setPosition(78, 206);
    rhythmIntDownBtn_->setSize(32, 22);
    rhythmIntDownBtn_->onPress([this] {
        if (onRhythmIntensityNudge)
            onRhythmIntensityNudge(-0.05f);
    });
    midiSettingsPanel_->add(rhythmIntDownBtn_);

    rhythmIntUpBtn_ = tgui::Button::create("Int+");
    rhythmIntUpBtn_->setPosition(114, 206);
    rhythmIntUpBtn_->setSize(32, 22);
    rhythmIntUpBtn_->onPress([this] {
        if (onRhythmIntensityNudge)
            onRhythmIntensityNudge(0.05f);
    });
    midiSettingsPanel_->add(rhythmIntUpBtn_);

    auto laneLabel = tgui::Label::create("Lanes: toggle / pulse / gain");
    laneLabel->setPosition(6, 160);
    laneLabel->setTextSize(11);
    laneLabel->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(laneLabel);

    // Improved vertical stack layout for each lane
    const int laneCount = 3;
    const int laneW = 160;
    const int laneH = 36;
    const int laneSpacing = 24;
    const int baseY = 190;
    for (int i = 0; i < laneCount; ++i) {
        int laneX = 6 + i * (laneW + laneSpacing);
        int y = baseY;

        rhythmLaneBtns_[i] = tgui::Button::create("Lane");
        rhythmLaneBtns_[i]->setPosition(laneX, y);
        rhythmLaneBtns_[i]->setSize(laneW, laneH);
        rhythmLaneBtns_[i]->setTextSize(13);
        rhythmLaneBtns_[i]->onPress([this, i] {
            if (onRhythmLaneToggle)
                onRhythmLaneToggle(i);
        });
        midiSettingsPanel_->add(rhythmLaneBtns_[i]);
        y += laneH + 4;

        rhythmLanePulseDownBtns_[i] = tgui::Button::create("Pulse -");
        rhythmLanePulseDownBtns_[i]->setPosition(laneX, y);
        rhythmLanePulseDownBtns_[i]->setSize(laneW/2-2, laneH-6);
        rhythmLanePulseDownBtns_[i]->setTextSize(12);
        rhythmLanePulseDownBtns_[i]->onPress([this, i] {
            if (onRhythmLanePulseNudge)
                onRhythmLanePulseNudge(i, -1);
        });
        midiSettingsPanel_->add(rhythmLanePulseDownBtns_[i]);

        rhythmLanePulseUpBtns_[i] = tgui::Button::create("Pulse +");
        rhythmLanePulseUpBtns_[i]->setPosition(laneX + laneW/2 + 2, y);
        rhythmLanePulseUpBtns_[i]->setSize(laneW/2-2, laneH-6);
        rhythmLanePulseUpBtns_[i]->setTextSize(12);
        rhythmLanePulseUpBtns_[i]->onPress([this, i] {
            if (onRhythmLanePulseNudge)
                onRhythmLanePulseNudge(i, 1);
        });
        midiSettingsPanel_->add(rhythmLanePulseUpBtns_[i]);
        y += laneH - 2;

        rhythmLaneGainDownBtns_[i] = tgui::Button::create("Gain -");
        rhythmLaneGainDownBtns_[i]->setPosition(laneX, y);
        rhythmLaneGainDownBtns_[i]->setSize(laneW/2-2, laneH-6);
        rhythmLaneGainDownBtns_[i]->setTextSize(12);
        rhythmLaneGainDownBtns_[i]->onPress([this, i] {
            if (onRhythmLaneGainNudge)
                onRhythmLaneGainNudge(i, -0.05f);
        });
        midiSettingsPanel_->add(rhythmLaneGainDownBtns_[i]);

        rhythmLaneGainUpBtns_[i] = tgui::Button::create("Gain +");
        rhythmLaneGainUpBtns_[i]->setPosition(laneX + laneW/2 + 2, y);
        rhythmLaneGainUpBtns_[i]->setSize(laneW/2-2, laneH-6);
        rhythmLaneGainUpBtns_[i]->setTextSize(12);
        rhythmLaneGainUpBtns_[i]->onPress([this, i] {
            if (onRhythmLaneGainNudge)
                onRhythmLaneGainNudge(i, 0.05f);
        });
        midiSettingsPanel_->add(rhythmLaneGainUpBtns_[i]);
    }

    lifMidiRangeLabel_ = tgui::Label::create("Range: 36-96");
    lifMidiRangeLabel_->setPosition(6, 324);
    lifMidiRangeLabel_->setTextSize(11);
    lifMidiRangeLabel_->getRenderer()->setTextColor(TEXT_DIM);
    midiSettingsPanel_->add(lifMidiRangeLabel_);

    lifMidiRangeMinDownBtn_ = tgui::Button::create("Lo-");
    lifMidiRangeMinDownBtn_->setPosition(6, 342);
    lifMidiRangeMinDownBtn_->setSize(32, 22);
    lifMidiRangeMinDownBtn_->onPress([this] {
        if (onLIFMidiRangeMinNudge)
            onLIFMidiRangeMinNudge(-1);
    });
    midiSettingsPanel_->add(lifMidiRangeMinDownBtn_);

    lifMidiRangeMinUpBtn_ = tgui::Button::create("Lo+");
    lifMidiRangeMinUpBtn_->setPosition(42, 342);
    lifMidiRangeMinUpBtn_->setSize(32, 22);
    lifMidiRangeMinUpBtn_->onPress([this] {
        if (onLIFMidiRangeMinNudge)
            onLIFMidiRangeMinNudge(1);
    });
    midiSettingsPanel_->add(lifMidiRangeMinUpBtn_);

    lifMidiRangeMaxDownBtn_ = tgui::Button::create("Hi-");
    lifMidiRangeMaxDownBtn_->setPosition(78, 342);
    lifMidiRangeMaxDownBtn_->setSize(32, 22);
    lifMidiRangeMaxDownBtn_->onPress([this] {
        if (onLIFMidiRangeMaxNudge)
            onLIFMidiRangeMaxNudge(-1);
    });
    midiSettingsPanel_->add(lifMidiRangeMaxDownBtn_);

    lifMidiRangeMaxUpBtn_ = tgui::Button::create("Hi+");
    lifMidiRangeMaxUpBtn_->setPosition(114, 342);
    lifMidiRangeMaxUpBtn_->setSize(32, 22);
    lifMidiRangeMaxUpBtn_->onPress([this] {
        if (onLIFMidiRangeMaxNudge)
            onLIFMidiRangeMaxNudge(1);
    });
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
    drawAudioMeter();
    drawPressureMeter();
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
