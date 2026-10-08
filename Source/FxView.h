#pragma once

#include "PluginProcessor.h"
#include "Sketch.h"

// One effect as a small card: on/off, its Mix knob, and a "tweak" button that pops up the rest
class FxCard : public juce::Component
{
public:
    FxCard(AudioPluginAudioProcessor&, int slot, const juce::String& name, const juce::String& prefix,
        std::function<void(ControlPanel&)> buildPanel);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AudioPluginAudioProcessor& proc;
    const juce::Colour colour;
    const juce::String name;
    std::function<void(ControlPanel&)> buildPanel;

    juce::ToggleButton onToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    SketchKnob mix;
    juce::TextButton tweakButton{ "tweak" };
};

// All four effects for one sound, in signal-chain order
class FxRow : public juce::Component
{
public:
    FxRow(AudioPluginAudioProcessor&, int slot);

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
};