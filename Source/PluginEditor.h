#pragma once

#include "PluginProcessor.h"
#include "Sketch.h"
#include "FxView.h"

// One sound: number, waveform (drop zone), Volume + Pitch, Mute/Reverse,
// and a row of buttons that pop up the rest (start, envelope, glide, arp) as little notes.
class SlotStrip : public juce::Component,
    public juce::FileDragAndDropTarget
{
public:
    SlotStrip(AudioPluginAudioProcessor&, int slot);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true;  repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped(const juce::StringArray&, int, int) override;

    void poll(); // called at 60 Hz by the editor
    void mouseMove(const juce::MouseEvent&) override;

    // Layer linking: clicking the number reports here; the editor runs the "pick a target" step
    std::function<void(int slot)> onNumberClicked;
    void setLinkMode(int sourceSlot);                 // -1 = not linking
    juce::Point<int> getLinkAnchor() const { return { numberArea.getX(), numberArea.getCentreY() }; }

private:
    struct PopupButton
    {
        juce::TextButton button;
        std::function<std::unique_ptr<ControlPanel>()> makePanel;
        std::function<bool()> isLit; // highlighted while that feature is in use
    };

    void addPopup(const juce::String& name, std::function<void(ControlPanel&)> build, std::function<bool()> isLit);
    bool paramOn(const juce::String& name) const;
    bool anyChanged(std::initializer_list<const char*> names) const;
    void rebuildWaveImage();

    AudioPluginAudioProcessor& proc;
    const int index;
    const juce::Colour colour;

    SketchKnob volume, pitch;
    juce::ToggleButton muteButton{ "mute" }, reverseButton{ "reverse" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, reverseAtt;
    juce::TextButton clearButton{ "x" };
    std::vector<std::unique_ptr<PopupButton>> popups;

    juce::Rectangle<int> numberArea, waveArea;
    juce::Image waveImage;           // waveform drawn once, reused every frame
    std::vector<float> peaks;
    juce::String sampleName;
    SamplePtr shownSample;
    bool shownReverse = false, dragOver = false;
    float shownStart = -1.0f, shownRand = -1.0f, flash = 0.0f;
    bool shownLoop = false;
    enum class MuteState { off, layerOnly, silent } muteState = MuteState::off;
    float shownLoopStart = 0.0f, shownLoopEnd = 1.0f;
    int linkSource = -1;
    int seenTriggers = 0;

    struct Playhead { float pos, level; };
    std::array<Playhead, kNumVoices> heads{};
    int numHeads = 0;

    std::unique_ptr<juce::FileChooser> chooser;
};

// Header preset controls:  < [preset list] >  Save
class PresetBar : public juce::Component
{
public:
    explicit PresetBar(AudioPluginAudioProcessor&);
    void resized() override;
    void refresh();                    // rescan the preset folder and show the current name
    void poll();                       // picks up a name change from the host (e.g. project reload)

private:
    enum { initId = 1, firstFileId = 2, openId = 10000, folderId = 10001 };

    void step(int delta);
    void save();
    void open();

    AudioPluginAudioProcessor& proc;
    juce::TextButton prevButton{ "<" }, nextButton{ ">" }, saveButton{ "Save" };
    juce::ComboBox box;
    juce::Array<juce::File> files;
    juce::String shownName;
    std::unique_ptr<juce::FileChooser> chooser;
};

class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor,
    public Sketch::NoteHost,
    private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void showNote(std::unique_ptr<juce::Component> content, juce::Component& anchor) override;

    void parentHierarchyChanged() override { useSoftwareRenderer(); }

private:
    void timerCallback() override;
    void showFx(bool fx);
    void closeNote();
    void numberClicked(int slot);
    juce::uint32 currentLinkMask() const;
    void useSoftwareRenderer();

    AudioPluginAudioProcessor& processorRef;
    juce::SharedResourcePointer<SketchLookAndFeel> lnf; // one per plugin, shared by open editors; outlives our components

    PresetBar presetBar;
    SketchKnob masterKnob;
    juce::TextButton soundsTab{ "sounds" }, fxTab{ "fx" };

    juce::Component soundsView, fxView;
    std::vector<std::unique_ptr<SlotStrip>> strips;
    std::vector<std::unique_ptr<FxRow>> fxRows;

    // Layer links: arrows drawn over the left margin of the sounds screen
    struct LinkOverlay : public juce::Component
    {
        explicit LinkOverlay(AudioPluginAudioProcessorEditor& e) : editor(e) { setInterceptsMouseClicks(false, false); }
        void paint(juce::Graphics&) override;
        AudioPluginAudioProcessorEditor& editor;
    };
    LinkOverlay linkOverlay{ *this };
    int linkSource = -1;            // sound whose number was clicked first, -1 = not linking
    juce::uint32 shownLinks = 0;
    juce::Rectangle<int> hintArea;

    std::unique_ptr<juce::Component> noteContent; // the open pop-up note, if any (owned here, see showNote)
    std::unique_ptr<juce::CallOutBox> note;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};