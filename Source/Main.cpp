#include "Session.h"
#include "PlanGrid.h"
#include "Smoke.h"

#include <algorithm>
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
        : list("Capture plan", this), stimulus(capture::makeStimulus()), progressBar(progress)
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
        for (auto* component : std::initializer_list<juce::Component*>{
                 &title, &subtitle, &deviceButton, &planTab, &recordTab, &nameLabel, &name,
                 &controlsLabel, &controls, &addControl, &prioritiesLabel, &priorities, &addPriority, &countsLabel,
                 &trainCount, &testCount, &stimulusButton, &stimulusName, &createButton, &openButton,
                 &sessionTitle, &routingLabel, &loopInput, &sendLabel, &sendLevel,
                 &selectedTake, &list, &calibrateButton, &recordButton, &cancelButton, &normalise, &exportButton, &statusLabel, &progressBar})
            addAndMakeVisible(component);

        title.setText("ParametricNAM Capture", juce::dontSendNotification);
        title.setFont(juce::FontOptions(29.0f, juce::Font::bold));
        selectedTake.setFont(juce::FontOptions(15.0f, juce::Font::bold));
        selectedTake.setColour(juce::Label::textColourId, juce::Colour(accent));
        subtitle.setText("Plan your capture", juce::dontSendNotification);
        subtitle.setColour(juce::Label::textColourId, muted);
        for (auto* label : {&statusLabel, &stimulusName, &sessionTitle})
            label->setColour(juce::Label::textColourId, juce::Colour(accent));
        sessionTitle.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        nameLabel.setText("Amplifier name", juce::dontSendNotification);
        controlsLabel.setText("Controls", juce::dontSendNotification);
        prioritiesLabel.setText("Priority zones   |   Value, range (8:10) or *", juce::dontSendNotification);
        for (auto* label : {&controlsLabel, &prioritiesLabel})
            label->setFont(juce::FontOptions(16.0f, juce::Font::bold));
        for (auto* button : {&createButton, &recordButton, &exportButton})
        {
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(accent));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(background));
        }
        countsLabel.setText("Training takes                Test takes", juce::dontSendNotification);
        routingLabel.setText("Loopback input", juce::dontSendNotification);
        sendLabel.setText("Send dB", juce::dontSendNotification);

        name.setTextToShowWhenEmpty("e.g. Tube Combo", juce::Colours::grey);
        name.setExplicitFocusOrder(1);
        controls.setHeadings({"ID", "Name", "Min", "Max", "Step", "Model min", "Model max"});
        controls.addRow({"gain", "Gain", "0", "10", "0.5", "0", "1"});
        controls.addRow({"treble", "Treble", "0", "10", "0.5", "0", "1"});
        controls.addRow({"mid", "Middle", "0", "10", "0.5", "0", "1"});
        controls.addRow({"bass", "Bass", "0", "10", "0.5", "0", "1"});
        priorities.placeholders = true;
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
        trainCount.setInputRestrictions(4, "0123456789");
        trainCount.setText("30");
        testCount.setInputRestrictions(4, "0123456789");
        testCount.setText("10");
        sendLevel.setInputRestrictions(6, "-0123456789.");
        sendLevel.setText("-18");
        sendLevel.onReturnKey = sendLevel.onFocusLost = [this] { applySend(); };
        loopInput.addItem("First active input", 1);
        loopInput.addItem("Second active input", 2);
        loopInput.setSelectedId(1);
        loopInput.onChange = [this] { calibratedNow = false; update(); };

        stimulusName.setText("Built-in 12 s test signal", juce::dontSendNotification);
        list.setRowHeight(34);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff202834));
        list.setMultipleSelectionEnabled(false);
        progressBar.setPercentageDisplay(true);

        deviceButton.onClick = [this] { chooseDevice(); };
        planTab.onClick = [this] { showCapture(false); };
        recordTab.onClick = [this] { showCapture(true); };
        stimulusButton.onClick = [this] { chooseStimulus(); };
        createButton.onClick = [this] { createSession(); };
        openButton.onClick = [this] { openSession(); };
        calibrateButton.onClick = [this] { start(true); };
        recordButton.onClick = [this] { start(false); };
        cancelButton.onClick = [this] { cancel(); };
        exportButton.onClick = [this]
        {
            run([this]
            {
                session->exportDataset(normalise.getToggleState());
                status("Exported dataset/manifest.json and dataset/profile.json.");
            });
        };

        showCapture(false);
        setSize(960, 760);
        startTimerHz(15);

        const auto safe = juce::Component::SafePointer<CaptureView>(this);
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [safe](bool granted)
        {
            if (safe == nullptr)
                return;
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
        graphics.fillRect(navigationDivider);
        if (selectedTake.isVisible())
        {
            drawMeter(graphics, loopMeterArea, "Loopback", loopMeter);
            drawMeter(graphics, micMeterArea, "Mic", micMeter);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(24);
        auto header = area.removeFromTop(43);
        deviceButton.setBounds(header.removeFromRight(142).withHeight(34));
        title.setBounds(header);
        subtitle.setBounds(area.removeFromTop(25));
        area.removeFromTop(12);
        auto navigation = area.removeFromTop(36);
        planTab.setBounds(navigation.removeFromLeft(118));
        navigation.removeFromLeft(8);
        recordTab.setBounds(navigation.removeFromLeft(118));
        openButton.setBounds(navigation.removeFromRight(142));
        const auto gap = area.removeFromTop(12);
        navigationDivider = gap.withY(gap.getCentreY()).withHeight(1);
        auto foot = area.removeFromBottom(52);
        progressBar.setBounds(foot.removeFromBottom(15));
        statusLabel.setBounds(foot);
        area.removeFromBottom(8);

        auto planArea = area;
        nameLabel.setBounds(planArea.removeFromTop(22));
        name.setBounds(planArea.removeFromTop(30));
        planArea.removeFromTop(6);
        auto controlHeader = planArea.removeFromTop(30);
        addControl.setBounds(controlHeader.removeFromRight(110));
        controlsLabel.setBounds(controlHeader);
        planArea.removeFromTop(4);
        controls.setBounds(planArea.removeFromTop(std::min(165, std::max(102, getHeight() - 575))));
        planArea.removeFromTop(6);
        auto priorityHeader = planArea.removeFromTop(30);
        addPriority.setBounds(priorityHeader.removeFromRight(110));
        prioritiesLabel.setBounds(priorityHeader);
        planArea.removeFromTop(4);
        priorities.setBounds(planArea.removeFromTop(90));
        planArea.removeFromTop(6);
        countsLabel.setBounds(planArea.removeFromTop(22));
        auto counts = planArea.removeFromTop(30);
        trainCount.setBounds(counts.removeFromLeft(134).withTrimmedRight(14));
        testCount.setBounds(counts.removeFromLeft(118));
        planArea.removeFromTop(8);
        auto create = planArea.removeFromTop(34);
        stimulusButton.setBounds(create.removeFromLeft(140));
        createButton.setBounds(create.removeFromRight(142));
        stimulusName.setBounds(create.reduced(8, 0));

        auto recordArea = area;
        sessionTitle.setBounds(recordArea.removeFromTop(34));
        auto setup = recordArea.removeFromTop(32);
        routingLabel.setBounds(setup.removeFromLeft(117));
        loopInput.setBounds(setup.removeFromLeft(170));
        setup.removeFromLeft(20);
        sendLabel.setBounds(setup.removeFromLeft(72));
        sendLevel.setBounds(setup.removeFromLeft(75));
        setup.removeFromLeft(20);
        loopMeterArea = setup.removeFromLeft(setup.getWidth() / 2).reduced(6, 1);
        micMeterArea = setup.reduced(6, 1);
        recordArea.removeFromTop(8);
        selectedTake.setBounds(recordArea.removeFromTop(30));
        auto actions = recordArea.removeFromBottom(36);
        calibrateButton.setBounds(actions.removeFromLeft(122));
        actions.removeFromLeft(8);
        recordButton.setBounds(actions.removeFromLeft(132));
        actions.removeFromLeft(8);
        cancelButton.setBounds(actions.removeFromLeft(100));
        exportButton.setBounds(actions.removeFromRight(142));
        normalise.setBounds(actions.removeFromRight(169));
        recordArea.removeFromBottom(8);
        list.setBounds(recordArea);
    }

private:
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

    void status(const juce::String& text) { statusLabel.setText(text, juce::dontSendNotification); }
    void run(const std::function<void()>& operation)
    {
        try { operation(); }
        catch (const std::exception& error) { status(juce::String::fromUTF8(error.what())); }
        update();
    }

    void showCapture(bool capturePage)
    {
        for (auto* component : std::initializer_list<juce::Component*>{
                 &nameLabel, &name, &controlsLabel, &controls, &addControl, &prioritiesLabel, &priorities, &addPriority,
                 &countsLabel, &trainCount, &testCount, &stimulusButton, &stimulusName, &createButton})
            component->setVisible(!capturePage);
        for (auto* component : std::initializer_list<juce::Component*>{
                 &sessionTitle, &routingLabel, &loopInput, &sendLabel, &sendLevel, &selectedTake, &list,
                 &calibrateButton, &recordButton, &cancelButton, &normalise, &exportButton, &progressBar})
            component->setVisible(capturePage);
        subtitle.setText(capturePage ? "Calibrate and record" : "Plan your capture", juce::dontSendNotification);
        planTab.setToggleState(!capturePage, juce::dontSendNotification);
        recordTab.setToggleState(capturePage, juce::dontSendNotification);
        update();
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
    }

    void chooseDevice()
    {
        calibratedNow = false;
        update();
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(devices, 0, 2, 0, 1, false, false, false, false);
        selector->setSize(560, 400);
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Audio device and channels";
        options.dialogBackgroundColour = juce::Colour(background);
        options.content.setOwned(selector.release());
        options.componentToCentreAround = this;
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
        status("Select 48 kHz, two input channels and one send output.");
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
            auto settings = capture::parseControls(controls.text());
            auto prioritiesParsed = capture::parsePriorities(priorities.text("*"), settings);
            auto points = capture::plan(settings, prioritiesParsed, trainCount.getText().getIntValue(), testCount.getText().getIntValue());
            choose("Choose a new folder name for the capture session", "",
                   juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                   [settings = std::move(settings), points = std::move(points)](CaptureView& view, const juce::File& file)
            {
                view.setSession(capture::Session::create(file, view.name.getText(), settings, points, view.stimulus, -18.0));
            });
        });
    }

    void openSession()
    {
        choose("Open session.json", "session.json",
               juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
               [](CaptureView& view, const juce::File& file)
        {
            view.setSession(capture::Session::open(file.getParentDirectory()));
        });
    }

    void setSession(capture::Session next)
    {
        auto samples = next.stimulus();
        session = std::move(next);
        sessionStimulus = std::move(samples);
        sendLevel.setText(juce::String(session->sendDb, 1), false);
        calibratedNow = false;
        loopMeter = micMeter = 0.0f;
        list.updateContent();
        selectNext();
        showCapture(true);
        status("Set the amplifier to its loudest condition, then calibrate.");
    }

    void applySend()
    {
        if (!session || pending)
            return;
        run([&]
        {
            const auto text = sendLevel.getText().trim().toStdString();
            size_t parsed = 0;
            if (text.empty() || text.find_first_of("0123456789") == std::string::npos)
                throw std::runtime_error("Enter a send level between -60 and 0 dB.");
            const auto db = std::stod(text, &parsed);
            if (parsed != text.size())
                throw std::runtime_error("Enter a send level between -60 and 0 dB.");
            if (db != session->sendDb)
            {
                session->setSendDb(db);
                calibratedNow = false;
                status("Send level changed. Calibrate again.");
            }
        });
        sendLevel.setText(juce::String(session->sendDb, 1), false);
    }

    void start(bool calibration)
    {
        run([&]
        {
            if (!session || pending || !callbackInstalled)
                throw std::runtime_error("Open a session and configure the audio device first.");
            applySend();
            const auto row = list.getSelectedRow();
            if (!calibration && (!calibratedNow || row < 0))
                throw std::runtime_error("Calibrate and select a take first.");
            const int loop = loopInput.getSelectedId() - 1;
            recorder.start(sessionStimulus, juce::Decibels::decibelsToGain(float(session->sendDb)), loop, 1 - loop);
            captureIndex = calibration ? -1 : row;
            pending = true;
            progress = 0;
            status(calibration ? "Recording calibration..." : "Recording " + session->pointLabel(size_t(row)));
        });
    }

    void cancel()
    {
        if (!pending)
            return;
        devices.removeAudioCallback(&recorder);
        recorder.cancel();
        devices.addAudioCallback(&recorder);
        pending = false;
        progress = 0;
        status("Recording cancelled.");
        update();
    }

    void timerCallback() override
    {
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
            calibratedNow = false;
            status("Audio device stopped or its channels changed. Check routing and recalibrate.");
            update();
            return;
        }
        if (auto recording = recorder.take())
        {
            pending = false;
            run([&]
            {
                const auto analysed = capture::analyse(*recording, int(sessionStimulus.size()), 0.3f);
                loopMeter = analysed.loopPeak;
                micMeter = analysed.micPeak;
                repaint(loopMeterArea.getUnion(micMeterArea));
                if (captureIndex < 0)
                {
                    calibratedNow = false;
                    session->calibrate(*recording, analysed);
                    calibratedNow = true;
                }
                else
                {
                    session->saveTake(size_t(captureIndex), *recording, analysed);
                    selectNext();
                }
                status((captureIndex < 0 ? juce::String("Calibrated") : juce::String("Take saved"))
                    + "   |   Loop delay " + juce::String(analysed.delay) + " samples");
            });
        }
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
        deviceButton.setEnabled(!pending);
        planTab.setEnabled(!pending);
        recordTab.setEnabled(ready && !pending);
        openButton.setEnabled(!pending);
        list.setEnabled(!pending);
        list.repaint();
        loopInput.setEnabled(!pending);
        sendLevel.setEnabled(ready && !pending && session->completed() == 0);
        calibrateButton.setEnabled(ready && !pending);
        const int row = list.getSelectedRow();
        const bool validRow = ready && row >= 0 && row < int(session->points.size());
        selectedTake.setText(validRow ? session->pointLabel(size_t(row)) : "Select a take", juce::dontSendNotification);
        recordButton.setEnabled(validRow && calibratedNow && !pending);
        recordButton.setButtonText(validRow && session->points[size_t(row)].recording.isNotEmpty() ? "Retake" : "Record take");
        cancelButton.setEnabled(pending);
        normalise.setEnabled(!pending);
        exportButton.setEnabled(ready && !pending && session->completed() == int(session->points.size()));
    }

    int getNumRows() override { return session ? int(session->points.size()) : 0; }
    void selectedRowsChanged(int) override { update(); }

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
    capture::Recorder recorder;
    juce::AudioDeviceManager devices;
    std::optional<capture::Session> session;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Label title, subtitle, nameLabel, controlsLabel, prioritiesLabel, countsLabel,
                stimulusName, sessionTitle, routingLabel, sendLabel, statusLabel, selectedTake;
    juce::TextEditor name, trainCount, testCount, sendLevel;
    PlanGrid controls, priorities;
    juce::ComboBox loopInput;
    juce::TextButton addControl{"+ Control"}, addPriority{"+ Priority"},
                     deviceButton{"Audio setup"}, planTab{"Plan session"}, recordTab{"Record"},
                     stimulusButton{"Import stimulus"}, createButton{"Create session"}, openButton{"Open session"},
                     calibrateButton{"Calibrate"}, recordButton{"Record take"}, cancelButton{"Cancel"},
                     exportButton{"Export dataset"};
    juce::ToggleButton normalise{"Normalise to -3 dBFS"};
    juce::ListBox list;
    std::vector<float> stimulus, sessionStimulus;
    juce::Rectangle<int> navigationDivider, loopMeterArea, micMeterArea;
    float loopMeter = 0.0f, micMeter = 0.0f;
    double progress = 0;
    juce::ProgressBar progressBar;
    bool callbackInstalled = false, pending = false, calibratedNow = false;
    int captureIndex = -1;
};

class CaptureWindow final : public juce::DocumentWindow
{
public:
    CaptureWindow() : DocumentWindow("ParametricNAM Capture", juce::Colour(background), allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(760, 720, 1600, 1100);
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
    const juce::String getApplicationVersion() override { return "0.1.0"; }
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
