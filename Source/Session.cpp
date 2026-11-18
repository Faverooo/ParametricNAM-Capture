#include "Session.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <set>
#include <stdexcept>

namespace capture
{
namespace
{
using juce::var;

var object() { return var(new juce::DynamicObject()); }
void put(var& value, const char* key, const var& entry) { value.getDynamicObject()->setProperty(key, entry); }

var readJson(const juce::File& file)
{
    var result;
    if (!file.existsAsFile() || juce::JSON::parse(file.loadFileAsString(), result).failed() || !result.isObject())
        throw std::runtime_error("Cannot read " + file.getFileName().toStdString());
    return result;
}

void writeJson(const juce::File& file, const var& value)
{
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(value)) || !temporary.overwriteTargetFileWithTemporary())
        throw std::runtime_error("Cannot write " + file.getFileName().toStdString());
}

void createDirectory(const juce::File& folder)
{
    const auto result = folder.createDirectory();
    if (result.failed())
        throw std::runtime_error(result.getErrorMessage().toStdString());
}

double number(const juce::String& text)
{
    const auto value = text.trim().toStdString();
    char* end = nullptr;
    const double result = std::strtod(value.c_str(), &end);
    if (value.empty() || *end != '\0' || !std::isfinite(result))
        throw std::runtime_error("Invalid number: " + value);
    return result;
}

juce::StringArray fields(const juce::String& line, const char* separator)
{
    auto list = juce::StringArray::fromTokens(line, separator, "");
    list.trim();
    return list;
}

int gridSteps(const Control& control)
{
    return int(std::floor((control.maximum - control.minimum) / control.step + 1e-8));
}

var controlsJson(const std::vector<Control>& controls)
{
    juce::Array<var> entries;
    for (const auto& control : controls)
    {
        auto entry = object();
        put(entry, "id", control.id);
        put(entry, "label", control.label);
        put(entry, "min", control.minimum);
        put(entry, "max", control.maximum);
        put(entry, "default", control.initial);
        put(entry, "model_min", control.modelMinimum);
        put(entry, "model_max", control.modelMaximum);
        put(entry, "capture_step", control.step);
        entries.add(entry);
    }
    return var(entries);
}

var dialsJson(const std::vector<double>& dials)
{
    juce::Array<var> entries;
    for (double dial : dials)
        entries.add(dial);
    return var(entries);
}

double halton(int index, int base)
{
    double value = 0, scale = 1;
    for (; index > 0; index /= base)
    {
        scale /= base;
        value += scale * (index % base);
    }
    return value;
}

juce::File revision(const juce::File& root, const juce::String& id)
{
    if (id.isEmpty() || id.containsAnyOf("/\\") || id == "." || id == "..")
        throw std::runtime_error("Invalid recording revision.");
    return root.getChildFile("recordings").getChildFile(id);
}
} // namespace

std::vector<Control> parseControls(const juce::String& text)
{
    std::vector<Control> controls;
    std::set<juce::String> ids;
    for (const auto& line : juce::StringArray::fromLines(text))
    {
        if (line.trim().isEmpty())
            continue;
        auto columns = fields(line, ",");
        if (columns.size() != 5 && columns.size() != 7)
            throw std::runtime_error("Each control needs ID, label, min, max, step and optionally model min/max.");
        const double minimum = number(columns[2]), maximum = number(columns[3]), step = number(columns[4]);
        const double modelMinimum = columns.size() == 7 ? number(columns[5]) : 0;
        const double modelMaximum = columns.size() == 7 ? number(columns[6]) : 1;
        if (columns[0].isEmpty() || columns[1].isEmpty()
            || !columns[0].containsOnly("abcdefghijklmnopqrstuvwxyz0123456789")
            || !ids.insert(columns[0]).second || minimum >= maximum || !(step > 0)
            || (maximum - minimum) / step > 100000 || step > maximum - minimum || modelMinimum >= modelMaximum)
            throw std::runtime_error("Invalid control: " + line.toStdString());
        const double steps = (maximum - minimum) / step;
        if (std::abs(steps - std::round(steps)) > 1e-7)
            throw std::runtime_error("Each range must contain a whole number of steps.");
        controls.push_back({columns[0], columns[1], minimum, maximum, step, (minimum + maximum) / 2,
                            modelMinimum, modelMaximum});
    }
    if (controls.empty() || controls.size() > 8)
        throw std::runtime_error("Provide between one and eight controls.");
    return controls;
}

std::vector<Priority> parsePriorities(const juce::String& text, const std::vector<Control>& controls)
{
    std::vector<Priority> priorities;
    for (const auto& line : juce::StringArray::fromLines(text))
    {
        if (line.trim().isEmpty())
            continue;
        const auto columns = fields(line, ",");
        if (columns.size() != int(controls.size()) + 1)
            throw std::runtime_error("Each priority needs a count and one value or range per control.");
        const double count = number(columns[0]);
        if (count < 1 || count > 2000 || count != std::floor(count))
            throw std::runtime_error("Invalid priority count.");
        Priority priority{static_cast<int>(count), {}};
        for (size_t i = 0; i < controls.size(); ++i)
        {
            const auto& control = controls[i];
            const auto column = columns[int(i + 1)];
            if (column == "*")
            {
                priority.ranges.emplace_back(control.minimum, control.maximum);
                continue;
            }
            const auto endpoints = fields(column, ":");
            if (endpoints.size() < 1 || endpoints.size() > 2)
                throw std::runtime_error("Invalid priority range.");
            const double low = number(endpoints[0]);
            const double high = endpoints.size() == 2 ? number(endpoints[1]) : low;
            if (low < control.minimum || high > control.maximum || low > high)
                throw std::runtime_error("Priority lies outside " + control.label.toStdString());
            priority.ranges.emplace_back(low, high);
        }
        priorities.push_back(std::move(priority));
    }
    return priorities;
}

std::vector<Point> plan(const std::vector<Control>& controls, const std::vector<Priority>& priorities,
                        int trainCount, int testCount)
{
    if (trainCount < 1 || testCount < 1 || trainCount > 2000 || testCount > 2000)
        throw std::runtime_error("Choose between 1 and 2000 takes in each split.");
    const int reserved = std::accumulate(priorities.begin(), priorities.end(), 0,
                                         [](int sum, const Priority& priority) { return sum + priority.count; });
    if (reserved > trainCount)
        throw std::runtime_error("The priority takes exceed the training budget.");
    std::set<std::vector<int>> used;
    std::vector<Point> training, testing;
    const std::array<int, 8> bases{2, 3, 5, 7, 11, 13, 17, 19};

    auto add = [&](int count, const std::vector<std::pair<double, double>>& ranges, bool test)
    {
        if (count == 0)
            return;
        std::vector<int> first, width;
        for (size_t i = 0; i < controls.size(); ++i)
        {
            const auto& control = controls[i];
            const auto low = int(std::ceil((ranges[i].first - control.minimum) / control.step - 1e-8));
            const auto high = std::min(gridSteps(control),
                int(std::floor((ranges[i].second - control.minimum) / control.step + 1e-8)));
            if (high < low)
                throw std::runtime_error("Priority includes no grid value for " + control.label.toStdString());
            first.push_back(low);
            width.push_back(high - low + 1);
        }
        auto accept = [&](const std::vector<int>& point)
        {
            if (!used.insert(point).second)
                return;
            Point entry;
            entry.test = test;
            for (size_t i = 0; i < controls.size(); ++i)
                entry.dials.push_back(std::min(controls[i].maximum, controls[i].minimum + point[i] * controls[i].step));
            (test ? testing : training).push_back(std::move(entry));
            --count;
        };

        for (int index = test ? 4001 : 1, attempts = 0; count > 0 && attempts < 50000; ++index, ++attempts)
        {
            auto point = first;
            for (size_t i = 0; i < point.size(); ++i)
                point[i] += int(halton(index, bases[i]) * width[i]);
            accept(point);
        }
        auto point = first;
        while (count > 0)
        {
            accept(point);
            size_t i = 0;
            for (; i < point.size(); ++i)
            {
                if (++point[i] < first[i] + width[i])
                    break;
                point[i] = first[i];
            }
            if (i == point.size())
                break;
        }
        if (count > 0)
            throw std::runtime_error("Not enough distinct points. Reduce the take count or refine the control grid.");
    };

    for (const auto& priority : priorities)
        add(priority.count, priority.ranges, false);
    std::vector<std::pair<double, double>> full;
    for (const auto& control : controls)
        full.emplace_back(control.minimum, control.maximum);
    add(testCount, full, true);
    add(trainCount - reserved, full, false);
    training.insert(training.end(), testing.begin(), testing.end());
    return training;
}

Session Session::create(const juce::File& folder, juce::String name, std::vector<Control> controls,
                        std::vector<Point> points, const std::vector<float>& input, double sendDb)
{
    if (name.trim().isEmpty() || sendDb > 0 || sendDb < -60)
        throw std::runtime_error("Provide an amplifier name and a send level from -60 to 0 dB.");
    if (folder.exists())
        throw std::runtime_error("Choose a new session folder.");
    createDirectory(folder);
    writeWav(folder.getChildFile("stimulus.wav"), input);

    Session result;
    result.folder = folder;
    result.amplifier = name.trim();
    result.controls = std::move(controls);
    result.points = std::move(points);
    result.sendDb = sendDb;
    result.save();
    return result;
}

Session Session::open(const juce::File& folder)
{
    const auto doc = readJson(folder.getChildFile("session.json"));
    if (doc["version"] != var(1) || !doc["controls"].isArray() || !doc["points"].isArray())
        throw std::runtime_error("Unsupported capture session.");
    Session result;
    result.folder = folder;
    result.amplifier = doc["amplifier"].toString();
    result.sendDb = double(doc["send_db"]);
    result.calibration = doc["calibration"].toString();
    for (const auto& c : *doc["controls"].getArray())
        result.controls.push_back({c["id"].toString(), c["label"].toString(), double(c["min"]), double(c["max"]),
                                   double(c["capture_step"]), double(c["default"]), double(c["model_min"]),
                                   double(c["model_max"])});
    for (const auto& p : *doc["points"].getArray())
    {
        Point point;
        if (!p["dials"].isArray() || p["dials"].size() != int(result.controls.size()))
            throw std::runtime_error("Invalid capture point.");
        for (const auto& dial : *p["dials"].getArray())
            point.dials.push_back(double(dial));
        point.test = bool(p["test"]);
        point.recording = p["recording"].toString();
        result.points.push_back(std::move(point));
    }
    if (result.controls.empty() || result.points.empty() || result.sendDb > 0 || result.sendDb < -60)
        throw std::runtime_error("Invalid capture session.");
    return result;
}

void Session::save() const
{
    auto doc = object();
    put(doc, "version", 1);
    put(doc, "amplifier", amplifier);
    put(doc, "send_db", sendDb);
    put(doc, "calibration", calibration);
    put(doc, "controls", controlsJson(controls));
    juce::Array<var> rows;
    for (const auto& point : points)
    {
        auto row = object();
        put(row, "dials", dialsJson(point.dials));
        put(row, "test", point.test);
        put(row, "recording", point.recording);
        rows.add(row);
    }
    put(doc, "points", var(rows));
    writeJson(folder.getChildFile("session.json"), doc);
}

juce::String Session::writeCapture(const Recording& recording, const Analysed& analysed) const
{
    const auto id = juce::Uuid().toString();
    const auto path = revision(folder, id);
    createDirectory(path);
    writeWav(path.getChildFile("raw-loopback.wav"), recording.loopback);
    writeWav(path.getChildFile("raw-microphone.wav"), recording.microphone);
    writeWav(path.getChildFile("input.wav"), analysed.input);
    writeWav(path.getChildFile("target.wav"), analysed.target);
    auto metadata = object();
    put(metadata, "delay_samples", analysed.delay);
    put(metadata, "loopback_peak", analysed.loopPeak);
    put(metadata, "microphone_peak", analysed.micPeak);
    put(metadata, "marker_correlation", analysed.correlation);
    writeJson(path.getChildFile("capture.json"), metadata);
    return id;
}

void Session::calibrate(const Recording& recording, const Analysed& analysed)
{
    requireUsable(analysed);
    if (completed() > 0)
    {
        const auto original = readWav(revision(folder, calibration).getChildFile("input.wav"));
        if (relativeInputError(original, analysed.input) > 0.12)
            throw std::runtime_error("The calibration differs from the original. Restore the interface levels and routing.");
        writeCapture(recording, analysed);
        return;
    }
    const auto id = writeCapture(recording, analysed);
    const auto old = calibration;
    calibration = id;
    try { save(); }
    catch (...) { calibration = old; throw; }
}

void Session::setSendDb(double db)
{
    if (!std::isfinite(db) || db > 0 || db < -60 || completed() != 0)
        throw std::runtime_error("Set a send level between -60 and 0 dB before the first take.");
    const auto old = calibration;
    const auto previousDb = sendDb;
    calibration.clear();
    sendDb = db;
    try { save(); }
    catch (...) { calibration = old; sendDb = previousDb; throw; }
}

void Session::saveTake(size_t index, const Recording& recording, const Analysed& analysed)
{
    requireUsable(analysed);
    if (!hasCalibration())
        throw std::runtime_error("Run calibration before recording a take.");
    const auto reference = readWav(revision(folder, calibration).getChildFile("input.wav"));
    if (relativeInputError(reference, analysed.input) > 0.12)
        throw std::runtime_error("The loopback differs from calibration. Check routing and interface levels.");
    auto& current = points.at(index).recording;
    const auto old = current;
    current = writeCapture(recording, analysed);
    try { save(); }
    catch (...) { current = old; throw; }
}

std::vector<float> Session::stimulus() const { return readWav(folder.getChildFile("stimulus.wav")); }

int Session::completed() const
{
    return int(std::count_if(points.begin(), points.end(), [](const auto& p) { return p.recording.isNotEmpty(); }));
}

juce::String Session::pointLabel(size_t index) const
{
    const auto& point = points.at(index);
    auto text = juce::String(int(index + 1)) + (point.test ? "  Test    " : "  Train  ");
    for (size_t i = 0; i < controls.size(); ++i)
        text += controls[i].label + " " + juce::String(point.dials[i], 3).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + "   ";
    return text;
}

void Session::exportDataset(bool normalise) const
{
    if (!hasCalibration() || completed() != int(points.size()))
        throw std::runtime_error("Record all planned takes before exporting.");
    const auto destination = folder.getChildFile("dataset");
    createDirectory(destination);
    auto document = object();
    auto profile = object();
    put(document, "sample_rate", sampleRate);
    put(document, "capture_session", folder.getFileName());
    put(profile, "amplifier", amplifier);
    put(profile, "controls", controlsJson(controls));
    juce::Array<var> names, train, test;
    for (const auto& control : controls)
        names.add(control.id);
    put(document, "knob_names", var(names));
    const auto input = readWav(revision(folder, calibration).getChildFile("input.wav"));
    const auto exportId = juce::Uuid().toString();
    const auto audioFolder = destination.getChildFile(exportId);
    createDirectory(audioFolder);
    writeWav(audioFolder.getChildFile("reference.wav"), input);
    put(document, "x", exportId + "/reference.wav");
    float scale = 1.0f;
    if (normalise)
    {
        float peak = 0.0f;
        for (const auto& point : points)
        {
            const auto target = readWav(revision(folder, point.recording).getChildFile("target.wav"));
            for (const auto sample : target)
                peak = std::max(peak, std::abs(sample));
        }
        if (peak < 0.001f)
            throw std::runtime_error("Cannot normalise a silent dataset.");
        scale = juce::Decibels::decibelsToGain(-3.0f) / peak;
    }
    put(document, "target_gain_db", double(juce::Decibels::gainToDecibels(scale)));
    for (size_t index = 0; index < points.size(); ++index)
    {
        const auto& point = points[index];
        const auto recording = revision(folder, point.recording);
        if (!recording.getChildFile("target.wav").existsAsFile()
            || relativeInputError(input, readWav(recording.getChildFile("input.wav"))) > 0.12)
            throw std::runtime_error("Take " + std::to_string(index + 1) + " no longer matches calibration. Record it again.");
        auto entry = object();
        put(entry, "id", juce::String(int(index + 1)));
        const auto takeName = juce::String(int(index + 1));
        const auto inputName = takeName + "-input.wav";
        const auto targetName = takeName + "-target.wav";
        if (!recording.getChildFile("input.wav").copyFileTo(audioFolder.getChildFile(inputName)))
            throw std::runtime_error("Cannot export input for take " + takeName.toStdString());
        auto target = readWav(recording.getChildFile("target.wav"));
        if (normalise)
            for (auto& sample : target)
                sample *= scale;
        writeWav(audioFolder.getChildFile(targetName), target);
        put(entry, "x", exportId + "/" + inputName);
        put(entry, "y", exportId + "/" + targetName);
        put(entry, "knobs_dial", dialsJson(point.dials));
        juce::Array<var> model;
        for (size_t i = 0; i < controls.size(); ++i)
        {
            const auto& c = controls[i];
            model.add(std::lerp(c.modelMinimum, c.modelMaximum, (point.dials[i] - c.minimum) / (c.maximum - c.minimum)));
        }
        put(entry, "g", var(model));
        (point.test ? test : train).add(entry);
    }
    put(document, "train", var(train));
    put(document, "test", var(test));
    writeJson(destination.getChildFile("profile.json"), profile);
    writeJson(destination.getChildFile("manifest.json"), document);
}
} // namespace capture
