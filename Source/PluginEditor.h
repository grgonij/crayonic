#pragma once

#include "PluginProcessor.h"
#include "Sketch.h"
#include "FxView.h"

// Draws and edits a sound's volume shape on top of its waveform (like an FL automation clip):
// click = add a point, drag = move, right-click = remove, drag a small circle = bend, double-click it = straighten.
class ShapeEditor : public juce::Component
{
public:
    ShapeEditor(AudioPluginAudioProcessor&, int slot, juce::Colour);
    void refresh();                              // re-read the shape (e.g. after loading a preset)
    bool isDragging() const { return dragPoint >= 0 || dragHandle >= 0; }

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { dragPoint = dragHandle = -1; }
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced(2.0f, 6.0f); }
    juce::Point<float> toScreen(float x, float y) const;
    juce::Point<float> handlePos(size_t segment) const;
    int pointAt(juce::Point<float>) const;
    int handleAt(juce::Point<float>) const;
    void commit();

    AudioPluginAudioProcessor& proc;
    const int slot;
    const juce::Colour colour;
    std::vector<ShapePoint> pts;
    int dragPoint = -1, dragHandle = -1;
    float bendAtStart = 0.0f, mouseYAtStart = 0.0f;
};

// One sound: number, waveform (drop zone), Volume + Pitch, Mute/Reverse,
// and a row of buttons that pop up the rest (start, envelope, glide, arp) as little notes.
class SlotStrip : public juce::Component,
    public juce::FileDragAndDropTarget,
    public juce::DragAndDropTarget,   // drop copied settings from another sound here
    public juce::TooltipClient
{
public:
    juce::String getTooltip() override; // only over the number
    SlotStrip(AudioPluginAudioProcessor&, int slot);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true;  repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped(const juce::StringArray&, int, int) override;

    void poll(); // called at 60 Hz by the editor

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // Layer linking: clicking the number reports here; the editor runs the "pick a target" step
    std::function<void(int slot)> onNumberClicked;
    void setLinkMode(int sourceSlot);                 // -1 = not linking
    juce::Point<int> getLinkAnchor() const { return { numberArea.getX(), numberArea.getCentreY() }; }

private:
    struct PopupButton
    {
        DragButton button;   // click = open the note, drag onto another sound = copy these settings
        std::function<std::unique_ptr<ControlPanel>()> makePanel;
        std::function<bool()> isLit; // highlighted while that feature is in use
    };

    void addPopup(const juce::String& name, std::function<void(ControlPanel&)> build, std::function<bool()> isLit,
        const juce::String& title = {}); // title defaults to the button's name
    bool paramOn(const juce::String& name) const;
    bool anyChanged(std::initializer_list<const char*> names) const;
    void rebuildWaveImage();

    AudioPluginAudioProcessor& proc;
    const int index;
    const juce::Colour colour;

    SketchKnob volume, pitch, highPass, lowPass;
    juce::ToggleButton muteButton{ "mute" }, reverseButton{ "reverse" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, reverseAtt;
    juce::TextButton clearButton{ "x" };
    ShapeEditor shapeEditor;
    DragButton shapeButton;       // click = edit the volume shape, drag onto another sound = copy it
    bool editingShape = false;
    ShapePtr shownShape;
    void setEditingShape(bool);
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
    bool numberHover = false;
    Sketch::CopyDrag copyHover;   // a copy being dragged over this sound (fromSlot -1 = none)
    int seenTriggers = 0;

    struct Playhead { float pos, level; };
    std::array<Playhead, kNumVoices> heads{};
    int numHeads = 0;

    std::unique_ptr<juce::FileChooser> chooser;
};

// A hand-drawn die. Every click rolls a new face (and the owner loads a random preset).
class DiceButton : public juce::Button
{
public:
    DiceButton() : juce::Button("random preset") { setTooltip("Random preset"); }
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    void clicked() override;

private:
    int face = 5;
    float tilt = -0.08f;
};

// Header preset controls:  < [preset list] >  dice  Save  kits
class PresetBar : public juce::Component
{
public:
    explicit PresetBar(AudioPluginAudioProcessor&);
    void resized() override;
    void refresh();                    // rescan the presets folder (and kits) and show the current preset
    void poll();                       // picks up a change from the host (e.g. project reload)

private:
    enum { initId = 1, firstFileId = 2, emptyKitBaseId = 20000, openId = 30000 };

    void step(int delta);
    void randomPreset();
    void save();
    void open();
    void showKitsMenu();
    void createKit();
    void exportKit(const juce::File& kit);
    void importKit();
    void changeFolder();
    void showMessage(const juce::String& title, const juce::String& body);

    AudioPluginAudioProcessor& proc;
    juce::TextButton prevButton{ "<" }, nextButton{ ">" }, saveButton{ "Save" }, kitsButton{ "kits" };
    DiceButton dice;
    juce::ComboBox box;
    juce::Array<juce::File> files;     // every preset: top level first, then each kit's
    juce::String shownName;
    juce::File shownFile;
    std::unique_ptr<juce::FileChooser> chooser;
};

class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor,
    public juce::DragAndDropContainer, // drag-to-copy between sounds
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
    std::array<int, kNumLayers* kNumLayers> seenLinkFires{};
    std::array<float, kNumLayers* kNumLayers> linkFlash{};   // 1 = just fired, fades to 0
    bool linkWasFlashing = false;
    juce::TooltipWindow tooltips{ this, 600 };                 // inside the plugin window, not a desktop window
    int linkSource = -1;            // sound whose number was clicked first, -1 = not linking
    juce::uint32 shownLinks = 0;
    juce::Rectangle<int> hintArea;

    std::unique_ptr<juce::Component> noteContent; // the open pop-up note, if any (owned here, see showNote)
    std::unique_ptr<juce::CallOutBox> note;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};