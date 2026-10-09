#include "Sketch.h"
#include "BinaryData.h"

namespace Sketch
{
    juce::Path wobble(const juce::Path& src, juce::Random& rng, float amount)
    {
        // Smooth low-frequency offsets along the path's length: reads as an unsteady hand, not noise
        const float f1 = 0.015f + rng.nextFloat() * 0.03f, f2 = 0.06f + rng.nextFloat() * 0.06f;
        const float p1 = rng.nextFloat() * 6.283f, p2 = rng.nextFloat() * 6.283f;
        const float p3 = rng.nextFloat() * 6.283f, p4 = rng.nextFloat() * 6.283f;
        float travelled = 0.0f;

        auto offset = [&](float x, float y)
            {
                return juce::Point<float>(x + amount * (0.7f * std::sin(travelled * f1 + p1) + 0.3f * std::sin(travelled * f2 + p2)),
                    y + amount * (0.7f * std::sin(travelled * f1 + p3) + 0.3f * std::sin(travelled * f2 + p4)));
            };

        juce::Path out;
        juce::PathFlatteningIterator it(src, {}, 0.8f);
        while (it.next())
        {
            if (it.subPathIndex == 0) // first segment of a new sub-path (the index counts segments within it)
                out.startNewSubPath(offset(it.x1, it.y1));

            // Subdivide long straight segments so the wobble shows on them too
            const float len = juce::Line<float>(it.x1, it.y1, it.x2, it.y2).getLength();
            if (!std::isfinite(len)) // invalid geometry (e.g. a window resized to nothing): skip, don't hang
                continue;
            const int steps = juce::jlimit(1, 400, (int)(len / 5.0f));
            for (int s = 1; s <= steps; ++s)
            {
                const float t = (float)s / (float)steps;
                travelled += len / (float)steps;
                out.lineTo(offset(it.x1 + (it.x2 - it.x1) * t, it.y1 + (it.y2 - it.y1) * t));
            }

            if (it.closesSubPath)
                out.closeSubPath();
        }
        return out;
    }

    void stroke(juce::Graphics& g, const juce::Path& p, juce::Colour c, float thickness, juce::uint32 seed,
        int passes, float wobbleAmount)
    {
        juce::Random rng((juce::int64)seed);
        const float amount = wobbleAmount >= 0.0f ? wobbleAmount : juce::jmax(0.6f, thickness * 0.35f);
        for (int i = 0; i < passes; ++i)
        {
            g.setColour(c.withMultipliedAlpha(i == 0 ? 0.8f : 0.4f));
            g.strokePath(wobble(p, rng, amount),
                juce::PathStrokeType(thickness * (0.6f + 0.5f * rng.nextFloat()),
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    Textures::Textures()
        : grain(juce::Image::ARGB, 128, 128, true, juce::SoftwareImageType()),
        paper(juce::Image::ARGB, 256, 256, true, juce::SoftwareImageType())
    {
        // Grain: paper-coloured speckles, tiled over pastel fills so they look chalky
        juce::Random rng(42);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x)
                if (rng.nextFloat() < 0.3f)
                    grain.setPixelAt(x, y, Theme::paper.withAlpha(0.25f + 0.5f * rng.nextFloat()));

        // Paper: faint dark flecks and fibres
        rng.setSeed(7);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
                paper.setPixelAt(x, y, Theme::ink.withAlpha(rng.nextFloat() * 0.045f));

        juce::Graphics g(paper);
        for (int i = 0; i < 40; ++i)
        {
            const float x = rng.nextFloat() * 256.0f, y = rng.nextFloat() * 256.0f;
            g.setColour(Theme::ink.withAlpha(0.03f + rng.nextFloat() * 0.04f));
            g.drawLine(x, y, x + (rng.nextFloat() - 0.5f) * 30.0f, y + (rng.nextFloat() - 0.5f) * 30.0f, 0.6f);
        }
    }

    void fill(juce::Graphics& g, const juce::Path& p, juce::Colour c, juce::uint32 seed)
    {
        juce::Random rng((juce::int64)seed);
        g.setColour(c.withMultipliedAlpha(0.92f));
        g.fillPath(wobble(p, rng, 1.0f));
        const juce::SharedResourcePointer<Textures> textures; // the LookAndFeel's copy; cheap to grab
        g.setFillType(juce::FillType(textures->grain, juce::AffineTransform::translation(rng.nextFloat() * 128.0f,
            rng.nextFloat() * 128.0f)));
        g.fillPath(p);
    }

    void paper(juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour(Theme::paper);
        g.fillRect(area);
        const juce::SharedResourcePointer<Textures> textures;
        g.setFillType(juce::FillType(textures->paper, {}));
        g.fillRect(area);
    }

    juce::Path roundedRect(juce::Rectangle<float> r, float corner)
    {
        juce::Path p;
        p.addRoundedRectangle(r, corner);
        return p;
    }
}

//==============================================================================
SketchLookAndFeel::SketchLookAndFeel()
{
    setDefaultSansSerifTypeface(juce::Typeface::createSystemTypefaceFor(
        BinaryData::PatrickHandRegular_ttf, (size_t)BinaryData::PatrickHandRegular_ttfSize));

    using namespace Theme;
    setColour(juce::Label::textColourId, ink);
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, ink);
    setColour(juce::TextButton::buttonOnColourId, lavender);
    setColour(juce::ToggleButton::textColourId, ink);
    setColour(juce::ToggleButton::tickColourId, lavender);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::arrowColourId, ink);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::backgroundColourId, paper); // JUCE uses this for bits it draws itself
    setColour(juce::PopupMenu::highlightedBackgroundColourId, lavender.withAlpha(0.6f));
    setColour(juce::PopupMenu::highlightedTextColourId, ink);
    setColour(juce::TooltipWindow::textColourId, ink); // slider value bubble text
    setColour(juce::Slider::rotarySliderFillColourId, lavender);
}

void SketchLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
    float a0, float a1, juce::Slider& s)
{
    const float size = juce::jmin((float)w, (float)h - 18.0f); // the label lives underneath
    const juce::Point<float> c((float)x + (float)w * 0.5f, (float)y + size * 0.5f + 2.0f);
    const float r = size * 0.5f - 4.0f;
    const float angle = a0 + pos * (a1 - a0);
    const auto colour = s.findColour(juce::Slider::rotarySliderFillColourId);
    const auto seed = Sketch::seedFor(&s);
    const bool hot = s.isMouseOverOrDragging();

    juce::Path track;
    track.addCentredArc(c.x, c.y, r, r, 0.0f, a0, a1, true);
    Sketch::stroke(g, track, Theme::ink.withAlpha(0.2f), 1.6f, seed, 2);

    if (pos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc(c.x, c.y, r, r, 0.0f, a0, angle, true);
        Sketch::stroke(g, value, colour.darker(0.25f), 3.6f, seed + 1);
    }

    const float br = r * 0.58f;
    juce::Path body;
    body.addEllipse(c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    Sketch::fill(g, body, colour.interpolatedWith(Theme::paper, 0.3f), seed + 2);
    Sketch::stroke(g, body, Theme::ink.withAlpha(hot ? 0.95f : 0.7f), 1.3f, seed + 3, 2);

    juce::Path pointer;
    pointer.startNewSubPath(c.getPointOnCircumference(br * 0.2f, angle));
    pointer.lineTo(c.getPointOnCircumference(br * 0.95f, angle));
    Sketch::stroke(g, pointer, Theme::ink, 2.2f, seed + 4, 2, 0.3f);
}

void SketchLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const float box = 15.0f;
    const juce::Rectangle<float> tick(bounds.getX() + 3.0f, bounds.getCentreY() - box * 0.5f, box, box);
    const auto seed = Sketch::seedFor(&b);
    const bool on = b.getToggleState();

    const auto p = Sketch::roundedRect(tick, 3.0f);
    if (on) Sketch::fill(g, p, b.findColour(juce::ToggleButton::tickColourId), seed);
    Sketch::stroke(g, p, Theme::ink.withAlpha(highlighted ? 0.95f : 0.7f), 1.3f, seed + 1, 2);

    if (on)
    {
        juce::Path check;
        check.startNewSubPath(tick.getX() + 3.0f, tick.getCentreY());
        check.lineTo(tick.getX() + box * 0.42f, tick.getBottom() - 2.5f);
        check.lineTo(tick.getRight() + 2.5f, tick.getY() - 3.0f);
        Sketch::stroke(g, check, Theme::ink, 2.0f, seed + 2, 2, 0.4f);
    }

    g.setColour(Theme::ink.withAlpha(b.isEnabled() ? 1.0f : 0.45f));
    g.setFont(17.0f);
    g.drawText(b.getButtonText(), bounds.withTrimmedLeft(box + 9.0f), juce::Justification::centredLeft);
}

void SketchLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto p = Sketch::roundedRect(b.getLocalBounds().toFloat().reduced(2.0f), 7.0f);
    const auto seed = Sketch::seedFor(&b);

    if (b.getToggleState())
        Sketch::fill(g, p, b.findColour(juce::TextButton::buttonOnColourId), seed);
    else
    {
        g.setColour(Theme::paper.withAlpha(0.9f)); // readable even on top of a waveform
        g.fillPath(p);
        if (highlighted || down)
        {
            g.setColour(Theme::ink.withAlpha(down ? 0.12f : 0.06f));
            g.fillPath(p);
        }
    }
    Sketch::stroke(g, p, Theme::ink.withAlpha(highlighted ? 0.9f : 0.55f), 1.2f, seed + 1, 2);
}

void SketchLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto seed = Sketch::seedFor(&box);
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float)w, (float)h).reduced(2.0f);
    Sketch::stroke(g, Sketch::roundedRect(r, 6.0f), Theme::ink.withAlpha(box.isMouseOver() ? 0.9f : 0.55f), 1.2f, seed, 2);

    juce::Path arrow; // little scribbled chevron
    const float ax = r.getRight() - 14.0f, ay = r.getCentreY();
    arrow.startNewSubPath(ax - 4.0f, ay - 2.0f);
    arrow.lineTo(ax, ay + 2.5f);
    arrow.lineTo(ax + 4.0f, ay - 2.0f);
    Sketch::stroke(g, arrow, Theme::ink, 1.6f, seed + 1, 2, 0.3f);
}

void SketchLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    Sketch::paper(g, { 0, 0, w, h });
    g.setColour(Theme::ink.withAlpha(0.5f));
    g.drawRect(0, 0, w, h, 1);
}

void SketchLookAndFeel::drawPopupMenuUpDownArrow(juce::Graphics& g, int w, int h, bool isScrollUpArrow)
{
    // The strip at each end of a long, scrollable menu: plain paper (JUCE's default is a dark
    // gradient "shadow") with a small pencil chevron so it's still obvious the list scrolls
    Sketch::paper(g, { 0, 0, w, h });

    const float cx = (float)w * 0.5f, cy = (float)h * 0.5f, dy = isScrollUpArrow ? -2.5f : 2.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 6.0f, cy - dy);
    chevron.lineTo(cx, cy + dy);
    chevron.lineTo(cx + 6.0f, cy - dy);
    Sketch::stroke(g, chevron, Theme::ink.withAlpha(0.6f), 1.6f, isScrollUpArrow ? 21u : 22u, 2, 0.3f);
}

void SketchLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    // Soft shadow from a few offset fills (juce::DropShadow would allocate a GPU image on Windows)
    for (int i = 1; i <= 4; ++i)
    {
        g.setColour(Theme::ink.withAlpha(0.05f));
        g.fillPath(path, juce::AffineTransform::translation((float)i * 0.6f, (float)i * 1.1f));
    }
    g.saveState();
    g.reduceClipRegion(path);
    Sketch::paper(g, box.getLocalBounds());
    g.restoreState();
    Sketch::stroke(g, path, Theme::ink.withAlpha(0.75f), 1.4f, Sketch::seedFor(&box), 2);
}

juce::Component* SketchLookAndFeel::getParentComponentForMenuOptions(const juce::PopupMenu::Options& options)
{
    // Open dropdown menus inside the plugin window rather than as separate desktop windows:
    // hosts handle that better, and the menu uses the plugin window's (software) renderer.
    if (auto* target = options.getTargetComponent())
        return target->getTopLevelComponent();
    return LookAndFeel_V4::getParentComponentForMenuOptions(options);
}

void SketchLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height);
    Sketch::paper(g, r.toNearestInt());
    g.setColour(Theme::ink.withAlpha(0.7f));
    g.drawRect(r, 1.0f);
    g.setColour(Theme::ink);
    g.setFont(16.0f);
    g.drawFittedText(text, r.reduced(8.0f, 3.0f).toNearestInt(), juce::Justification::centredLeft, 3, 1.0f); // wrap, never squash
}

juce::Rectangle<int> SketchLookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> area)
{
    const int textW = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(16.0f), text);
    const int w = juce::jmin(300, textW + 20);
    const int lines = juce::jlimit(1, 3, (int)std::ceil((float)textW / 270.0f));
    const int h = 8 + lines * 19;
    return juce::Rectangle<int>(pos.x > area.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
        pos.y > area.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h).constrainedWithin(area);
}

void SketchLookAndFeel::drawBubble(juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>& tip,
    const juce::Rectangle<float>& body)
{
    juce::Path p;
    p.addBubble(body.reduced(0.5f), body.getUnion({ tip.x, tip.y, 1.0f, 1.0f }), tip, 5.0f,
        juce::jmin(12.0f, body.getWidth() * 0.2f));
    g.setColour(Theme::paper);
    g.fillPath(p);
    g.setColour(Theme::ink.withAlpha(0.7f));
    g.strokePath(p, juce::PathStrokeType(1.2f));
}

//==============================================================================
SketchKnob::SketchKnob(juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& text,
    const juce::String& suffix, juce::Colour colour, double snapStep)
    : label(text)
{
    slider.snapStep = snapStep;
    slider.onRightClick = [this] { showValueEditor(); };

    // Knobs that belong to a sound ("l2_attack") can be dragged by their name onto another sound to copy
    int slot = -1;
    juce::String name;
    if (Sketch::splitSlotParam(paramID, slot, name))
    {
        // Describe it by its full parameter name ("Slot 1 Delay Mix" -> "delay mix"), clearer than just "mix"
        juce::String what = label;
        if (auto* param = state.getParameter(paramID))
            what = param->getName(64).fromFirstOccurrenceOf("Slot " + juce::String(slot + 1) + " ", false, false).toLowerCase();
        slider.onCopyDrag = [this, slot, name, what] { Sketch::startCopyDrag(*this, slot, what, { name }); };
    }
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::rotarySliderFillColourId, colour);
    slider.setMouseDragSensitivity(200);
    addAndMakeVisible(slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, paramID, slider);

    if (auto* param = state.getParameter(paramID)) // double-click = back to default
        slider.setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));

    setSize(kWidth, kHeight);
}

void Sketch::startCopyDrag(juce::Component& source, int fromSlot, const juce::String& what, const juce::StringArray& names)
{
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(&source);
    if (container == nullptr || container->isDragAndDropActive() || names.isEmpty()) return;

    // A little paper tag that follows the mouse: "copy envelope"
    const auto text = "copy " + what;
    const int w = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(17.0f), text) + 26, h = 28;
    juce::Image tag(juce::Image::ARGB, w * 2, h * 2, true, juce::SoftwareImageType());
    {
        juce::Graphics g(tag);
        g.addTransform(juce::AffineTransform::scale(2.0f));
        const auto r = juce::Rectangle<float>(1.0f, 1.0f, (float)w - 2.0f, (float)h - 2.0f);
        g.setColour(Theme::paper);
        g.fillRoundedRectangle(r, 6.0f);
        Sketch::stroke(g, Sketch::roundedRect(r, 6.0f), Theme::slotColour(fromSlot).darker(0.5f), 1.6f, 77u, 2, 0.4f);
        g.setColour(Theme::ink);
        g.setFont(17.0f);
        g.drawText(text, r, juce::Justification::centred);
    }

    const juce::String description = "copy\n" + juce::String(fromSlot) + "\n" + what + "\n" + names.joinIntoString(",");
    const juce::Point<int> offset(-w / 2, -h - 6); // tag sits just above the pointer, so the target stays visible
    container->startDragging(description, &source, juce::ScaledImage(tag, 2.0), false, &offset);
}

bool Sketch::readCopyDrag(const juce::var& description, CopyDrag& out)
{
    const auto parts = juce::StringArray::fromLines(description.toString());
    if (parts.size() != 4 || parts[0] != "copy") return false;
    out.fromSlot = parts[1].getIntValue();
    out.what = parts[2];
    out.names = juce::StringArray::fromTokens(parts[3], ",", {});
    return out.fromSlot >= 0 && !out.names.isEmpty();
}

bool Sketch::splitSlotParam(const juce::String& paramID, int& slot, juce::String& name)
{
    if (paramID.length() < 4 || paramID[0] != 'l' || !juce::CharacterFunctions::isDigit(paramID[1]) || paramID[2] != '_')
        return false;
    slot = paramID[1] - '0';
    name = paramID.substring(3);
    return true;
}

double SketchKnob::KnobSlider::snapValue(double value, DragMode mode)
{
    if (snapStep > 0.0 && mode != notDragging && !juce::ModifierKeys::currentModifiers.isShiftDown())
        return std::round(value / snapStep) * snapStep;
    return value;
}

void SketchKnob::KnobSlider::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick) onRightClick();
        return;
    }
    // Grabbing the knob's name (or Alt/Option + anywhere) picks up a copy instead of turning it
    copyMode = onCopyDrag != nullptr && (inLabel(e.getPosition()) || e.mods.isAltDown());
    copyStarted = false;
    if (!copyMode) Slider::mouseDown(e);
}

void SketchKnob::KnobSlider::mouseDrag(const juce::MouseEvent& e)
{
    if (copyMode)
    {
        if (!copyStarted && e.getDistanceFromDragStart() > 4)
        {
            copyStarted = true;
            onCopyDrag();
        }
        return;
    }
    if (!e.mods.isPopupMenu()) Slider::mouseDrag(e);
}

void SketchKnob::KnobSlider::mouseUp(const juce::MouseEvent& e)
{
    if (copyMode) { copyMode = copyStarted = false; return; }
    if (!e.mods.isPopupMenu()) Slider::mouseUp(e);
}

void SketchKnob::KnobSlider::mouseMove(const juce::MouseEvent& e)
{
    Slider::mouseMove(e);
    setMouseCursor(onCopyDrag != nullptr && inLabel(e.getPosition()) ? juce::MouseCursor::DraggingHandCursor
        : juce::MouseCursor::NormalCursor);
}

juce::String SketchKnob::KnobSlider::getTooltip()
{
    if (onCopyDrag != nullptr && inLabel(getMouseXYRelative()))
        return "Drag this name onto another sound to copy the setting (or Alt-drag the knob)";
    return {};
}

void SketchKnob::showValueEditor()
{
    valueEditor = std::make_unique<juce::TextEditor>();
    auto& ed = *valueEditor;
    ed.setColour(juce::TextEditor::backgroundColourId, Theme::paper);
    ed.setColour(juce::TextEditor::textColourId, Theme::ink);
    ed.setColour(juce::TextEditor::outlineColourId, Theme::ink.withAlpha(0.6f));
    ed.setColour(juce::TextEditor::focusedOutlineColourId, slider.findColour(juce::Slider::rotarySliderFillColourId).darker(0.4f));
    ed.setColour(juce::TextEditor::highlightColourId, slider.findColour(juce::Slider::rotarySliderFillColourId).withAlpha(0.6f));
    ed.setFont(juce::FontOptions(15.0f));
    ed.setJustification(juce::Justification::centred);
    ed.setIndents(2, 1);
    ed.setText(slider.getTextFromValue(slider.getValue()), false);

    // Enter or clicking away applies; Escape cancels
    ed.onReturnKey = [this] { closeValueEditor(true); };
    ed.onFocusLost = [this] { closeValueEditor(true); };
    ed.onEscapeKey = [this] { closeValueEditor(false); };

    addAndMakeVisible(ed);
    ed.setBounds(getLocalBounds().removeFromBottom(20));
    ed.grabKeyboardFocus();
    ed.selectAll();
}

void SketchKnob::closeValueEditor(bool apply)
{
    if (valueEditor == nullptr) return;

    if (apply) // accepts "7", "7 st", "-3.5" ...; out-of-range values are clamped
        slider.setValue(slider.getValueFromText(valueEditor->getText()), juce::sendNotificationSync);

    // Delete it after the TextEditor's own callback has returned
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SketchKnob>(this)]
        { if (safe != nullptr) safe->valueEditor.reset(); });
    valueEditor->onFocusLost = nullptr;
    valueEditor->setVisible(false);
}

void SketchKnob::parentHierarchyChanged()
{
    // Value bubble lives on the plugin window (not a separate desktop window, which hosts dislike)
    if (auto* top = getTopLevelComponent(); top != this)
        slider.setPopupDisplayEnabled(true, true, top, 900);
}

void SketchKnob::paint(juce::Graphics& g)
{
    g.setColour(Theme::ink.withAlpha(slider.isMouseOverOrDragging() ? 1.0f : 0.8f));
    g.setFont(16.0f);
    g.drawText(label, getLocalBounds().removeFromBottom(18), juce::Justification::centred);
}

//==============================================================================
ControlPanel::ControlPanel(juce::AudioProcessorValueTreeState& s, juce::Colour c, const juce::String& t)
    : state(s), colour(c), title(t)
{
}

void ControlPanel::addKnob(const juce::String& paramID, const juce::String& text, const juce::String& suffix,
    double snapStep)
{
    auto knob = std::make_unique<SketchKnob>(state, paramID, text, suffix, colour, snapStep);
    addAndMakeVisible(*knob);
    rows.back().push_back({ std::move(knob), nullptr, SketchKnob::kWidth });
}

void ControlPanel::addToggle(const juce::String& paramID, const juce::String& text)
{
    auto toggle = std::make_unique<juce::ToggleButton>(text);
    toggle->setColour(juce::ToggleButton::tickColourId, colour);
    addAndMakeVisible(*toggle);
    buttonAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, paramID, *toggle));
    const int width = juce::jmax(70, juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(17.0f), text) + 34);
    rows.back().push_back({ std::move(toggle), nullptr, width });
}

void ControlPanel::addCombo(const juce::String& paramID, const juce::String& text, const juce::StringArray& items)
{
    auto box = std::make_unique<juce::ComboBox>();
    box->addItemList(items, 1); // ids 1..n match the choice parameter
    addAndMakeVisible(*box);
    comboAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, paramID, *box));

    auto label = std::make_unique<juce::Label>(juce::String(), text);
    label->setFont(juce::FontOptions(16.0f));
    label->setJustificationType(juce::Justification::centred);
    addAndMakeVisible(*label);

    rows.back().push_back({ std::move(box), std::move(label), 100 });
}

void ControlPanel::newRow()
{
    if (!rows.back().empty())
        rows.emplace_back();
}

void ControlPanel::finish()
{
    int width = juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(20.0f), title);
    for (auto& row : rows)
    {
        int rowWidth = 0;
        for (auto& item : row) rowWidth += item.width + 4;
        width = juce::jmax(width, rowWidth);
    }
    setSize(width + 2 * kPad, kTitleH + (int)rows.size() * SketchKnob::kHeight + kPad);
}

void ControlPanel::paint(juce::Graphics& g)
{
    g.setColour(Theme::ink);
    g.setFont(20.0f);
    g.drawText(title, getLocalBounds().reduced(kPad, 4).removeFromTop(kTitleH - 4), juce::Justification::centredLeft);

    juce::Path underline; // scribbled underline in the slot colour
    const float w = (float)juce::GlyphArrangement::getStringWidthInt(juce::FontOptions(20.0f), title);
    underline.startNewSubPath((float)kPad, (float)kTitleH - 2.0f);
    underline.lineTo((float)kPad + w, (float)kTitleH - 3.0f);
    Sketch::stroke(g, underline, colour.darker(0.2f), 3.0f, Sketch::seedFor(this), 2);
}

void ControlPanel::resized()
{
    auto r = getLocalBounds().reduced(kPad, 0).withTrimmedTop(kTitleH);
    for (auto& row : rows)
    {
        auto rowArea = r.removeFromTop(SketchKnob::kHeight);
        for (auto& item : row)
        {
            auto cell = rowArea.removeFromLeft(item.width);
            rowArea.removeFromLeft(4);
            if (item.label != nullptr) // dropdown: label above, box below
            {
                cell = cell.withSizeKeepingCentre(cell.getWidth(), 48);
                item.label->setBounds(cell.removeFromTop(20));
                item.comp->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 26));
            }
            else if (dynamic_cast<SketchKnob*> (item.comp.get()) != nullptr)
                item.comp->setBounds(cell.withSizeKeepingCentre(SketchKnob::kWidth, SketchKnob::kHeight));
            else
                item.comp->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 28));
        }
    }
}