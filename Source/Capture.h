#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <optional>
#include <vector>

namespace capture
{
constexpr int sampleRate = 48000;
constexpr int leadFrames = sampleRate / 4;
constexpr int markerFrames = 255;
constexpr int tailFrames = sampleRate;

struct Recording
{
    std::vector<float> loopback, microphone;
};

struct Analysed
{
    int delay = 0;
    float loopPeak = 0, micPeak = 0, correlation = 0;
    std::vector<float> input, target;
};

std::vector<float> makeStimulus();
std::vector<float> readWav(const juce::File&);
void writeWav(const juce::File&, const std::vector<float>&);
Analysed analyse(const Recording&, int frames, float markerLevel);
double relativeInputError(const std::vector<float>& reference, const std::vector<float>& actual);
void requireUsable(const Analysed&);

class Recorder final : public juce::AudioIODeviceCallback
{
public:
    void start(const std::vector<float>& input, float sendGain, int loopIndex, int micIndex);
    void cancel();
    bool isBusy() const;
    bool failed() const;
    double progress() const;
    std::pair<float, float> currentPeaks() const;
    std::optional<Recording> take();

    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*, int, int,
                                          const juce::AudioIODeviceCallbackContext&) override;

private:
    enum class State { idle, recording, complete, failed };
    std::atomic<State> state{State::idle};
    std::atomic<int> cursor{0}, length{0};
    std::atomic<double> rate{0};
    std::atomic<float> loopPeak{0}, micPeak{0};
    std::atomic<int> inputCount{0}, outputCount{0};
    Recording result;
    std::vector<float> playback;
    int loop = 0, mic = 1;
};
} // namespace capture
