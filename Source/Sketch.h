#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

// Hand-drawn drawing helpers. Every stroke is drawn a few times with a smooth, seeded wobble,
// so it looks like crayon but stays identical between repaints (no shimmering).
namespace Sketch
{
    juce::Path wobble(const juce::Path& src, juce::Random& rng, float amount);
    void stroke(juce::Graphics&, const juce::Path&, juce::Colour, float thickness, juce::uint32 seed,
        int passes = 3, float wobbleAmount = -1.0f);
    void fill(juce::Graphics&, const juce::Path&, juce::Colour, juce::uint32 seed); // pastel fill with chalky grain
    void paper(juce::Graphics&, juce::Rectangle<int> area);                        // paper background
    juce::Path roundedRect(juce::Rectangle<float>, float corner);

    // The plugin window owns every pop-up note, so it can close them safely before the plugin is removed
    struct NoteHost
    {
        virtual ~NoteHost() = default;
        virtual void showNote(std::unique_ptr<juce::Component> content, juce::Component& anchor) = 0;
    };

    // Pops up `content` as a note under `anchor`, via the plugin window that contains it
    inline void showNote(juce::Component& anchor, std::unique_ptr<juce::Component> content)
    {
        if (auto* host = anchor.findParentComponentOfClass<NoteHost>())
            host->showNote(std::move(content), anchor);
    }

    // Paper + chalk textures: plain memory images (never GPU-backed), shared by everything that draws.
    // They live exactly as long as someone holds a SharedResourcePointer to them (the LookAndFeel,
    // i.e. while a plugin window is open) - NOT in static storage. A GPU image left in a static
    // gets destroyed while Windows unloads the DLL, which deadlocked FL on plugin removal.
    struct Textures
    {
        Textures();
        juce::Image grain, paper;
    };

    inline juce::uint32 seedFor(const void* p, juce::uint32 salt = 0)
    {
        return (juce::uint32)(((juce::pointer_sized_uint)p * 2654435761u) >> 3) ^ (salt * 40503u);
    }
}

//==============================================================================
class SketchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SketchLookAndFeel();

    int openEditors = 0; // plugin windows currently using this (message thread only)

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos,
        float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return juce::FontOptions(17.0f); }
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(17.0f); }
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawPopupMenuUpDownArrow(juce::Graphics&, int w, int h, bool isScrollUpArrow) override;
    juce::Font getPopupMenuFont() override { return juce::FontOptions(18.0f); }
    void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    juce::Component* getParentComponentForMenuOptions(const juce::PopupMenu::Options&) override;
    void drawBubble(juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
    juce::Font getSliderPopupFont(juce::Slider&) override { return juce::FontOptions(16.0f); }

private:
    juce::SharedResourcePointer<Sketch::Textures> textures; // keeps the textures alive while a window is open
};

//==============================================================================
// Compact knob: dial on top, handwritten label underneath. No text box: the value shows in a
// bubble while hovering/dragging. The whole thing (label included) is draggable.
//   double-click = reset to default, right-click = type an exact value,
//   snapStep > 0 = dragging snaps to that step (hold Shift for free movement).
class SketchKnob : public juce::Component
{
public:
    static constexpr int kWidth = 58, kHeight = 62;

    SketchKnob(juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& label,
        const juce::String& suffix, juce::Colour, double snapStep = 0.0);

    void paint(juce::Graphics&) override;
    void resized() override { slider.setBounds(getLocalBounds()); }
    void parentHierarchyChanged() override;

    struct KnobSlider : public juce::Slider
    {
        double snapStep = 0.0;
        std::function<void()> onRightClick;

        double snapValue(double value, DragMode mode) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent& e) override { if (!e.mods.isPopupMenu()) Slider::mouseDrag(e); }
        void mouseUp(const juce::MouseEvent& e) override { if (!e.mods.isPopupMenu()) Slider::mouseUp(e); }
    };
    KnobSlider slider;

private:
    void showValueEditor();
    void closeValueEditor(bool apply);

    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::unique_ptr<juce::TextEditor> valueEditor;
};

//==============================================================================
// Content of a pop-up "note": a title and rows of knobs / toggles / dropdowns bound to parameters.
class ControlPanel : public juce::Component
{
public:
    ControlPanel(juce::AudioProcessorValueTreeState&, juce::Colour, const juce::String& title);

    void addKnob(const juce::String& paramID, const juce::String& label, const juce::String& suffix = {},
        double snapStep = 0.0);
    void addToggle(const juce::String& paramID, const juce::String& label);
    void addCombo(const juce::String& paramID, const juce::String& label, const juce::StringArray& items);
    void newRow();
    void finish(); // sizes the panel to its contents; call after adding everything

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Item
    {
        std::unique_ptr<juce::Component> comp;
        std::unique_ptr<juce::Label> label; // dropdowns only
        int width;
    };

    juce::AudioProcessorValueTreeState& state;
    const juce::Colour colour;
    const juce::String title;
    std::vector<std::vector<Item>> rows{ 1 };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtts;

    static constexpr int kPad = 12, kTitleH = 26;
};