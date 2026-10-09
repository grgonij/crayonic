#include "FxView.h"

FxCard::FxCard(AudioPluginAudioProcessor& p, int slot, const juce::String& n, const juce::String& prefix,
    std::function<void(ControlPanel&)> build)
    : proc(p), slot(slot), prefix(prefix), colour(Theme::slotColour(slot)), name(n), buildPanel(std::move(build)),
    onToggle(n),
    mix(p.apvts, layerParamID(slot, prefix + "_mix"), "mix", " %", colour)
{
    onToggle.setColour(juce::ToggleButton::tickColourId, colour);
    addAndMakeVisible(onToggle);
    onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, layerParamID(slot, prefix + "_on"), onToggle);
    onToggle.onStateChange = [this] { repaint(); };

    addAndMakeVisible(mix);
    addAndMakeVisible(tweakButton);
    tweakButton.setButtonText("tweak");
    tweakButton.onDragOut = [this] { startDrag(tweakButton); };
    setTooltip("Drag this card onto another sound's row to copy the whole " + n);
    tweakButton.onClick = [this]
        {
            auto panel = std::make_unique<ControlPanel>(proc.apvts, colour, name);
            buildPanel(*panel);
            panel->finish();
            Sketch::showNote(tweakButton, std::move(panel));
        };
}

void FxCard::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragStarted && e.getDistanceFromDragStart() > 6)
    {
        dragStarted = true;
        startDrag(*this);
    }
}

void FxCard::startDrag(juce::Component& source)
{
    Sketch::startCopyDrag(source, slot, name, proc.settingNamesWithPrefix(slot, prefix + "_")); // every setting of this effect
}

void FxCard::paint(juce::Graphics& g)
{
    const auto card = Sketch::roundedRect(getLocalBounds().toFloat().reduced(5.0f), 10.0f);
    const auto seed = Sketch::seedFor(this);
    const bool on = onToggle.getToggleState();

    if (on) Sketch::fill(g, card, colour.withAlpha(0.35f), seed); // light pastel wash when active
    Sketch::stroke(g, card, on ? colour.darker(0.4f) : Theme::ink.withAlpha(0.3f), on ? 1.6f : 1.2f, seed + 1, 2);
}

void FxCard::resized()
{
    auto r = getLocalBounds().reduced(12, 10);
    onToggle.setBounds(r.removeFromTop(28));
    mix.setBounds(r.removeFromRight(SketchKnob::kWidth).withSizeKeepingCentre(SketchKnob::kWidth, SketchKnob::kHeight));
    tweakButton.setBounds(r.removeFromBottom(30).removeFromLeft(70));
}

//==============================================================================
FxRow::FxRow(AudioPluginAudioProcessor& p, int s)
    : proc(p), slot(s), colour(Theme::slotColour(s))
{
    auto id = [s](const char* n) { return layerParamID(s, n); };

    cards[0] = std::make_unique<FxCard>(p, s, "distortion", "dist", [id](ControlPanel& c)
        {
            c.addCombo(id("dist_type"), "type", kDistTypes);
            c.addKnob(id("dist_drive"), "drive", " dB");
            c.addKnob(id("dist_tone"), "tone", " Hz");
            c.addKnob(id("dist_mix"), "mix", " %");
        });
    cards[1] = std::make_unique<FxCard>(p, s, "granular", "gr", [id](ControlPanel& c)
        {
            c.addKnob(id("gr_size"), "size", " ms");
            c.addKnob(id("gr_density"), "density", " /s");
            c.addKnob(id("gr_spray"), "spray", " ms");
            c.addKnob(id("gr_pitch"), "pitch", " st", 1.0); // snaps to semitones
            c.addKnob(id("gr_mix"), "mix", " %");
        });
    cards[2] = std::make_unique<FxCard>(p, s, "delay", "dly", [id](ControlPanel& c)
        {
            c.addCombo(id("dly_time"), "time", kDelayTimes);
            c.addToggle(id("dly_ping"), "ping-pong");
            c.newRow();
            c.addKnob(id("dly_fb"), "feedback", " %");
            c.addKnob(id("dly_mix"), "mix", " %");
        });
    cards[3] = std::make_unique<FxCard>(p, s, "reverb", "rev", [id](ControlPanel& c)
        {
            c.addKnob(id("rev_size"), "size", " %");
            c.addKnob(id("rev_damp"), "damping", " %");
            c.addKnob(id("rev_width"), "width", " %");
            c.addKnob(id("rev_mix"), "mix", " %");
        });

    for (auto& card : cards)
        addAndMakeVisible(*card);

    poll();
}

void FxRow::poll()
{
    auto current = proc.getSample(slot);
    if (current != shownSample)
    {
        shownSample = current;
        sampleName = current ? juce::File(current->path).getFileNameWithoutExtension() : juce::String("(empty)");
        repaint(nameArea);
    }
}

bool FxRow::isInterestedInDragSource(const SourceDetails& details)
{
    Sketch::CopyDrag drag;
    return Sketch::readCopyDrag(details.description, drag) && drag.fromSlot != slot;
}

void FxRow::itemDragEnter(const SourceDetails& details) { Sketch::readCopyDrag(details.description, copyHover); repaint(); }
void FxRow::itemDragExit(const SourceDetails&) { copyHover = {}; repaint(); }

void FxRow::itemDropped(const SourceDetails& details)
{
    Sketch::CopyDrag drag;
    if (Sketch::readCopyDrag(details.description, drag))
        proc.copySettings(drag.fromSlot, slot, drag.names);
    copyHover = {};
    repaint();
}

void FxRow::paintOverChildren(juce::Graphics& g)
{
    if (copyHover.fromSlot < 0) return;
    const auto ink = Theme::slotColour(copyHover.fromSlot).darker(0.5f);
    Sketch::stroke(g, Sketch::roundedRect(getLocalBounds().toFloat().reduced(3.0f), 10.0f), ink, 3.0f, Sketch::seedFor(this, 15), 2);
    const auto text = "drop to copy " + copyHover.what + " from sound " + juce::String(copyHover.fromSlot + 1);
    const int w = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(19.0f), text) + 28;
    const auto box = getLocalBounds().withSizeKeepingCentre(w, 32).toFloat();
    g.setColour(Theme::paper.withAlpha(0.95f));
    g.fillRoundedRectangle(box, 7.0f);
    Sketch::stroke(g, Sketch::roundedRect(box, 7.0f), ink, 1.6f, Sketch::seedFor(this, 16), 2, 0.4f);
    g.setColour(Theme::ink);
    g.setFont(19.0f);
    g.drawText(text, box, juce::Justification::centred);
}

void FxRow::paint(juce::Graphics& g)
{
    g.setColour(colour.darker(0.35f));
    g.setFont(40.0f);
    g.drawText(juce::String(slot + 1), numberArea, juce::Justification::centred);

    g.setColour(Theme::ink.withAlpha(0.75f));
    g.setFont(17.0f);
    g.drawFittedText(sampleName, nameArea, juce::Justification::centredLeft, 3);

    juce::Path divider;
    divider.startNewSubPath(10.0f, (float)getHeight() - 1.5f);
    divider.lineTo((float)getWidth() - 10.0f, (float)getHeight() - 1.5f);
    Sketch::stroke(g, divider, Theme::ink.withAlpha(0.15f), 1.0f, Sketch::seedFor(this), 1);
}

void FxRow::resized()
{
    auto r = getLocalBounds().reduced(8, 6);
    r.removeFromLeft(28);            // same gutter as the sounds screen, so the numbers line up
    numberArea = r.removeFromLeft(36);
    r.removeFromLeft(12);
    nameArea = r.removeFromLeft(96);

    // Signal flows left to right: distortion -> granular -> delay -> reverb
    const int w = r.getWidth() / 4;
    for (auto& card : cards)
        card->setBounds(r.removeFromLeft(w));
}