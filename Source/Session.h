#pragma once

#include "Capture.h"

namespace capture
{
struct Control
{
    juce::String id, label;
    double minimum, maximum, step, initial;
    double modelMinimum, modelMaximum;
};

struct Priority
{
    int count;
    std::vector<std::pair<double, double>> ranges;
};

struct Point
{
    std::vector<double> dials;
    bool test = false;
    juce::String recording;
};

std::vector<Control> parseControls(const juce::String&);
std::vector<Priority> parsePriorities(const juce::String&, const std::vector<Control>&);
std::vector<Point> plan(const std::vector<Control>&, const std::vector<Priority>&, int trainCount, int testCount);

class Session
{
public:
    static Session create(const juce::File&, juce::String name, std::vector<Control>,
                          std::vector<Point>, const std::vector<float>& stimulus, double sendDb);
    static Session open(const juce::File&);

    void calibrate(const Recording&, const Analysed&);
    void saveTake(size_t index, const Recording&, const Analysed&);
    void exportDataset(bool normalise = false) const;
    void setSendDb(double);
    bool hasCalibration() const { return !calibration.isEmpty(); }
    std::vector<float> stimulus() const;
    int completed() const;
    juce::String pointLabel(size_t index) const;

    juce::File folder;
    juce::String amplifier;
    std::vector<Control> controls;
    std::vector<Point> points;
    double sendDb = -18;

private:
    void save() const;
    juce::String writeCapture(const Recording&, const Analysed&) const;
    juce::String calibration;
};
} // namespace capture
