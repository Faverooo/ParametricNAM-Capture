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
    auto points = plan(controls, parsePriorities("1, 9.7, *", controls), 7);
    check(points.size() == 7 && points[0].dials[0] == 9.7
          && points[1].dials == std::vector<double>{10, 10}
          && points[2].dials == std::vector<double>{2, 0}
          && !points[0].test && !points[1].test && !points[2].test
          && std::count_if(points.begin(), points.end(), [](const auto& point) { return point.test; }) == 1,
          "Invalid priority or missing endpoints.");
    const auto oneDial = plan(parseControls("gain, Gain, 0, 10, 0.1"), {}, 6);
    check(oneDial.size() == 6 && oneDial[0].dials[0] == 10.0 && oneDial[1].dials[0] == 0.0
          && oneDial[2].dials[0] > 9.0 && !oneDial[2].test, "One-control plan skipped the high end.");

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
        juce::BigInteger getActiveInputChannels() const override { return inputMask; }
        int inputMask = 3;
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
    check(resumed.hasCalibration() && resumed.completed() == int(points.size()), "Session resume failed.");
    resumed.calibrate(reference, aligned, excerpt.start);
    resumed.exportDataset(true);
    const auto manifest = juce::JSON::parse(resumed.folder.getChildFile("dataset").getChildFile("manifest.json"));
    check(manifest["train"].size() == 6 && manifest["test"].size() == 1, "Export did not preserve the held-out take.");
    const auto referenceFile = resumed.folder.getChildFile("dataset").getChildFile(manifest["x"].toString());
    check(readWav(referenceFile).size() == size_t(n), "Export used the short calibration input as the training reference.");
    constexpr int micDelay = 4000;
    auto simulateMic = [&](std::span<const float> audio, float volume, int drift = 0)
    {
        constexpr int block = 257;
        device.inputMask = 4;
        device.outputMask = 8;
        recorder.audioDeviceAboutToStart(&device);
        recorder.start(audio, 0.5f, -1, 0);
        std::vector<float> sent;
        std::array<float, block> input{}, output{};
        const float* inputs[]{nullptr, nullptr, input.data()};
        float* outputs[]{nullptr, nullptr, nullptr, output.data()};
        float lowpass = 0;
        while (recorder.isBusy())
        {
            for (int i = 0; i < block; ++i)
            {
                auto position = int(sent.size()) + i - micDelay;
                if (position > leadFrames + int(audio.size()) + 1000)
                    position -= drift;
                const float value = position >= 0 ? sent[size_t(position)] : 0.0f;
                lowpass += 0.25f * (value - lowpass);
                input[size_t(i)] = -0.5f * std::tanh(volume * lowpass) + (i % 2 == 0 ? 0.00002f : -0.00002f);
            }
            recorder.audioDeviceIOCallbackWithContext(inputs, 3, outputs, 4, block, {});
            sent.insert(sent.end(), output.begin(), output.end());
        }
        auto result = recorder.take();
        check(result && result->loopback.empty() && result->sent.size() == audio.size(),
              "Microphone-only capture did not store the digital send.");
        return std::move(*result);
    };

    auto micSession = Session::create(folder.getChildFile("microphone-only"), "Simulated mic amp",
                                      controls, plan(controls, {}, 3), stimulus, -6.0, true);
    const auto excerptAudio = std::span<const float>(stimulus).subspan(excerpt.start, excerpt.length);
    const auto micCalibration = simulateMic(excerptAudio, 17.0f);
    const auto micAligned = analyse(micCalibration, int(excerptAudio.size()));
    check(micAligned.syncVerified && std::abs(micAligned.delay - micDelay) < 12,
          "Microphone timing failed with filtering, clipping-like saturation and inverted polarity.");
    micSession.calibrate(micCalibration, micAligned, excerpt.start);

    for (size_t i = 0; i < micSession.points.size(); ++i)
    {
        auto micRaw = simulateMic(stimulus, 6.0f);
        auto micTake = analyse(micRaw, n);
        check(micTake.syncVerified && micTake.input == micRaw.sent,
              "Microphone timing confirmation or input reference is incorrect.");
        for (size_t frame = 0; frame < stimulus.size(); ++frame)
            check(micTake.input[frame] == stimulus[frame] * 0.5f, "Digital send samples changed.");
        micSession.saveTake(i, micRaw, micTake);
    }
    const auto muted = simulateMic(excerptAudio, 0.0f);
    bool mutedRejected = false;
    try { (void) analyse(muted, int(excerptAudio.size())); }
    catch (const std::runtime_error&) { mutedRejected = true; }
    check(mutedRejected, "A muted take with unverified timing was accepted.");
    const auto quietCheck = analyse(muted, int(excerptAudio.size()), micAligned.delay);
    check(!quietCheck.syncVerified, "Quiet check incorrectly marked timing as verified.");
    bool quietSaveRejected = false;
    try { micSession.saveTake(0, muted, quietCheck, true); }
    catch (const std::runtime_error&) { quietSaveRejected = true; }
    check(quietSaveRejected, "Manual acceptance bypassed microphone timing verification.");
    bool driftRejected = false;
    try { (void) analyse(simulateMic(stimulus, 4.0f, 8), n); }
    catch (const std::runtime_error&) { driftRejected = true; }
    check(driftRejected, "A drifting microphone recording was accepted.");

    auto micResumed = Session::open(micSession.folder);
    check(micResumed.microphoneOnly, "Microphone-only mode was lost when reopening the session.");
    micResumed.exportDataset();
    const auto micDataset = micSession.folder.getChildFile("dataset");
    const auto micManifest = juce::JSON::parse(micDataset.getChildFile("manifest.json"));
    check(micManifest["capture_mode"].toString() == "microphone_only"
          && readWav(micDataset.getChildFile(micManifest["x"].toString())).size() == stimulus.size(),
          "Microphone-only dataset did not export the digital send.");
    const auto exportedInput = readWav(micDataset.getChildFile(micManifest["x"].toString()));
    for (size_t i = 0; i < stimulus.size(); ++i)
        check(std::abs(exportedInput[i] - stimulus[i] * 0.5f) < 2.0e-7f, "Export changed the send beyond PCM24 precision.");
    const auto captureMeta = micSession.folder.getChildFile("recordings")
        .getChildFile(micSession.points.front().recording).getChildFile("capture.json");
    auto oldMeta = juce::JSON::parse(captureMeta);
    oldMeta.getDynamicObject()->setProperty("sync_verified", false);
    check(captureMeta.replaceWithText(juce::JSON::toString(oldMeta)), "Could not create legacy unverified fixture.");
    auto reopened = Session::open(micSession.folder);
    check(reopened.completed() == 2 && reopened.points.front().recording.isEmpty(),
          "An old unverified take was not reopened for retake.");
    std::cout << "Simulation passed: loopback and microphone-only recording, alignment, drift rejection, resume and export.\n";
}
