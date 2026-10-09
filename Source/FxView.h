#pragma once

#include "PluginProcessor.h"
#include "Sketch.h"

// One effect as a small card: on/off, its Mix knob, and a "tweak" button that pops up the rest
class FxCard : public juce::Component,
    public juce::SettableTooltipClient
{
public:
    FxCard(AudioPluginAudioProcessor&, int slot, const juce::String& name, const juce::String& prefix,
        std::function<void(ControlPanel&)> buildPanel);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override { dragStarted = false; }
    void mouseDrag(const juce::MouseEvent&) override; // drag the card onto another row = copy this effect

private:
    void startDrag(juce::Component& source);

    AudioPluginAudioProcessor& proc;
    const int slot;
    const juce::String prefix;
    const juce::Colour colour;
    const juce::String name;
    bool dragStarted = false;
    std::function<void(ControlPanel&)> buildPanel;

    juce::ToggleButton onToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    SketchKnob mix;
    DragButton tweakButton;
};

// All four effects for one sound, in signal-chain order
class FxRow : public juce::Component,
    public juce::DragAndDropTarget   // drop a copied effect/knob from another sound here
{
public:
    FxRow(AudioPluginAudioProcessor&, int slot);

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    void paintOverChildren(juce::Graphics&) override; // the "drop to copy" highlight sits above the cards

    void paint(juce::Graphics&) override;
    void resized() override;
    void poll(); // keeps the sample name current

private:
    AudioPluginAudioProcessor& proc;
    const int slot;
    const juce::Colour colour;
    SamplePtr shownSample;
    juce::String sampleName;
    juce::Rectangle<int> numberArea, nameArea;
    std::array<std::unique_ptr<FxCard>, 4> cards;
    Sketch::CopyDrag copyHover;
};