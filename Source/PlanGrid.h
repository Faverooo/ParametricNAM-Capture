#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <memory>
#include <vector>

class PlanGrid final : public juce::Component
{
public:
    PlanGrid()
    {
        grid.setSize(1, 1);
        viewport.setViewedComponent(&grid, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
    }

    void setHeadings(const juce::StringArray& newHeadings)
    {
        headings = newHeadings;
        for (auto& row : rows)
            sync(row);
        resized();
    }

    void addRow(const juce::StringArray& values)
    {
        rows.emplace_back();
        auto& row = rows.back();
        row.remove = std::make_unique<juce::TextButton>("X");
        auto* button = row.remove.get();
        button->setTitle("Remove row");
        grid.addAndMakeVisible(*button);
        button->onClick = [this, button]
        {
            const auto it = std::find_if(rows.begin(), rows.end(), [button](const auto& item) { return item.remove.get() == button; });
            const auto index = static_cast<int>(it - rows.begin());
            rows.erase(it);
            resized();
            if (onRemove)
                onRemove(index);
            notifyChange();
        };
        sync(row);
        for (int i = 0; i < values.size() && i < headings.size(); ++i)
            row.fields[static_cast<size_t>(i)]->setText(values[i], false);
        resized();
        if (getHeight() > 0)
            viewport.setViewPosition(0, grid.getHeight());
        notifyChange();
    }

    void removeColumn(int column)
    {
        if (column < 0 || column >= headings.size())
            return;

        headings.remove(column);
        for (auto& row : rows)
            row.fields.erase(row.fields.begin() + column);
        resized();
    }

    int size() const { return static_cast<int>(rows.size()); }

    juce::String value(int row, int column) const
    {
        return rows.at(static_cast<size_t>(row)).fields.at(static_cast<size_t>(column))->getText().trim();
    }

    juce::String text(juce::String emptyValue = {}) const
    {
        juce::StringArray lines;
        for (int row = 0; row < size(); ++row)
        {
            juce::StringArray cells;
            for (int col = 0; col < headings.size(); ++col)
            {
                auto cell = value(row, col);
                if (cell.containsChar(','))
                    throw std::runtime_error("Use a decimal point for values and omit commas in labels.");
                cells.add(cell.isEmpty() && col > 0 ? emptyValue : cell);
            }
            lines.add(cells.joinIntoString(","));
        }
        return lines.joinIntoString("\n");
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto area = getLocalBounds().toFloat();
        graphics.setColour(juce::Colour(0xff182238));
        graphics.fillRoundedRectangle(area, 10.0f);
        graphics.setColour(juce::Colour(0xff9caecf));
        graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        for (int i = 0; i < headings.size(); ++i)
            graphics.drawFittedText(headings[i], 8 + i * columnWidth(), 1, columnWidth() - 6, 23,
                                    juce::Justification::centredLeft, 1);
    }

    void resized() override
    {
        viewport.setBounds(getLocalBounds().withTrimmedTop(25));
        const int width = std::max(1, viewport.getWidth() - viewport.getScrollBarThickness());
        grid.setSize(width, std::max(1, size() * 32 + 3));
        for (int r = 0; r < size(); ++r)
        {
            const int y = 2 + r * 32;
            for (int c = 0; c < headings.size(); ++c)
                rows[static_cast<size_t>(r)].fields[static_cast<size_t>(c)]->setBounds(
                    6 + c * columnWidth(), y, std::max(1, columnWidth() - 6), 26);
            rows[static_cast<size_t>(r)].remove->setBounds(width - 32, y, 26, 26);
        }
        repaint();
    }

    std::function<void()> onChange;
    std::function<void(int)> onRemove;

private:
    struct Row
    {
        std::vector<std::unique_ptr<juce::TextEditor>> fields;
        std::unique_ptr<juce::TextButton> remove;
    };

    void notifyChange()
    {
        if (onChange)
            onChange();
    }

    void sync(Row& row)
    {
        row.fields.resize(static_cast<size_t>(headings.size()));
        for (size_t i = 0; i < row.fields.size(); ++i)
        {
            auto& field = row.fields[i];
            if (!field)
            {
                field = std::make_unique<juce::TextEditor>();
                field->onTextChange = [this] { notifyChange(); };
                grid.addAndMakeVisible(*field);
            }
            field->setTitle(headings[static_cast<int>(i)]);
            field->setTextToShowWhenEmpty(placeholders ? "*" : "", juce::Colour(0xff8094b6));
        }
    }

    int columnWidth() const
    {
        return std::max(1, (grid.getWidth() - 38) / std::max(1, headings.size()));
    }

public:
    bool placeholders = false;

private:
    juce::Component grid;
    juce::Viewport viewport;
    juce::StringArray headings;
    std::vector<Row> rows;
};
