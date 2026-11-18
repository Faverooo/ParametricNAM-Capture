#include "Capture.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace capture
{
namespace
{
std::vector<float> marker()
{
    std::mt19937 random(6871);
    std::vector<float> signal(markerFrames);
    for (auto& sample : signal)
        sample = (random() & 1u) != 0 ? 1.0f : -1.0f;
    return signal;
}

float peak(const std::vector<float>& samples)
{
    float value = 0;
    for (float sample : samples)
        value = std::max(value, std::abs(sample));
    return value;
}
} // namespace

std::vector<float> makeStimulus()
{
    std::mt19937 random(4426);
    std::vector<float> signal(12 * sampleRate);
    float low = 0, lower = 0;
    for (size_t i = 0; i < signal.size(); ++i)
    {
        const double t = double(i) / sampleRate;
        const float noise = float(double(random()) / std::mt19937::max() * 2.0 - 1.0);
        low += 0.35f * (noise - low);
        lower += 0.009f * (low - lower);
        const float envelope = std::min({1.0f, float(t * 25), float((12.0 - t) * 25)});
        const float level = 0.18f + 0.82f * float(t / 12.0);
        signal[i] = envelope * level * (low - lower);
    }
    const float scale = 0.7f / peak(signal);
    for (auto& sample : signal)
        sample *= scale;
    return signal;
}

std::vector<float> readWav(const juce::File& file)
{
    juce::WavAudioFormat format;
    auto stream = file.createInputStream();
    if (!stream)
        throw std::runtime_error("Cannot read the WAV file.");
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
    if (!reader || reader->sampleRate != sampleRate || reader->numChannels != 1
        || reader->lengthInSamples < sampleRate * 3 || reader->lengthInSamples > sampleRate * 240)
        throw std::runtime_error("Choose a mono 48 kHz WAV from 3 to 240 seconds.");
    juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, false))
        throw std::runtime_error("Cannot decode the WAV file.");
    const auto* samples = buffer.getReadPointer(0);
    const auto* end = samples + buffer.getNumSamples();
    if (!std::all_of(samples, end, [](float sample) { return std::isfinite(sample); }))
        throw std::runtime_error("The WAV contains nonfinite samples.");
    return {samples, end};
}

void writeWav(const juce::File& file, const std::vector<float>& samples)
{
    juce::TemporaryFile temporary(file);
    auto stream = temporary.getFile().createOutputStream();
    juce::WavAudioFormat format;
    if (!stream)
        throw std::runtime_error("Cannot open " + file.getFileName().toStdString());
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), sampleRate, 1, 24, {}, 0));
    if (writer)
        stream.release();
    const float* channels[]{samples.data()};
    if (!writer || !writer->writeFromFloatArrays(channels, 1, static_cast<int>(samples.size())))
        throw std::runtime_error("Cannot write " + file.getFileName().toStdString());
    writer.reset();
    if (!temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("Cannot replace " + file.getFileName().toStdString());
}

Analysed analyse(const Recording& recording, int frames, float markerLevel)
{
    if (frames <= 0 || recording.loopback.size() != recording.microphone.size()
        || recording.loopback.size() < size_t(leadFrames + frames + tailFrames))
        throw std::runtime_error("Incomplete recording.");
    const auto finite = [](float sample) { return std::isfinite(sample); };
    if (!std::all_of(recording.loopback.begin(), recording.loopback.end(), finite)
        || !std::all_of(recording.microphone.begin(), recording.microphone.end(), finite))
        throw std::runtime_error("The audio device produced nonfinite samples.");
    const auto signal = marker();
    float best = 0;
    int bestIndex = -1;
    for (int start = 0; start < leadFrames + tailFrames - markerFrames; ++start)
    {
        float dot = 0, energy = 0;
        for (int k = 0; k < markerFrames; ++k)
        {
            const float sample = recording.loopback[size_t(start + k)];
            dot += sample * signal[size_t(k)];
            energy += sample * sample;
        }
        const float score = energy > 0 ? std::abs(dot) / std::sqrt(energy * markerFrames) : 0;
        if (score > best)
        {
            best = score;
            bestIndex = start;
        }
    }
    if (best < 0.65f || bestIndex < 0 || markerLevel <= 0)
        throw std::runtime_error("Sync marker missing on the loopback channel. Check routing and input selection.");
    const int delay = bestIndex - leadFrames / 2;
    const int offset = leadFrames + delay;
    if (offset < 0 || size_t(offset + frames) > recording.loopback.size())
        throw std::runtime_error("Measured latency is outside the capture window.");

    return {delay, peak(recording.loopback), peak(recording.microphone), best,
            {recording.loopback.begin() + offset, recording.loopback.begin() + offset + frames},
            {recording.microphone.begin() + offset, recording.microphone.begin() + offset + frames}};
}

double relativeInputError(const std::vector<float>& reference, const std::vector<float>& actual)
{
    if (reference.size() != actual.size())
        throw std::runtime_error("Calibration length differs. Recalibrate before recording.");
    double signal = 0, error = 0;
    for (size_t i = 0; i < reference.size(); ++i)
    {
        const double delta = double(reference[i]) - actual[i];
        signal += double(reference[i]) * reference[i];
        error += delta * delta;
    }
    return signal > 0 ? std::sqrt(error / signal) : 1.0;
}

void requireUsable(const Analysed& take)
{
    if (take.loopPeak >= 0.995f || take.micPeak >= 0.995f)
        throw std::runtime_error("Clipping detected. Lower interface gain and recalibrate.");
    if (peak(take.input) < 0.003f || peak(take.target) < 0.001f)
        throw std::runtime_error("Input or microphone signal is too quiet. Check routing and levels.");
}

void Recorder::start(const std::vector<float>& input, float sendGain, int loopIndex, int micIndex)
{
    if (state.load() == State::recording)
        throw std::runtime_error("A recording is already running.");
    if (rate.load() != sampleRate || outputCount.load() != 1 || inputCount.load() != 2
        || loopIndex < 0 || micIndex < 0 || loopIndex > 1 || micIndex > 1 || loopIndex == micIndex)
        throw std::runtime_error("Select two active inputs and one active output at 48 kHz.");
    if (peak(input) * sendGain >= 1.0f)
        throw std::runtime_error("The send trim would clip the stimulus.");
    loop = loopIndex;
    mic = micIndex;
    playback.assign(leadFrames + input.size() + tailFrames, 0.0f);
    const auto sync = marker();
    for (size_t k = 0; k < sync.size(); ++k)
        playback[leadFrames / 2 + k] = sync[k] * sendGain * 0.3f;
    std::transform(input.begin(), input.end(), playback.begin() + leadFrames,
                   [sendGain](float sample) { return sample * sendGain; });
    result.loopback.assign(playback.size(), 0);
    result.microphone.assign(playback.size(), 0);
    cursor.store(0);
    loopPeak.store(0);
    micPeak.store(0);
    length.store(static_cast<int>(playback.size()));
    state.store(State::recording);
}

void Recorder::cancel()
{
    // The owner removes the callback before cancellation or changing buffers.
    state.store(State::idle);
}

bool Recorder::isBusy() const { return state.load() == State::recording; }
bool Recorder::failed() const { return state.load() == State::failed; }

double Recorder::progress() const
{
    const int total = length.load();
    return total > 0 ? double(cursor.load()) / total : 0.0;
}

std::pair<float, float> Recorder::currentPeaks() const
{
    return {loopPeak.load(std::memory_order_relaxed), micPeak.load(std::memory_order_relaxed)};
}

std::optional<Recording> Recorder::take()
{
    if (state.load() != State::complete)
        return {};
    state.store(State::idle);
    return std::move(result);
}

void Recorder::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    rate.store(device->getCurrentSampleRate());
    inputCount.store(device->getActiveInputChannels().countNumberOfSetBits());
    outputCount.store(device->getActiveOutputChannels().countNumberOfSetBits());
}

void Recorder::audioDeviceStopped()
{
    rate.store(0);
    auto expected = State::recording;
    state.compare_exchange_strong(expected, State::failed);
}

void Recorder::audioDeviceIOCallbackWithContext(const float* const* inputs, int ni, float* const* outputs, int no,
                                                int samples, const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < no; ++ch)
        if (outputs[ch])
            std::fill_n(outputs[ch], samples, 0.0f);
    if (state.load() != State::recording)
        return;
    const float* activeInputs[2]{};
    float* activeOutput = nullptr;
    int activeInputCount = 0, activeOutputCount = 0;
    for (int ch = 0; ch < ni; ++ch)
        if (inputs[ch] && activeInputCount < 2)
            activeInputs[activeInputCount++] = inputs[ch];
    for (int ch = 0; ch < no; ++ch)
        if (outputs[ch])
        {
            activeOutput = outputs[ch];
            ++activeOutputCount;
        }
    if (activeInputCount != 2 || activeOutputCount != 1)
    {
        state.store(State::failed);
        return;
    }

    const int from = cursor.load();
    const int count = std::min(samples, length.load() - from);
    std::copy_n(playback.data() + from, count, activeOutput);
    std::copy_n(activeInputs[loop], count, result.loopback.data() + from);
    std::copy_n(activeInputs[mic], count, result.microphone.data() + from);
    float inputPeak = 0.0f, targetPeak = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        inputPeak = std::max(inputPeak, std::abs(activeInputs[loop][i]));
        targetPeak = std::max(targetPeak, std::abs(activeInputs[mic][i]));
    }
    loopPeak.store(inputPeak, std::memory_order_relaxed);
    micPeak.store(targetPeak, std::memory_order_relaxed);
    cursor.store(from + count);
    if (from + count == length.load())
        state.store(State::complete);
}
} // namespace capture
