#pragma once

#include "Session.h"
#include <iostream>
#include <array>
#include <stdexcept>

inline void smokeTest(const juce::File& folder)
{
    using namespace capture;
    const auto check = [](bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };

    auto controls = parseControls("gain, Gain, 2, 10, 0.5, 0.2, 1\nbass, Bass, 0, 10, 1");
    auto points = plan(controls, parsePriorities("1, 10, 3", controls), 2, 1);
    check(points.size() == 3 && points[0].dials == std::vector<double>{10, 3}
            && !points[1].test && points[2].test && points[2].dials != points[0].dials
            && points[2].dials != points[1].dials, "Invalid priority or held-out split.");

    const auto signal = makeStimulus();
    const int n = sampleRate * 3;
    std::vector<float> stimulus(signal.begin(), signal.begin() + n);
    Recorder recorder;
    class Device final : public juce::AudioIODevice
    {
    public:
        Device() : AudioIODevice("Simulation", "Simulation") {}
        juce::StringArray getOutputChannelNames() override { return {"Line"}; }
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
        juce::BigInteger getActiveOutputChannels() const override { return 1; }
        juce::BigInteger getActiveInputChannels() const override { return 3; }
        int getOutputLatencyInSamples() override { return 0; }
        int getInputLatencyInSamples() override { return 0; }
    } device;
    recorder.audioDeviceAboutToStart(&device);

    auto simulate = [&](float volume)
    {
        constexpr int delay = 257;
        recorder.start(stimulus, 0.5f, 0, 1);
        std::array<float, delay> output{}, input{}, target{};
        // Non-contiguous device channels exercise ordinal input/output routing.
        const float* inputs[]{nullptr, input.data(), nullptr, target.data()};
        float* outputs[]{nullptr, output.data()};
        while (recorder.isBusy())
        {
            recorder.audioDeviceIOCallbackWithContext(inputs, 4, outputs, 2, delay, {});
            input = output;
            for (size_t i = 0; i < input.size(); ++i)
                target[i] = std::tanh(volume * input[i]);
        }
        auto recording = recorder.take();
        check(recording.has_value(), "Capture callback did not finish.");
        auto analysed = analyse(*recording, n, 0.15f);
        check(analysed.delay == delay && analysed.correlation > 0.99f, "Sync marker alignment failed.");
        return std::pair{std::move(*recording), std::move(analysed)};
    };

    auto session = Session::create(folder, "Simulated amp", controls, points, stimulus, -6.0);
    auto [reference, aligned] = simulate(0.8f);
    session.calibrate(reference, aligned);
    for (size_t i = 0; i < points.size(); ++i)
    {
        auto [recording, analysed] = simulate(0.6f + 0.2f * float(i));
        session.saveTake(i, recording, analysed);
    }
    const auto oldRecording = session.points[0].recording;
    auto [retake, analysed] = simulate(0.5f);
    session.saveTake(0, retake, analysed);
    check(session.points[0].recording != oldRecording, "Retake did not create a revision.");
    auto resumed = Session::open(folder);
    check(resumed.hasCalibration() && resumed.completed() == 3, "Session resume failed.");
    resumed.exportDataset(true);
    std::cout << "Simulation passed: plan, recording, alignment, retake, resume and normalised export.\n";
}
