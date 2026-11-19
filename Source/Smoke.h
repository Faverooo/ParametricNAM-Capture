#pragma once

#include "Session.h"
#include "UpdateCheck.h"
#include <iostream>
#include <array>
#include <clocale>
#include <stdexcept>

inline void smokeTest(const juce::File& folder)
{
    using namespace capture;
    std::setlocale(LC_NUMERIC, "");
    const auto check = [](bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };

    const auto older = UpdateCheck::parseVersion("0.2.9");
    const auto newer = UpdateCheck::parseVersion("v0.10.0");
    check(older && newer && *newer > *older && !UpdateCheck::parseVersion("v1.2.3-beta")
          && !UpdateCheck::parseVersion("v1.2"), "Release version comparison failed.");

    auto controls = parseControls("gain, Gain, 2, 10, 0.5, 0.2, 1\nbass, Bass, 0, 10, 1");
    auto points = plan(controls, parsePriorities("1, 9.7, *", controls), 3);
    check(points.size() == 3 && points[0].dials[0] == 9.7
          && points[1].dials == std::vector<double>{10, 10}
          && points[2].dials == std::vector<double>{2, 0}
          && std::none_of(points.begin(), points.end(), [](const auto& point) { return point.test; }),
          "Invalid priority or missing endpoints.");
    const auto oneDial = plan(parseControls("gain, Gain, 0, 10, 0.1"), {}, 6);
    check(oneDial.size() == 6 && oneDial[0].dials[0] == 10.0 && oneDial[1].dials[0] == 0.0
          && oneDial[2].dials[0] > 9.0, "One-control plan skipped the high end.");

    const auto signal = makeStimulus();
    const int n = sampleRate * 8;
    std::vector<float> stimulus(signal.begin(), signal.begin() + n);
    const auto excerpt = calibrationWindow(stimulus);
    check(excerpt.start > 0 && excerpt.length == 6 * sampleRate, "Calibration did not select a loud excerpt.");
    Recorder recorder;
    class Device final : public juce::AudioIODevice
    {
    public:
        Device() : AudioIODevice("Simulation", "Simulation") {}
        juce::StringArray getOutputChannelNames() override { return {"Amp", "Loopback"}; }
        juce::StringArray getInputChannelNames() override { return {"Loopback", "Mic"}; }
        juce::Array<double> getAvailableSampleRates() override { return {sampleRate}; }
        juce::Array<int> getAvailableBufferSizes() override { return {257}; }
        int getDefaultBufferSize() override { return 257; }
        juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
        void close() override {}
        bool isOpen() override { return true; }
        void start(juce::AudioIODeviceCallback*) override {}
        void stop() override {}
        bool isPlaying() override { return true; }
        juce::String getLastError() override { return {}; }
        int getCurrentBufferSizeSamples() override { return 257; }
        double getCurrentSampleRate() override { return sampleRate; }
        int getCurrentBitDepth() override { return 24; }
        juce::BigInteger getActiveOutputChannels() const override { return outputMask; }
        int outputMask = 1;
        juce::BigInteger getActiveInputChannels() const override { return 3; }
        int getOutputLatencyInSamples() override { return 0; }
        int getInputLatencyInSamples() override { return 0; }
    } device;
    recorder.audioDeviceAboutToStart(&device);

    auto simulate = [&](float volume, bool twoOutputs = true, bool calibration = false)
    {
        constexpr int delay = 257;
        device.outputMask = twoOutputs ? 3 : 1;
        recorder.audioDeviceAboutToStart(&device);
        const auto full = std::span<const float>(stimulus);
        const auto playback = calibration ? full.subspan(excerpt.start, excerpt.length) : full;
        recorder.start(playback, 0.5f, 0, 1);
        std::array<float, delay> output{}, loopOutput{}, input{}, target{};
        // Non-contiguous device channels exercise ordinal input/output routing.
        const float* inputs[]{nullptr, input.data(), nullptr, target.data()};
        float* outputs[]{nullptr, output.data(), nullptr, twoOutputs ? loopOutput.data() : nullptr};
        while (recorder.isBusy())
        {
            recorder.audioDeviceIOCallbackWithContext(inputs, 4, outputs, 4, delay, {});
            check(!twoOutputs || output == loopOutput, "Amp and loopback sends differ.");
            input = twoOutputs ? loopOutput : output;
            for (size_t i = 0; i < input.size(); ++i)
                target[i] = std::tanh(volume * input[i]);
        }
        auto recording = recorder.take();
        check(recording.has_value(), "Capture callback did not finish.");
        auto analysed = analyse(*recording, int(playback.size()));
        check(analysed.delay == delay && analysed.correlation > 0.99f, "Sync marker alignment failed.");
        return std::pair{std::move(*recording), std::move(analysed)};
    };

    auto session = Session::create(folder, "Simulated amp", controls, points, stimulus, -6.0);
    auto [reference, aligned] = simulate(0.8f, false, true);
    session.calibrate(reference, aligned, excerpt.start);

    auto quietRecording = reference;
    auto quietTake = aligned;
    std::fill(quietRecording.microphone.begin(), quietRecording.microphone.end(), 0.001f);
    std::fill(quietTake.target.begin(), quietTake.target.end(), std::sqrt(101.0f) * 0.001f);
    quietTake.micPeak = 0.011f;
    const auto quietSnr = session.checkQuiet(quietRecording, quietTake, excerpt.start);
    check(quietSnr && std::abs(*quietSnr - 20.0) < 0.01, "Quiet check SNR estimate is incorrect.");
    std::fill(quietTake.target.begin(), quietTake.target.end(), 0.001f);
    const auto nearNoise = session.checkQuiet(quietRecording, quietTake, excerpt.start);
    check(nearNoise && *nearNoise < 1.0, "Quiet check did not detect a target at the noise floor.");
    auto silentLoopback = reference;
    std::fill(silentLoopback.loopback.begin(), silentLoopback.loopback.end(), 0.0f);
    bool detected = false;
    try { (void) analyse(silentLoopback, int(excerpt.length)); }
    catch (const LoopbackSyncError& error) { detected = error.loopPeak == 0 && error.micPeak > 0; }
    check(detected, "Missing loopback was not diagnosed.");
    for (size_t i = 0; i < points.size(); ++i)
    {
        auto [recording, analysed] = simulate(0.6f + 0.2f * float(i));
        session.saveTake(i, recording, analysed);
    }
    auto [quietRaw, quietAligned] = simulate(0.0f);
    bool quietRejected = false;
    try { session.saveTake(0, quietRaw, quietAligned); }
    catch (const std::runtime_error&) { quietRejected = true; }
    check(quietRejected && hasQuietMicrophone(quietAligned), "A quiet take must require confirmation.");
    session.saveTake(0, quietRaw, quietAligned, true);

    const auto oldRecording = session.points[0].recording;
    auto [retake, analysed] = simulate(0.5f);
    session.saveTake(0, retake, analysed);
    check(session.points[0].recording != oldRecording, "Retake did not create a revision.");
    auto resumed = Session::open(folder);
    check(resumed.hasCalibration() && resumed.completed() == 3, "Session resume failed.");
    resumed.calibrate(reference, aligned, excerpt.start);
    resumed.exportDataset(true);
    const auto manifest = juce::JSON::parse(resumed.folder.getChildFile("dataset").getChildFile("manifest.json"));
    const auto referenceFile = resumed.folder.getChildFile("dataset").getChildFile(manifest["x"].toString());
    check(readWav(referenceFile).size() == size_t(n), "Export used the short calibration input as the training reference.");
    std::cout << "Simulation passed: plan, recording, alignment, retake, resume and normalised export.\n";
}
