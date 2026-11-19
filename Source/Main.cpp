#include "Session.h"
#include "UpdateCheck.h"
#include "PlanGrid.h"
#include "Smoke.h"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr auto background = 0xff10182a;
constexpr auto accent = 0xff83f4ce;
const juce::Colour ink{0xffedf3ff};
const juce::Colour muted{0xffa6b7d6};
const juce::Colour line{0xff354562};

class CaptureTheme final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                              bool hover, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const auto fill = button.getToggleState() ? juce::Colour(0xff354e76) : colour;
        g.setColour(fill.withAlpha(button.isEnabled() ? 1.0f : 0.42f).brighter(down ? 0.18f : hover ? 0.10f : 0.0f));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(button.hasKeyboardFocus(true) ? juce::Colour(accent) : line.withAlpha(0.75f));
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
    }
};

class CaptureView final : public juce::Component, private juce::Timer, private juce::ListBoxModel
{
public:
    CaptureView()
        : audioSelector(devices, 0, 2, 0, 2, false, false, false, true), list("Capture plan", this), progressBar(progress)
    {
        theme.setColour(juce::Label::textColourId, ink);
        theme.setColour(juce::TextEditor::textColourId, ink);
        theme.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff111c30));
        theme.setColour(juce::TextEditor::outlineColourId, line);
        theme.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(accent));
        theme.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff24324b));
        theme.setColour(juce::TextButton::textColourOffId, ink);
        theme.setColour(juce::TextButton::textColourOnId, ink);
        theme.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff24324b));
        theme.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(accent));
        setLookAndFeel(&theme);
        planViewport.setViewedComponent(&planContent, false);
        audioViewport.setViewedComponent(&audioSelector, false);
        planViewport.setScrollBarsShown(true, false);
        audioViewport.setScrollBarsShown(true, false);
        addChildComponent(updateButton);
        updateButton.setColour(juce::TextButton::textColourOffId, juce::Colour(accent));
        updateButton.setTooltip("Open the latest Capture release on GitHub");
        updateButton.onClick = [this] { updates.openReleasePage(); };
        for (auto* component : planFields())
            planContent.addAndMakeVisible(component);
        for (auto* component : std::initializer_list<juce::Component*>{
                 &title, &subtitle, &wiringButton, &openButton, &planViewport, &audioViewport,
                 &welcomeLead, &microphoneHelp, &loopbackHelp, &sendHelp,
                 &sessionTitle, &routingLabel, &loopInput, &ampOutputLabel, &ampOutput, &deviceSummary, &modeLabel, &captureMode, &sendLabel, &sendLevel,
                 &selectedTake, &list, &nextButton, &backButton, &cancelButton, &retryLoudButton, &retryQuietButton, &normalise, &statusLabel, &progressBar})
            addAndMakeVisible(component);

        title.setText("ParametricNAM Capture", juce::dontSendNotification);
        title.setFont(juce::FontOptions(29.0f, juce::Font::bold));
        selectedTake.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        selectedTake.setColour(juce::Label::textColourId, juce::Colour(accent));

        subtitle.setColour(juce::Label::textColourId, ink);
        subtitle.setFont(juce::FontOptions(23.0f, juce::Font::bold));
        welcomeLead.setText("Use one audio interface at 48 kHz. Choose a capture mode.", juce::dontSendNotification);
        welcomeLead.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        welcomeLead.setColour(juce::Label::textColourId, ink);
        microphoneHelp.setText("Microphone only | 1 input, 1 output\nConnect the microphone to an input and send one output to the amp via a reamp box.", juce::dontSendNotification);
        loopbackHelp.setText("With loopback | 2 inputs, 1 or 2 outputs\nAdd a clean return of the send signal on a line input. Use a second output or internal loopback.", juce::dontSendNotification);
        sendHelp.setText("With loopback, Capture measures the actual send. In Microphone only, it uses the stimulus and checks timing through the microphone.", juce::dontSendNotification);
        for (auto* label : {&microphoneHelp, &loopbackHelp, &sendHelp})
        {
            label->setColour(juce::Label::textColourId, muted);
            label->setFont(juce::FontOptions(15.0f));
        }
        for (auto* label : {&statusLabel, &stimulusName, &sessionTitle})
            label->setColour(juce::Label::textColourId, juce::Colour(accent));
        sessionTitle.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        nameLabel.setText("Amplifier name", juce::dontSendNotification);
        controlsLabel.setText("Controls", juce::dontSendNotification);
        prioritiesLabel.setText("Priority takes   |   Exact value, range (8:10) or blank", juce::dontSendNotification);
        for (auto* label : {&controlsLabel, &prioritiesLabel})
            label->setFont(juce::FontOptions(16.0f, juce::Font::bold));
        nextButton.setColour(juce::TextButton::buttonColourId, juce::Colour(accent));
        nextButton.setColour(juce::TextButton::textColourOffId, juce::Colour(background));
        deviceSummary.setColour(juce::Label::textColourId, muted);
        countsLabel.setText("Total takes", juce::dontSendNotification);
        modeLabel.setText("Capture mode", juce::dontSendNotification);
        captureMode.addItem("With loopback", 1);
        captureMode.addItem("Microphone only", 2);
        captureMode.setSelectedId(1, juce::dontSendNotification);
        captureMode.onChange = [this]
        {
            levelCheck = LevelCheck::loud;
            refreshAudioChannels();
            resized();
        };
        routingLabel.setText("Loopback input", juce::dontSendNotification);
        ampOutputLabel.setText("Amp output", juce::dontSendNotification);
        deviceSummary.setMinimumHorizontalScale(0.92f);
        sendLabel.setText("Send dB", juce::dontSendNotification);

        name.setTextToShowWhenEmpty("e.g. Tube Combo", juce::Colours::grey);
        name.setExplicitFocusOrder(1);
        controls.setHeadings({"ID", "Name", "Min", "Max", "Dial step", "Model min", "Model max"});
        controls.addRow({"gain", "Gain", "0", "10", "0.5", "0", "1"});
        controls.addRow({"treble", "Treble", "0", "10", "0.5", "0", "1"});
        controls.addRow({"mid", "Middle", "0", "10", "0.5", "0", "1"});
        controls.addRow({"bass", "Bass", "0", "10", "0.5", "0", "1"});
        priorities.placeholders = true;
        stepHint.setText("Dial step is the distance between automatic positions. E.g. 0.5 gives 2, 2.5, 3...", juce::dontSendNotification);
        stepHint.setColour(juce::Label::textColourId, muted);
        budgetSummary.setColour(juce::Label::textColourId, muted);
        priorities.onChange = [this] { updateBudget(); };
        controls.onChange = [this] { updatePriorityColumns(); };
        controls.onRemove = [this](int row) { priorities.removeColumn(row + 1); };
        updatePriorityColumns();
        addControl.onClick = [this]
        {
            int index = controls.size() + 1;
            for (int row = 0; row < controls.size();)
            {
                if (controls.value(row, 0) == "control" + juce::String(index))
                {
                    ++index;
                    row = 0;
                }
                else
                    ++row;
            }
            controls.addRow({"control" + juce::String(index), "Control " + juce::String(index),
                             "0", "10", "0.5", "0", "1"});
        };
        addPriority.onClick = [this] { priorities.addRow({"1"}); };
        takeCount.setInputRestrictions(4, "0123456789");
        takeCount.onTextChange = [this] { customTakeCount = true; updateBudget(); };
        updateSuggestedCount();
        sendLevel.setInputRestrictions(6, "-0123456789.");
        sendLevel.setText("0");
        sendLevel.onReturnKey = sendLevel.onFocusLost = [this] { run([this] { applySend(); }); };
        loopInput.onChange = [this] { levelCheck = LevelCheck::loud; refreshAudioChannels(); };
        ampOutput.onChange = [this] { levelCheck = LevelCheck::loud; refreshAudioChannels(); };

        const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
#if JUCE_MAC
        const auto resources = executable.getParentDirectory().getParentDirectory().getChildFile("Resources");
#else
        const auto resources = executable.getParentDirectory();
#endif
        try
        {
            stimulus = capture::readWav(resources.getChildFile("CaptureStimulus.wav"));
            stimulusName.setText("Standard stimulus  /  90 s", juce::dontSendNotification);
        }
        catch (const std::exception&)
        {
            stimulusName.setText("Standard WAV unavailable. Import a stimulus.", juce::dontSendNotification);
        }
        list.setRowHeight(34);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff202834));
        list.setMultipleSelectionEnabled(false);
        progressBar.setPercentageDisplay(false);
        progressBar.setTitle("Recording progress");

        wiringButton.onClick = []
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Connect your amplifier",
                "1. Send: interface line output to a reamp box, then to the amp input.\n"
                "2. Loopback: a second output can go straight to a line input. With one output, split the line signal or use the interface's internal loopback.\n"
                "3. Mic: place a microphone at the speaker and connect it to a mic input.\n\n"
                "Keep the speaker or rated load connected. Never connect a speaker output to the interface.\n\n"
                "In With loopback mode, enable two inputs and one or two outputs. For Microphone only, enable one mic input and one amp output. Capture sets 48 kHz. "
                "Set the amp to its loudest planned setting and calibrate. Adjust gains for headroom, then keep them fixed.",
                "OK");
        };
        stimulusButton.onClick = [this] { chooseStimulus(); };
        openButton.onClick = [this] { openSession(); };
        nextButton.onClick = [this] { advance(); };
        backButton.onClick = [this] { goBack(); };
        cancelButton.onClick = [this] { cancel(); };
        retryLoudButton.onClick = [this] { recheckLevels(LevelCheck::loud); };
        retryQuietButton.onClick = [this] { recheckLevels(LevelCheck::quiet); };

        showStep(Step::welcome);
        setSize(960, 700);
        startTimerHz(15);

    }

    void initialiseAudio()
    {
        if (callbackInstalled || requestingAudio)
            return;
        requestingAudio = true;
        const auto safe = juce::Component::SafePointer<CaptureView>(this);
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [safe](bool granted)
        {
            if (safe == nullptr)
                return;
            safe->requestingAudio = false;
            if (!granted)
            {
                safe->status("Microphone access is required. Enable it in system settings.");
                return;
            }
            const auto error = safe->devices.initialise(2, 1, nullptr, true);
            safe->devices.addAudioCallback(&safe->recorder);
            safe->callbackInstalled = true;
            if (error.isNotEmpty())
                safe->status(error);
            else
                safe->forceSampleRate();
            safe->refreshAudioChannels();
        });
    }

    ~CaptureView() override
    {
        stopTimer();
        setLookAndFeel(nullptr);
        if (callbackInstalled)
            devices.removeAudioCallback(&recorder);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setGradientFill(juce::ColourGradient(juce::Colour(0xff253552), 0, 0, juce::Colour(background),
                                                     float(getWidth()), float(getHeight()), false));
        graphics.fillAll();
        graphics.setColour(line);
        drawSteps(graphics);
        if (step == Step::calibration || step == Step::record)
        {
            drawMeter(graphics, loopMeterArea, microphoneOnly() ? "Send" : "Loopback", loopMeter);
            drawMeter(graphics, micMeterArea, "Mic", micMeter);
        }
        if (step == Step::record && session && list.getSelectedRow() >= 0
            && list.getSelectedRow() < static_cast<int>(session->points.size()))
            drawTakeSettings(graphics);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(24);
        auto header = area.removeFromTop(44);
        wiringButton.setBounds(header.removeFromRight(112).withHeight(34));
        if (updateButton.isVisible())
        {
            header.removeFromRight(8);
            updateButton.setBounds(header.removeFromRight(124).withHeight(34));
        }
        title.setBounds(header);
        stepperArea = area.removeFromTop(54);
        area.removeFromTop(8);
        subtitle.setBounds(area.removeFromTop(38));
        area.removeFromTop(5);

        auto actions = area.removeFromBottom(42);
        nextButton.setBounds(actions.removeFromRight(164));
        actions.removeFromRight(10);
        cancelButton.setBounds(actions.removeFromRight(94));
        backButton.setBounds(actions.removeFromLeft(84));
        actions.removeFromLeft(10);
        openButton.setBounds(actions.removeFromLeft(128));
        if (step == Step::calibration)
        {
            actions = {backButton.getRight() + 10, backButton.getY(),
                       cancelButton.getX() - backButton.getRight() - 20, backButton.getHeight()};
            retryLoudButton.setBounds(actions.removeFromLeft(124));
            actions.removeFromLeft(8);
            retryQuietButton.setBounds(actions.removeFromLeft(124));
        }
        area.removeFromBottom(10);
        statusLabel.setBounds(area.removeFromBottom(32));
        area.removeFromBottom(6);
        progressBar.setBounds(area.removeFromBottom(7).reduced(2, 0));
        area.removeFromBottom(8);

        auto welcome = area.withTrimmedTop(6);
        welcomeLead.setBounds(welcome.removeFromTop(34));
        microphoneHelp.setBounds(welcome.removeFromTop(48));
        loopbackHelp.setBounds(welcome.removeFromTop(48));
        sendHelp.setBounds(welcome.removeFromTop(65));

        planViewport.setBounds(area);
        layoutPlan(std::max(1, area.getWidth() - planViewport.getScrollBarThickness()));

        auto audioArea = area;
        auto modeRow = audioArea.removeFromTop(34);
        modeLabel.setBounds(modeRow.removeFromLeft(118));
        captureMode.setBounds(modeRow.removeFromLeft(225));
        audioArea.removeFromTop(6);
        deviceSummary.setBounds(audioArea.removeFromBottom(34));
        auto roles = audioArea.removeFromBottom(32);
        if (!microphoneOnly())
        {
            routingLabel.setBounds(roles.removeFromLeft(114));
            loopInput.setBounds(roles.removeFromLeft(196));
            roles.removeFromLeft(16);
        }
        ampOutputLabel.setBounds(roles.removeFromLeft(88));
        ampOutput.setBounds(roles.removeFromLeft(196));
        audioViewport.setBounds(audioArea.withTrimmedBottom(8));
        audioSelector.setSize(std::max(1, audioViewport.getWidth() - audioViewport.getScrollBarThickness()), 480);

        sessionTitle.setBounds(area.removeFromTop(40));
        auto meters = area.removeFromTop(36);
        sendLabel.setBounds(meters.removeFromLeft(80));
        sendLevel.setBounds(meters.removeFromLeft(72));
        meters.removeFromLeft(12);
        loopMeterArea = meters.removeFromLeft(meters.getWidth() / 2).reduced(8, 2);
        micMeterArea = meters.reduced(8, 2);
        selectedTake.setBounds(area.removeFromTop(40));
        normalise.setBounds(area.removeFromTop(33));
        list.setBounds(area.withTrimmedTop(6));
        takeSettingsArea = {};
        if (step == Step::record)
        {
            takeSettingsArea = normalise.getBounds().withHeight(56);
            area.removeFromTop(29);
            list.setBounds(area);
        }
    }

private:
    enum class Step { welcome, audio, plan, calibration, record, exportData, done };
    static constexpr std::array<const char*, 6> stepNames{"Start", "Audio", "Take plan", "Levels", "Record", "Export"};

    void drawSteps(juce::Graphics& g)
    {
        const int active = static_cast<int>(step);
        const float width = float(stepperArea.getWidth()) / float(stepNames.size());
        const float y = float(stepperArea.getY() + 17);
        const auto green = juce::Colour(accent);
        for (int i = 1; i < static_cast<int>(stepNames.size()); ++i)
        {
            const float x = float(stepperArea.getX()) + (float(i) - 0.5f) * width;
            g.setColour(i <= active ? green.withAlpha(0.65f) : line);
            g.drawLine(x + 12, y, x + width - 12, y, 1.0f);
        }
        for (int i = 0; i < static_cast<int>(stepNames.size()); ++i)
        {
            const float x = float(stepperArea.getX()) + (float(i) + 0.5f) * width;
            const auto dot = juce::Rectangle<float>(22.0f, 22.0f).withCentre({x, y});
            const bool reached = i <= active;
            g.setColour(i == active ? green : i < active ? juce::Colour(0xff244d48) : juce::Colour(background));
            g.fillEllipse(dot);
            g.setColour(reached ? green : line);
            g.drawEllipse(dot, 1.0f);
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            if (i < active)
            {
                juce::Path tick;
                tick.startNewSubPath(x - 4, y);
                tick.lineTo(x - 1, y + 3);
                tick.lineTo(x + 5, y - 3);
                g.strokePath(tick, juce::PathStrokeType(1.5f));
            }
            else
            {
                g.setColour(i == active ? juce::Colour(background) : muted);
                g.drawText(juce::String(i + 1), dot, juce::Justification::centred);
            }
            g.setColour(i == active ? ink : muted);
            g.drawFittedText(stepNames[size_t(i)], int(x - width / 2), int(y + 14), int(width), 20,
                             juce::Justification::centred, 1);
        }
    }
    enum class LevelCheck { loud, quiet, complete };

    std::array<juce::Component*, 14> planFields()
    {
        return {&nameLabel, &name, &controlsLabel, &controls, &stepHint, &addControl,
                &prioritiesLabel, &priorities, &addPriority, &countsLabel, &takeCount,
                &budgetSummary, &stimulusButton, &stimulusName};
    }

    void layoutPlan(int width)
    {
        planContent.setSize(width, 490);
        auto area = planContent.getLocalBounds().withTrimmedRight(6);
        nameLabel.setBounds(area.removeFromTop(22));
        name.setBounds(area.removeFromTop(30));
        area.removeFromTop(8);
        auto header = area.removeFromTop(30);
        addControl.setBounds(header.removeFromRight(110));
        controlsLabel.setBounds(header);
        area.removeFromTop(4);
        controls.setBounds(area.removeFromTop(156));
        stepHint.setBounds(area.removeFromTop(23));
        header = area.removeFromTop(30);
        addPriority.setBounds(header.removeFromRight(110));
        prioritiesLabel.setBounds(header);
        area.removeFromTop(4);
        priorities.setBounds(area.removeFromTop(83));
        area.removeFromTop(8);
        countsLabel.setBounds(area.removeFromTop(22));
        auto counts = area.removeFromTop(30);
        takeCount.setBounds(counts.removeFromLeft(134).withTrimmedRight(14));
        budgetSummary.setBounds(counts.withTrimmedLeft(12));
        area.removeFromTop(8);
        auto stimulusRow = area.removeFromTop(32);
        stimulusButton.setBounds(stimulusRow.removeFromLeft(140));
        stimulusName.setBounds(stimulusRow.reduced(8, 0));
    }

    void drawTakeSettings(juce::Graphics& g)
    {
        const auto& point = session->points[static_cast<size_t>(list.getSelectedRow())];
        auto area = takeSettingsArea;
        const auto width = area.getWidth() / static_cast<int>(session->controls.size());
        for (size_t i = 0; i < session->controls.size(); ++i)
        {
            auto card = area.removeFromLeft(width).withTrimmedRight(6);
            g.setColour(juce::Colour(0xff182238));
            g.fillRoundedRectangle(card.toFloat().withTrimmedBottom(5), 7.0f);
            card = card.reduced(7, 3);
            g.setColour(muted);
            g.setFont(12.0f);
            g.drawFittedText(session->controls[i].label, card.removeFromTop(18), juce::Justification::centred, 1);
            g.setColour(juce::Colour(accent));
            g.setFont(juce::FontOptions(19.0f, juce::Font::bold));
            const auto value = juce::String(point.dials[i], 3).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
            g.drawFittedText(value, card, juce::Justification::centredTop, 1);
        }
    }

    void drawMeter(juce::Graphics& g, juce::Rectangle<int> meter, const juce::String& label, float peak)
    {
        const auto db = juce::Decibels::gainToDecibels(peak, -60.0f);
        auto heading = meter.removeFromTop(20);
        g.setColour(muted);
        g.setFont(12.0f);
        g.drawFittedText(label, heading.removeFromLeft(heading.getWidth() / 2), juce::Justification::centredLeft, 1);
        const auto reading = peak > 0.0f ? juce::String(db, 1) + " dB" : juce::String("-");
        g.drawFittedText(reading, heading, juce::Justification::centredRight, 1);
        const auto track = meter.withHeight(5).toFloat();
        g.setColour(line);
        g.fillRoundedRectangle(track, 2.5f);
        g.setColour(db >= -1.0f ? juce::Colour(0xffff957e) : juce::Colour(accent));
        g.fillRoundedRectangle(track.withWidth(track.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f)), 2.5f);
    }

    void status(const juce::String& text, bool problem = false)
    {
        statusLabel.setColour(juce::Label::textColourId, problem ? juce::Colour(0xffffad96) : juce::Colour(accent));
        statusLabel.setText(text, juce::dontSendNotification);
    }
    void run(const std::function<void()>& operation)
    {
        try { operation(); }
        catch (const std::exception& error) { status(juce::String::fromUTF8(error.what()), true); }
        update();
    }

    void showStep(Step next)
    {
        step = next;
        const bool welcome = step == Step::welcome;
        const bool audio = step == Step::audio;
        for (auto* label : {&welcomeLead, &microphoneHelp, &loopbackHelp, &sendHelp})
            label->setVisible(welcome);
        const bool calibration = step == Step::calibration;
        const bool recording = step == Step::record;
        const bool exporting = step == Step::exportData;
        audioViewport.setVisible(audio);
        modeLabel.setVisible(audio);
        captureMode.setVisible(audio);
        routingLabel.setVisible(audio && !microphoneOnly());
        loopInput.setVisible(audio && !microphoneOnly());
        ampOutputLabel.setVisible(audio);
        ampOutput.setVisible(audio);
        deviceSummary.setVisible(audio);
        planViewport.setVisible(step == Step::plan);
        sessionTitle.setVisible(calibration || recording || exporting || step == Step::done);
        sendLabel.setVisible(calibration);
        sendLevel.setVisible(calibration);
        selectedTake.setVisible(calibration || recording || exporting || step == Step::done);
        list.setVisible(recording);
        normalise.setVisible(exporting);
        backButton.setVisible(!welcome);
        openButton.setVisible(welcome || audio);
        const juce::StringArray titles{
            "Before you start", "Set up audio", "Plan your takes", "Check your levels",
            "Record takes", "Export your dataset", "Capture complete"};
        subtitle.setText(titles[static_cast<int>(step)], juce::dontSendNotification);
        status("");
        if (audio)
        {
            initialiseAudio();
            refreshAudioChannels();
        }
        resized();
        update();
        repaint();
    }

    bool microphoneOnly() const { return captureMode.getSelectedId() == 2; }

    bool audioReady() const
    {
        const auto* device = devices.getCurrentAudioDevice();
        if (!callbackInstalled || !device || ampOutput.getSelectedId() == 0)
            return false;
        const int inputs = device->getActiveInputChannels().countNumberOfSetBits();
        const int outputs = device->getActiveOutputChannels().countNumberOfSetBits();
        return microphoneOnly() ? inputs == 1 && outputs == 1
                                : inputs == 2 && outputs >= 1 && outputs <= 2 && loopInput.getSelectedId() != 0;
    }

    bool forceSampleRate()
    {
        auto* device = devices.getCurrentAudioDevice();
        if (device == nullptr)
            return false;
        if (device->getCurrentSampleRate() == capture::sampleRate)
            return true;
        if (!device->getAvailableSampleRates().contains(capture::sampleRate))
        {
            status("This device does not offer 48 kHz. Choose a different device or set its driver to 48 kHz.");
            return false;
        }
        auto setup = devices.getAudioDeviceSetup();
        setup.sampleRate = capture::sampleRate;
        const auto error = devices.setAudioDeviceSetup(setup, true);
        if (error.isNotEmpty())
        {
            status("Could not set 48 kHz. " + error);
            return false;
        }
        device = devices.getCurrentAudioDevice();
        if (device == nullptr || device->getCurrentSampleRate() != capture::sampleRate)
        {
            status("The driver did not switch to 48 kHz. Set its sample rate in the driver settings.");
            return false;
        }
        return true;
    }

    void refreshAudioChannels()
    {
        auto* device = devices.getCurrentAudioDevice();
        juce::StringArray active, sends;
        if (device)
        {
            const auto names = device->getInputChannelNames();
            const auto enabled = device->getActiveInputChannels();
            for (int i = 0; i < names.size(); ++i)
                if (enabled[i])
                    active.add(names[i]);
            const auto outputs = device->getOutputChannelNames();
            const auto enabledOutputs = device->getActiveOutputChannels();
            for (int i = 0; i < outputs.size(); ++i)
                if (enabledOutputs[i])
                    sends.add(outputs[i]);
        }
        if (active != activeInputs)
        {
            const auto oldName = loopInput.getText();
            activeInputs = active;
            loopInput.clear(juce::dontSendNotification);
            for (int i = 0; i < active.size(); ++i)
                loopInput.addItem(active[i], i + 1);
            const auto restored = active.indexOf(oldName);
            if (restored >= 0)
                loopInput.setSelectedId(restored + 1, juce::dontSendNotification);
        }
        if (sends != activeOutputs)
        {
            const auto previous = ampOutput.getText();
            activeOutputs = sends;
            ampOutput.clear(juce::dontSendNotification);
            for (int i = 0; i < sends.size(); ++i)
                ampOutput.addItem(sends[i], i + 1);
            if (!sends.isEmpty())
                ampOutput.setSelectedId(std::max(0, sends.indexOf(previous)) + 1, juce::dontSendNotification);
        }
        const int loop = loopInput.getSelectedId();
        const int amp = ampOutput.getSelectedId();
        routingLabel.setVisible(step == Step::audio && !microphoneOnly());
        loopInput.setVisible(step == Step::audio && !microphoneOnly());
        if (audioReady() && microphoneOnly() && active.size() == 1 && sends.size() == 1)
        {
            deviceSummary.setText("Amp: " + sends[0] + "   |   Mic: " + active[0]
                                  + "   |   No loopback", juce::dontSendNotification);
        }
        else if (audioReady() && active.size() == 2 && amp <= sends.size())
        {
            const auto returnRoute = sends.size() == 2
                ? sends[2 - amp] + " -> " + active[loop - 1]
                : "internal or split -> " + active[loop - 1];
            deviceSummary.setText("Amp: " + sends[amp - 1] + "   |   Loopback: " + returnRoute
                                  + "   |   Mic: " + active[2 - loop], juce::dontSendNotification);
        }
        else
        {
            deviceSummary.setText(microphoneOnly() ? "Enable one mic input and one amp output above."
                : "Enable two inputs and one or two outputs above, then choose the loopback input.",
                                  juce::dontSendNotification);
        }
        update();
    }

    void advance()
    {
        if (pending)
            return;
        switch (step)
        {
            case Step::welcome: showStep(Step::audio); break;
            case Step::audio:
                if (audioReady() && forceSampleRate())
                {
                    refreshAudioChannels();
                    if (audioReady())
                    {
                        confirmedDeviceRevision = recorder.deviceRevision();
                        showStep(session ? (allRecorded() ? Step::exportData : Step::calibration) : Step::plan);
                    }
                }
                break;
            case Step::plan: createSession(); break;
            case Step::calibration:
                if (levelCheck == LevelCheck::complete)
                    showStep(Step::record);
                else
                    start(true);
                break;
            case Step::record: start(false); break;
            case Step::exportData:
                run([this]
                {
                    session->exportDataset(normalise.getToggleState());
                    showStep(Step::done);
                    status("Upload the dataset folder to Drive for Colab.");
                });
                break;
            case Step::done:
                session->folder.getChildFile("dataset").revealToUser();
                break;
        }
    }

    void goBack()
    {
        if (pending)
            return;
        if (step == Step::audio)
        {
            showStep(Step::welcome);
            return;
        }
        if (step == Step::done || step == Step::exportData)
        {
            showStep(levelCheck == LevelCheck::complete ? Step::record : Step::calibration);
            return;
        }
        if (step == Step::record)
        {
            levelCheck = LevelCheck::loud;
            showStep(Step::calibration);
            return;
        }
        levelCheck = LevelCheck::loud;
        showStep(Step::audio);
    }

    bool allRecorded() const
    {
        return session && session->completed() == static_cast<int>(session->points.size());
    }

    void updatePriorityColumns()
    {
        juce::StringArray headings{"Takes"};
        for (int row = 0; row < controls.size(); ++row)
        {
            const auto label = controls.value(row, 1);
            headings.add(label.isEmpty() ? "Control " + juce::String(row + 1) : label);
        }
        priorities.setHeadings(headings);
        addControl.setEnabled(controls.size() < 8);
        addPriority.setEnabled(controls.size() > 0);
        updateSuggestedCount();
    }

    void updateSuggestedCount()
    {
        constexpr std::array<int, 8> suggested{6, 16, 24, 40, 56, 72, 88, 104};
        if (!customTakeCount)
            takeCount.setText(juce::String(suggested[size_t(juce::jlimit(1, 8, controls.size()) - 1)]), false);
        updateBudget();
    }

    void updateBudget()
    {
        int reserved = 0;
        for (int row = 0; row < priorities.size(); ++row)
            reserved += juce::jlimit(0, 2000, priorities.value(row, 0).getIntValue());
        const int total = takeCount.getText().getIntValue();
        budgetSummary.setText(reserved > total ? "Priority takes exceed the total"
            : juce::String(reserved) + " priority + " + juce::String(total - reserved) + " automatic", juce::dontSendNotification);
    }

    void choose(const juce::String& titleText, const juce::String& pattern, int flags,
                std::function<void(CaptureView&, const juce::File&)> action)
    {
        chooser = std::make_unique<juce::FileChooser>(titleText, juce::File{}, pattern);
        chooser->launchAsync(flags, [safe = juce::Component::SafePointer<CaptureView>(this),
                                     action = std::move(action)](const juce::FileChooser& dialog)
        {
            if (safe != nullptr && dialog.getResult() != juce::File{})
                safe->run([&] { action(*safe, dialog.getResult()); });
        });
    }

    void chooseStimulus()
    {
        choose("Choose a mono 48 kHz WAV", "*.wav",
               juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
               [](CaptureView& view, const juce::File& file)
        {
            view.stimulus = capture::readWav(file);
            view.stimulusName.setText(file.getFileName(), juce::dontSendNotification);
        });
    }

    void createSession()
    {
        run([this]
        {
            if (stimulus.empty())
                throw std::runtime_error("Import a mono 48 kHz WAV before continuing.");
            auto settings = capture::parseControls(controls.text());
            auto prioritiesParsed = capture::parsePriorities(priorities.text("*"), settings);
            auto points = capture::plan(settings, prioritiesParsed, takeCount.getText().getIntValue());
            choose("Choose a new folder name for the capture session", "",
                   juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                   [settings = std::move(settings), points = std::move(points)](CaptureView& view, const juce::File& file)
            {
                view.setSession(capture::Session::create(file, view.name.getText(), settings, points, view.stimulus, 0.0, view.microphoneOnly()));
            });
        });
    }

    void openSession()
    {
        choose("Open session.json", "session.json",
               juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
               [](CaptureView& view, const juce::File& file)
        {
            view.setSession(capture::Session::open(file.getParentDirectory()), true);
        });
    }

    void setSession(capture::Session next, bool opened = false)
    {
        auto samples = next.stimulus();
        session = std::move(next);
        captureMode.setSelectedId(session->microphoneOnly ? 2 : 1, juce::dontSendNotification);
        sessionStimulus = std::move(samples);
        calibrationExcerpt = capture::calibrationWindow(sessionStimulus);
        sendLevel.setText(juce::String(session->sendDb, 1), false);
        levelCheck = LevelCheck::loud;
        loopMeter = micMeter = 0.0f;
        list.updateContent();
        selectNext();
        showStep(opened ? Step::audio : Step::calibration);
    }

    void applySend()
    {
        if (!session || pending)
            return;
        const auto db = capture::parseNumber(sendLevel.getText());
        if (db != session->sendDb)
        {
            session->setSendDb(db);
            levelCheck = LevelCheck::loud;
            status("Send level changed. Calibrate again.");
        }
        sendLevel.setText(juce::String(session->sendDb, 1), false);
    }

    void recheckLevels(LevelCheck check)
    {
        if (step != Step::calibration || pending)
            return;
        levelCheck = check;
        start(true);
    }

    void start(bool calibration)
    {
        run([&]
        {
            if (!session || pending || !callbackInstalled)
                throw std::runtime_error("Open a session and configure the audio device first.");
            if (!audioReady() || recorder.deviceRevision() != confirmedDeviceRevision
                || devices.getCurrentAudioDevice()->getCurrentSampleRate() != capture::sampleRate)
            {
                levelCheck = LevelCheck::loud;
                showStep(Step::audio);
                throw std::runtime_error("Audio routing changed. Set up the device again before recording.");
            }
            if (calibration)
                applySend();
            const auto row = list.getSelectedRow();
            if (!calibration && (levelCheck != LevelCheck::complete || row < 0))
                throw std::runtime_error("Calibrate and select a take first.");
            const int loop = microphoneOnly() ? -1 : loopInput.getSelectedId() - 1;
            const auto source = std::span<const float>(sessionStimulus);
            const auto playback = calibration ? source.subspan(calibrationExcerpt.start, calibrationExcerpt.length) : source;
            captureStartXRuns = devices.getXRunCount();
            recorder.start(playback, juce::Decibels::decibelsToGain(float(session->sendDb)), loop, microphoneOnly() ? 0 : 1 - loop);
            captureIndex = calibration ? -1 : row;
            pending = true;
            progress = 0;
            status(calibration ? "Playing the loudest excerpt..." : "Recording " + session->pointLabel(size_t(row)));
        });
    }

    void cancel()
    {
        if (!pending)
            return;
        {
            const juce::ScopedLock lock(devices.getAudioCallbackLock());
            recorder.cancel();
        }
        pending = false;
        progress = 0;
        status("Recording cancelled.");
        update();
    }

    void timerCallback() override
    {
        if (!updateButton.isVisible())
        {
            const auto version = updates.availableVersion();
            if (version.isNotEmpty())
            {
                updateButton.setButtonText("Update " + version);
                updateButton.setVisible(true);
                resized();
            }
        }
        if (step == Step::audio)
            refreshAudioChannels();
        if (!pending)
            return;
        progress = recorder.progress();
        const auto [loopPeak, micPeak] = recorder.currentPeaks();
        loopMeter = loopPeak;
        micMeter = micPeak;
        repaint(loopMeterArea.getUnion(micMeterArea));
        if (recorder.failed())
        {
            pending = false;
            levelCheck = LevelCheck::loud;
            status("Audio device stopped or its channels changed. Check routing and recalibrate.");
            update();
            return;
        }
        if (auto recording = recorder.take())
        {
            pending = false;
            run([&]
            {
                if (recorder.deviceRevision() != confirmedDeviceRevision)
                {
                    levelCheck = LevelCheck::loud;
                    showStep(Step::audio);
                    throw std::runtime_error("The device changed during this take. Confirm audio setup and record again.");
                }
                if (devices.getXRunCount() > captureStartXRuns)
                    throw std::runtime_error("The interface reported an audio dropout. Increase the buffer size and record this take again.");
                capture::Analysed analysed;
                try
                {
                    const auto length = captureIndex < 0 ? calibrationExcerpt.length : sessionStimulus.size();
                    const auto quietDelay = microphoneOnly() && captureIndex < 0 && levelCheck == LevelCheck::quiet
                        ? liveMicDelay : std::nullopt;
                    analysed = capture::analyse(*recording, int(length), quietDelay);
                }
                catch (const capture::LoopbackSyncError& error)
                {
                    loopMeter = error.loopPeak;
                    micMeter = error.micPeak;
                    repaint(loopMeterArea.getUnion(micMeterArea));
                    const auto input = loopInput.getText();
                    const int amp = ampOutput.getSelectedId();
                    const auto source = activeOutputs.size() == 2 && amp > 0
                        ? " Connect " + activeOutputs[2 - amp] + " to " + input + "."
                        : " Check the internal loopback or line split feeding " + input + ".";
                    const auto reason = error.loopPeak < 0.003f
                        ? "No usable signal on " + input + "."
                        : "Signal on " + input + ", but no sync marker. Check that this is the loopback, not the mic.";
                    status(reason + source, true);
                    return;
                }
                loopMeter = analysed.loopPeak;
                micMeter = analysed.micPeak;
                repaint(loopMeterArea.getUnion(micMeterArea));
                if (captureIndex < 0 && levelCheck == LevelCheck::loud)
                {
                    session->calibrate(*recording, analysed, calibrationExcerpt.start);
                    liveMicDelay = analysed.delay;
                    levelCheck = LevelCheck::quiet;
                    const auto micDb = juce::Decibels::gainToDecibels(analysed.micPeak);
                    if (micDb > -3.0f)
                        status("The mic is close to clipping. Lower the mic gain and recheck the loud setting.", true);
                    else
                        status("The loud setting has headroom. Now check the quietest setting.");
                }
                else if (captureIndex < 0)
                {
                    const auto snr = session->checkQuiet(*recording, analysed, calibrationExcerpt.start);
                    levelCheck = LevelCheck::complete;
                    if (!snr)
                        status("Could not measure the background noise. Let the room and amp settle, then recheck quiet.", true);
                    else if (*snr < 20.0)
                        status("The quiet setting is hard to hear above the background noise. "
                               "Reduce nearby noise and recheck quiet. If you move the mic or change its gain, recheck loud first.", true);
                    else
                        status("The quiet setting is clearly above the background noise.");
                }
                else
                {
                    const auto index = size_t(captureIndex);
                    if (capture::hasQuietMicrophone(analysed))
                        confirmQuietTake(index, std::move(*recording), std::move(analysed));
                    else
                        saveRecording(index, *recording, analysed, false);
                }
            });
        }
    }

    void confirmQuietTake(size_t index, capture::Recording recording, capture::Analysed analysed)
    {
        auto take = std::make_shared<std::pair<capture::Recording, capture::Analysed>>(
            std::move(recording), std::move(analysed));
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Very quiet microphone",
            "This take peaks below -60 dBFS. Keep it if the amp is intentionally this quiet.",
            "Keep take", "Record again", this,
            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<CaptureView>(this), index, take](int keep)
            {
                if (safe != nullptr && keep != 0)
                    safe->run([&] { safe->saveRecording(index, take->first, take->second, true); });
            }));
    }

    void saveRecording(size_t index, const capture::Recording& recording, const capture::Analysed& analysed, bool quiet)
    {
        session->saveTake(index, recording, analysed, quiet);
        selectNext();
        if (allRecorded())
            showStep(Step::exportData);
        status(juce::String(microphoneOnly() ? "Take saved   |   Mic delay " : "Take saved   |   Loop delay ") + juce::String(analysed.delay) + " samples");
    }

    void selectNext()
    {
        const auto next = std::find_if(session->points.begin(), session->points.end(),
                                       [](const auto& point) { return point.recording.isEmpty(); });
        if (next != session->points.end())
            list.selectRow(int(next - session->points.begin()));
        else if (list.getSelectedRow() < 0)
            list.selectRow(0);
    }

    void update()
    {
        const bool ready = session.has_value();
        sessionTitle.setText(ready ? session->amplifier + "   |   " + juce::String(session->completed()) + " / "
                                   + juce::String(int(session->points.size())) + " takes" : "No session",
                             juce::dontSendNotification);
        openButton.setEnabled(!pending);
        list.setEnabled(!pending);
        list.repaint();
        loopInput.setEnabled(activeInputs.size() == 2 && !pending);
        ampOutput.setEnabled(activeOutputs.size() == 2 && !pending && !microphoneOnly());
        captureMode.setEnabled(!session && !pending);
        sendLevel.setEnabled(ready && !pending && session->completed() == 0);
        backButton.setEnabled(!pending);
        cancelButton.setVisible(pending);
        const bool canRecheck = step == Step::calibration && !pending;
        retryLoudButton.setVisible(canRecheck && levelCheck != LevelCheck::loud);
        retryQuietButton.setVisible(canRecheck && levelCheck == LevelCheck::complete);
        progressBar.setVisible(pending);
        const int row = list.getSelectedRow();
        const bool validRow = ready && row >= 0 && row < int(session->points.size());
        const bool retake = validRow && session->points[size_t(row)].recording.isNotEmpty();
        if (step == Step::calibration)
            selectedTake.setText(levelCheck == LevelCheck::complete ? "Checks complete. Continue to the first take."
                : levelCheck == LevelCheck::quiet ? "Set the amp to its quietest planned setting. Keep interface gains fixed."
                : "Set the amp to its loudest planned setting to check headroom.", juce::dontSendNotification);
        else if (step == Step::record)
            selectedTake.setText(validRow ? "Take " + juce::String(row + 1) + " / "
                                          + juce::String(int(session->points.size())) + "   |   Set the knobs below"
                                          : "Select a take", juce::dontSendNotification);
        else
            selectedTake.setText(step == Step::done ? "Dataset exported" : "All takes are saved. Export the dataset for Colab.",
                                 juce::dontSendNotification);

        nextButton.setButtonText(step == Step::welcome ? "Get started"
            : step == Step::calibration && levelCheck != LevelCheck::complete
            ? (levelCheck == LevelCheck::loud ? "Check loud setting" : "Check quiet setting")
            : step == Step::record ? (retake ? "Retake" : "Record take")
            : step == Step::exportData ? "Export dataset" : step == Step::done ? "Open dataset folder" : "Next");
        nextButton.setEnabled(!pending && (step != Step::audio || audioReady())
                              && (step != Step::record || (validRow && levelCheck == LevelCheck::complete)));
    }

    int getNumRows() override { return session ? int(session->points.size()) : 0; }
    void selectedRowsChanged(int) override { update(); repaint(takeSettingsArea); }

    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override
    {
        if (!session || row < 0 || row >= int(session->points.size()))
            return;
        if (selected)
            graphics.fillAll(juce::Colour(0xff264965));
        else if (row % 2 == 0)
            graphics.fillAll(juce::Colour(0xff1d2940));
        graphics.setColour(juce::Colours::white);
        graphics.setFont(14.0f);
        graphics.drawFittedText(session->pointLabel(size_t(row)), 10, 0, width - 65, height,
                                juce::Justification::centredLeft, 1);
        if (session->points[size_t(row)].recording.isNotEmpty())
        {
            graphics.setColour(juce::Colour(accent));
            graphics.drawText("Saved", width - 60, 0, 55, height, juce::Justification::centred);
        }
    }

    CaptureTheme theme;
    UpdateCheck updates{"Faverooo/ParametricNAM-Capture", PNAM_BUILD_VERSION};
    capture::Recorder recorder;
    juce::AudioDeviceManager devices;
    juce::AudioDeviceSelectorComponent audioSelector;
    juce::Component planContent;
    juce::Viewport planViewport, audioViewport;
    juce::StringArray activeInputs, activeOutputs;
    Step step = Step::welcome;
    std::optional<capture::Session> session;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Label title, subtitle, welcomeLead, microphoneHelp, loopbackHelp, sendHelp, nameLabel, controlsLabel, prioritiesLabel, countsLabel,
                stimulusName, sessionTitle, routingLabel, ampOutputLabel, modeLabel, sendLabel, statusLabel, selectedTake, stepHint, budgetSummary, deviceSummary;
    juce::TextEditor name, takeCount, sendLevel;
    PlanGrid controls, priorities;
    juce::ComboBox loopInput, ampOutput, captureMode;
    juce::TextButton addControl{"+ Control"}, addPriority{"+ Priority"}, wiringButton{"Wiring guide"},
                     stimulusButton{"Import stimulus"}, openButton{"Open session"},
                     nextButton{"Next"}, backButton{"Back"}, cancelButton{"Cancel"},
                     retryLoudButton{"Recheck loud"}, retryQuietButton{"Recheck quiet"}, updateButton;
    juce::ToggleButton normalise{"Normalise to -3 dBFS"};
    juce::ListBox list;
    std::vector<float> stimulus, sessionStimulus;
    capture::StimulusWindow calibrationExcerpt;
    std::optional<int> liveMicDelay;
    juce::Rectangle<int> stepperArea, loopMeterArea, micMeterArea, takeSettingsArea;
    float loopMeter = 0.0f, micMeter = 0.0f;
    double progress = 0;
    juce::ProgressBar progressBar;
    bool customTakeCount = false;
    bool requestingAudio = false;
    bool callbackInstalled = false, pending = false;
    LevelCheck levelCheck = LevelCheck::loud;
    int captureIndex = -1;
    int captureStartXRuns = 0;
    unsigned confirmedDeviceRevision = 0;
};

class CaptureWindow final : public juce::DocumentWindow
{
public:
    CaptureWindow() : DocumentWindow("ParametricNAM Capture", juce::Colour(background), allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(740, 490, 1600, 1100);
        setContentOwned(new CaptureView(), true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class CaptureApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "ParametricNAM Capture"; }
    const juce::String getApplicationVersion() override { return PNAM_BUILD_VERSION; }
    void initialise(const juce::String& arguments) override
    {
        const auto args = juce::StringArray::fromTokens(arguments, true);
        if (args.size() == 2 && args[0] == "--self-test")
        {
            try
            {
                smokeTest(juce::File(args[1].unquoted()));
            }
            catch (const std::exception& error)
            {
                std::cerr << error.what() << '\n';
                setApplicationReturnValue(1);
            }
            quit();
            return;
        }
        window = std::make_unique<CaptureWindow>();
    }
    void shutdown() override { window.reset(); }

private:
    std::unique_ptr<CaptureWindow> window;
};
} // namespace

START_JUCE_APPLICATION(CaptureApplication)
