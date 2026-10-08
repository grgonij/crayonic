#include "PluginEditor.h"

//==============================================================================
SlotStrip::SlotStrip(AudioPluginAudioProcessor& p, int slot)
    : proc(p), index(slot), colour(Theme::slotColour(slot)),
    volume(p.apvts, layerParamID(slot, "vol"), "volume", " dB", colour),
    pitch(p.apvts, layerParamID(slot, "pitch"), "pitch", " st", colour, 1.0) // snaps to semitones
{
    addAndMakeVisible(volume);
    addAndMakeVisible(pitch);

    for (auto* b : { &muteButton, &reverseButton })
    {
        b->setColour(juce::ToggleButton::tickColourId, colour);
        addAndMakeVisible(*b);
    }
    muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, layerParamID(index, "mute"), muteButton);
    reverseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, layerParamID(index, "reverse"), reverseButton);

    addChildComponent(clearButton);
    clearButton.setTooltip("Remove this sound");
    clearButton.onClick = [this] { proc.clearSample(index); poll(); };

    // Everything else lives in pop-up notes
    auto id = [this](const char* name) { return layerParamID(index, name); };

    addPopup("start", [id](ControlPanel& c)
        {
            c.addKnob(id("start"), "start");
            c.addKnob(id("startrand"), "random");
            c.addKnob(id("stretch"), "stretch", " %");
        },
        [this] { return anyChanged({ "start", "startrand", "stretch" }); });

    addPopup("envelope", [id](ControlPanel& c)
        {
            c.addKnob(id("attack"), "attack", " ms");
            c.addKnob(id("decay"), "decay", " ms");
            c.addKnob(id("sustain"), "sustain");
            c.addKnob(id("release"), "release", " ms");
        },
        [this] { return anyChanged({ "attack", "decay", "sustain", "release" }); });

    addPopup("glide", [id](ControlPanel& c)
        {
            c.addToggle(id("glide"), "glide");
            c.addToggle(id("legato"), "legato");
            c.addKnob(id("glidetime"), "time", " ms");
        },
        [this] { return paramOn("glide") || paramOn("legato"); });

    addPopup("loop", [id](ControlPanel& c)
        {
            c.addToggle(id("loop"), "loop");
            c.addKnob(id("loopstart"), "start");
            c.addKnob(id("loopend"), "end");
        },
        [this] { return paramOn("loop"); });

    addPopup("arp", [id](ControlPanel& c)
        {
            c.addToggle(id("arp"), "arp on");
            c.addCombo(id("arprate"), "rate", kArpRates);
            c.addCombo(id("arppattern"), "pattern", kArpPatterns);
            c.newRow();
            c.addKnob(id("arpoct"), "octaves");
            c.addKnob(id("arpgate"), "gate", " %");
            c.addKnob(id("arpswing"), "swing", " %");
            c.addKnob(id("arpvel"), "velocity");
        },
        [this] { return paramOn("arp"); });

    poll();
}

void SlotStrip::addPopup(const juce::String& name, std::function<void(ControlPanel&)> build, std::function<bool()> isLit)
{
    auto popup = std::make_unique<PopupButton>();
    popup->button.setButtonText(name);
    popup->button.setColour(juce::TextButton::buttonOnColourId, colour);
    popup->isLit = std::move(isLit);
    popup->makePanel = [this, name, build = std::move(build)]
        {
            auto panel = std::make_unique<ControlPanel>(proc.apvts, colour, name);
            build(*panel);
            panel->finish();
            return panel;
        };

    auto* raw = popup.get();
    raw->button.onClick = [this, raw]
        {
            // A note pops out under the button; clicking anywhere else closes it
            Sketch::showNote(raw->button, raw->makePanel());
        };

    addAndMakeVisible(raw->button);
    popups.push_back(std::move(popup));
}

bool SlotStrip::paramOn(const juce::String& name) const
{
    return proc.apvts.getRawParameterValue(layerParamID(index, name))->load() > 0.5f;
}

bool SlotStrip::anyChanged(std::initializer_list<const char*> names) const
{
    for (auto* name : names)
        if (auto* param = proc.apvts.getParameter(layerParamID(index, name)))
            if (std::abs(param->getValue() - param->getDefaultValue()) > 1.0e-4f)
                return true;
    return false;
}

void SlotStrip::poll()
{
    bool redrawWave = false;

    auto current = proc.getSample(index);
    if (current != shownSample)
    {
        shownSample = current;
        sampleName = current ? juce::File(current->path).getFileNameWithoutExtension() : juce::String();
        clearButton.setVisible(current != nullptr);
        redrawWave = true;
    }

    const bool reverse = paramOn("reverse");
    if (reverse != shownReverse)
    {
        shownReverse = reverse;
        redrawWave = true;
    }

    if (redrawWave)
    {
        // Peaks, flipped when reversed so the picture matches what you hear
        peaks.clear();
        if (shownSample != nullptr)
        {
            const auto& buf = shownSample->buffer;
            const int len = buf.getNumSamples();
            constexpr int bins = 200;
            peaks.resize(bins, 0.0f);
            float maxPeak = 0.0f;
            for (int b = 0; b < bins; ++b)
            {
                const int s0 = (int)((juce::int64)len * b / bins);
                const int s1 = juce::jmin(len, juce::jmax(s0 + 1, (int)((juce::int64)len * (b + 1) / bins)));
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    peaks[(size_t)b] = juce::jmax(peaks[(size_t)b], buf.getMagnitude(ch, s0, s1 - s0));
                maxPeak = juce::jmax(maxPeak, peaks[(size_t)b]);
            }
            if (shownReverse) std::reverse(peaks.begin(), peaks.end());
            if (maxPeak > 0.0f) for (auto& pk : peaks) pk /= maxPeak;
        }
        rebuildWaveImage();
        repaint();
    }

    const float start = proc.apvts.getRawParameterValue(layerParamID(index, "start"))->load();
    const float rand = proc.apvts.getRawParameterValue(layerParamID(index, "startrand"))->load();
    if (start != shownStart || rand != shownRand)
    {
        shownStart = start;
        shownRand = rand;
        repaint(waveArea);
    }

    const bool loopOn = paramOn("loop");
    const float loopStart = proc.apvts.getRawParameterValue(layerParamID(index, "loopstart"))->load();
    const float loopEnd = proc.apvts.getRawParameterValue(layerParamID(index, "loopend"))->load();
    if (loopOn != shownLoop || loopStart != shownLoopStart || loopEnd != shownLoopEnd)
    {
        shownLoop = loopOn;
        shownLoopStart = loopStart;
        shownLoopEnd = loopEnd;
        repaint(waveArea);
    }

    // Muted = never picked on its own; it still plays as a layer if another sound links to it
    auto muteNow = MuteState::off;
    if (paramOn("mute"))
    {
        bool linkedTo = false;
        for (int from = 0; from < kNumLayers; ++from)
            if (from != index && proc.apvts.getRawParameterValue(linkParamID(from, index))->load() > 0.5f)
                linkedTo = true;
        muteNow = linkedTo ? MuteState::layerOnly : MuteState::silent;
    }
    if (muteNow != muteState)
    {
        muteState = muteNow;
        repaint(waveArea);
    }

    const int triggers = proc.triggerCount[(size_t)index].load();
    if (triggers != seenTriggers)
    {
        seenTriggers = triggers;
        flash = 1.0f;
    }
    if (flash > 0.0f)
    {
        flash = juce::jmax(0.0f, flash - 0.05f);
        repaint(numberArea.expanded(6));
    }

    for (auto& popup : popups)
    {
        const bool lit = popup->isLit();
        if (popup->button.getToggleState() != lit)
            popup->button.setToggleState(lit, juce::dontSendNotification);
    }

    const int prevHeads = numHeads;
    numHeads = 0;
    for (size_t v = 0; v < kNumVoices; ++v)
    {
        const float level = proc.playheadLevel[v].load();
        if (level > 0.001f && proc.playheadSlot[v].load() == index)
            heads[(size_t)numHeads++] = { proc.playheadPos[v].load(), level };
    }
    if (numHeads > 0 || prevHeads > 0)
        repaint(waveArea);
}

void SlotStrip::rebuildWaveImage()
{
    if (waveArea.isEmpty()) return;

    // Drawn at 2x so it stays crisp on high-DPI screens
    waveImage = juce::Image(juce::Image::ARGB, waveArea.getWidth() * 2, waveArea.getHeight() * 2, true,
        juce::SoftwareImageType()); // plain memory, no GPU resources
    juce::Graphics g(waveImage);
    g.addTransform(juce::AffineTransform::scale(2.0f));
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float)waveArea.getWidth(), (float)waveArea.getHeight());

    if (peaks.empty())
    {
        g.setColour(Theme::ink.withAlpha(0.45f));
        g.setFont(19.0f);
        g.drawText("drop a sound here (or click)", r, juce::Justification::centred);
        return;
    }

    const float mid = r.getCentreY(), amp = r.getHeight() * 0.42f;
    const int n = (int)peaks.size();
    auto xAt = [&](int i) { return r.getWidth() * (float)i / (float)(n - 1); };

    juce::Path wave;
    wave.startNewSubPath(0.0f, mid);
    for (int i = 0; i < n; ++i)      wave.lineTo(xAt(i), mid - juce::jmax(0.01f, peaks[(size_t)i]) * amp);
    for (int i = n - 1; i >= 0; --i) wave.lineTo(xAt(i), mid + juce::jmax(0.01f, peaks[(size_t)i]) * amp);
    wave.closeSubPath();

    const auto seed = Sketch::seedFor(this, 7);
    Sketch::fill(g, wave, colour, seed);
    Sketch::stroke(g, wave, colour.darker(0.45f), 1.1f, seed + 1, 2, 0.6f);
}

void SlotStrip::paint(juce::Graphics& g)
{
    const auto seed = Sketch::seedFor(this);

    // Big handwritten number; circled briefly when this sound gets picked
    g.setColour(colour.darker(0.35f));
    g.setFont(40.0f);
    g.drawText(juce::String(index + 1), numberArea, juce::Justification::centred);
    if (flash > 0.0f)
    {
        juce::Path ring;
        ring.addEllipse(numberArea.toFloat().withSizeKeepingCentre(38.0f, 44.0f));
        Sketch::stroke(g, ring, colour.darker(0.3f).withAlpha(flash), 2.4f, seed + 9, 2);
    }
    if (linkSource >= 0) // linking: the chosen sound is circled firmly, the others faintly (pick one)
    {
        juce::Path ring;
        ring.addEllipse(numberArea.toFloat().withSizeKeepingCentre(40.0f, 46.0f));
        Sketch::stroke(g, ring, linkSource == index ? Theme::slotColour(linkSource).darker(0.4f)
            : Theme::ink.withAlpha(0.25f),
            linkSource == index ? 3.0f : 1.4f, seed + 11, 2);
    }

    const auto wa = waveArea.toFloat();
    Sketch::stroke(g, Sketch::roundedRect(wa.expanded(4.0f), 9.0f), Theme::ink.withAlpha(0.35f), 1.2f, seed, 2);
    if (waveImage.isValid())
    {
        g.setOpacity(1.0f); // drawImage uses the current colour's alpha, which the frame stroke left faint
        g.drawImage(waveImage, wa);
    }

    if (!peaks.empty())
    {
        // Part skipped by Start: "rubbed out"
        const float startX = wa.getX() + wa.getWidth() * juce::jmax(0.0f, shownStart);
        if (shownStart > 0.001f)
        {
            g.setColour(Theme::paper.withAlpha(0.72f));
            g.fillRect(wa.withRight(startX));
        }

        // Where a random start can land: hatched
        const float randEndX = startX + (wa.getRight() - startX) * shownRand;
        if (randEndX > startX + 1.0f)
        {
            g.saveState();
            g.reduceClipRegion(juce::Rectangle<float>(startX, wa.getY(), randEndX - startX, wa.getHeight()).toNearestInt());
            g.setColour(colour.darker(0.5f).withAlpha(0.35f));
            for (float x = startX - wa.getHeight(); x < randEndX; x += 7.0f)
                g.drawLine(x, wa.getBottom(), x + wa.getHeight(), wa.getY(), 1.2f);
            g.restoreState();
        }

        juce::Path marker;
        marker.startNewSubPath(startX, wa.getY() + 2.0f);
        marker.lineTo(startX, wa.getBottom() - 2.0f);
        Sketch::stroke(g, marker, Theme::ink.withAlpha(0.8f), 1.8f, seed + 3, 2, 0.5f);

        // Loop region: a pastel wash between two bracket marks  [ ... ]
        if (shownLoop && shownLoopEnd > shownLoopStart)
        {
            const float x0 = wa.getX() + wa.getWidth() * shownLoopStart;
            const float x1 = wa.getX() + wa.getWidth() * shownLoopEnd;
            g.setColour(colour.withAlpha(0.22f));
            g.fillRect(juce::Rectangle<float>(x0, wa.getY(), x1 - x0, wa.getHeight()));

            juce::Path brackets;
            const float t = wa.getY() + 3.0f, b = wa.getBottom() - 3.0f, tick = 6.0f;
            brackets.startNewSubPath(x0 + tick, t); brackets.lineTo(x0, t); brackets.lineTo(x0, b); brackets.lineTo(x0 + tick, b);
            brackets.startNewSubPath(x1 - tick, t); brackets.lineTo(x1, t); brackets.lineTo(x1, b); brackets.lineTo(x1 - tick, b);
            Sketch::stroke(g, brackets, colour.darker(0.6f), 1.8f, seed + 12, 2, 0.5f);

            if (x1 - x0 > 44.0f)
            {
                g.setColour(colour.darker(0.6f));
                g.setFont(15.0f);
                g.drawText("loop", juce::Rectangle<float>(x0, b - 18.0f, x1 - x0, 16.0f), juce::Justification::centred);
            }
        }

        // Playheads: soft colour wash with a charcoal line, fading with the envelope
        for (int h = 0; h < numHeads; ++h)
        {
            const float x = wa.getX() + wa.getWidth() * juce::jlimit(0.0f, 1.0f, heads[(size_t)h].pos);
            const float lvl = juce::jlimit(0.0f, 1.0f, heads[(size_t)h].level);
            g.setColour(colour.darker(0.2f).withAlpha(0.35f * lvl));
            g.fillRect(x - 5.0f, wa.getY(), 10.0f, wa.getHeight());
            g.setColour(Theme::ink.withAlpha(0.85f * lvl));
            g.fillRect(x - 1.0f, wa.getY(), 2.0f, wa.getHeight());
        }

        if (muteState == MuteState::silent) // nothing will play it: fade it out like it's been rubbed off
        {
            g.setColour(Theme::paper.withAlpha(0.6f));
            g.fillRect(wa);
        }

        if (muteState != MuteState::off)
        {
            const juce::String tag = muteState == MuteState::layerOnly ? "layer only" : "muted";
            const int tagW = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(16.0f), tag) + 14;
            const auto tagBox = juce::Rectangle<int>(waveArea.getRight() - 30 - tagW, waveArea.getY() + 2, tagW, 20).toFloat();
            g.setColour(Theme::paper.withAlpha(0.85f));
            g.fillRoundedRectangle(tagBox, 5.0f);
            Sketch::stroke(g, Sketch::roundedRect(tagBox, 5.0f), colour.darker(0.5f), 1.2f, seed + 13, 2, 0.4f);
            g.setColour(colour.darker(0.6f));
            g.setFont(16.0f);
            g.drawText(tag, tagBox, juce::Justification::centred);
        }

        // Name on a little scrap of paper so it stays readable over the waveform
        const int nameW = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(17.0f), sampleName) + 12;
        const auto nameBox = juce::Rectangle<int>(waveArea.getX() + 2, waveArea.getY() + 2, nameW, 20).toFloat();
        g.setColour(Theme::paper.withAlpha(0.85f));
        g.fillRoundedRectangle(nameBox, 5.0f);
        g.setColour(Theme::ink.withAlpha(0.85f));
        g.setFont(17.0f);
        g.drawText(sampleName, nameBox, juce::Justification::centred);
    }

    if (dragOver)
        Sketch::stroke(g, Sketch::roundedRect(wa.expanded(4.0f), 9.0f), colour.darker(0.35f), 3.0f, seed + 5);

    juce::Path divider; // notebook-style rule between sounds
    divider.startNewSubPath(10.0f, (float)getHeight() - 1.5f);
    divider.lineTo((float)getWidth() - 10.0f, (float)getHeight() - 1.5f);
    Sketch::stroke(g, divider, Theme::ink.withAlpha(0.15f), 1.0f, seed + 6, 1);
}

void SlotStrip::resized()
{
    auto r = getLocalBounds().reduced(8, 8);
    r.removeFromLeft(28);            // gutter for the layer-link arrows
    numberArea = r.removeFromLeft(36);
    r.removeFromLeft(12);

    auto toggles = r.removeFromRight(92);
    muteButton.setBounds(toggles.removeFromTop(toggles.getHeight() / 2).withSizeKeepingCentre(92, 28));
    reverseButton.setBounds(toggles.withSizeKeepingCentre(92, 28));
    r.removeFromRight(6);

    auto knobs = r.removeFromRight(2 * SketchKnob::kWidth);
    volume.setBounds(knobs.removeFromLeft(SketchKnob::kWidth).withSizeKeepingCentre(SketchKnob::kWidth, SketchKnob::kHeight));
    pitch.setBounds(knobs.withSizeKeepingCentre(SketchKnob::kWidth, SketchKnob::kHeight));
    r.removeFromRight(14);

    auto tabs = r.removeFromBottom(28);
    r.removeFromBottom(8);
    waveArea = r.reduced(4);

    for (auto& popup : popups)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(17.0f), popup->button.getButtonText()) + 26;
        popup->button.setBounds(tabs.removeFromLeft(w));
        tabs.removeFromLeft(6);
    }

    clearButton.setBounds(waveArea.getRight() - 24, waveArea.getY() + 1, 24, 24);
    rebuildWaveImage();
}

void SlotStrip::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(numberArea.contains(e.getPosition()) ? juce::MouseCursor::PointingHandCursor
        : juce::MouseCursor::NormalCursor);
}

void SlotStrip::setLinkMode(int sourceSlot)
{
    if (linkSource != sourceSlot)
    {
        linkSource = sourceSlot;
        repaint(numberArea.expanded(8));
    }
}

void SlotStrip::mouseDown(const juce::MouseEvent& e)
{
    if (numberArea.contains(e.getPosition()))
    {
        if (onNumberClicked) onNumberClicked(index);
        return;
    }

    if (!waveArea.contains(e.getPosition()))
        return;

    chooser = std::make_unique<juce::FileChooser>("Choose a one-shot", juce::File(),
        proc.formatManager.getWildcardForAllFormats());
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile() && proc.loadSample(index, fc.getResult()))
                poll();
        });
}

bool SlotStrip::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (proc.formatManager.findFormatForFileExtension(juce::File(f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void SlotStrip::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;

    // Dropping several files fills this slot and the ones below it
    int slot = index;
    for (const auto& f : files)
    {
        if (slot >= kNumLayers) break;
        if (proc.loadSample(slot, juce::File(f)))
            ++slot;
    }

    if (auto* parent = getParentComponent())
        for (auto* c : parent->getChildren())
            if (auto* strip = dynamic_cast<SlotStrip*> (c))
                strip->poll();
    repaint();
}

//==============================================================================
PresetBar::PresetBar(AudioPluginAudioProcessor& p) : proc(p)
{
    for (auto* b : { &prevButton, &nextButton, &saveButton })
        addAndMakeVisible(*b);
    addAndMakeVisible(box);

    prevButton.onClick = [this] { step(-1); };
    nextButton.onClick = [this] { step(+1); };
    saveButton.onClick = [this] { save(); };

    box.setTextWhenNothingSelected("Init");
    box.onChange = [this]
        {
            const int id = box.getSelectedId();
            if (id == initId)                                    proc.initPreset();
            else if (id >= firstFileId && id < firstFileId + files.size()) proc.loadPreset(files[id - firstFileId]);
            else if (id == openId) { open(); return; }
            else if (id == folderId)                             AudioPluginAudioProcessor::getPresetFolder().startAsProcess();
            refresh();
        };

    refresh();
}

void PresetBar::refresh()
{
    files = AudioPluginAudioProcessor::getPresetFolder().findChildFiles(
        juce::File::findFiles, false, "*" + AudioPluginAudioProcessor::presetExtension);
    files.sort();

    box.clear(juce::dontSendNotification);
    box.addItem("Init", initId);
    box.addSeparator();
    for (int i = 0; i < files.size(); ++i)
        box.addItem(files[i].getFileNameWithoutExtension(), firstFileId + i);
    box.addSeparator();
    box.addItem("Open preset file...", openId);
    box.addItem("Show preset folder", folderId);

    shownName = proc.currentPresetName;
    int match = 0;
    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFileNameWithoutExtension() == shownName)
            match = firstFileId + i;

    if (match != 0)               box.setSelectedId(match, juce::dontSendNotification);
    else if (shownName.isEmpty()) box.setSelectedId(initId, juce::dontSendNotification);
    else                          box.setText(shownName, juce::dontSendNotification); // loaded from elsewhere
}

void PresetBar::poll()
{
    if (proc.currentPresetName != shownName)
        refresh();
}

void PresetBar::step(int delta)
{
    if (files.isEmpty()) return;

    int index = -1;
    for (int i = 0; i < files.size(); ++i)
        if (files[i].getFileNameWithoutExtension() == proc.currentPresetName)
            index = i;

    index = index < 0 ? (delta > 0 ? 0 : files.size() - 1)
        : (index + delta + files.size()) % files.size(); // wrap around
    proc.loadPreset(files[index]);
    refresh();
}

void PresetBar::save()
{
    const auto name = proc.currentPresetName.isNotEmpty() ? proc.currentPresetName : juce::String("My Preset");
    chooser = std::make_unique<juce::FileChooser>(
        "Save preset", AudioPluginAudioProcessor::getPresetFolder().getChildFile(name + AudioPluginAudioProcessor::presetExtension),
        "*" + AudioPluginAudioProcessor::presetExtension);

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file != juce::File())
                proc.savePreset(file.withFileExtension(AudioPluginAudioProcessor::presetExtension));
            refresh();
        });
}

void PresetBar::open()
{
    chooser = std::make_unique<juce::FileChooser>("Open preset", AudioPluginAudioProcessor::getPresetFolder(),
        "*" + AudioPluginAudioProcessor::presetExtension);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
                proc.loadPreset(fc.getResult());
            refresh();
        });
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    saveButton.setBounds(r.removeFromRight(64));
    r.removeFromRight(8);
    prevButton.setBounds(r.removeFromLeft(28));
    nextButton.setBounds(r.removeFromRight(28));
    box.setBounds(r.reduced(4, 0));
}

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p), presetBar(p),
    masterKnob(p.apvts, "master", "master", " dB", Theme::lavender)
{
    // Fonts resolve through the *default* LookAndFeel, so ours becomes the default too
    setLookAndFeel(&lnf.get());
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf.get());
    ++lnf->openEditors;

    addAndMakeVisible(presetBar);
    addAndMakeVisible(masterKnob);

    for (int l = 0; l < kNumLayers; ++l)
    {
        strips.push_back(std::make_unique<SlotStrip>(p, l));
        soundsView.addAndMakeVisible(*strips.back());

        fxRows.push_back(std::make_unique<FxRow>(p, l));
        fxView.addAndMakeVisible(*fxRows.back());
    }
    addAndMakeVisible(soundsView);
    addChildComponent(fxView);
    addAndMakeVisible(linkOverlay); // on top of the sounds screen, mouse clicks pass through

    for (auto& s : strips)
        s->onNumberClicked = [this](int slot) { numberClicked(slot); };

    for (auto* tab : { &soundsTab, &fxTab })
    {
        tab->setClickingTogglesState(true);
        tab->setRadioGroupId(1);
        addAndMakeVisible(*tab);
    }
    soundsTab.setToggleState(true, juce::dontSendNotification);
    soundsTab.onClick = [this] { showFx(false); };
    fxTab.onClick = [this] { showFx(true); };

    setSize(900, 620);
    startTimerHz(60); // smooth playheads
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    stopTimer(); // no more 60 fps updates while we tear down
    closeNote(); // first, while the parameters its knobs are attached to still exist

    setLookAndFeel(nullptr);
    if (--lnf->openEditors == 0 && &juce::LookAndFeel::getDefaultLookAndFeel() == &lnf.get())
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr); // last editor closing
}

void AudioPluginAudioProcessorEditor::showNote(std::unique_ptr<juce::Component> content, juce::Component& anchor)
{
    closeNote();

    // We own both the note and its contents (rather than JUCE's fire-and-forget launchAsynchronously),
    // so nothing outlives the plugin window.
    noteContent = std::move(content);
    note = std::make_unique<juce::CallOutBox>(*noteContent, getLocalArea(&anchor, anchor.getLocalBounds()), this);
    note->setVisible(true);

    auto* shown = note.get();
    note->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe = juce::Component::SafePointer<AudioPluginAudioProcessorEditor>(this), shown](int)
        {
            // Dismissed (clicked outside / Escape): tidy up on the next message, not inside the callback
            juce::MessageManager::callAsync([safe, shown]
                {
                    if (safe != nullptr && safe->note.get() == shown)
                        safe->closeNote();
                });
        }), false);
}

void AudioPluginAudioProcessorEditor::closeNote()
{
    note.reset();        // the box first (it shows the content but doesn't own it)...
    noteContent.reset(); // ...then the knobs inside, detaching them from their parameters
}

void AudioPluginAudioProcessorEditor::showFx(bool fx)
{
    soundsView.setVisible(!fx);
    linkOverlay.setVisible(!fx);
    fxView.setVisible(fx);
    if (fx && linkSource >= 0) numberClicked(linkSource); // leaving the screen cancels linking
}

void AudioPluginAudioProcessorEditor::numberClicked(int slot)
{
    if (linkSource < 0)
        linkSource = slot;                 // step 1: which sound triggers the layer
    else if (slot == linkSource)
        linkSource = -1;                   // clicked the same number again: cancel
    else
    {
        // step 2: toggle "linkSource also plays slot" (one way)
        if (auto* param = processorRef.apvts.getParameter(linkParamID(linkSource, slot)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.0f : 1.0f);
            param->endChangeGesture();
        }
        linkSource = -1;
    }

    for (auto& s : strips)
        s->setLinkMode(linkSource);
    linkOverlay.repaint();
    repaint(hintArea);
}

juce::uint32 AudioPluginAudioProcessorEditor::currentLinkMask() const
{
    juce::uint32 mask = 0;
    for (int from = 0; from < kNumLayers; ++from)
        for (int to = 0; to < kNumLayers; ++to)
            if (from != to && processorRef.apvts.getRawParameterValue(linkParamID(from, to))->load() > 0.5f)
                mask |= 1u << (from * kNumLayers + to);
    return mask;
}

void AudioPluginAudioProcessorEditor::LinkOverlay::paint(juce::Graphics& g)
{
    // One crayon arrow per link, curving through the left margin from the trigger to its layer.
    // Leaves from the upper half of the trigger's number, arrives at the lower half of the layer's, so a
    // two-way link shows as two separate arrows. Longer links bulge further out, so they nest.
    for (int from = 0; from < kNumLayers; ++from)
        for (int to = 0; to < kNumLayers; ++to)
        {
            if (from == to || editor.processorRef.apvts.getRawParameterValue(linkParamID(from, to))->load() < 0.5f)
                continue;

            auto anchor = [this](int slot)
                {
                    const auto& strip = *editor.strips[(size_t)slot];
                    return (strip.getPosition() + strip.getLinkAnchor()).toFloat();
                };
            const auto a = anchor(from), b = anchor(to);
            // Each target gets its own departure height and each source its own landing height,
            // so several arrows out of / into one number never share an arrowhead
            const juce::Point<float> start(a.x - 2.0f, a.y - 15.0f + 3.0f * (float)to),
                end(b.x - 2.0f, b.y + 5.0f + 3.0f * (float)from);
            const float bulge = 1.3f * (8.0f + 5.0f * (float)std::abs(to - from) + (to > from ? 0.0f : 3.0f));

            juce::Path arrow;
            arrow.startNewSubPath(start);
            arrow.cubicTo(start.translated(-bulge, 0.0f), end.translated(-bulge, 0.0f), end);
            arrow.startNewSubPath(end.translated(-6.0f, -4.0f)); // arrowhead into the layer's number
            arrow.lineTo(end);
            arrow.lineTo(end.translated(-6.0f, 4.0f));

            Sketch::stroke(g, arrow, Theme::slotColour(from).darker(0.35f), 2.0f,
                Sketch::seedFor(this, (juce::uint32)(from * 8 + to)), 2, 0.5f);
        }
}

void AudioPluginAudioProcessorEditor::useSoftwareRenderer()
{
#if JUCE_WINDOWS
    // JUCE 8+ draws with Direct2D (GPU) by default. Its Direct3D resources can end up being released
    // while the host unloads the plugin DLL, which deadlocks some graphics drivers (FL froze on
    // plugin removal). The CPU renderer has no such resources, and is plenty fast for this UI.
    if (auto* peer = getPeer())
    {
        const int software = peer->getAvailableRenderingEngines().indexOf("Software Renderer");
        if (software >= 0 && peer->getCurrentRenderingEngine() != software)
            peer->setCurrentRenderingEngine(software);
    }
#endif
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    useSoftwareRenderer(); // cheap check; catches hosts that create/replace the window after we're added
    presetBar.poll();
    if (const auto mask = currentLinkMask(); mask != shownLinks)
    {
        shownLinks = mask;
        linkOverlay.repaint();
    }
    if (soundsView.isVisible())
        for (auto& s : strips) s->poll();
    else
        for (auto& r : fxRows) r->poll();
}

void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    Sketch::paper(g, getLocalBounds());

    // Notebook margin line next to the slot numbers
    juce::Path margin;
    margin.startNewSubPath(86.0f, 78.0f);
    margin.lineTo(86.0f, (float)getHeight() - 6.0f);
    Sketch::stroke(g, margin, Theme::pink.withAlpha(0.7f), 1.2f, Sketch::seedFor(this, 1), 2);

    juce::Path rule; // under the header
    rule.startNewSubPath(14.0f, 74.0f);
    rule.lineTo((float)getWidth() - 14.0f, 74.0f);
    Sketch::stroke(g, rule, Theme::ink.withAlpha(0.3f), 1.2f, Sketch::seedFor(this, 2), 2);

    if (linkSource >= 0)
    {
        const auto n = juce::String(linkSource + 1);
        g.setColour(Theme::slotColour(linkSource).darker(0.5f));
        g.setFont(17.0f);
        g.drawFittedText("click the sound that " + n + " should also play\n(click " + n + " again to cancel)",
            hintArea, juce::Justification::centred, 2);
    }
}

void AudioPluginAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop(76).reduced(14, 6);

    fxTab.setBounds(header.removeFromRight(56).withSizeKeepingCentre(56, 34));
    header.removeFromRight(6);
    soundsTab.setBounds(header.removeFromRight(84).withSizeKeepingCentre(84, 34));
    header.removeFromRight(18);
    masterKnob.setBounds(header.removeFromRight(SketchKnob::kWidth).withSizeKeepingCentre(SketchKnob::kWidth, SketchKnob::kHeight));
    presetBar.setBounds(header.removeFromLeft(420).withSizeKeepingCentre(420, 34));
    hintArea = header.reduced(8, 0);

    r.reduce(8, 0);
    r.removeFromBottom(6);
    soundsView.setBounds(r);
    fxView.setBounds(r);
    linkOverlay.setBounds(r);

    const int h = r.getHeight() / kNumLayers;
    auto stripArea = r.withZeroOrigin(), fxArea = r.withZeroOrigin();
    for (auto& s : strips)
        s->setBounds(stripArea.removeFromTop(h));
    for (auto& f : fxRows)
        f->setBounds(fxArea.removeFromTop(h));
}