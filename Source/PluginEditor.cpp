#include "PluginEditor.h"

namespace n2m
{
using namespace look;

static constexpr int kTabsH = 38, kMixerH = 128;

juce::Colour boardColour (int index)
{
    static const juce::Colour c[8] = { bandMint, bandBlue, bandPurple, bandPink, bandOrange, bandYellow, bandGreen, bandMagenta };
    return c[((index % 8) + 8) % 8];
}

static juce::Font smallCaps() { return font (9.0f, Weight::Medium, 0.4f); }
// For anything sitting on a board: tell the board it is about to change (for Undo), and when the gesture is over
static void boardWillChange (juce::Component& c, int removing = 0) { if (auto* v = c.findParentComponentOfClass<BoardView>()) v->willChange (removing); }
static void boardSettled (juce::Component& c) { if (auto* v = c.findParentComponentOfClass<BoardView>()) v->settled(); }
static juce::String groupLetter (int group) { return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (1, kLinkGroups, group) - 1)); }
static juce::Colour groupColour (int group) { return boardColour (group + 2); }

//==============================================================================
void N2MLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    if (down || b.getToggleState()) sunken (g, r, radiusControl);
    else raised (g, r, radiusControl, over);
}

// The small arrow on a drop-down
static void chevron (juce::Graphics& g, float cx, float cy, juce::Colour c)
{
    juce::Path p;
    p.startNewSubPath (cx - 4.0f, cy - 2.0f); p.lineTo (cx, cy + 2.0f); p.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// A piano keyboard: so many white keys up from a C. "lit" says which notes are down; they get a bar in "mark".
void drawKeyboard (juce::Graphics& g, juce::Rectangle<float> r, int lowC, int nWhite, const std::function<bool (int)>& lit, juce::Colour mark)
{
    static const int whiteSemis[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const float kw = r.getWidth() / (float) nWhite;
    for (int k = 0; k < nWhite; ++k)
    {
        const int note = lowC + 12 * (k / 7) + whiteSemis[k % 7];
        const auto key = juce::Rectangle<float> (r.getX() + (float) k * kw + 0.5f, r.getY(), kw - 1.0f, r.getHeight());
        const bool on = lit (note);
        g.setGradientFill (juce::ColourGradient (on ? juce::Colours::white : whiteKeyTop, key.getX(), key.getY(), on ? juce::Colours::white.interpolatedWith (mark, 0.12f) : whiteKeyBottom, key.getX(), key.getBottom(), false));
        g.fillRoundedRectangle (key, 2.0f);
        if (on) { g.setColour (mark); g.fillRoundedRectangle (key.getX() + 2.5f, key.getBottom() - 7.0f, key.getWidth() - 5.0f, 3.5f, 1.5f); }
    }
    const float bw = kw * 0.6f, bh = r.getHeight() * 0.6f;
    for (int k = 0; k + 1 < nWhite; ++k)
    {
        if (k % 7 == 2 || k % 7 == 6) continue;          // no black key after E or B
        const int note = lowC + 12 * (k / 7) + whiteSemis[k % 7] + 1;
        const auto key = juce::Rectangle<float> (r.getX() + (float) (k + 1) * kw - bw * 0.5f, r.getY(), bw, bh);
        const bool on = lit (note);
        g.setGradientFill (juce::ColourGradient (blackKeyTop, key.getX(), key.getY(), blackKeyBottom, key.getX(), key.getBottom(), false));
        g.fillRoundedRectangle (key, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (key, 2.0f, 1.0f);
        if (on) { g.setColour (mark); g.fillRoundedRectangle (key.getX() + 2.0f, key.getBottom() - 7.0f, key.getWidth() - 4.0f, 3.5f, 1.5f); }
    }
}

void N2MLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (controlFont());
    g.setColour (! b.isEnabled() ? n6 : b.getToggleState() ? accent : n8);
    if (b.getProperties()["dropdown"])
    {
        // a drop-down: what is chosen on the left, an arrow on the right
        g.drawFittedText (b.getButtonText(), 10, 0, b.getWidth() - 32, b.getHeight(), juce::Justification::centredLeft, 1, 0.75f);
        chevron (g, (float) b.getWidth() - 14.0f, (float) b.getHeight() * 0.5f, n7);
        return;
    }
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
ParamCell::ParamCell (Nebula2ModulationProcessor& p, Module& m, int index) : proc (p), module (m), idx (index), param (m.info->params[(size_t) index])
{
    setSize (lay::cell, lay::cellMinH);
}

void ParamCell::setNorm (float n)
{
    module.params[(size_t) idx].store (paramFromNorm (param, n));
    changed();
}

void ParamCell::changed()
{
    repaint();
    const bool knobLinked = module.paramLink[(size_t) idx] > 0;
    // A linked sequencer's length knobs are part of its rhythm
    const bool patternLinked = module.patternLink > 0 && (std::string (param.id) == "bars" || std::string (param.id) == "length");
    if (knobLinked) proc.rack.pushParam (module, idx);
    if (patternLinked) proc.rack.pushPattern (module);
    if (knobLinked || patternLinked)
        if (auto* view = findParentComponentOfClass<BoardView>()) view->repaintAll();
}

// Right-click a knob: put it in a link group. Knobs in the same group, on any tab, turn together.
void ParamCell::linkMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Link this knob");
    const int cur = module.paramLink[(size_t) idx];
    m.addItem (1, "Not linked", true, cur == 0);
    for (int g = 1; g <= kLinkGroups; ++g)
    {
        const int n = proc.rack.membersOf (g, false);
        m.addItem (g + 1, "Group " + groupLetter (g) + (n > 0 ? "   (" + juce::String (n) + (n == 1 ? " knob)" : " knobs)") : ""), true, cur == g);
    }
    // ...or give it a line on the timeline, to move it through the song
    auto* comp = dynamic_cast<ModuleComp*> (getParentComponent());
    if (comp != nullptr && param.choices.empty())
    {
        m.addSeparator();
        m.addItem (100, "Move this knob through the song (timeline)", true, comp->onTimeline (idx));
    }
    juce::Component::SafePointer<ParamCell> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (r == 100)
        {
            auto* mc = dynamic_cast<ModuleComp*> (safe->getParentComponent());
            auto* view = safe->findParentComponentOfClass<BoardView>();
            if (mc != nullptr && view != nullptr && view->onDrawKnob) view->onDrawKnob (mc->itsBoard(), safe->module.uid, safe->idx);
            return;
        }
        boardWillChange (*safe);
        safe->proc.rack.linkParam (safe->module, safe->idx, r - 1);
        safe->repaint();
    });
}

void ParamCell::paint (juce::Graphics& g)
{
    const float v = module.P (idx);
    g.setFont (font (8.0f, Weight::Medium, 0.2f));
    g.setColour (n8);
    const bool drop = dropdown && ! param.choices.empty();       // a drop-down's label starts at its left edge
    g.drawFittedText (juce::String (param.name).toUpperCase(), drop ? 2 : 0, 0, getWidth() - (drop ? 2 : 0), 11, drop ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.6f);

    // A linked knob wears its group's letter
    if (const int group = module.paramLink[(size_t) idx]; group > 0)
    {
        const auto badge = juce::Rectangle<float> ((float) getWidth() - 12.0f, 11.0f, 12.0f, 12.0f);
        g.setColour (groupColour (group));
        g.fillEllipse (badge);
        g.setFont (font (8.5f, Weight::SemiBold));
        g.setColour (n0);
        g.drawText (groupLetter (group), badge.toNearestInt(), juce::Justification::centred, false);
    }

    // A knob the timeline moves wears a small mark
    if (auto* comp = dynamic_cast<ModuleComp*> (getParentComponent()); comp != nullptr && comp->onTimeline (idx))
    {
        g.setColour (noteAccent);
        g.fillRoundedRectangle (1.0f, 13.0f, 9.0f, 3.0f, 1.5f);
    }
    const float W = (float) getWidth(), H = (float) getHeight();
    if (! param.choices.empty() && dropdown)
    {
        // On a wide panel: a drop-down, level with the knobs beside it
        const auto r = juce::Rectangle<float> (1.0f, juce::jmax (13.0f, 12.0f + (H - 24.0f) * 0.5f - 11.0f), W - 2.0f, 22.0f);
        raised (g, r.reduced (0.5f), radiusControl, isMouseOver());
        g.setFont (controlFont());
        g.setColour (n8);
        g.drawFittedText (paramText (param, v), (int) r.getX() + 10, (int) r.getY(), (int) r.getWidth() - 30, (int) r.getHeight(), juce::Justification::centredLeft, 1, 0.75f);
        chevron (g, r.getRight() - 13.0f, r.getCentreY(), n7);
        return;
    }
    if (! param.choices.empty())
    {
        // A switch: a small display showing the choice
        const auto r = juce::Rectangle<float> (2.0f, 13.0f + (H - 13.0f - 22.0f) * 0.5f - 1.0f, W - 4.0f, 22.0f);
        g.setColour (juce::Colour (0xff0b0c10));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (accent.withAlpha (isMouseOver() ? 0.55f : 0.25f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        g.setFont (font (10.0f, Weight::Medium));
        g.setColour (accent.brighter (0.3f));
        g.drawFittedText (paramText (param, v), r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
        return;
    }

    // The knob is as big as its cell allows: a lone knob on a panel is a big one
    const float norm = paramToNorm (param, v);
    const float D = juce::jlimit (18.0f, 46.0f, juce::jmin (W - 14.0f, H - 30.0f));
    const juce::Point<float> c (W * 0.5f, 12.0f + (H - 12.0f - 12.0f) * 0.5f);
    const float a0 = -2.36f, a1 = 2.36f, va = a0 + norm * (a1 - a0);
    const bool bipolar = param.lo < 0.0f && param.hi > 0.0f;
    const float mid = bipolar ? a0 + paramToNorm (param, 0.0f) * (a1 - a0) : a0;
    // The arc round it: a dim track, lit from the rest position to where the knob is
    const float ar = D * 0.5f + 3.5f;
    juce::Path track; track.addCentredArc (c.x, c.y, ar, ar, 0.0f, a0, a1, true);
    g.setColour (juce::Colours::white.withAlpha (isMouseOver() ? 0.16f : 0.09f));
    g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (std::abs (va - mid) > 0.02f)
    {
        juce::Path lit; lit.addCentredArc (c.x, c.y, ar, ar, 0.0f, juce::jmin (mid, va), juce::jmax (mid, va), true);
        g.setColour (dragging ? accent.brighter (0.2f) : accent);
        g.strokePath (lit, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const auto body = juce::Rectangle<float> (D, D).withCentre (c);
    juce::Path bp; bp.addEllipse (body);
    shadow (g, bp, juce::Colours::black.withAlpha (0.6f), 0.3f * D, 0.0f, 0.12f * D);
    g.setGradientFill (juce::ColourGradient (isMouseOver() ? juce::Colour (0xff4a4e5a) : juce::Colour (0xff3a3d48), body.getX(), body.getY(), juce::Colour (0xff15161b), body.getX(), body.getBottom(), false));
    g.fillPath (bp);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);
    // the cap, and a white pointer line like a hardware knob
    const auto cap = body.reduced (D * 0.16f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2f38), cap.getX(), cap.getY(), juce::Colour (0xff1d1f26), cap.getX(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (dragging ? accent : juce::Colours::white.withAlpha (0.92f));
    g.drawLine ({ { c.x + D * 0.14f * std::sin (va), c.y - D * 0.14f * std::cos (va) }, { c.x + D * 0.46f * std::sin (va), c.y - D * 0.46f * std::cos (va) } }, juce::jmax (1.6f, D * 0.07f));

    g.setFont (font (9.5f));
    g.setColour (n8);
    g.drawFittedText (minus (paramText (param, v)), -2, getHeight() - 12, getWidth() + 4, 12, juce::Justification::centred, 1, 0.75f);
}

void ParamCell::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { linkMenu(); return; }
    if (! param.choices.empty())
    {
        juce::PopupMenu m;
        const int cur = (int) std::lround (module.P (idx));
        for (int i = 0; i < (int) param.choices.size(); ++i) m.addItem (i + 1, param.choices[(size_t) i], true, i == cur);
        juce::Component::SafePointer<ParamCell> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe != nullptr && r > 0) { boardWillChange (*safe); safe->module.params[(size_t) safe->idx].store ((float) (r - 1)); safe->changed(); }
        });
        return;
    }
    boardWillChange (*this);
    dragStart = paramToNorm (param, module.P (idx));
    dragging = true;
    juce::ignoreUnused (e);
    repaint();
}

void ParamCell::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    setNorm (dragStart - (float) e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 600.0f : 150.0f));
}

void ParamCell::mouseUp (const juce::MouseEvent&) { if (dragging) boardSettled (*this); dragging = false; repaint(); }

void ParamCell::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param.choices.empty()) { boardWillChange (*this); module.params[(size_t) idx].store (param.def); changed(); }
}

void ParamCell::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! param.choices.empty()) { Component::mouseWheelMove (e, w); return; }
    // One undo step for a spin of the wheel, not one per notch
    static juce::uint32 lastWheel = 0;
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastWheel > 700) boardWillChange (*this);
    lastWheel = now;
    const float step = param.fmt == Fmt::Int ? (w.deltaY > 0 ? 1.0f : -1.0f) / std::max (1.0f, param.hi - param.lo) : w.deltaY * 0.25f;
    setNorm (paramToNorm (param, module.P (idx)) + step);
}

//==============================================================================
static constexpr int kDegLo = -7, kDegHi = 14;

SeqView::SeqView (Nebula2ModulationProcessor& p, Board& b, Module& m) : wide (lay::isWide (*m.info)), proc (p), board (b), module (m)
{
    addAndMakeVisible (ideas); addAndMakeVisible (random);
    ideas.onClick = [this] { showIdeas(); };
    random.onClick = [this]
    {
        boardWillChange (*this);
        if (module.info->seq == Seq::Drum) randomDrum (ideaKind(), proc.nextSeed(), module);
        else if (module.info->seq == Seq::Value) randomValues (proc.nextSeed(), module);
        else randomNotes (board.type, proc.nextSeed(), module);
        edited();
    };
    if (wide)
    {
        // The drum and note sequencers: bar buttons, the idea as a drop-down, Random, then the steps
        const auto g = lay::geo (*m.info);
        ideas.getProperties().set ("dropdown", true);
        setSize (g.screen.w, g.screen.h);
        refreshIdea();
    }
    else setSize (lay::seqW, lay::seqHeight (*m.info));
}

void SeqView::resized()
{
    if (wide) { ideas.setBounds (134, 0, 150, 22); random.setBounds (288, 0, 64, 22); return; }
    ideas.setBounds (0, 0, 58, 20); random.setBounds (62, 0, 66, 20);
}

// ---- The wide layout: where things are ----
juce::Rectangle<int> SeqView::barButton (int k) const { return k == 0 ? juce::Rectangle<int> (0, 0, 44, 22) : juce::Rectangle<int> (46 + (k - 1) * 28, 0, 26, 22); }
// One thin mark per bar of the pattern, under the top line: the bar on show is lit, the bar playing is yellow
juce::Rectangle<int> SeqView::pageMark (int bar) const { const int w = lay::wideSeqW / bars(); return { bar * w + 1, 25, w - 2, 7 }; }
juce::Rectangle<int> SeqView::stepRow() const { return { 0, 36, lay::wideSeqW, 32 }; }
juce::Rectangle<int> SeqView::velRow() const { return { 0, 76, lay::wideSeqW, getHeight() - 76 - 2 }; }
juce::Rectangle<int> SeqView::nameRow() const { return { 0, getHeight() - 16, lay::wideSeqW, 14 }; }

void SeqView::setBars (int index)
{
    const int k = module.paramIndex ("bars");
    if (k < 0) return;
    boardWillChange (*this);
    module.params[(size_t) k].store ((float) juce::jlimit (0, 3, index));
    page = juce::jmin (page, bars() - 1);
    edited();           // (a linked sequencer's length is part of its rhythm, so the others follow)
}

void SeqView::showPage (int bar) { page = juce::jlimit (0, bars() - 1, bar); repaint(); }

void SeqView::toggleAccentAt (int step)
{
    if (module.info->seq != Seq::Drum) return;
    boardWillChange (*this);
    if (toggleAccent (module, step)) edited(); else boardSettled (*this);
}

void SeqView::chooseIdea (int index)
{
    const bool drum = module.info->seq == Seq::Drum, value = module.info->seq == Seq::Value;
    const int count = drum ? (int) drumIdeas (ideaKind()).size() : value ? (int) valueIdeas().size() : (int) noteIdeas (board.type).size();
    if (index < 0 || index >= count) return;
    boardWillChange (*this);
    if (drum) setDrum (module, drumIdeas (ideaKind())[(size_t) index].pat);
    else if (value) setValues (module, valueIdeas()[(size_t) index].v);
    else { auto& d = noteIdeas (board.type)[(size_t) index]; setNotes (module, d.gates, d.deg); }
    edited();
}

// The idea the pattern is now, by name. Once it has been edited or randomised it is nobody's idea any more.
juce::String SeqView::ideaName() const
{
    if (module.info->seq == Seq::Drum) { const int i = drumIdeaIndex (module, ideaKind()); return i >= 0 ? drumIdeas (ideaKind())[(size_t) i].name : "Your own"; }
    if (module.info->seq == Seq::Note) { const int i = noteIdeaIndex (module, board.type); return i >= 0 ? noteIdeas (board.type)[(size_t) i].name : "Your own"; }
    return "Ideas";
}

void SeqView::refreshIdea()
{
    if (! wide) return;
    const auto name = ideaName();
    if (ideas.getButtonText() != name) ideas.setButtonText (name);
}

// Which set of pattern ideas suits this unit: a trance gate has its own, otherwise the tab's kind of sound decides
std::string SeqView::ideaKind() const { return std::string (module.info->type) == "gate" ? "gate" : board.type; }

void SeqView::edited()
{
    repaint();
    refreshIdea();
    if (module.patternLink <= 0) return;
    proc.rack.pushPattern (module);
    if (auto* view = findParentComponentOfClass<BoardView>()) view->repaintAll();
}

juce::Rectangle<int> SeqView::grid() const
{
    if (wide) return module.info->seq == Seq::Drum ? stepRow() : juce::Rectangle<int> (0, 36, lay::wideSeqW, getHeight() - 36 - 20);
    return { 0, 24, lay::seqW, getHeight() - 24 - 4 };
}
// The step under the mouse, on the bar being shown
int SeqView::stepAtX (int x) const { return page * kSteps + juce::jlimit (0, kSteps - 1, x / (wide ? lay::stepW : 20)); }
int SeqView::bars() const { return 1 << juce::jlimit (0, 3, (int) std::lround (module.P (module.paramIndex ("bars")))); }
// One small button per bar, to the right of Random
juce::Rectangle<int> SeqView::pill (int bar) const { return { 136 + bar * 23, 1, 21, 18 }; }

void SeqView::showIdeas()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Pattern ideas");
    const bool drum = module.info->seq == Seq::Drum, value = module.info->seq == Seq::Value;
    int i = 1;
    if (drum) for (auto& d : drumIdeas (ideaKind())) m.addItem (i++, d.name);
    else if (value) for (auto& d : valueIdeas()) m.addItem (i++, d.name);
    else for (auto& d : noteIdeas (board.type)) m.addItem (i++, d.name);
    juce::Component::SafePointer<SeqView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&ideas), [safe, drum, value] (int r)
    {
        juce::ignoreUnused (drum, value);
        if (safe != nullptr && r > 0) safe->chooseIdea (r - 1);
    });
}

// The wide drum and note sequencers
void SeqView::paintWide (juce::Graphics& g)
{
    const auto col = boardColour (board.colour);
    const int len = loopSteps (module.P (module.paramIndex ("length")), module.P (module.paramIndex ("bars")));
    const int nBars = bars(), barsIndex = juce::jlimit (0, 3, (int) std::lround (module.P (module.paramIndex ("bars"))));
    page = juce::jmin (page, nBars - 1);
    const bool drum = module.info->seq == Seq::Drum;
    const float sw = (float) lay::stepW;

    // How long the pattern is: 1, 2, 4 or 8 bars
    static const char* const lengths[4] = { "1 bar", "2", "4", "8" };
    for (int k = 0; k < 4; ++k)
    {
        const auto r = barButton (k);
        if (k == barsIndex) sunken (g, r.toFloat().reduced (0.5f), radiusControl);
        g.setFont (controlFont());
        g.setColour (k == barsIndex ? accent : n7);
        g.drawText (lengths[k], r, juce::Justification::centred, false);
    }
    // Which bar is on show
    if (nBars > 1)
        for (int b = 0; b < nBars; ++b)
        {
            const auto r = pageMark (b).toFloat().withSizeKeepingCentre ((float) pageMark (b).getWidth(), 3.0f);
            g.setColour (shownStep >= 0 && shownStep / kSteps == b ? noteAccent : b == page ? col : n5);
            g.fillRoundedRectangle (r, 1.5f);
        }

    if (drum)
    {
        const auto sr = stepRow().toFloat(), vr = velRow().toFloat();
        sunken (g, vr, radiusControl);
        for (int i = 0; i < kSteps; ++i)
        {
            const int st = page * kSteps + i;
            const float alpha = st < len ? 1.0f : 0.3f;
            const float d = module.data[(size_t) st].load();
            const auto tile = juce::Rectangle<float> ((float) i * sw + 2.0f, sr.getY(), sw - 4.0f, sr.getHeight());
            if (d > 0.5f) g.setColour (col.withAlpha ((d > 1.5f ? 1.0f : 0.5f) * alpha));
            else g.setColour ((i % 4 == 0 ? n4 : raisedHi).withAlpha (alpha));
            g.fillRoundedRectangle (tile, 3.0f);
            g.setColour (materialRim);
            g.drawRoundedRectangle (tile.reduced (0.5f), 3.0f, 1.0f);
            if (st == shownStep) { g.setColour (noteAccent); g.drawRoundedRectangle (tile.expanded (1.0f), 4.0f, 1.6f); }
            // its velocity: tall for an accent, shorter for a plain hit
            if (d > 0.5f)
            {
                // (a plain hit and an accent are drawn as they always were; hits set in the bigger view fall in between or below)
                const float v = drumVel (d);
                const float h = (vr.getHeight() - 12.0f) * (v >= 0.72f ? 0.55f + (v - 0.72f) / 0.28f * 0.45f : juce::jmax (0.12f, 0.55f * v / 0.72f));
                g.setColour (col.withAlpha ((d > 1.5f ? 0.95f : 0.6f) * alpha));
                g.fillRoundedRectangle (tile.getX() + 1.0f, vr.getBottom() - 6.0f - h, tile.getWidth() - 2.0f, h, 2.0f);
            }
        }
        return;
    }

    // Notes, piano-roll style: a short bar per note at the height of its pitch
    const auto gr = grid().toFloat();
    const int root = proc.rack.root.load();
    const int* sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, proc.rack.scale.load())].iv;
    const float rowH = gr.getHeight() / (float) (kDegHi - kDegLo + 1);
    g.setColour (juce::Colour (0xff07080b));
    g.fillRoundedRectangle (gr, 4.0f);
    for (int i = 0; i < kSteps; ++i)          // the playing step is shaded; steps past the end are dark
    {
        const int st = page * kSteps + i;
        const auto column = juce::Rectangle<float> ((float) i * sw, gr.getY(), sw, gr.getHeight());
        if (st == shownStep) { g.setColour (juce::Colours::white.withAlpha (0.09f)); g.fillRect (column); }
        if (i > 0) { g.setColour (juce::Colours::white.withAlpha (i % 4 == 0 ? 0.09f : 0.03f)); g.fillRect (column.getX(), gr.getY() + 1.0f, 1.0f, gr.getHeight() - 2.0f); }
    }
    for (int deg : { 0, 7, 14 })               // the key note in each octave
    {
        g.setColour (juce::Colours::white.withAlpha (deg == 0 ? 0.10f : 0.05f));
        g.fillRect (gr.getX() + 1.0f, gr.getBottom() - (float) (deg - kDegLo) * rowH, gr.getWidth() - 2.0f, 1.0f);
    }
    for (int i = 0; i < kSteps; ++i)
    {
        const int st = page * kSteps + i;
        const float alpha = st < len ? 1.0f : 0.3f;
        const bool on = module.data[(size_t) st].load() > 0.5f;
        const int deg = juce::jlimit (kDegLo, kDegHi, (int) std::lround (module.data[(size_t) (kMaxSteps + st)].load()));
        if (on)
        {
            const float y = gr.getBottom() - ((float) (deg - kDegLo) + 0.5f) * rowH;
            g.setColour (col.withAlpha (alpha));
            g.fillRoundedRectangle ((float) i * sw + 2.0f, y - 2.0f, sw - 4.0f, 4.0f, 2.0f);
        }
        // its note, named underneath (click the name to switch the step on or off)
        const auto cell = juce::Rectangle<int> (i * lay::stepW, nameRow().getY(), lay::stepW, nameRow().getHeight());
        g.setFont (font (8.5f, Weight::Medium));
        g.setColour ((on ? n8 : n6).withAlpha (alpha));
        g.drawText (kNoteNames[(((root + degreeToSemis (deg, sc)) % 12) + 12) % 12], cell, juce::Justification::centred, false);
        if (st == shownStep) { g.setColour (noteAccent); g.fillRoundedRectangle (cell.toFloat().withTop ((float) cell.getBottom() - 2.0f).reduced (3.0f, 0.0f), 1.0f); }
    }
    g.setColour (col.withAlpha (0.22f));
    g.drawRoundedRectangle (gr.reduced (0.5f), 4.0f, 1.0f);
}

void SeqView::paint (juce::Graphics& g)
{
    if (wide) { paintWide (g); return; }
    const auto col = boardColour (board.colour);
    const auto gr = grid();
    const int len = loopSteps (module.P (module.paramIndex ("length")), module.P (module.paramIndex ("bars")));
    const int nBars = bars();
    page = juce::jmin (page, nBars - 1);
    const bool drum = module.info->seq == Seq::Drum;
    const int root = proc.rack.root.load();
    const int* sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, proc.rack.scale.load())].iv;

    // Bar buttons: the one shown is lit, the one playing has a yellow mark
    if (nBars > 1)
        for (int b = 0; b < nBars; ++b)
        {
            const auto r = pill (b).toFloat();
            if (b == page) { g.setColour (col.withAlpha (0.85f)); g.fillRoundedRectangle (r, 4.0f); }
            else { g.setColour (n4); g.fillRoundedRectangle (r, 4.0f); }
            g.setFont (font (10.0f, Weight::SemiBold));
            g.setColour (b == page ? n0 : n7);
            g.drawText (juce::String (b + 1), pill (b), juce::Justification::centred, false);
            if (shownStep >= 0 && shownStep / kSteps == b) { g.setColour (noteAccent); g.fillRoundedRectangle (r.withTop (r.getBottom() - 2.5f).reduced (4.0f, 0.0f), 1.0f); }
        }

    for (int i = 0; i < kSteps; ++i)
    {
        const int st = page * kSteps + i;
        const float alpha = st < len ? 1.0f : 0.3f;
        const bool now = st == shownStep;
        if (drum)
        {
            const auto r = juce::Rectangle<float> ((float) (i * 20 + 1), (float) gr.getY(), 18.0f, (float) gr.getHeight());
            const float d = module.data[(size_t) st].load();
            if (d > 0.5f)
            {
                g.setColour (col.withAlpha ((d > 1.5f ? 1.0f : 0.5f) * alpha));
                g.fillRoundedRectangle (r, 3.0f);
            }
            else
            {
                g.setColour ((i % 4 == 0 ? n4 : sunkenHi).withAlpha (alpha));
                g.fillRoundedRectangle (r, 3.0f);
            }
            if (now) { g.setColour (noteAccent); g.fillRoundedRectangle (r.withTop (r.getBottom() - 3.0f), 1.5f); }
        }
        else
        {
            const auto bar = juce::Rectangle<float> ((float) (i * 20 + 1), (float) gr.getY(), 18.0f, (float) gr.getHeight() - 18.0f);
            const auto cell = juce::Rectangle<float> ((float) (i * 20 + 1), bar.getBottom() + 4.0f, 18.0f, 14.0f);
            g.setColour ((i % 4 == 0 ? n4.withAlpha (0.6f) : sunkenHi).withAlpha (alpha));
            g.fillRoundedRectangle (bar, 3.0f);
            const bool on = module.data[(size_t) st].load() > 0.5f;
            const int deg = juce::jlimit (kDegLo, kDegHi, (int) std::lround (module.data[(size_t) (kMaxSteps + st)].load()));
            const float rowH = bar.getHeight() / (float) (kDegHi - kDegLo + 1);
            const float y = bar.getBottom() - (float) (deg - kDegLo + 1) * rowH;
            g.setColour (on ? col.withAlpha (alpha) : n6.withAlpha (0.5f * alpha));
            g.fillRoundedRectangle (bar.getX() + 2.0f, y - 1.0f, 14.0f, std::max (4.0f, rowH + 2.0f), 2.0f);
            g.setColour (on ? col.withAlpha (0.22f * alpha) : sunkenHi.withAlpha (alpha));
            g.fillRoundedRectangle (cell, 3.0f);
            g.setFont (font (8.5f, Weight::Medium));
            g.setColour (on ? n8.withAlpha (alpha) : n6.withAlpha (alpha));
            // A note sequencer names the note; a step modulator shows how far up the step is
            if (module.info->seq == Seq::Value) g.drawText (juce::String ((int) std::lround ((float) (deg - kDegLo) / (float) (kDegHi - kDegLo) * 100.0f)), cell.toNearestInt(), juce::Justification::centred, false);
            else g.drawText (kNoteNames[(((root + degreeToSemis (deg, sc)) % 12) + 12) % 12], cell.toNearestInt(), juce::Justification::centred, false);
            if (now) { g.setColour (noteAccent); g.fillRoundedRectangle (cell.withTop (cell.getBottom() - 2.5f), 1.0f); }
        }
    }
}

void SeqView::setDegreeFromY (int step, int y)
{
    const auto gr = grid();
    const float barH = (float) gr.getHeight() - (wide ? 0.0f : 18.0f);
    const float rowH = barH / (float) (kDegHi - kDegLo + 1);
    const int deg = juce::jlimit (kDegLo, kDegHi, kDegLo + (int) (((float) gr.getY() + barH - (float) y) / rowH));
    module.data[(size_t) (kMaxSteps + step)].store ((float) deg);
    module.data[(size_t) step].store (1.0f);
}

void SeqView::mouseDown (const juce::MouseEvent& e)
{
    const auto gr = grid();
    dragStep = -1;
    if (wide)
    {
        const auto at = e.getPosition();
        const int barsIndex = juce::jlimit (0, 3, (int) std::lround (module.P (module.paramIndex ("bars"))));
        // A bar button sets the length; the one already chosen steps through the bars instead
        for (int k = 0; k < 4; ++k)
            if (barButton (k).contains (at)) { if (k == barsIndex) showPage ((page + 1) % bars()); else setBars (k); return; }
        for (int b = 0; b < bars(); ++b)
            if (bars() > 1 && pageMark (b).expanded (0, 2).contains (at)) { showPage (b); return; }
        const int s = stepAtX (e.x);
        if (module.info->seq == Seq::Drum)
        {
            if (stepRow().contains (at))
            {
                boardWillChange (*this);
                const float d = module.data[(size_t) s].load();
                module.data[(size_t) s].store (d < 0.5f ? 1.0f : d < 1.5f ? 2.0f : 0.0f);
                edited();
            }
            else if (velRow().contains (at)) toggleAccentAt (s);
            return;
        }
        if (nameRow().contains (at))
        {
            boardWillChange (*this);
            module.data[(size_t) s].store (module.data[(size_t) s].load() > 0.5f ? 0.0f : 1.0f);
            module.data[(size_t) (kLenRow + s)].store (0.0f);       // (a length given in the bigger view goes with its note)
            edited();
        }
        else if (gr.contains (at))
        {
            boardWillChange (*this);
            // a click right on a note takes it away; anywhere else puts the step's note there
            const bool on = module.data[(size_t) s].load() > 0.5f;
            const int was = (int) std::lround (module.data[(size_t) (kMaxSteps + s)].load());
            setDegreeFromY (s, e.y);
            if (! on) module.data[(size_t) (kLenRow + s)].store (0.0f);
            if (on && was == (int) std::lround (module.data[(size_t) (kMaxSteps + s)].load())) { module.data[(size_t) s].store (0.0f); module.data[(size_t) (kLenRow + s)].store (0.0f); }
            else dragStep = s;
            edited();
        }
        return;
    }
    for (int b = 0; b < bars(); ++b)
        if (bars() > 1 && pill (b).contains (e.getPosition())) { page = b; repaint(); return; }
    if (! gr.contains (e.getPosition())) return;
    boardWillChange (*this);
    const int s = stepAtX (e.x);
    if (module.info->seq == Seq::Drum)
    {
        const float d = module.data[(size_t) s].load();
        module.data[(size_t) s].store (d < 0.5f ? 1.0f : d < 1.5f ? 2.0f : 0.0f);
    }
    else if (e.y >= gr.getBottom() - 16)
        module.data[(size_t) s].store (module.data[(size_t) s].load() > 0.5f ? 0.0f : 1.0f);
    else { dragStep = s; setDegreeFromY (s, e.y); }
    edited();
}

void SeqView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0) return;
    dragStep = stepAtX (e.x);
    setDegreeFromY (dragStep, e.y);
    edited();
}

void SeqView::tick()
{
    const int s = module.playStep.load();
    if (s != shownStep) { shownStep = s; repaint(); }
    refreshIdea();
}

//==============================================================================
ChordView::ChordView (Nebula2ModulationProcessor& p, Module& m) : proc (p), module (m)
{
    for (auto* b : { &mood, &generate, &length, &each, &more }) addAndMakeVisible (b);
    auto changed = [this] { proc.chordsVersion.fetch_add (1); };
    // More: how adventurous the chords are, and how much the repeats differ
    more.onClick = [this, changed]
    {
        juce::PopupMenu strange, variation, menu;
        for (int v : { 0, 20, 35, 50, 70, 100 }) strange.addItem (1000 + v, juce::String (v) + "%", true, proc.rack.harmony.strange == v);
        for (int v : { 0, 25, 50, 75, 100 }) variation.addItem (2000 + v, juce::String (v) + "%", true, proc.rack.harmony.variation == v);
        menu.addSubMenu ("Strange: rarer chords, richer notes (" + juce::String (proc.rack.harmony.strange) + "%)", strange);
        menu.addSubMenu ("Variation: how much the repeats change (" + juce::String (proc.rack.harmony.variation) + "%)", variation);
        juce::PopupMenu lift;
        const char* liftNames[4] = { "None", "Up 1 semitone", "Up 2 semitones", "Up 3 semitones" };
        for (int v = 0; v < 4; ++v) lift.addItem (3000 + v, liftNames[v], true, proc.rack.harmony.lift == v);
        menu.addSubMenu (juce::String ("Key change for the last part of the song (") + liftNames[juce::jlimit (0, 3, proc.rack.harmony.lift)] + ")", lift);
        juce::Component::SafePointer<ChordView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&more), [safe, changed] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->proc.history.beforeChords();
            if (r >= 3000) safe->proc.rack.harmony.lift = r - 3000;
            else if (r >= 2000) safe->proc.rack.harmony.variation = r - 2000; else safe->proc.rack.harmony.strange = r - 1000;
            safe->proc.rack.generateChords (safe->proc.rack.harmony.seed);
            changed();
        });
    };
    generate.onClick = [this, changed] { proc.history.beforeChords(); proc.rack.generateChords (proc.nextSeed()); changed(); };
    mood.onClick = [this, changed]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Mood");
        int i = 1;
        for (auto& md : n2c::moods()) menu.addItem (i++, md.name, true, md.id == proc.rack.harmony.mood);
        juce::Component::SafePointer<ChordView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mood), [safe, changed] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->proc.history.beforeChords();
            safe->proc.rack.setMood (n2c::moods()[(size_t) (r - 1)].id);
            changed();
        });
    };
    length.onClick = [this, changed]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Song length");
        for (int bars : { 8, 16, 32 }) menu.addItem (bars, juce::String (bars) + " bars", true, bars == proc.rack.harmony.lenBars);
        juce::Component::SafePointer<ChordView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&length), [safe, changed] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->proc.history.beforeChords();
            safe->proc.rack.harmony.lenBars = r;
            safe->proc.rack.generateChords (safe->proc.rack.harmony.seed);
            changed();
        });
    };
    each.onClick = [this, changed]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Each chord lasts");
        for (int bars : { 1, 2, 4 }) menu.addItem (bars, bars == 1 ? "1 bar" : juce::String (bars) + " bars", true, bars == proc.rack.harmony.chordBars);
        juce::Component::SafePointer<ChordView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&each), [safe, changed] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->proc.history.beforeChords();
            safe->proc.rack.harmony.chordBars = r;
            safe->proc.rack.generateChords (safe->proc.rack.harmony.seed);
            changed();
        });
    };
    mood.getProperties().set ("dropdown", true);
    setSize (lay::chordColX + lay::chordColW, lay::wideBodyH);
    refreshLabels();
}

void ChordView::refreshLabels()
{
    const auto& h = proc.rack.harmony;
    mood.setButtonText (n2c::moods()[(size_t) n2c::moodIndex (h.mood)].name);
    length.setButtonText (juce::String (h.lenBars) + " bars");
    each.setButtonText (h.chordBars == 1 ? "1 bar each" : juce::String (h.chordBars) + " bars each");
}

// The tiles and the keyboard are on the left; the unit's own buttons are in a column to the right of them.
// (The Rhythm drop-down in that column is one of the unit's knobs, so the panel puts it there.)
void ChordView::resized()
{
    const int cx = lay::chordColX;
    mood.setBounds (cx, 13, 150, 22);
    generate.setBounds (cx + 156, 13, 94, 22);
    more.setBounds (cx + 110, 55, 60, 22);
    length.setBounds (cx, 101, 80, 22);
    each.setBounds (cx + 86, 101, 104, 22);
}

juce::Rectangle<int> ChordView::tile (int i) { return { 4 + (i % 8) * 59, 4 + (i / 8) * lay::wideTileH, 57, lay::wideTileH - 2 }; }

// Under the tiles, however many rows of them there are
juce::Rectangle<int> ChordView::keyboardArea() const
{
    const int n = juce::jlimit (1, 32, (int) proc.rack.harmony.names.size());
    const int top = 4 + ((n + 7) / 8) * lay::wideTileH + 2;
    return { 4, top, lay::chordW - 8, getHeight() - top - 4 };
}

std::vector<int> ChordView::litNotes() const
{
    std::vector<int> notes;
    const auto& tl = proc.rack.timeline;
    const int i = shown >= 0 ? shown : 0;
    if (i >= (int) tl.slots.size()) return notes;
    const auto& slot = tl.slots[(size_t) i];
    const int oct = (int) std::lround (module.P (module.paramIndex ("octave")));
    notes.push_back (60 + (int) std::lround (slot.bass * 12.0f));
    for (int k = 0; k < 4; ++k) notes.push_back (60 + (int) std::lround (slot.voice[k] * 12.0f) + 12 * oct);
    return notes;
}

bool ChordView::hitTest (int x, int y)
{
    for (auto* c : getChildren()) if (c->isVisible() && c->getBounds().contains (x, y)) return true;
    const int n = juce::jmin (32, (int) proc.rack.harmony.seq.size());
    for (int i = 0; i < n; ++i) if (tile (i).contains (x, y)) return true;
    return false;
}

void ChordView::paint (juce::Graphics& g)
{
    const auto& h = proc.rack.harmony;
    const auto kb = keyboardArea();
    // the screen: black glass behind the tiles and the keyboard
    const auto glass = juce::Rectangle<float> (0.0f, 0.0f, (float) lay::chordW, (float) kb.getBottom() + 4.0f);
    g.setColour (juce::Colour (0xff07080b));
    g.fillRoundedRectangle (glass, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (glass.reduced (0.5f), 4.0f, 1.0f);

    for (int i = 0; i < (int) h.names.size() && i < 32; ++i)
    {
        const auto r = tile (i).toFloat();
        const auto col = degreeColour (h.seq[(size_t) i].deg);
        const bool now = i == shown;
        g.setColour (raisedHi.interpolatedWith (col, 0.10f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col.withAlpha (0.9f));
        g.fillRoundedRectangle (r.getX() + 3.0f, r.getY() + 1.0f, r.getWidth() - 6.0f, 2.0f, 1.0f);
        if (now) { g.setColour (noteAccent); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.6f); }
        // its name, over how long it lasts
        g.setFont (font (10.0f, Weight::SemiBold));
        g.setColour (now ? juce::Colours::white : n8);
        g.drawFittedText (h.names[(size_t) i], (int) r.getX() + 3, (int) r.getY() + 3, (int) r.getWidth() - 6, 11, juce::Justification::centred, 1, 0.7f);
        const int bars = h.seq[(size_t) i].bars;
        g.setFont (font (8.0f));
        g.setColour (n7);
        g.drawText (bars == 1 ? juce::String ("1 bar") : juce::String (bars) + " bars", (int) r.getX(), (int) r.getY() + 14, (int) r.getWidth(), 9, juce::Justification::centred, false);
        if (h.seq[(size_t) i].locked)        // a locked chord wears a small padlock-coloured dot
        {
            g.setColour (noteAccent);
            g.fillEllipse (r.getRight() - 7.5f, r.getBottom() - 7.5f, 4.5f, 4.5f);
        }
    }

    // the keyboard: the notes of the chord playing now (the first chord while the song is stopped)
    const auto notes = litNotes();
    const int lowC = 48;
    auto lit = [&notes, lowC] (int note)
    {
        for (int nn : notes) { while (nn < lowC) nn += 12; while (nn >= lowC + 24) nn -= 12; if (nn == note) return true; }
        return false;
    };
    const int idx = shown >= 0 ? shown : 0;
    drawKeyboard (g, kb.toFloat(), lowC, 14, lit, shown >= 0 || idx >= (int) h.seq.size() ? noteAccent : degreeColour (h.seq[(size_t) idx].deg));

    g.setFont (font (8.0f, Weight::Medium, 0.2f));        // (as the knobs are labelled)
    g.setColour (n8);
    g.drawText ("MOOD", lay::chordColX + 1, 0, 80, 11, juce::Justification::centredLeft, false);
    g.drawText ("SONG", lay::chordColX + 1, 88, 80, 11, juce::Justification::centredLeft, false);
}

void ChordView::mouseDown (const juce::MouseEvent& e)
{
    const int n = juce::jmin (32, (int) proc.rack.harmony.seq.size());
    for (int i = 0; i < n; ++i) if (tile (i).contains (e.getPosition())) { chordMenu (i); return; }
}

// Click a chord: change its colour, its bass note, which chord of the key it is, how long it lasts, or lock it
void ChordView::chordMenu (int index)
{
    auto& rack = proc.rack;
    if (! rack.validChord (index)) return;
    const auto t = rack.theory();
    const auto chord = rack.harmony.seq[(size_t) index];
    juce::PopupMenu menu, colour, bass, which, len;
    menu.addSectionHeader ("Chord " + juce::String (index + 1) + ": " + juce::String (rack.harmony.names[(size_t) index]));
    const auto colours = t.sortedColours (chord.deg);
    for (size_t k = 0; k < colours.size(); ++k)
    {
        auto c = chord; c.t = colours[k].s; c.inv = 0;
        colour.addItem (100 + (int) k, juce::String (t.chordName (c)) + "   " + n2c::colourLevelName (colours[k].lv), true, colours[k].s == t.colourOf (chord).s);
    }
    const int notes = (int) t.colourOf (chord).iv.size();
    for (int inv = 0; inv < juce::jmin (3, notes); ++inv)
    {
        auto c = chord; c.inv = inv;
        bass.addItem (200 + inv, inv == 0 ? juce::String ("Root: ") + t.chordName (c) : juce::String (t.chordName (c)), true, chord.inv == inv);
    }
    for (int d = 0; d < 7; ++d)
    {
        n2c::Chord c; c.deg = d; c.t = t.colourOf (c).s;
        which.addItem (300 + d, t.chordName (c), true, d == chord.deg);
    }
    for (int b : { 1, 2, 4, 8 }) len.addItem (400 + b, b == 1 ? "1 bar" : juce::String (b) + " bars", true, chord.bars == b);
    menu.addSubMenu ("Colour", colour);
    menu.addSubMenu ("Bass note", bass);
    menu.addSubMenu ("Change to", which);
    menu.addSubMenu ("Length", len);
    menu.addSeparator();
    menu.addItem (1, "Lock (keep through Generate)", true, chord.locked);
    juce::Component::SafePointer<ChordView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (tile (index))), [safe, index, colours] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& rk = safe->proc.rack;
        safe->proc.history.beforeChords();
        if (r == 1) rk.toggleChordLock (index);
        else if (r >= 400) rk.setChordBars (index, r - 400);
        else if (r >= 300) rk.setChordDegree (index, r - 300);
        else if (r >= 200) rk.setChordBass (index, r - 200);
        else if (r >= 100 && r - 100 < (int) colours.size()) rk.setChordColour (index, colours[(size_t) (r - 100)].s);
        safe->proc.chordsVersion.fetch_add (1);
        safe->repaint();
    });
}

void ChordView::tick()
{
    const int v = proc.chordsVersion.load(), s = module.playStep.load();
    if (v != seenVersion) { seenVersion = v; refreshLabels(); repaint(); }
    if (s != shown) { shown = s; repaint(); }
}

//==============================================================================
NotesScreen::NotesScreen (Nebula2ModulationProcessor& p, Board& b, Module& m) : proc (p), board (b), module (m)
{
    setInterceptsMouseClicks (false, false);        // it only shows things: the panel under it can be picked up by it
    seen = signature();
}

// Everything the picture depends on, so it is only redrawn when one of them changes
uint64_t NotesScreen::signature() const
{
    uint64_t h = (uint64_t) (module.playStep.load() + 2);
    for (auto& p : module.params) h = h * 1000003u + (uint64_t) std::lround (p.load() * 1000.0f + 100000.0f);
    if (auto* k = dynamic_cast<KeysIn*> (&module)) h = h * 31u + k->heldLo.load() * 7u + k->heldHi.load();
    int cabled = 0;
    for (auto& c : board.cables) if (c.dst == module.uid) cabled += 1 << juce::jmin (20, c.dstPort);
    return h * 31u + (uint64_t) cabled + (uint64_t) board.colour * 977u;
}

void NotesScreen::tick()
{
    const auto now = signature();
    if (now != seen) { seen = now; repaint(); }
}

void NotesScreen::paint (juce::Graphics& g)
{
    const std::string type = module.info->type;
    const auto col = boardColour (board.colour);
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff07080b));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    const auto inner = r.reduced (6.0f);
    auto corner = [&g, inner] (const juce::String& text, bool right)
    {
        g.setFont (smallCaps());
        g.setColour (n7);
        g.drawText (text.toUpperCase(), inner.toNearestInt(), right ? juce::Justification::bottomRight : juce::Justification::bottomLeft, false);
    };
    auto P = [this] (const char* id) { const int k = module.paramIndex (id); return k >= 0 ? module.P (k) : 0.0f; };
    auto text = [this] (const char* id) { const int k = module.paramIndex (id); return k >= 0 ? juce::String (paramText (module.info->params[(size_t) k], module.P (k))) : juce::String(); };

    if (auto* keys = dynamic_cast<KeysIn*> (&module))
    {
        // Two octaves of keys. They move to wherever the keys being held are.
        int lowest = -1;
        for (int n = 0; n < 128 && lowest < 0; ++n) if (keys->isHeld (n)) lowest = n;
        bool inView = false;
        for (int n = lowC; n < lowC + 24; ++n) inView = inView || keys->isHeld (n);
        if (lowest >= 0 && ! inView) lowC = juce::jlimit (0, 96, (lowest / 12) * 12);
        drawKeyboard (g, inner, lowC, 14, [keys] (int n) { return keys->isHeld (n); }, noteAccent);
        return;
    }
    if (type == "arp")
    {
        // The order the notes play in, low at the bottom: one round of the pattern over the notes plugged in
        int nn = 0;
        for (auto& c : board.cables) if (c.dst == module.uid && c.dstPort < 4) ++nn;
        if (nn == 0) nn = 4;
        const int pattern = (int) std::lround (P ("pattern")), range = juce::jlimit (1, 4, (int) std::lround (P ("range")));
        const int count = nn * range;
        const int steps = pattern == 2 ? juce::jmax (1, 2 * count - 2) : count;
        const auto plot = inner.withTrimmedBottom (14.0f).reduced (2.0f, 4.0f);
        const float sw = plot.getWidth() / (float) steps;
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (int s = nn; s < steps; s += nn) g.fillRect (plot.getX() + (float) s * sw - 0.5f, plot.getY(), 1.0f, plot.getHeight());
        const int playing = module.playStep.load();
        for (int s = 0; s < steps; ++s)
        {
            const int idx = Arpeggiator::pick (pattern, s, count, module.uid);
            const float y = plot.getBottom() - 3.0f - (plot.getHeight() - 3.0f) * (count > 1 ? (float) idx / (float) (count - 1) : 0.5f);
            g.setColour (idx == playing ? noteAccent : col);
            g.fillRoundedRectangle (plot.getX() + (float) s * sw + 1.0f, y, juce::jmax (3.0f, sw - 2.0f), 3.0f, 1.5f);
        }
        corner (text ("pattern"), false);
        corner (text ("rate"), true);
        return;
    }
    if (type == "euclid")
    {
        // The steps round a ring, starting at the top: hits lit, accents ringed, the step playing marked
        const int steps = juce::jlimit (1, 16, (int) std::lround (P ("steps"))), hits = juce::jlimit (0, steps, (int) std::lround (P ("hits")));
        const int turn = (int) std::lround (P ("turn")), acc = (int) std::lround (P ("accent"));
        const auto c = r.getCentre();
        const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 14.0f;
        const int playing = module.playStep.load();
        for (int k = 0; k < steps; ++k)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) k / (float) steps;
            const juce::Point<float> at (c.x + radius * std::sin (a), c.y - radius * std::cos (a));
            const bool hit = Euclid::hitAt (k, steps, hits, turn);
            if (hit)
            {
                g.setColour (col);
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (at));
                if (Euclid::accentAt (k, acc)) g.drawEllipse (juce::Rectangle<float> (10.5f, 10.5f).withCentre (at), 1.2f);
            }
            else { g.setColour (n6); g.fillEllipse (juce::Rectangle<float> (2.5f, 2.5f).withCentre (at)); }
            if (k == playing) { g.setColour (noteAccent); g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (at), 1.6f); }
        }
        g.setFont (font (11.0f, Weight::Medium));
        g.setColour (n8);
        g.drawText (juce::String (hits) + " / " + juce::String (steps), r.toNearestInt(), juce::Justification::centred, false);
        return;
    }
    // Ratchet and Swing: the hits going in, evenly spaced, and what comes out
    const bool ratchet = type == "ratchet";
    const auto plot = inner.withTrimmedBottom (12.0f);
    const float yIn = plot.getY() + plot.getHeight() * 0.3f, yOut = plot.getY() + plot.getHeight() * 0.7f;
    g.setFont (smallCaps());
    g.setColour (n7);
    g.drawText ("IN", (int) plot.getX() + 2, (int) yIn - 6, 30, 12, juce::Justification::centredLeft, false);
    g.drawText ("OUT", (int) plot.getX() + 2, (int) yOut - 6, 30, 12, juce::Justification::centredLeft, false);
    const float x0 = plot.getX() + 38.0f, slot = (plot.getRight() - 6.0f - x0) / 8.0f;
    const int count = ratchet ? 1 + juce::jlimit (0, 3, (int) std::lround (P ("count"))) : 1;
    const float fade = ratchet ? P ("fade") : 0.0f, swing = ratchet ? 0.0f : P ("amount");
    for (int k = 0; k < 8; ++k)
    {
        const float x = x0 + (float) k * slot + 3.0f;
        g.setColour (col);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x, yIn }));
        for (int b = 0; b < count; ++b)
        {
            // a burst is a tight group inside its step; an off-beat hit is held back by half the swing
            const float ox = ratchet ? (float) b * slot * 0.8f / (float) juce::jmax (3, count) : (k % 2 == 1 ? swing * 0.5f * slot : 0.0f);
            g.setColour (col.withAlpha (1.0f - 0.8f * fade * (float) b / (float) juce::jmax (1, count)));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x + ox, yOut }));
        }
    }
    corner (ratchet ? text ("count") + (count == 1 ? " hit" : " hits") : text ("amount"), true);
}

//==============================================================================
ScopeView::ScopeView (BoardView& o, Scope& m) : owner (o), module (m) { setSize (lay::seqW, lay::scopeSeqH); }

void ScopeView::paint (juce::Graphics& g)
{
    const auto col = boardColour (owner.board != nullptr ? owner.board->colour : 0);
    const auto well = juce::Rectangle<float> (0.0f, 22.0f, (float) getWidth(), (float) getHeight() - 26.0f);
    sunken (g, well, radiusControl);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (well.getX() + 4.0f, well.getCentreY(), well.getWidth() - 8.0f, 1.0f);

    // The note, how far off it is, and the frequency
    const float hz = module.hz.load();
    int cents = 0;
    const auto name = noteText (hz, &cents);
    g.setFont (font (13.0f, Weight::SemiBold));
    g.setColour (name.empty() ? n6 : std::abs (cents) <= 5 ? bandGreen : n8);
    g.drawText (name.empty() ? juce::String ("no note") : juce::String (name), 0, 0, 70, 20, juce::Justification::centredLeft, false);
    if (! name.empty())
    {
        g.setFont (font (11.0f));
        g.setColour (n7);
        g.drawText ((cents == 0 ? juce::String ("in tune") : minus (juce::String (cents > 0 ? "+" : "") + juce::String (cents) + " cents")) + "     " + juce::String (hz, 1) + " Hz",
                    70, 0, getWidth() - 70, 20, juce::Justification::centredLeft, false);
    }

    // The wave, started from an upward crossing so it holds still instead of sliding about
    const int n = Scope::size, head = module.head.load();
    int start = (head + 1) % n;
    for (int k = 1; k < n / 2; ++k)
    {
        const float a = module.ring[(size_t) ((head + k) % n)], b = module.ring[(size_t) ((head + k + 1) % n)];
        if (a <= 0.0f && b > 0.0f) { start = (head + k + 1) % n; break; }
    }
    const int shown = n / 2;
    juce::Path p;
    for (int k = 0; k < shown; ++k)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, module.ring[(size_t) ((start + k) % n)]);
        const float x = well.getX() + 4.0f + (well.getWidth() - 8.0f) * (float) k / (float) (shown - 1);
        const float y = well.getCentreY() - v * (well.getHeight() * 0.5f - 5.0f);
        if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col.withAlpha (0.95f));
    g.strokePath (p, juce::PathStrokeType (1.4f));
}

void ScopeView::tick()
{
    const bool quiet = module.level.load() < 0.0005f;
    if (! quiet || ! wasQuiet) repaint();        // redraw while there is sound, and once more as it stops
    wasQuiet = quiet;
}

//==============================================================================
HostView::HostView (BoardView& o, HostedModule& m) : owner (o), module (m)
{
    addAndMakeVisible (load); addAndMakeVisible (open); addAndMakeVisible (knobs);
    load.onClick = [this] { showLoadMenu(); };
    knobs.onClick = [this] { showKnobMenu(); };
    addChildComponent (presets);
    presets.onClick = [this] { showPresetMenu(); };
    open.onClick = [this] { if (module.hasPlugin() && owner.onOpenPlugin) owner.onOpenPlugin (module); };
    setSize (lay::hostW, lay::hostSeqH);
    refresh();
}

void HostView::refresh()
{
    open.setEnabled (module.hasPlugin()); knobs.setEnabled (module.hasPlugin() && module.numControls() > 0);
    presets.setVisible (module.hasPlugin() && module.presetNames().size() > 1);     // only for plugins that offer a list
    repaint();
}
void HostView::resized() { load.setBounds (0, 30, 72, 24); open.setBounds (78, 30, 72, 24); knobs.setBounds (156, 30, 72, 24); presets.setBounds (234, 30, 78, 24); }

// Presets: the plugin's own list of presets
void HostView::showPresetMenu()
{
    const auto names = module.presetNames();
    if (names.size() < 2) return;
    juce::PopupMenu menu;
    menu.addSectionHeader (module.pluginName() + " presets");
    const int now = module.currentPreset(), page = 40;
    if (names.size() <= page) for (int i = 0; i < names.size(); ++i) menu.addItem (1 + i, names[i], true, i == now);
    else
        for (int from = 0; from < names.size(); from += page)
        {
            juce::PopupMenu part;
            const int to = juce::jmin (names.size(), from + page);
            for (int i = from; i < to; ++i) part.addItem (1 + i, names[i], true, i == now);
            menu.addSubMenu (names[from] + "  ...  " + names[to - 1], part, true, nullptr, now >= from && now < to);
        }
    juce::Component::SafePointer<HostView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presets), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        safe->module.choosePreset (r - 1);
        safe->repaint();
    });
}

void HostView::paint (juce::Graphics& g)
{
    const bool failed = module.status.isNotEmpty();
    g.setFont (font (13.0f, Weight::SemiBold));
    g.setColour (module.hasPlugin() ? juce::Colours::white : n7);
    g.drawText (module.hasPlugin() ? module.pluginName() : "Empty", 0, 2, 190, 18, juce::Justification::centredLeft, true);
    g.setFont (helpFont());
    // What the slot's four knobs are pointed at
    juce::String pointed;
    for (int k = 0; k < HostedModule::kKnobs; ++k)
        if (module.knobTarget (k) >= 0) pointed << (pointed.isEmpty() ? "" : "   ") << (k + 1) << ": " << module.controlName (module.knobTarget (k));
    g.setColour (n8);
    g.drawText (pointed, 196, 2, getWidth() - 196, 18, juce::Justification::centredRight, true);
    g.setColour (failed ? bandOrange : n7);
    g.drawFittedText (failed ? module.status : module.hasPlugin() ? "Open shows its own window. Knobs points Knob 1 to 4 at its controls" : "Load a plugin, or drop a .vst3 file on the board",
                      presets.isVisible() ? 320 : 236, 30, getWidth() - (presets.isVisible() ? 320 : 236), 24, juce::Justification::centredLeft, 2, 0.8f);
}

// Knobs: point each of the slot's four knobs at one of the plugin's own controls
void HostView::showKnobMenu()
{
    if (! module.hasPlugin()) return;
    const int count = module.numControls(), touched = module.lastTouched();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Point a knob at one of " + module.pluginName() + "'s controls");
    for (int k = 0; k < HostedModule::kKnobs; ++k)
    {
        const int base = (k + 1) * 100000, now = module.knobTarget (k);
        juce::PopupMenu sub;
        sub.addItem (base, "Nothing", true, now < 0);
        if (touched >= 0) sub.addItem (base + 1 + touched, "The control I last moved in its window: " + module.controlName (touched));
        sub.addSeparator();
        // Big plugins have hundreds of controls: list them in pages
        const int page = 40;
        if (count <= page) for (int i = 0; i < count; ++i) sub.addItem (base + 1 + i, module.controlName (i), true, now == i);
        else
            for (int from = 0; from < count && from < 4000; from += page)
            {
                juce::PopupMenu part;
                const int to = juce::jmin (count, from + page);
                for (int i = from; i < to; ++i) part.addItem (base + 1 + i, module.controlName (i), true, now == i);
                sub.addSubMenu (module.controlName (from) + "  ...  " + module.controlName (to - 1), part, true, nullptr, now >= from && now < to);
            }
        menu.addSubMenu ("Knob " + juce::String (k + 1) + ": " + (now >= 0 ? module.controlName (now) : juce::String ("nothing")), sub);
    }
    juce::Component::SafePointer<HostView> safe (this);
    BoardView* board = &owner;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&knobs), [safe, board] (int r)
    {
        if (safe == nullptr || r < 100000) return;
        board->unitWillChange (safe->module.uid, false);     // Undo puts the knob and what it pointed at back
        safe->module.assignKnob (r / 100000 - 1, r % 100000 - 1);
        board->repaintAll();
    });
}

// Just before the slot's plugin changes or goes: Undo brings back the one it held, with its settings
void HostView::willChange() { owner.unitWillChange (module.uid); }

void HostView::loadFile (const juce::String& path)
{
    const auto found = pluginsInFile (path);
    if (found.empty()) module.status = "That file isn't a VST3 plugin this can load";
    else { willChange(); module.load (found[0]); }
    refresh();
}

void HostView::showLoadMenu()
{
    juce::PopupMenu menu;
    const auto recent = recentPlugins();
    if (! recent.empty()) menu.addSectionHeader ("Loaded before");
    int id = 1;
    for (auto& d : recent) menu.addItem (id++, d.name + (d.isInstrument ? "" : "  (effect)"));
    if (! recent.empty()) menu.addSeparator();
    // Everything in the computer's VST3 folders, by name
    const auto installed = installedPlugins();
    if (! installed.empty())
    {
        juce::PopupMenu all;
        int k = 2000;
        for (auto& p : installed) all.addItem (k++, p.first);
        menu.addSubMenu ("All installed plugins (" + juce::String ((int) installed.size()) + ")", all);
    }
    menu.addItem (1002, "Look for plugins again");
    menu.addItem (1000, "Browse for a .vst3...");
    if (module.hasPlugin()) menu.addItem (1001, "Empty this slot");
    juce::Component::SafePointer<HostView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&load), [safe, recent, installed] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (r == 1001) { safe->willChange(); safe->module.unload(); safe->refresh(); return; }
        if (r == 1002) { installedPlugins (true); return; }
        if (r >= 2000) { if (r - 2000 < (int) installed.size()) safe->loadFile (installed[(size_t) (r - 2000)].second); return; }
        if (r < 1000) { safe->willChange(); safe->module.load (recent[(size_t) (r - 1)]); safe->refresh(); return; }
        juce::File start;
       #if JUCE_WINDOWS
        start = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Common Files").getChildFile ("VST3");
       #elif JUCE_MAC
        start = juce::File ("~/Library/Audio/Plug-Ins/VST3");
       #else
        start = juce::File ("~/.vst3");
       #endif
        safe->chooser = std::make_unique<juce::FileChooser> ("Choose a VST3 plugin", start, "*.vst3");
        safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
                                    [safe] (const juce::FileChooser& fc)
                                    {
                                        if (safe != nullptr && fc.getResult() != juce::File()) safe->loadFile (fc.getResult().getFullPathName());
                                    });
    });
}

//==============================================================================
SampleView::SampleView (BoardView& o, SampleModule& m) : owner (o), module (m)
{
    addAndMakeVisible (load);
    load.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), SampleModule::wildcard());
        juce::Component::SafePointer<SampleView> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult() != juce::File()) { safe->module.load (fc.getResult()); safe->repaint(); }
        });
    };
    setSize (lay::seqW, lay::sampleSeqH);
}

void SampleView::resized() { load.setBounds (0, 0, 58, 20); }

bool SampleView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (SampleModule::canRead (f)) return true;
    return false;
}

void SampleView::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files) if (SampleModule::canRead (f)) { module.load (juce::File (f)); repaint(); return; }
}

void SampleView::paint (juce::Graphics& g)
{
    const auto col = boardColour (owner.board != nullptr ? owner.board->colour : 0);
    const bool failed = module.status.isNotEmpty();
    g.setFont (font (11.0f, Weight::Medium));
    g.setColour (failed ? bandOrange : module.hasSample() ? n8 : n7);
    const juce::String text = failed ? module.status
                            : module.hasSample() ? module.fileName() + "   " + juce::String (module.seconds(), 2) + " s"
                            : juce::String ("Load an audio file, or drop one here");
    g.drawText (text, 66, 0, getWidth() - 66, 20, juce::Justification::centredLeft, true);

    // The shape of the sound, with the part that plays lit and the rest dimmed
    const auto well = juce::Rectangle<float> (0.0f, 26.0f, (float) getWidth(), (float) getHeight() - 30.0f);
    sunken (g, well, radiusControl);
    if (module.peaks.empty()) return;
    const float start = module.P (0), end = start + module.P (1) * (1.0f - start);
    const float colW = well.getWidth() / (float) module.peaks.size(), mid = well.getCentreY();
    for (size_t k = 0; k < module.peaks.size(); ++k)
    {
        const float at = ((float) k + 0.5f) / (float) module.peaks.size();
        const float h = juce::jmax (1.0f, juce::jmin (1.0f, module.peaks[k]) * (well.getHeight() - 8.0f));
        g.setColour (at >= start && at <= end ? col.withAlpha (0.9f) : n5);
        g.fillRect (well.getX() + (float) k * colW, mid - h * 0.5f, juce::jmax (1.0f, colW - 0.5f), h);
    }
    if (shownHead >= 0.0f)
    {
        g.setColour (noteAccent);
        g.fillRect (well.getX() + shownHead * well.getWidth(), well.getY() + 2.0f, 1.5f, well.getHeight() - 4.0f);
    }
}

void SampleView::tick()
{
    const float h = module.playhead.load();
    if (std::abs (h - shownHead) > 0.002f) { shownHead = h; repaint(); }
}

//==============================================================================
// A hosted plugin's own window, floating beside ours so the board stays in view and usable
struct PluginWindow : juce::DocumentWindow
{
    PluginWindow (const juce::String& name, std::function<void()> whenClosed)
        : DocumentWindow (name, n2m::look::surface, DocumentWindow::closeButton), closed (std::move (whenClosed))
    {
        setUsingNativeTitleBar (true);
        setAlwaysOnTop (true);
        setResizable (false, false);
    }
    void closeButtonPressed() override { if (closed) closed(); }
    std::function<void()> closed;
};

HostOverlay::HostOverlay()
{
    addAndMakeVisible (back);
    addAndMakeVisible (viewport);
    back.onClick = [this] { close(); };
    addClosingListener (this, [this] (HostedModule* m) { if (m == showing) close(); });
    setVisible (false);
}

HostOverlay::~HostOverlay() { removeClosingListener (this); close(); }

void HostOverlay::open (HostedModule& m, const juce::String& tabName)
{
    close();
    auto* inst = m.instance();
    if (inst == nullptr) return;
    pluginEditor.reset (inst->createEditorIfNeeded());
    if (pluginEditor == nullptr) pluginEditor = std::make_unique<juce::GenericAudioProcessorEditor> (*inst);   // a plugin with no window of its own
    showing = &m;
    title = m.pluginName() + "   on " + tabName;
    if (! juce::Desktop::getInstance().getDisplays().displays.isEmpty())
    {
        // Its own window, on top, next to ours: the board stays where it is. Closing that window is all "Back" was.
        juce::Component::SafePointer<HostOverlay> safe (this);
        window = std::make_unique<PluginWindow> (title, [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->close(); }); });
        window->setContentNonOwned (pluginEditor.get(), true);
        if (auto* top = getTopLevelComponent()) window->centreAroundComponent (top, window->getWidth(), window->getHeight());
        window->setVisible (true);
        window->toFront (true);
        return;
    }
    // With no screen to open a window on (the tests), it is shown over the board instead
    back.setButtonText ("Back to " + tabName);
    viewport.setViewedComponent (pluginEditor.get(), false);
    setVisible (true);
    toFront (true);
    resized();
}

void HostOverlay::close()
{
    viewport.setViewedComponent (nullptr, false);
    if (window != nullptr) { window->clearContentComponent(); window.reset(); }
    pluginEditor.reset();          // the window goes before the plugin can
    showing = nullptr;
    setVisible (false);
}

void HostOverlay::resized()
{
    back.setBounds (space4, (topbarH - 28) / 2, 170, 28);
    auto area = getLocalBounds().withTrimmedTop (topbarH);
    if (pluginEditor != nullptr)
    {
        // Centred when it fits; scrollable when the plugin's window is bigger than ours
        const int w = juce::jmin (area.getWidth(), pluginEditor->getWidth() + (pluginEditor->getHeight() > area.getHeight() ? viewport.getScrollBarThickness() : 0));
        const int h = juce::jmin (area.getHeight(), pluginEditor->getHeight() + (pluginEditor->getWidth() > area.getWidth() ? viewport.getScrollBarThickness() : 0));
        viewport.setBounds (area.withSizeKeepingCentre (w, h));
    }
    else viewport.setBounds (area);
}

void HostOverlay::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (windowTop, 0, 0, windowBottom, 0, (float) getHeight(), false));
    g.fillAll();
    g.setFont (titleFont());
    g.setColour (n8);
    g.drawText (title, 200, 0, getWidth() - 216, topbarH, juce::Justification::centredLeft, true);
    dividerH (g, 0, getWidth(), topbarH - 2);
}

//==============================================================================
ModuleComp::ModuleComp (BoardView& o, Nebula2ModulationProcessor& p, Board& b, Module& m) : module (m), owner (o), proc (p), board (b)
{
    const auto& info = *m.info;
    setBounds ((int) m.x, (int) m.y, lay::width (info), lay::height (info));
    if (const auto wg = lay::geo (info); wg.wide)
    {
        // A wide panel (Notes and rhythm): the screen on the left, each knob or drop-down in its own place beside it
        if (info.seq == Seq::Chord) { chords = std::make_unique<ChordView> (p, m); addAndMakeVisible (*chords); chords->setTopLeftPosition (wg.screen.x, wg.screen.y); }
        else if (info.seq == Seq::Drum || info.seq == Seq::Note) { seq = std::make_unique<SeqView> (p, b, m); addAndMakeVisible (*seq); seq->setTopLeftPosition (wg.screen.x, wg.screen.y); }
        else if (! wg.screen.empty()) { screen = std::make_unique<NotesScreen> (p, b, m); addAndMakeVisible (*screen); screen->setBounds (wg.screen.x, wg.screen.y, wg.screen.w, wg.screen.h); }
        for (int i = 0; i < (int) info.params.size() && i < lay::kMaxCells; ++i)
        {
            const auto& box = wg.cellBox[i];
            if (box.empty()) continue;
            auto* c = cells.add (new ParamCell (p, m, i));
            c->dropdown = ! info.params[(size_t) i].choices.empty();
            addAndMakeVisible (c);
            c->setBounds (box.x, box.y, box.w, box.h);
        }
        setAlpha (m.live ? 1.0f : 0.45f);
        return;
    }
    if (info.seq == Seq::Scope)
    {
        if (auto* sm = dynamic_cast<Scope*> (&m))
        {
            scope = std::make_unique<ScopeView> (o, *sm);
            addAndMakeVisible (*scope);
            scope->setTopLeftPosition ((getWidth() - lay::seqW) / 2, lay::header);
        }
    }
    else if (info.seq == Seq::Sample)
    {
        if (auto* sm = dynamic_cast<SampleModule*> (&m))
        {
            sample = std::make_unique<SampleView> (o, *sm);
            addAndMakeVisible (*sample);
            sample->setTopLeftPosition ((getWidth() - lay::seqW) / 2, lay::header);
        }
    }
    else if (info.seq == Seq::Host)
    {
        if (auto* hm = dynamic_cast<HostedModule*> (&m))
        {
            host = std::make_unique<HostView> (o, *hm);
            addAndMakeVisible (*host);
            host->setTopLeftPosition ((getWidth() - lay::hostW) / 2, lay::header);
        }
    }
    else if (info.seq == Seq::Chord)
    {
        chords = std::make_unique<ChordView> (p, m);
        addAndMakeVisible (*chords);
        chords->setTopLeftPosition ((getWidth() - lay::chordW) / 2, lay::header);
    }
    else if (info.seq != Seq::None)
    {
        seq = std::make_unique<SeqView> (p, b, m);
        addAndMakeVisible (*seq);
        seq->setTopLeftPosition ((getWidth() - lay::seqW) / 2, lay::header);
    }
    const auto g = lay::geo (info);
    for (int i = 0; i < (int) info.params.size(); ++i)
    {
        auto* c = cells.add (new ParamCell (p, m, i));
        addAndMakeVisible (c);
        // a last row with fewer knobs is centred
        const int row = i / g.cols, inRow = juce::jmin (g.cols, (int) info.params.size() - row * g.cols);
        const int rowX = g.knobsX + (g.cols - inRow) * g.cellW / 2;
        c->setBounds (rowX + (i % g.cols) * g.cellW, g.knobsY + row * g.cellH, g.cellW, g.cellH);
    }
    setAlpha (m.live ? 1.0f : 0.45f);
}

// Jacks sit in a strip along the bottom. When they all fit in one row the inputs are on the left and the
// outputs on the right; otherwise the inputs have their own rows above the outputs.
juce::Point<int> ModuleComp::jackCentre (bool isOut, int port) const
{
    const auto g = lay::geo (*module.info);
    const int nIn = (int) module.info->ins.size(), nOut = (int) module.info->outs.size();
    const int inner = getWidth() - 2 * lay::pad;
    const int slot = juce::jmin (lay::jackW + 4, inner / juce::jmax (1, g.jackCols));
    const int left = lay::pad + 2, right = getWidth() - lay::pad - 2;
    int row = 0, col = port, count = isOut ? nOut : nIn;
    if (g.jackRows > 1)
    {
        const int inRows = (nIn + g.jackCols - 1) / g.jackCols;
        row = (isOut ? inRows : 0) + port / g.jackCols;
        col = port % g.jackCols;
        count = juce::jmin (g.jackCols, (isOut ? nOut : nIn) - (port / g.jackCols) * g.jackCols);
    }
    const int y = g.jacksY + row * lay::jackH + lay::jackH - 13;
    if (isOut) return { right - (count - col) * slot + slot / 2, y };
    return { left + col * slot + slot / 2, y };
}

static juce::String linkText (const Module& m)
{
    if (m.patternLink <= 0) return "LINK";
    return "LINK " + groupLetter (m.patternLink) + (m.patternShare == 1 ? ": RHYTHM" : m.patternShare == 2 ? ": NOTES" : m.patternShare == 4 ? ": FOLLOWS" : ": RHYTHM + NOTES");
}

juce::Rectangle<int> ModuleComp::linkArea() const
{
    if (module.info->seq != Seq::Drum && module.info->seq != Seq::Note) return {};
    const int nameW = juce::GlyphArrangement::getStringWidthInt (font (10.0f, Weight::SemiBold, 0.5f), juce::String (module.info->name).toUpperCase());
    const int w = juce::GlyphArrangement::getStringWidthInt (font (9.0f, Weight::SemiBold, 0.4f), linkText (module)) + 14;
    // On a wide panel the name is centred, so the labels sit at the bottom left, where a sequencer has no inputs
    if (const auto g = lay::geo (*module.info); g.wide) return { g.midi.right() + 8, g.midi.y + 1, juce::jmin (w, lay::linkRoom), g.midi.h - 2 };
    return { 20 + nameW + 10, 4, w, 15 };
}

juce::Rectangle<int> ModuleComp::expandArea() const
{
    if (! BigPanel::has (*module.info)) return {};
    return { getWidth() - 15 - 48, 3, 48, 15 };       // top right, clear of the corner screw
}

juce::Rectangle<int> ModuleComp::midiArea() const
{
    if (! canExportMidi (module) && std::string (module.info->type) != "output") return {};
    // The Output is a narrow panel: its label sits under the name, across the panel
    if (std::string (module.info->type) == "output") return { lay::pad, lay::header + 2, getWidth() - 2 * lay::pad, 15 };
    if (const auto g = lay::geo (*module.info); g.wide) return { g.midi.x, g.midi.y, g.midi.w, g.midi.h };
    const int nameW = juce::GlyphArrangement::getStringWidthInt (font (10.0f, Weight::SemiBold, 0.5f), juce::String (module.info->name).toUpperCase());
    const auto la = linkArea();
    const int x = la.isEmpty() ? 20 + nameW + 10 : la.getRight() + 6;
    return { x, 4, 62, 15 };
}

// A sequencer's Link label: put its pattern in a group. Sequencers in the same group, on any tab, share
// their rhythm, their notes, or both.
void ModuleComp::linkMenu()
{
    juce::PopupMenu m;
    // A link is just a letter. Give two sequencers the same letter, on any tabs, and they are linked.
    m.addSectionHeader ("Link: pick the same letter on each sequencer you want joined");
    m.addItem (1, "Not linked", true, module.patternLink == 0);
    for (int g = 1; g <= 4; ++g)
    {
        // name the tabs already using this letter
        juce::StringArray who;
        for (auto& b : proc.rack.boards)
            for (auto& mm : b->modules)
                if (mm->patternLink == g && mm.get() != &module) who.addIfNotAlreadyThere (b->name);
        m.addItem (g + 1, "Link " + groupLetter (g) + (who.isEmpty() ? "   (nothing on it yet)" : "   with " + who.joinIntoString (", ")), true, module.patternLink == g);
    }
    m.addSeparator();
    m.addSectionHeader ("How it is linked: follow");
    if (module.info->seq == Seq::Note)
        m.addItem (104, "Keep my pattern, move to match the others", true, module.patternShare == 4);
    m.addSectionHeader ("Or share the same pattern");
    m.addItem (103, "Same rhythm and notes", true, module.patternShare == 3);
    m.addItem (101, "Same rhythm only", true, module.patternShare == 1);
    m.addItem (102, "Same notes only", true, module.patternShare == 2);
    juce::Component::SafePointer<ModuleComp> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (linkArea())), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& mod = safe->module;
        safe->owner.willChange();
        if (r > 100) safe->proc.rack.linkPattern (mod, mod.patternLink, r - 100);
        else
        {
            // A note sequencer joining a group that already has members follows them, keeping its own pattern,
            // unless it was already set to share
            const bool joining = mod.patternLink == 0 && r - 1 > 0 && safe->proc.rack.membersOf (r - 1, true) > 0;
            safe->proc.rack.linkPattern (mod, r - 1, joining && mod.info->seq == Seq::Note ? 4 : mod.patternShare);
        }
        safe->owner.repaintAll();
    });
}

bool ModuleComp::jackAt (juce::Point<int> p, bool& isOut, int& port) const
{
    for (int pass = 0; pass < 2; ++pass)
    {
        const int n = (int) (pass == 0 ? module.info->ins.size() : module.info->outs.size());
        for (int i = 0; i < n; ++i)
            if (jackCentre (pass == 1, i).getDistanceFrom (p) <= 13) { isOut = pass == 1; port = i; return true; }
    }
    return false;
}

// The colour a kind of unit wears on its panel, so a rack reads by function at a glance
static juce::Colour shelfColour (const Info& info)
{
    const std::string s = info.shelf;
    if (s == "Sources") return bandMint;
    if (s == "Shapers") return bandYellow;
    if (s == "Filters") return bandBlue;
    if (s == "EQ") return bandGreen;
    if (s == "Dynamics" || s == "Distortion") return bandOrange;
    if (s == "Reverb" || s == "Delay and echo") return bandPurple;
    if (s == "Movement" || s == "Pitch and width") return bandMagenta;
    if (s == "Movers") return bandPink;
    if (s == "Notes and rhythm") return accent;
    return n7;
}

// A rack panel: a metal faceplate with a screw in each corner, the name printed across the top, a screen for
// units that have one, and a darker strip along the bottom holding the jacks.
void ModuleComp::paint (juce::Graphics& g)
{
    const auto& info = *module.info;
    const auto geo = lay::geo (info);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto col = boardColour (board.colour);
    const auto tone = shelfColour (info);
    const std::string shelf = info.shelf;
    const bool black = shelf == "Filters" || shelf == "Notes and rhythm" || shelf == "Dynamics" || std::string (info.type) == "output";

    // the plate
    juce::Path plate; plate.addRoundedRectangle (r, 4.0f);
    shadow (g, plate, juce::Colours::black.withAlpha (0.5f), 8.0f, 0.0f, 3.0f);
    g.setGradientFill (juce::ColourGradient (black ? juce::Colour (0xff17181d) : juce::Colour (0xff2b2e38), 0.0f, r.getY(), black ? juce::Colour (0xff0e0f13) : juce::Colour (0xff1d1f27), 0.0f, r.getBottom(), false));
    g.fillPath (plate);
    g.setColour (tone.withAlpha (0.035f));
    g.fillPath (plate);
    g.setColour (juce::Colours::white.withAlpha (0.10f));                       // a lit top edge, a dark rim
    g.fillRect (r.getX() + 4.0f, r.getY() + 0.5f, r.getWidth() - 8.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (plate, juce::PathStrokeType (1.0f));

    // screws
    for (int k = 0; k < 4; ++k)
    {
        const juce::Point<float> c (k % 2 == 0 ? r.getX() + 6.5f : r.getRight() - 6.5f, k < 2 ? r.getY() + 6.5f : r.getBottom() - 6.5f);
        g.setColour (juce::Colour (0xff0a0b0e));
        g.fillEllipse (juce::Rectangle<float> (5.6f, 5.6f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawEllipse (juce::Rectangle<float> (5.6f, 5.6f).withCentre (c), 0.7f);
        g.drawLine (c.x - 1.6f, c.y + (k % 2 == 0 ? 1.0f : -1.0f), c.x + 1.6f, c.y - (k % 2 == 0 ? 1.0f : -1.0f), 0.8f);
    }

    // the name, printed on the plate, over a line in the colour of its kind
    g.setFont (font (10.0f, Weight::SemiBold, 0.5f));
    g.setColour (juce::Colours::white.withAlpha (0.92f));
    const auto ea = expandArea();
    const int nameW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), juce::String (info.name).toUpperCase());
    // (on a narrow panel with an Expand label the name moves to the left, out of its way)
    const bool crowded = ! ea.isEmpty() && (getWidth() + nameW) / 2 > ea.getX() - 6;
    const bool labels = crowded || (! geo.wide && (! linkArea().isEmpty() || (! midiArea().isEmpty() && std::string (info.type) != "output")));
    g.drawFittedText (juce::String (info.name).toUpperCase(), labels ? 20 : 12, 2, crowded ? ea.getX() - 24 : getWidth() - (labels ? 28 : 24), lay::header - 5,
                      labels ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.7f);
    g.setColour ((geo.wide ? col : tone).withAlpha (0.85f));      // (a wide Notes and rhythm unit wears its sound's colour, like its steps)
    g.fillRoundedRectangle (r.getX() + 12.0f, (float) lay::header - 3.0f, r.getWidth() - 24.0f, 1.6f, 0.8f);

    if (const auto la = linkArea(); ! la.isEmpty())
    {
        const bool on = module.patternLink > 0;
        const auto lc = groupColour (module.patternLink);
        g.setColour (on ? lc.withAlpha (0.9f) : n4);
        g.fillRoundedRectangle (la.toFloat(), 4.0f);
        g.setFont (font (9.0f, Weight::SemiBold, 0.4f));
        g.setColour (on ? n0 : n8);
        g.drawText (linkText (module), la, juce::Justification::centred, false);
    }
    if (const auto ma = midiArea(); ! ma.isEmpty())
    {
        if (geo.wide) raised (g, ma.toFloat().reduced (0.5f), radiusChip);
        else { g.setColour (n4); g.fillRoundedRectangle (ma.toFloat(), 4.0f); }
        g.setFont (font (8.5f, Weight::SemiBold, 0.3f));
        g.setColour (n8);
        g.drawFittedText (std::string (info.type) == "output" ? "DRAG AUDIO" : "DRAG MIDI", ma.reduced (2, 0), juce::Justification::centred, 1, 0.7f);
    }

    if (! ea.isEmpty())
    {
        g.setColour (n4);
        g.fillRoundedRectangle (ea.toFloat(), 4.0f);
        g.setFont (font (8.5f, Weight::SemiBold, 0.3f));
        g.setColour (n8);
        g.drawFittedText ("EXPAND", ea.reduced (2, 0), juce::Justification::centred, 1, 0.7f);
    }

    // a screen behind the pattern, the chords, the wave: black glass with a faint glow
    if (const int sh = lay::seqHeight (info); sh > 0 && ! geo.wide)
    {
        const auto screen = juce::Rectangle<float> ((float) lay::pad - 4.0f, (float) lay::header - 0.5f, (float) getWidth() - 2.0f * (float) lay::pad + 8.0f, (float) sh + 3.0f);
        g.setColour (juce::Colour (0xff07080b));
        g.fillRoundedRectangle (screen, 4.0f);
        g.setColour (tone.withAlpha (0.22f));
        g.drawRoundedRectangle (screen.reduced (0.5f), 4.0f, 1.0f);
    }

    // the jack strip
    const int nIn = (int) info.ins.size(), nOut = (int) info.outs.size();
    if (geo.wide && nOut > 0)
    {
        // A wide panel has no strip: the inputs sit plain on the plate and the outputs share one raised pad
        const auto first = jackCentre (true, 0).toFloat(), last = jackCentre (true, nOut - 1).toFloat();
        const auto padArea = juce::Rectangle<float> (first.x - 18.0f, first.y - 25.0f, last.x - first.x + 36.0f, 40.0f);
        juce::Path pp; pp.addRoundedRectangle (padArea, radiusControl);
        shadow (g, pp, juce::Colours::black.withAlpha (0.5f), 6.0f, 0.0f, 2.0f);
        g.setColour (juce::Colour (0xff22242c));
        g.fillPath (pp);
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.drawRoundedRectangle (padArea.reduced (0.5f), radiusControl, 1.0f);
    }
    else if (nIn + nOut > 0)
    {
        const auto strip = juce::Rectangle<float> (r.getX() + 4.0f, (float) geo.jacksY - 1.0f, r.getWidth() - 8.0f, (float) (geo.jackRows * lay::jackH) + 2.0f);
        g.setColour (juce::Colours::black.withAlpha (black ? 0.45f : 0.32f));
        g.fillRoundedRectangle (strip, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (strip.reduced (0.5f), 4.0f, 1.0f);
    }
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto& ports = pass == 0 ? info.ins : info.outs;
        for (int i = 0; i < (int) ports.size(); ++i)
        {
            const auto c = jackCentre (pass == 1, i).toFloat();
            // An output sits on a light pad, as on hardware, so ins and outs can be told apart at a glance
            if (pass == 1 && ! geo.wide)
            {
                g.setColour (juce::Colours::white.withAlpha (0.13f));
                g.fillRoundedRectangle (c.x - 14.0f, c.y - 22.0f, 28.0f, 34.0f, 4.0f);
            }
            g.setFont (font (7.5f, Weight::SemiBold, 0.2f));
            g.setColour (pass == 1 ? juce::Colours::white : n8);
            g.drawFittedText (juce::String (ports[(size_t) i].name).toUpperCase(), (int) c.x - lay::jackW / 2, (int) c.y - 22, lay::jackW, 10, juce::Justification::centred, 1, 0.6f);
            // While a cable is being dragged, every jack it could go into lights up
            if (owner.couldTake (module.uid, pass == 1))
            {
                g.setColour (accent.withAlpha (0.22f));
                g.fillEllipse (juce::Rectangle<float> (28.0f, 28.0f).withCentre (c));
                g.setColour (accent);
                g.drawEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (c), 1.6f);
            }
            // the nut: silver, with a ring in the sound's colour on outputs, and the hole
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb9bec9), c.x, c.y - 9.0f, juce::Colour (0xff5d616c), c.x, c.y + 9.0f, false));
            g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (c));
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (c), 1.0f);
            if (pass == 1) { g.setColour (col); g.drawEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (c), 1.8f); }
            g.setColour (juce::Colour (0xff050608));
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c));
        }
    }
}

void ModuleComp::paintOverChildren (juce::Graphics& g)
{
    if (module.live) return;
    g.setFont (font (7.5f, Weight::SemiBold, 0.3f));
    g.setColour (bandOrange);
    g.drawFittedText ("NOT CONNECTED", 12, getHeight() - lay::foot, getWidth() - 24, lay::foot - 2, juce::Justification::centred, 1, 0.6f);
}

void ModuleComp::mouseDown (const juce::MouseEvent& e)
{
    toFront (false);
    bool isOut = false; int port = 0;
    moving = cabling = false;
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addSectionHeader (module.info->name);
        m.addItem (1, "Disconnect all cables");
        m.addItem (2, "Delete unit");
        juce::Component::SafePointer<BoardView> view (&owner);
        const int uid = module.uid;
        auto* rack = &proc.rack; auto* b = &board;
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [view, uid, rack, b] (int r)
        {
            if (view == nullptr || r <= 0) return;
            if (r == 1) { view->willChange(); rack->disconnectAll (*b, uid); view->refreshLive(); }
            if (r == 2) { view->willChange (uid); rack->removeModule (*b, uid); view->rebuild(); }
        });
        return;
    }
    if (expandArea().contains (e.getPosition())) { if (owner.onExpand) owner.onExpand (board, module); return; }
    if (linkArea().contains (e.getPosition())) { linkMenu(); return; }
    midiDrag = midiArea().contains (e.getPosition());
    if (midiDrag) return;
    if (jackAt (e.getPosition(), isOut, port))
    {
        cabling = true;
        owner.beginCable (*this, isOut, port, e.getEventRelativeTo (&owner).getPosition());
        return;
    }
    moving = true; movedYet = false;
    dragOffset = e.getPosition();
}

void ModuleComp::mouseDrag (const juce::MouseEvent& e)
{
    if (cabling) { owner.dragCable (e.getEventRelativeTo (&owner).getPosition()); return; }
    if (midiDrag)
    {
        // Pulled off the label: hand the host a MIDI file of this pattern
        if (e.getDistanceFromDragStart() < 6) return;
        midiDrag = false;
        juce::File file;
        if (std::string (module.info->type) == "output")
        {
            // The Output's label hands over this whole sound as audio
            for (int i = 0; i < (int) proc.rack.boards.size(); ++i) if (proc.rack.boards[(size_t) i].get() == &board) file = proc.renderTab (i);
        }
        else file = proc.writeMidi (module, juce::String (board.name) + " " + module.info->name);
        if (file != juce::File()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
        return;
    }
    if (! moving) return;
    if (! movedYet) { movedYet = true; owner.willChange(); }
    const auto p = e.getEventRelativeTo (&owner).getPosition() - dragOffset;
    const int x = juce::jmax (8, (p.x / 4) * 4), y = juce::jmax (8, (p.y / 4) * 4);
    module.x = (float) x; module.y = (float) y;
    setTopLeftPosition (x, y);
    owner.repaint();
}

void ModuleComp::mouseUp (const juce::MouseEvent& e)
{
    if (cabling) owner.endCable (e.getEventRelativeTo (&owner).getPosition());
    else if (moving) owner.fitToContent();
    moving = cabling = midiDrag = false;
}

//==============================================================================
BoardView::BoardView (Nebula2ModulationProcessor& p) : proc (p) { addChildComponent (cover); }

void BoardView::resized() { cover.setBounds (getLocalBounds()); }
void BoardView::syncCover()
{
    const bool frozen = board != nullptr && board->frozen.load();
    if (frozen != cover.isVisible()) { cover.setVisible (frozen); repaint(); }
    if (frozen) { cover.setBounds (getLocalBounds()); cover.toFront (false); }
}
void BoardView::Cover::paint (juce::Graphics& g)
{
    g.fillAll (windowBottom.withAlpha (0.62f));
    const juce::String text ("Frozen: Unfreeze to edit");
    g.setFont (titleFont());
    const juce::Rectangle<int> pill (12, 10, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 28, 30);
    raised (g, pill.toFloat(), radiusControl);
    g.setColour (accent);
    g.drawText (text, pill, juce::Justification::centred, false);
}

void BoardView::setBoard (Board* b) { board = b; rebuild(); }

void BoardView::rebuild()
{
    drag.active = false;
    comps.clear();
    if (board != nullptr)
        for (auto& m : board->modules)
            addAndMakeVisible (comps.add (new ModuleComp (*this, proc, *board, *m)));
    fitToContent();
    syncCover();
    repaint();
    if (onChanged) onChanged();
    if (onRebuilt) onRebuilt();
}

void BoardView::refreshLive()
{
    for (auto* c : comps) { c->setAlpha (c->module.live ? 1.0f : 0.45f); c->repaint(); }
    repaint();
    if (onChanged) onChanged();
}

void BoardView::repaintAll()
{
    for (auto* c : comps) c->repaint();
    repaint();
}

void BoardView::fitToContent()
{
    int w = 0, h = 0;
    for (auto* c : comps) { w = juce::jmax (w, c->getRight()); h = juce::jmax (h, c->getBottom()); }
    if (embedded)
    {
        // On the All view a board is just as big as what is on it; the All view places it and does the zooming
        setTransform ({});
        setSize (juce::jmax (360, w + 28), juce::jmax (110, h + 20));
        if (onFit) onFit();
        return;
    }
    w += 500; h += 320;        // always some room to scroll into and put new units
    const float zoom = juce::jlimit (0.5f, 1.5f, proc.zoom.load());
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        // Leave room for the scroll bars, so one appearing doesn't summon the other
        // (and always more than a windowful, so there is somewhere to scroll to and put new units)
        w = juce::jmax (w, (int) std::ceil ((float) (vp->getWidth() - vp->getScrollBarThickness()) / zoom) + 260);
        h = juce::jmax (h, (int) std::ceil ((float) (vp->getHeight() - vp->getScrollBarThickness()) / zoom) + 160);
    }
    setSize (w, h);
    // Zoom: the board is drawn scaled inside a holder that is the scaled size, so scrolling covers all of it
    setTransform (juce::AffineTransform::scale (zoom));
    if (auto* holder = getParentComponent())
        if (dynamic_cast<juce::Viewport*> (holder) == nullptr && holder->findParentComponentOfClass<juce::Viewport>() != nullptr)
            holder->setSize (juce::roundToInt ((float) w * zoom), juce::roundToInt ((float) h * zoom));
}

ModuleComp* BoardView::compFor (int uid) const
{
    for (auto* c : comps) if (c->module.uid == uid) return c;
    return nullptr;
}

void BoardView::paint (juce::Graphics& g)
{
    g.fillAll (sunkenLo);
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    const auto clip = g.getClipBounds();
    for (int y = (clip.getY() / 24) * 24; y < clip.getBottom(); y += 24)
        for (int x = (clip.getX() / 24) * 24; x < clip.getRight(); x += 24)
            g.fillRect (x, y, 1, 1);

    if (board != nullptr && board->modules.size() <= 1)
    {
        g.setFont (font (13.0f));
        g.setColour (n7);
        g.drawText ("This board is empty. Press Add units to choose what goes on it, then cable them to the Output.",
                    180, 40, 640, 40, juce::Justification::centredLeft, true);
    }
}

void BoardView::drawCable (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour col, bool ghost) const
{
    const float sag = 26.0f + a.getDistanceFrom (b) * 0.22f;
    juce::Path p;
    p.startNewSubPath (a);
    p.cubicTo (a.x, a.y + sag, b.x, b.y + sag, b.x, b.y);
    g.setColour (juce::Colours::black.withAlpha (ghost ? 0.10f : 0.22f));
    g.strokePath (p, juce::PathStrokeType (4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col.withAlpha (ghost ? 0.35f : 0.62f));
    g.strokePath (p, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col.withAlpha (ghost ? 0.45f : 0.95f));
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (a));
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (b));
    g.setColour (juce::Colours::black.withAlpha (ghost ? 0.2f : 0.45f));
    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (a));
    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (b));
}

void BoardView::paintOverChildren (juce::Graphics& g)
{
    if (board == nullptr) return;
    for (auto& c : board->cables)
    {
        auto* a = compFor (c.src); auto* b = compFor (c.dst);
        if (a == nullptr || b == nullptr) continue;
        const bool dead = ! (a->module.live && b->module.live) || cover.isVisible();      // a frozen board's cables are dimmed with the rest
        drawCable (g, (a->getPosition() + a->jackCentre (true, c.srcPort)).toFloat(),
                   (b->getPosition() + b->jackCentre (false, c.dstPort)).toFloat(),
                   boardColour (board->colour + c.src), dead);
    }
    if (drag.active)
        if (auto* a = compFor (drag.uid))
            drawCable (g, (a->getPosition() + a->jackCentre (drag.fromOut, drag.port)).toFloat(), drag.at.toFloat(),
                       drag.fromOut ? boardColour (board->colour + drag.uid) : n8, true);
    if (flashUid != 0)
        if (auto* c = compFor (flashUid))
        {
            g.setColour (accent);
            g.drawRoundedRectangle (c->getBounds().toFloat().expanded (3.0f), radiusCard + 2.0f, 2.0f);
        }
}

void BoardView::flash (int uid) { flashUid = uid; flashUntil = juce::Time::getMillisecondCounter() + 1200; repaint(); }

bool BoardView::findJack (juce::Point<int> at, ModuleComp*& comp, bool& isOut, int& port) const
{
    for (int i = getNumChildComponents(); --i >= 0;)
        if (auto* c = dynamic_cast<ModuleComp*> (getChildComponent (i)))
            if (c->jackAt (at - c->getPosition(), isOut, port)) { comp = c; return true; }
    return false;
}

void BoardView::willChange (int removingUid) { if (board != nullptr) proc.history.before (*board, removingUid); }
void BoardView::settled() { proc.history.discardIfUnchanged(); }
void BoardView::unitWillChange (int uid, bool whole) { if (board != nullptr) proc.history.beforeUnit (*board, uid, whole); }

void BoardView::beginCable (ModuleComp& comp, bool isOut, int port, juce::Point<int> at)
{
    if (board == nullptr) return;
    willChange();
    drag = { true, isOut, comp.module.uid, port, at };
    if (! isOut)
    {
        // Pulling at an input that has a cable in it picks that cable up by its plug
        for (int i = (int) board->cables.size(); --i >= 0;)
        {
            const auto c = board->cables[(size_t) i];
            if (c.dst == comp.module.uid && c.dstPort == port)
            {
                proc.rack.disconnect (*board, c.src, c.srcPort, c.dst, c.dstPort);
                drag = { true, true, c.src, c.srcPort, at };
                refreshLive();
                break;
            }
        }
    }
    repaint();
}

void BoardView::dragCable (juce::Point<int> at) { drag.at = at; repaint(); }

void BoardView::endCable (juce::Point<int> at)
{
    if (! drag.active || board == nullptr) return;
    ModuleComp* comp = nullptr; bool isOut = false; int port = 0;
    if (findJack (at, comp, isOut, port) && isOut != drag.fromOut && comp->module.uid != drag.uid)
    {
        if (drag.fromOut) proc.rack.connectStereo (*board, drag.uid, drag.port, comp->module.uid, port);
        else proc.rack.connectStereo (*board, comp->module.uid, port, drag.uid, drag.port);
    }
    drag.active = false;
    settled();
    refreshLive();
}

void BoardView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() && onZoomStep) { if (std::abs (w.deltaY) > 0.0f) onZoomStep (w.deltaY > 0 ? 1 : -1); return; }
    Component::mouseWheelMove (e, w);
}

void BoardView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        // Right-click on the board: add a unit. Favourites can be added straight from here.
        juce::PopupMenu m;
        const auto favs = AddPanel::favourites();
        if (! favs.empty()) m.addSectionHeader ("Add a favourite");
        for (int i = 0; i < (int) favs.size(); ++i) if (auto* t = findType (favs[(size_t) i])) m.addItem (10 + i, t->info->name);
        if (! favs.empty()) m.addSeparator();
        m.addItem (1, "Add units...");
        juce::Component::SafePointer<BoardView> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, favs] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r == 1) { if (safe->onAddMenu) safe->onAddMenu(); }
            else if (r - 10 < (int) favs.size() && safe->onAddTypes) safe->onAddTypes ({ favs[(size_t) (r - 10)] });
        });
        return;
    }
    if (auto* vp = findParentComponentOfClass<juce::Viewport>()) panStart = vp->getViewPosition();
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void BoardView::mouseDrag (const juce::MouseEvent& e)
{
    // Screen distance, because the board itself moves under the mouse as it scrolls
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const auto moved = e.getScreenPosition() - e.getMouseDownScreenPosition();
        vp->setViewPosition (panStart.x - moved.x, panStart.y - moved.y);
    }
}

void BoardView::mouseUp (const juce::MouseEvent&) { setMouseCursor (juce::MouseCursor::NormalCursor); }

bool BoardView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (f.endsWithIgnoreCase (".vst3") || SampleModule::canRead (f)) return board != nullptr && ! board->frozen.load();
    return false;
}

void BoardView::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (board == nullptr) return;
    willChange();
    for (auto& f : files)
    {
        // An audio file dropped on the board arrives in a new Sample player
        if (SampleModule::canRead (f))
        {
            if (auto* sm = dynamic_cast<SampleModule*> (proc.rack.addModule (*board, "sample", (float) juce::jmax (8, x - 40), (float) juce::jmax (8, y - 20))))
                sm->load (juce::File (f));
            x += 40; y += 40;
            continue;
        }
        if (! f.endsWithIgnoreCase (".vst3")) continue;
        const auto found = pluginsInFile (f);
        if (found.empty()) continue;
        if (auto* hm = dynamic_cast<HostedModule*> (proc.rack.addModule (*board, "plugin", (float) juce::jmax (8, x - 40), (float) juce::jmax (8, y - 20))))
            hm->load (found[0]);
        x += 40; y += 40;
    }
    rebuild();
}

void BoardView::tick()
{
    syncCover();
    if (flashUid != 0 && juce::Time::getMillisecondCounter() > flashUntil) { flashUid = 0; repaint(); }
    // knobs the timeline is moving are redrawn as they turn
    const bool moving = board != nullptr && ! board->autos.empty() && proc.isPlaying();
    bool linkedMoving = false;
    if (board != nullptr && proc.isPlaying()) for (auto& o : proc.rack.boards) linkedMoving = linkedMoving || ! o->autos.empty();
    for (auto* c : comps)
    {
        if (moving && c->anyOnTimeline()) c->repaint();
        else if (linkedMoving) { bool linked = false; for (int g : c->module.paramLink) linked = linked || g > 0; if (linked) c->repaint(); }      // knobs linked to one with a line turn with it
        if (c->seq != nullptr) c->seq->tick();
        if (c->chords != nullptr) c->chords->tick();
        if (c->sample != nullptr) c->sample->tick();
        if (c->scope != nullptr) c->scope->tick();
        if (c->screen != nullptr) c->screen->tick();
    }
}

//==============================================================================
StyleBar::StyleBar (Nebula2ModulationProcessor& p) : proc (p) {}

const std::vector<std::pair<const char*, const char*>>& StyleBar::parts()
{
    static const std::vector<std::pair<const char*, const char*>> v {
        { "kick", "Kick" }, { "snare", "Snare" }, { "clap", "Clap" }, { "hat", "Hat" }, { "perc", "Perc" }, { "bass", "Bass" },
        { "lead", "Lead" }, { "arp", "Arp" }, { "stab", "Stab" }, { "pad", "Pad" }, { "strings", "Strings" } };
    return v;
}

// Top row: the styles. Bottom row: Make track, Variation, Random knobs, then one chip per kind of sound, then the mode.
juce::Rectangle<int> StyleBar::styleChip (int index) const
{
    const int n = (int) n2m::styles().size();
    const int w = (getWidth() - 24) / juce::jmax (1, n);
    return { 12 + index * w, 5, w - 4, 24 };
}
juce::Rectangle<int> StyleBar::makeArea() const { return { 12, 34, 150, 25 }; }
juce::Rectangle<int> StyleBar::variationArea() const { return { 168, 34, 86, 25 }; }
juce::Rectangle<int> StyleBar::knobsArea() const { return { 260, 34, 116, 25 }; }
juce::Rectangle<int> StyleBar::modeArea (bool add) const { return { getWidth() - 12 - (add ? 58 : 132), 34, add ? 58 : 72, 25 }; }
juce::Rectangle<int> StyleBar::partChip (int index) const
{
    const int x0 = 432, x1 = modeArea (false).getX() - 52;
    const int w = (x1 - x0) / (int) parts().size();
    return { x0 + index * w, 34, w - 4, 25 };
}

void StyleBar::paint (juce::Graphics& g)
{
    const auto mouse = getMouseXYRelative();
    const auto& now = n2m::styleById (proc.rack.style);
    int i = 0;
    for (auto& st : n2m::styles())
    {
        const auto r = styleChip (i).toFloat();
        const bool on = &st == &now, over = r.contains (mouse.toFloat());
        const auto col = boardColour (i);
        if (on) sunken (g, r, radiusControl); else if (over) raised (g, r, radiusControl, true);
        g.setColour (col.withAlpha (on ? 1.0f : 0.55f));
        g.fillRoundedRectangle (r.getX() + 7.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f, 1.5f);
        g.setFont (font (11.5f, on ? Weight::SemiBold : Weight::Medium));
        g.setColour (on ? juce::Colours::white : over ? n8 : n7);
        g.drawFittedText (st.name, styleChip (i).withTrimmedLeft (18).withTrimmedRight (2), juce::Justification::centredLeft, 1, 0.75f);
        ++i;
    }
    // Make track: the one lit button on the screen
    {
        const auto r = makeArea().toFloat();
        g.setColour (accent.withAlpha (r.contains (mouse.toFloat()) ? 1.0f : 0.88f));
        g.fillRoundedRectangle (r, radiusControl);
        g.setFont (font (12.0f, Weight::SemiBold));
        g.setColour (n0);
        g.drawFittedText (juce::String (proc.addMode.load() ? "Add " : "Make ") + now.name + " track", makeArea().reduced (6, 0), juce::Justification::centred, 1, 0.75f);
    }
    auto button = [&] (juce::Rectangle<int> area, const juce::String& text, bool lit)
    {
        const auto r = area.toFloat();
        if (lit) sunken (g, r, radiusControl); else raised (g, r, radiusControl, r.contains (mouse.toFloat()));
        g.setFont (controlFont());
        g.setColour (lit ? accent : n8);
        g.drawFittedText (text, area.reduced (4, 0), juce::Justification::centred, 1, 0.8f);
    };
    button (variationArea(), "Variation", false);
    button (knobsArea(), "Random knobs", false);
    caption (g, "Parts", { knobsArea().getRight() + 14, 34, 40, 25 });
    // A part chip is lit when the track has that kind of sound
    for (int k = 0; k < (int) parts().size(); ++k)
    {
        bool has = false; int colour = 0;
        for (auto& b : proc.rack.boards) if (b->type == parts()[(size_t) k].first) { has = true; colour = b->colour; }
        const auto area = partChip (k);
        const auto r = area.toFloat();
        raised (g, r, radiusControl, r.contains (mouse.toFloat()));
        g.setColour (has ? boardColour (colour) : n5);
        g.fillEllipse (r.getX() + 8.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setFont (font (11.5f, Weight::Medium));
        g.setColour (has ? n8 : n7);
        g.drawFittedText (parts()[(size_t) k].second, area.withTrimmedLeft (19).withTrimmedRight (2), juce::Justification::centredLeft, 1, 0.75f);
    }
    caption (g, "Mode", { modeArea (false).getX() - 44, 34, 40, 25 });
    button (modeArea (false), "Replace", ! proc.addMode.load());
    button (modeArea (true), "Add", proc.addMode.load());
    dividerH (g, 0, getWidth(), getHeight() - 2);
}

void StyleBar::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    for (int i = 0; i < (int) n2m::styles().size(); ++i)
        if (styleChip (i).contains (p)) { proc.rack.style = n2m::styles()[(size_t) i].id; repaint(); return; }     // picking a style only chooses it
    if (makeArea().contains (p)) { if (onMake) onMake (proc.rack.style); return; }
    if (variationArea().contains (p)) { if (onVariation) onVariation(); return; }
    if (knobsArea().contains (p)) { if (onRandomKnobs) onRandomKnobs(); return; }
    for (int k = 0; k < (int) parts().size(); ++k)
        if (partChip (k).contains (p)) { if (onPart) onPart (parts()[(size_t) k].first); return; }
    if (modeArea (false).contains (p)) { proc.addMode.store (false); repaint(); }
    if (modeArea (true).contains (p)) { proc.addMode.store (true); repaint(); }
}

//==============================================================================
AllView::AllView (Nebula2ModulationProcessor& p) : proc (p) { addMouseListener (this, true); }

void AllView::clear()
{
    for (auto* v : views) v->setBoard (nullptr);
    views.clear();
    areas.clear();
}

void AllView::rebuild()
{
    clear();
    laying = true;
    for (auto& b : proc.rack.boards)
    {
        auto* v = views.add (new BoardView (proc));
        v->embedded = true;
        if (configure) configure (*v);
        v->onFit = [this] { layout(); };
        addAndMakeVisible (v);
        v->setBoard (b.get());
    }
    laying = false;
    layout();
}

// The areas go in rows, like words on a page. The row width is whichever gives the biggest picture when the
// whole thing is fitted into the window.
void AllView::layout()
{
    if (laying) return;
    laying = true;
    auto* vp = findParentComponentOfClass<juce::Viewport>();
    const float vw = vp != nullptr ? (float) juce::jmax (200, vp->getWidth() - vp->getScrollBarThickness()) : 1200.0f;
    const float vh = vp != nullptr ? (float) juce::jmax (120, vp->getHeight() - vp->getScrollBarThickness()) : 500.0f;
    auto place = [this] (int rowW, std::vector<juce::Rectangle<int>>* out)
    {
        const int head = headNow();
        int x = margin, y = margin, rowH = 0, right = 0;
        for (auto* v : views)
        {
            const int w = v->getWidth(), h = v->getHeight() + head;
            if (x > margin && x + w > rowW) { x = margin; y += rowH + gap; rowH = 0; }
            if (out != nullptr) out->push_back ({ x, y, w, h });
            x += w + gap; rowH = juce::jmax (rowH, h); right = juce::jmax (right, x - gap);
        }
        return juce::Point<int> (right + margin, y + rowH + margin);
    };
    int widest = 0, sum = margin * 2;
    for (auto* v : views) { widest = juce::jmax (widest, v->getWidth()); sum += v->getWidth() + gap; }
    fitScale = 0.4f;             // a starting guess, the same every time, so the same boards always lay out the same way
    int bestW = widest + margin * 2; float bestFit = 0.0f;
    for (int w = widest + margin * 2; w <= sum + widest; w += 60)
    {
        const auto size = place (w, nullptr);
        const float fit = juce::jmin (vw / (float) juce::jmax (1, size.x), vh / (float) juce::jmax (1, size.y));
        if (fit > bestFit + 0.0005f) { bestFit = fit; bestW = w; }
    }
    areas.clear();
    auto size = place (bestW, &areas);
    // The name strips are sized to stay readable at the zoom, and the zoom that fits depends on them:
    // go round a few times, which settles it
    for (int pass = 0; pass < 4; ++pass)
    {
        areas.clear();
        size = place (bestW, &areas);
        fitScale = juce::jlimit (0.1f, 1.0f, juce::jmin (vw / (float) juce::jmax (1, size.x), vh / (float) juce::jmax (1, size.y)));
    }
    areas.clear();
    size = place (bestW, &areas);
    for (int i = 0; i < views.size(); ++i) views[i]->setTopLeftPosition (areas[(size_t) i].getX(), areas[(size_t) i].getY() + headNow());
    const float zoom = zoomNow();
    // Never smaller than the window, so the background can always be grabbed and dragged
    size.x = juce::jmax (size.x, (int) std::ceil (vw / zoom)); size.y = juce::jmax (size.y, (int) std::ceil (vh / zoom));
    setSize (size.x, size.y);
    setTransform (juce::AffineTransform::scale (zoom));
    if (auto* holder = getParentComponent())
        if (isVisible() && dynamic_cast<juce::Viewport*> (holder) == nullptr && holder->findParentComponentOfClass<juce::Viewport>() != nullptr)
            holder->setSize (juce::roundToInt ((float) size.x * zoom), juce::roundToInt ((float) size.y * zoom));
    laying = false;
    repaint();
}

float AllView::fitZoom() const { return fitScale; }
// The name strip above each sound: taller when zoomed out, so the name is always about the same size on screen
int AllView::headNow() const { return juce::jlimit (headerH, 90, juce::roundToInt (24.0f / zoomNow())); }
float AllView::zoomNow() const { const float z = proc.zoomAll.load(); return z > 0.01f ? juce::jlimit (0.1f, 1.5f, z) : fitScale; }
juce::Rectangle<int> AllView::areaOf (int tab) const { return tab >= 0 && tab < (int) areas.size() ? areas[(size_t) tab] : juce::Rectangle<int>(); }

void AllView::tick() { for (auto* v : views) v->tick(); }
void AllView::repaintAll() { for (auto* v : views) v->repaintAll(); repaint(); }

void AllView::paint (juce::Graphics& g)
{
    g.fillAll (sunkenLo);
    const int sel = proc.selectedTab.load();
    for (int i = 0; i < (int) areas.size() && i < (int) proc.rack.boards.size(); ++i)
    {
        auto& b = *proc.rack.boards[(size_t) i];
        const auto col = boardColour (b.colour);
        const auto r = areas[(size_t) i].toFloat();
        // A frame round each sound, in its colour; the selected one is brighter
        g.setColour (col.withAlpha (i == sel ? 0.16f : 0.07f));
        g.fillRoundedRectangle (r.expanded (4.0f), radiusPanel);
        g.setColour (col.withAlpha (i == sel ? 0.9f : 0.35f));
        g.drawRoundedRectangle (r.expanded (4.0f), radiusPanel, i == sel ? 2.0f : 1.2f);
        const float head = (float) headNow(), dot = head * 0.34f;
        g.fillEllipse (r.getX() + head * 0.3f, r.getY() + (head - dot) * 0.5f, dot, dot);
        g.setFont (font (head * 0.52f, Weight::SemiBold));
        g.setColour (i == sel ? juce::Colours::white : n8);
        g.drawText (b.name, areas[(size_t) i].withHeight ((int) head).withTrimmedLeft (juce::roundToInt (head * 0.85f)), juce::Justification::centredLeft, true);
    }
}

// Which sound a click belongs to: the area its component sits in, or the name strip above an area
int AllView::tabAt (juce::Component* c, juce::Point<int> inThis) const
{
    for (int i = 0; i < views.size(); ++i) if (c == views[i] || views[i]->isParentOf (c)) return i;
    if (c == this) for (int i = 0; i < (int) areas.size(); ++i) if (areas[(size_t) i].expanded (4).contains (inThis)) return i;
    return -1;
}

void AllView::mouseDown (const juce::MouseEvent& e)
{
    const int tab = tabAt (e.eventComponent, e.getEventRelativeTo (this).getPosition());
    if (tab >= 0 && tab != proc.selectedTab.load() && onFocus) onFocus (tab);
    if (e.eventComponent != this) return;
    if (auto* vp = findParentComponentOfClass<juce::Viewport>()) panStart = vp->getViewPosition();
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void AllView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent != this) return;
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        const auto moved = e.getScreenPosition() - e.getMouseDownScreenPosition();
        vp->setViewPosition (panStart.x - moved.x, panStart.y - moved.y);
    }
}

void AllView::mouseUp (const juce::MouseEvent& e) { if (e.eventComponent == this) setMouseCursor (juce::MouseCursor::NormalCursor); }

void AllView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.eventComponent != this) return;
    const auto p = e.getPosition();
    for (int i = 0; i < (int) areas.size(); ++i)
        if (areas[(size_t) i].withHeight (headNow()).contains (p)) { if (onOpen) onOpen (i); return; }
}

void AllView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.eventComponent != this) return;
    if (e.mods.isCommandDown() && onZoomStep) { if (std::abs (w.deltaY) > 0.0f) onZoomStep (w.deltaY > 0 ? 1 : -1); return; }
    Component::mouseWheelMove (e, w);
}

//==============================================================================
TabBar::TabBar (Nebula2ModulationProcessor& p) : proc (p) {}

int TabBar::totalWidth() const
{
    int w = 0;
    for (auto& b : proc.rack.boards) w += juce::jmax (84, juce::GlyphArrangement::getStringWidthInt (controlFont(), b->name) + 44) + 4;
    return w;
}
bool TabBar::overflowing() const { return totalWidth() > getWidth() - 24 - 40 - allW; }
juce::Rectangle<int> TabBar::allArea() const { return { 12, 5, allW - 6, 28 }; }
juce::Rectangle<int> TabBar::plusArea() const
{
    if (overflowing()) return { getWidth() - 12 - 32, 5, 32, 28 };
    return { 12 + allW + totalWidth() + 2, 5, 32, 28 };
}
juce::Rectangle<int> TabBar::leftArrow() const { return { getWidth() - 12 - 32 - 6 - 56, 5, 26, 28 }; }
juce::Rectangle<int> TabBar::rightArrow() const { return { getWidth() - 12 - 32 - 6 - 28, 5, 26, 28 }; }
juce::Rectangle<int> TabBar::tabsArea() const
{
    return { 12 + allW, 0, (overflowing() ? leftArrow().getX() - 6 - 12 : getWidth() - 24) - allW, getHeight() };
}

std::vector<TabBar::Slot> TabBar::slots() const
{
    std::vector<Slot> s;
    int x = 12 + allW - (overflowing() ? scroll : 0), i = 0;
    for (auto& b : proc.rack.boards)
    {
        const int w = juce::jmax (84, juce::GlyphArrangement::getStringWidthInt (controlFont(), b->name) + 44);
        s.push_back ({ { x, 5, w, 28 }, i++ });
        x += w + 4;
    }
    return s;
}

void TabBar::showSelected()
{
    if (! overflowing()) { scroll = 0; repaint(); return; }
    const int maxScroll = juce::jmax (0, totalWidth() - tabsArea().getWidth());
    scroll = juce::jlimit (0, maxScroll, scroll);
    const int sel = proc.selectedTab.load();
    for (auto& s : slots())
        if (s.index == sel)
        {
            if (s.r.getX() < tabsArea().getX()) scroll -= tabsArea().getX() - s.r.getX();
            if (s.r.getRight() > tabsArea().getRight()) scroll += s.r.getRight() - tabsArea().getRight();
        }
    scroll = juce::jlimit (0, maxScroll, scroll);
    repaint();
}

void TabBar::paint (juce::Graphics& g)
{
    const int sel = proc.selectedTab.load();
    const bool all = proc.showAll.load();
    {
        // All: every sound on one board
        const auto r = allArea().toFloat();
        if (all) { raised (g, r, radiusControl); g.setColour (accent); g.fillRoundedRectangle (r.getX() + 10.0f, r.getBottom() - 3.0f, r.getWidth() - 20.0f, 2.0f, 1.0f); }
        g.setFont (controlFont());
        g.setColour (all ? juce::Colours::white : n7);
        g.drawText ("All", allArea(), juce::Justification::centred, false);
    }
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (tabsArea());
        for (auto& s : slots())
        {
            auto& b = *proc.rack.boards[(size_t) s.index];
            const auto col = boardColour (b.colour);
            const auto r = s.r.toFloat();
            if (s.index == sel)
            {
                if (! all) raised (g, r, radiusControl);
                g.setColour (col);
                g.fillRoundedRectangle (r.getX() + 10.0f, r.getBottom() - 3.0f, r.getWidth() - 20.0f, 2.0f, 1.0f);
            }
            g.setColour (col.withAlpha (s.index == sel ? 1.0f : 0.6f));
            g.fillEllipse (r.getX() + 12.0f, r.getCentreY() - 3.5f, 7.0f, 7.0f);
            g.setFont (controlFont());
            g.setColour (s.index == sel ? juce::Colours::white : n7);
            g.drawText (b.name, s.r.withTrimmedLeft (26).withTrimmedRight (8), juce::Justification::centredLeft, true);
        }
    }
    auto button = [&g] (juce::Rectangle<int> r, const juce::String& text)
    {
        raised (g, r.toFloat(), radiusControl);
        g.setFont (font (15.0f, Weight::Medium));
        g.setColour (n8);
        g.drawText (text, r, juce::Justification::centred, false);
    };
    button (plusArea(), "+");
    if (overflowing())
    {
        button (leftArrow(), juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xb9")));
        button (rightArrow(), juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")));
    }
    dividerH (g, 0, getWidth(), getHeight() - 2);
}

void TabBar::mouseDown (const juce::MouseEvent& e)
{
    if (plusArea().contains (e.getPosition())) { addMenu(); return; }
    if (allArea().contains (e.getPosition())) { if (onAll) onAll(); return; }
    if (overflowing())
    {
        if (leftArrow().contains (e.getPosition())) { scroll -= 180; scroll = juce::jmax (0, scroll); repaint(); return; }
        if (rightArrow().contains (e.getPosition())) { scroll = juce::jmin (juce::jmax (0, totalWidth() - tabsArea().getWidth()), scroll + 180); repaint(); return; }
    }
    if (! tabsArea().contains (e.getPosition())) return;
    for (auto& s : slots())
        if (s.r.contains (e.getPosition()))
        {
            if (onSelect) onSelect (s.index);
            if (e.mods.isPopupMenu()) tabMenu (s.index);
            return;
        }
}

void TabBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! tabsArea().contains (e.getPosition())) return;
    for (auto& s : slots()) if (s.r.contains (e.getPosition())) { rename (s.index); return; }
}

void TabBar::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! overflowing()) return;
    scroll = juce::jlimit (0, juce::jmax (0, totalWidth() - tabsArea().getWidth()), scroll - (int) ((w.deltaY + w.deltaX) * 240.0f));
    repaint();
}

void TabBar::addMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Add a sound");
    int i = 1;
    for (auto& t : boardTypes()) m.addItem (i++, t.name);
    // Sounds saved before, from any project
    const auto saved = Nebula2ModulationProcessor::savedSounds();
    m.addSeparator();
    if (! saved.empty())
    {
        juce::PopupMenu mine;
        int k = 1000;
        for (auto& f : saved) mine.addItem (k++, f.getFileNameWithoutExtension());
        m.addSubMenu ("Your saved sounds (" + juce::String ((int) saved.size()) + ")", mine);
    }
    m.addItem (900, "Load a sound from a file...");
    // Your own plugins, each as a tab with the plugin already loaded and cabled
    const auto own = Nebula2ModulationProcessor::ownPlugins();
    juce::PopupMenu plugins;
    for (int k = 0; k < own.size(); ++k) plugins.addItem (500 + k, own[k]);
    m.addSubMenu ("Your plugins", plugins);
    juce::Component::SafePointer<TabBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (plusArea())), [safe, saved, own] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (r >= 500 && r < 900) { if (r - 500 < own.size()) safe->added (safe->proc.addPluginSound (own[r - 500])); return; }
        if (r >= 1000) { if (r - 1000 < (int) saved.size()) safe->added (safe->proc.loadSound (saved[(size_t) (r - 1000)])); return; }
        if (r == 900)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("Load a sound", Nebula2ModulationProcessor::soundsFolder(), "*.n2msound");
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult() != juce::File()) safe->added (safe->proc.loadSound (fc.getResult()));
            });
            return;
        }
        auto& rack = safe->proc.rack;
        auto b = makeBoard (boardTypes()[(size_t) (r - 1)].id, rack.sr);
        // A second Kick is "Kick 2", so tabs and mixer channels can be told apart
        int same = 0;
        for (auto& o : rack.boards) if (o->type == b->type) ++same;
        if (same > 0) b->name += " " + std::to_string (same + 1);
        rack.addBoard (std::move (b));
        safe->proc.history.tabAdded ((int) rack.boards.size() - 1);
        if (safe->onChanged) safe->onChanged();
        if (safe->onShow) safe->onShow ((int) rack.boards.size() - 1);
        safe->showSelected();
    });
}

void TabBar::added (int index)
{
    if (index < 0) return;
    if (onChanged) onChanged();
    if (onShow) onShow (index);
    showSelected();
}

// Change sound: swap what a tab sounds like while its pattern keeps playing
void TabBar::soundMenu (int index, juce::Component* target)
{
    if (index < 0 || index >= (int) proc.rack.boards.size()) return;
    const std::string kind = proc.rack.boards[(size_t) index]->type;
    juce::PopupMenu m;
    m.addSectionHeader ("Change this sound (the pattern stays)");
    // presets of the same kind first, then every other kind
    juce::StringArray kinds, names;
    for (auto& p : soundPresets()) if (kind == p.kind) { kinds.add (p.kind); names.add (p.name); }
    const int own = kinds.size();
    for (int k = 0; k < own; ++k) m.addItem (1 + k, names[k]);
    if (own > 0) m.addSeparator();
    juce::PopupMenu others;
    for (auto& t : boardTypes())
    {
        if (kind == t.id) continue;
        juce::PopupMenu sub;
        for (auto& p : soundPresets()) if (std::string (t.id) == p.kind) { kinds.add (p.kind); names.add (p.name); sub.addItem (kinds.size(), p.name); }
        if (sub.getNumItems() > 0) others.addSubMenu (t.name, sub);
    }
    m.addSubMenu (own > 0 ? "Another kind of sound" : "A ready-made sound", others);
    m.addItem (5000, "A sample from a file...");
    const auto mine = Nebula2ModulationProcessor::ownPlugins();
    const auto installed = installedPlugins();
    juce::PopupMenu plugs, all;
    for (int k = 0; k < mine.size(); ++k) plugs.addItem (6000 + k, mine[k]);
    for (int k = 0; k < (int) installed.size() && k < 2000; ++k) all.addItem (8000 + k, installed[(size_t) k].first);
    if (! installed.empty()) { plugs.addSeparator(); plugs.addSubMenu ("All installed plugins (" + juce::String ((int) installed.size()) + ")", all); }
    m.addSubMenu ("A plugin (instrument)", plugs);
    const auto saved = Nebula2ModulationProcessor::savedSounds();
    if (! saved.empty())
    {
        juce::PopupMenu sv;
        for (int k = 0; k < (int) saved.size() && k < 900; ++k) sv.addItem (7000 + k, saved[(size_t) k].getFileNameWithoutExtension());
        m.addSubMenu ("One of your saved sounds", sv);
    }
    juce::Component::SafePointer<TabBar> safe (this);
    auto options = target != nullptr ? juce::PopupMenu::Options().withTargetComponent (target) : juce::PopupMenu::Options().withMousePosition();
    m.showMenuAsync (options, [safe, index, kinds, names, mine, installed, saved] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& p = safe->proc;
        if (safe->onShow) safe->onShow (-1);                  // the board on screen is about to be rebuilt
        p.problem.clear();
        bool done = true;
        if (r >= 8000) { if (r - 8000 < (int) installed.size()) done = p.changeToPlugin (index, installed[(size_t) (r - 8000)].first, installed[(size_t) (r - 8000)].second); }
        else if (r >= 7000) { if (r - 7000 < (int) saved.size()) p.changeToSaved (index, saved[(size_t) (r - 7000)]); }
        else if (r >= 6000) { if (r - 6000 < mine.size()) done = p.changeToPlugin (index, mine[r - 6000]); }
        else if (r == 5000)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), SampleModule::wildcard());
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, index] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                if (safe->onShow) safe->onShow (-1);
                if (! safe->proc.changeToSample (index, fc.getResult()))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Sound not changed", safe->proc.problem);
                safe->added (index);
            });
        }
        else if (r <= kinds.size()) p.changeToPreset (index, kinds[r - 1], names[r - 1]);
        safe->added (index);
        // Say why, rather than quietly doing nothing
        if (! done && p.problem.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Sound not changed", p.problem);
    });
}

void TabBar::tabMenu (int index)
{
    juce::PopupMenu m;
    const int n = (int) proc.rack.boards.size();
    m.addItem (1, "Rename");
    m.addItem (7, "Change sound...");
    m.addItem (2, "Duplicate");
    m.addItem (6, "Save this sound...");
    m.addItem (8, index < n && proc.rack.boards[(size_t) index]->frozen.load() ? "Unfreeze" : "Freeze");
    m.addSeparator();
    m.addItem (3, "Move left", index > 0);
    m.addItem (4, "Move right", index < n - 1);
    m.addSeparator();
    m.addItem (5, "Delete this sound", n > 1);
    juce::Component::SafePointer<TabBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, index] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& rack = safe->proc.rack;
        if (index >= (int) rack.boards.size()) return;
        int select = index;
        if (r == 1) { safe->rename (index); return; }
        if (r == 7) { safe->soundMenu (index); return; }
        if (r == 8) { safe->toggleFreeze (index); return; }
        if (r == 6)
        {
            const auto start = Nebula2ModulationProcessor::soundsFolder().getChildFile (juce::File::createLegalFileName (rack.boards[(size_t) index]->name) + ".n2msound");
            start.getParentDirectory().createDirectory();
            safe->chooser = std::make_unique<juce::FileChooser> ("Save this sound", start, "*.n2msound");
            safe->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe, index] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult() != juce::File()) safe->proc.saveSound (index, fc.getResult().withFileExtension ("n2msound"));
            });
            return;
        }
        if (r == 2)
        {
            auto copy = rack.copyBoard (*rack.boards[(size_t) index]);
            if (copy == nullptr) return;
            copy->name += " copy";
            rack.addBoard (std::move (copy));
            safe->proc.history.tabAdded ((int) rack.boards.size() - 1);
            select = (int) rack.boards.size() - 1;
        }
        if (r == 3) { rack.moveBoard (index, index - 1); safe->proc.history.tabMoved (index, index - 1); select = index - 1; }
        if (r == 4) { rack.moveBoard (index, index + 1); safe->proc.history.tabMoved (index, index + 1); select = index + 1; }
        if (r == 5)
        {
            // The board on screen must stop pointing at this one before it goes
            if (safe->onShow) safe->onShow (-1);
            safe->proc.history.tabRemoving (index);
            rack.removeBoard (index);
            select = juce::jmin (index, (int) rack.boards.size() - 1);
        }
        if (safe->onChanged) safe->onChanged();
        if (safe->onShow) safe->onShow (select);
        safe->showSelected();
    });
}

// Freeze: the sound is swapped for its own audio, so it takes almost none of the computer's time. Unfreeze brings
// its units back as they were.
void TabBar::toggleFreeze (int index)
{
    if (index < 0 || index >= (int) proc.rack.boards.size()) return;
    const bool done = proc.rack.boards[(size_t) index]->frozen.load() ? proc.unfreezeTab (index) : proc.freezeTab (index);
    if (! done && proc.problem.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Not frozen", proc.problem);
    if (onChanged) onChanged();
    repaint();
}

void TabBar::rename (int index)
{
    for (auto& s : slots())
        if (s.index == index)
        {
            editor = std::make_unique<juce::TextEditor>();
            addAndMakeVisible (*editor);
            editor->setBounds (s.r.getIntersection (tabsArea()));
            editor->setFont (controlFont());
            editor->setInputRestrictions (24);
            editor->setText (proc.rack.boards[(size_t) index]->name, false);
            editor->selectAll();
            editor->grabKeyboardFocus();
            // (the pointer is made out here: Visual Studio mis-reads 'this' inside a lambda inside a lambda)
            juce::Component::SafePointer<TabBar> safe (this);
            auto done = [this, safe, index] (bool keep)
            {
                if (editor == nullptr) return;
                const auto text = editor->getText().trim();
                if (keep && text.isNotEmpty() && index < (int) proc.rack.boards.size())
                {
                    proc.history.before (*proc.rack.boards[(size_t) index]);
                    proc.rack.boards[(size_t) index]->name = text.toStdString();
                    proc.history.discardIfUnchanged();
                }
                juce::MessageManager::callAsync ([safe]
                {
                    if (safe != nullptr) { safe->editor.reset(); safe->repaint(); if (safe->onChanged) safe->onChanged(); }
                });
            };
            editor->onReturnKey = [done] { done (true); };
            editor->onFocusLost = [done] { done (true); };
            editor->onEscapeKey = [done] { done (false); };
        }
}

//==============================================================================
MixerStrip::MixerStrip (Nebula2ModulationProcessor& p) : proc (p) {}

static constexpr int kChanW = 88, kMasterW = 112;
static constexpr float kFaderTop = 1.25f;   // the top of a fader is 125%

// Closed, the desk shows level, mute, solo and output. Opened, it is about twice as tall: every fader, the master
// included, grows to fill it, and each channel gains pan and its load figure. Mixing only: nothing here changes a sound.
static constexpr int kExtraH = 124;
int MixerStrip::heightFor (bool tall) { return tall ? kMixerH + kExtraH : kMixerH; }
void MixerStrip::setTall (bool tall) { proc.mixerTall.store (tall); if (onResize) onResize(); repaint(); }

//---- Inside: the selected sound's own levels and mixes ----
static constexpr int kInsideHeadW = 84;
void MixerStrip::setInside (bool on) { proc.mixerInside.store (on); dragChannel = -2; dragUid = 0; if (onResize) onResize(); repaint(); }
Board* MixerStrip::insideBoard() const
{
    const int t = proc.selectedTab.load();
    return t >= 0 && t < (int) proc.rack.boards.size() ? proc.rack.boards[(size_t) t].get() : nullptr;
}
std::vector<InsideStrip> MixerStrip::insideList() const { if (auto* b = insideBoard()) return insideStrips (*b); return {}; }
juce::String MixerStrip::insideHeading() const { if (auto* b = insideBoard()) return "Inside: " + juce::String (b->name); return "Inside"; }
juce::Rectangle<int> MixerStrip::insideStrip (int i) const { return { 12 + kInsideHeadW + 6 + i * (kChanW + 6) - scrollIn, 8, kChanW, getHeight() - 16 }; }
juce::Rectangle<int> MixerStrip::insideFader (juce::Rectangle<int> s) { return { s.getX() + 10, s.getY() + 50, 26, s.getHeight() - 60 }; }
juce::Rectangle<int> MixerStrip::insideName (juce::Rectangle<int> s) { return { s.getX() + 4, s.getY() + 8, s.getWidth() - 8, 34 }; }
float MixerStrip::insideNorm (const InsideStrip& s) const
{
    auto* b = insideBoard();
    auto* m = b != nullptr ? b->find (s.uid) : nullptr;
    if (m == nullptr || s.param < 0 || s.param >= (int) m->info->params.size()) return -1.0f;
    return paramToNorm (m->info->params[(size_t) s.param], m->P (s.param));
}
// A fader turns its knob the way a hand on the board does: through the knob itself (ParamCell::setNorm), so the
// knob redraws and linked knobs follow. The unit is looked up by id on the board that is on screen now.
bool MixerStrip::turnInside (int uid, int param, float norm)
{
    auto* view = boardFor ? boardFor() : nullptr;
    if (view == nullptr || view->board == nullptr || view->board != insideBoard()) return false;
    auto* comp = view->compFor (uid);
    // (the unit on screen must be the one the rack holds now, looked up by id: never trust a kept address)
    if (comp == nullptr || view->board->find (uid) != &comp->module) return false;
    auto* cell = comp->cellFor (param);
    if (cell == nullptr || ! isMixKnob (*comp->module.info, comp->module.info->params[(size_t) param])) return false;
    cell->setNorm (norm);
    repaint();
    return true;
}
bool MixerStrip::moveInside (const InsideStrip& s, float norm)
{
    auto* view = boardFor ? boardFor() : nullptr;
    if (view == nullptr || view->board == nullptr || view->board != insideBoard()) return false;
    view->willChange();
    const bool ok = turnInside (s.uid, s.param, norm);
    view->settled();
    return ok;
}
void MixerStrip::setPan (int channel, float pan)
{
    if (channel < 0 || channel >= (int) proc.rack.boards.size()) return;
    proc.rack.boards[(size_t) channel]->pan.store (std::abs (pan) < 0.04f ? 0.0f : juce::jlimit (-1.0f, 1.0f, pan));
    repaint();
}
juce::Rectangle<int> MixerStrip::masterArea() const { return { getWidth() - 12 - kMasterW, 8, kMasterW, getHeight() - 16 }; }
juce::Rectangle<int> MixerStrip::channel (int i) const { return { 12 + i * (kChanW + 6) - scroll, 8, kChanW, getHeight() - 16 }; }
// Pan sits in the channel's right-hand column, under Solo
juce::Rectangle<int> MixerStrip::panIn (juce::Rectangle<int> ch) { return { ch.getX() + 40, ch.getY() + 112, 42, 14 }; }
juce::Rectangle<int> MixerStrip::faderIn (juce::Rectangle<int> ch) { return { ch.getX() + 10, ch.getY() + 30, 26, ch.getHeight() - 40 }; }
juce::Rectangle<int> MixerStrip::muteIn (juce::Rectangle<int> ch) { return { ch.getX() + 44, ch.getY() + 30, 34, 22 }; }
juce::Rectangle<int> MixerStrip::soloIn (juce::Rectangle<int> ch) { return { ch.getX() + 44, ch.getY() + 56, 34, 22 }; }
juce::Rectangle<int> MixerStrip::outIn (juce::Rectangle<int> ch) { return { ch.getX() + 40, ch.getBottom() - 26, 42, 18 }; }

// Which output a sound goes to: the main mix, or its own channel in the host's mixer
void MixerStrip::outMenu (int channel)
{
    if (channel < 0 || channel >= (int) proc.rack.boards.size()) return;
    auto* board = proc.rack.boards[(size_t) channel].get();
    const int on = proc.rack.outsOn.load();
    juce::PopupMenu m;
    m.addSectionHeader ("Send " + juce::String (board->name) + " to");
    m.addItem (1, "Main mix", true, board->out.load() == 0);
    // Only the outputs switched on in the host are offered (plus the one this sound is set to, if that is off)
    int shown = 0;
    for (int k = 1; k < kOuts; ++k)
    {
        const bool active = (on & (1 << k)) != 0, mine = board->out.load() == k;
        if (! active && ! mine) continue;
        m.addItem (k + 1, "Out " + juce::String (k + 1) + (active ? "" : "   (not switched on in Cubase)"), true, mine);
        if (active) ++shown;
    }
    m.addSeparator();
    m.addItem (9999, shown == 0 ? "For more outputs, switch them on in Cubase: Activate Outputs on the instrument" : "More outputs: Activate Outputs in Cubase", false);
    juce::Component::SafePointer<MixerStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (outIn (this->channel (channel)))), [safe, channel] (int r)
    {
        if (safe == nullptr || r <= 0 || channel >= (int) safe->proc.rack.boards.size()) return;
        safe->proc.history.before (*safe->proc.rack.boards[(size_t) channel]);
        safe->proc.rack.boards[(size_t) channel]->out.store (r - 1);
        safe->proc.history.discardIfUnchanged();
        safe->repaint();
    });
}

static void drawFader (juce::Graphics& g, juce::Rectangle<int> f, float level, float meterA, float meterB, juce::Colour col)
{
    // Track, with the level as a bar and a handle; beside it the meter(s)
    const auto track = juce::Rectangle<float> ((float) f.getX(), (float) f.getY(), 10.0f, (float) f.getHeight());
    sunken (g, track, 4.0f);
    const float n = juce::jlimit (0.0f, 1.0f, level / kFaderTop);
    const float hy = track.getBottom() - n * track.getHeight();
    g.setColour (col.withAlpha (0.35f));
    g.fillRoundedRectangle (track.withTop (hy).reduced (2.0f, 1.0f), 2.0f);
    g.setColour (n8);
    g.fillRoundedRectangle (track.getX() - 2.0f, hy - 2.0f, 14.0f, 4.0f, 2.0f);
    for (int k = 0; k < 2; ++k)
    {
        const float m = k == 0 ? meterA : meterB;
        if (m < 0.0f) continue;
        const auto bar = juce::Rectangle<float> ((float) f.getX() + 15.0f + (float) k * 6.0f, (float) f.getY(), 4.0f, (float) f.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (bar, 2.0f);
        const float mh = juce::jlimit (0.0f, 1.0f, std::sqrt (m)) * bar.getHeight();
        g.setColour (m > 0.95f ? bandPink : bandGreen);
        g.fillRoundedRectangle (bar.withTop (bar.getBottom() - mh), 2.0f);
    }
}

void MixerStrip::paint (juce::Graphics& g)
{
    dividerH (g, 0, getWidth(), 0);
    const int sel = proc.selectedTab.load();
    const int n = (int) proc.rack.boards.size();
    shown.resize ((size_t) n, 0.0f);
    if (proc.mixerInside.load())
    {
        // Inside: which sound, then a strip for each of its levels and mixes. No pan, mute, solo or output here.
        auto* b = insideBoard();
        const auto list = insideList();
        const int left = 12 + kInsideHeadW, right = masterArea().getX() - 10;
        scrollIn = juce::jlimit (0, juce::jmax (0, (int) list.size() * (kChanW + 6) - (right - left - 6)), scrollIn);
        const auto col = boardColour (b != nullptr ? b->colour : 0);
        g.setColour (col);
        g.fillRoundedRectangle (12.0f, 12.0f, (float) kInsideHeadW - 12.0f, 2.0f, 1.0f);
        caption (g, "Inside:", { 12, 22, kInsideHeadW - 8, 12 });
        g.setFont (font (12.0f, Weight::Medium));
        g.setColour (juce::Colours::white);
        if (b != nullptr) g.drawFittedText (b->name, 12, 36, kInsideHeadW - 8, 30, juce::Justification::topLeft, 2, 0.8f);
        if (list.empty())
        {
            g.setFont (font (11.0f));
            g.setColour (n7);
            g.drawText ("Nothing to mix in this sound", left + 10, 8, juce::jmax (0, right - left - 20), getHeight() - 16, juce::Justification::centredLeft, true);
        }
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (left + 2, 0, juce::jmax (0, right - left - 2), getHeight());
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& s = list[(size_t) i];
            const auto ch = insideStrip (i);
            if (ch.getRight() < left || ch.getX() > right) continue;
            auto* m = b->find (s.uid);
            if (m == nullptr) continue;
            const auto& prm = m->info->params[(size_t) s.param];
            const bool dim = ! m->live;                         // a unit that can't be heard: its strip is dimmed, like the unit
            if (dim) g.beginTransparencyLayer (0.4f);
            g.setColour (juce::Colours::white.withAlpha (0.025f));
            g.fillRoundedRectangle (ch.toFloat(), radiusCard);
            g.setColour (col);
            g.fillRoundedRectangle ((float) ch.getX() + 10.0f, (float) ch.getY() + 4.0f, (float) ch.getWidth() - 20.0f, 2.0f, 1.0f);
            const auto nm = insideName (ch);
            g.setFont (font (10.5f, Weight::Medium));
            g.setColour (n8);
            g.drawFittedText (s.unit, nm.withHeight (24), juce::Justification::centred, 2, 0.8f);
            const auto f = insideFader (ch);
            const float norm = paramToNorm (prm, m->P (s.param));
            drawFader (g, f, norm * kFaderTop, -1.0f, -1.0f, col);
            // beside the fader: which knob it is, and what it is set to
            caption (g, s.knob, { ch.getX() + 34, f.getY() - 1, ch.getWidth() - 38, 12 }, juce::Justification::centred);
            g.setColour (n4);
            const juce::Rectangle<int> vb (ch.getX() + 38, ch.getBottom() - 26, ch.getWidth() - 46, 18);
            g.fillRoundedRectangle (vb.toFloat(), 4.0f);
            g.setFont (font (9.5f, Weight::Medium));
            g.setColour (dragUid == s.uid && dragParam == s.param ? juce::Colours::white : n8);
            g.drawFittedText (juce::String (paramText (prm, m->P (s.param))), vb.reduced (2, 0), juce::Justification::centred, 1, 0.75f);
            if (dim) g.endTransparencyLayer();
        }
    }
    else
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (0, 0, masterArea().getX() - 10, getHeight());
        for (int i = 0; i < n; ++i)
        {
            auto& b = *proc.rack.boards[(size_t) i];
            const auto ch = channel (i);
            const auto col = boardColour (b.colour);
            const bool tall = proc.mixerTall.load();
            if (i == sel) raised (g, ch.toFloat(), radiusCard);
            else { g.setColour (juce::Colours::white.withAlpha (0.025f)); g.fillRoundedRectangle (ch.toFloat(), radiusCard); }
            if (tall)
            {
                // Opened up: pan and load, in the right-hand column under Solo
                const float pan = b.pan.load();
                const auto bar = panIn (ch).toFloat();
                caption (g, "Pan", { (int) bar.getX() - 2, (int) bar.getY() - 16, (int) bar.getWidth() + 4, 12 }, juce::Justification::centred);
                sunken (g, bar, 4.0f);
                const float mid = bar.getCentreX(), at = mid + pan * (bar.getWidth() * 0.5f - 4.0f);
                g.setColour (col.withAlpha (0.55f));
                g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (mid, at), bar.getY() + 4.0f, std::abs (at - mid), bar.getHeight() - 8.0f), 2.0f);
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.fillRoundedRectangle (at - 2.0f, bar.getY() + 2.0f, 4.0f, bar.getHeight() - 4.0f, 2.0f);
                g.setFont (font (9.0f));
                g.setColour (n7);
                const int pc = (int) std::lround (std::abs (pan) * 100.0f);
                g.drawText (pc == 0 ? juce::String ("Centre") : (pan < 0 ? "Left " : "Right ") + juce::String (pc), (int) bar.getX() - 4, (int) bar.getBottom() + 3, (int) bar.getWidth() + 8, 12, juce::Justification::centred, false);
                const int load = (int) std::lround (b.load.load() * 100.0f);
                g.setFont (font (8.5f, Weight::Medium, 0.3f));
                g.setColour (load >= 40 ? bandOrange : n7);
                g.drawText ("LOAD " + juce::String (juce::jmin (999, load)) + "%", (int) bar.getX() - 4, (int) bar.getBottom() + 26, (int) bar.getWidth() + 8, 11, juce::Justification::centred, false);
            }
            g.setColour (col);
            g.fillRoundedRectangle ((float) ch.getX() + 10.0f, (float) ch.getY() + 4.0f, (float) ch.getWidth() - 20.0f, 2.0f, 1.0f);
            g.setFont (font (11.0f, Weight::Medium));
            g.setColour (i == sel ? juce::Colours::white : n8);
            g.drawText (b.name, ch.getX() + 6, ch.getY() + 10, ch.getWidth() - 12, 14, juce::Justification::centred, true);
            drawFader (g, faderIn (ch), b.level.load(), shown[(size_t) i], -1.0f, col);

            auto pill = [&g] (juce::Rectangle<int> r, const char* text, bool on, juce::Colour onCol)
            {
                if (on) { g.setColour (onCol); g.fillRoundedRectangle (r.toFloat(), radiusControl); }
                else raised (g, r.toFloat(), radiusControl);
                g.setFont (font (11.0f, Weight::SemiBold));
                g.setColour (on ? n0 : n7);
                g.drawText (text, r, juce::Justification::centred, false);
            };
            pill (muteIn (ch), "M", b.mute.load(), bandPink);
            pill (soloIn (ch), "S", b.solo.load(), bandYellow);
            // The output label: MAIN, or OUT n. Orange when that output isn't switched on in the host
            // (the sound then stays in the main mix). While the fader is dragged it shows the level instead.
            const auto ob = outIn (ch);
            const int out = b.out.load();
            const bool missing = out > 0 && (proc.rack.outsOn.load() & (1 << out)) == 0;
            g.setColour (out > 0 ? (missing ? bandOrange : accent).withAlpha (0.9f) : n4);
            g.fillRoundedRectangle (ob.toFloat(), 4.0f);
            g.setFont (font (9.0f, Weight::SemiBold, 0.3f));
            g.setColour (out > 0 ? n0 : n7);
            g.drawText (dragChannel == i ? juce::String ((int) std::lround (b.level.load() * 100)) + "%" : out > 0 ? "OUT " + juce::String (out + 1) : juce::String ("MAIN"),
                        ob, juce::Justification::centred, false);
        }
    }
    const auto m = masterArea();
    dividerV (g, m.getX() - 8, 8, getHeight() - 8);
    caption (g, "Master", { m.getX(), m.getY() + 10, m.getWidth(), 14 }, juce::Justification::centred);
    drawFader (g, faderIn (m).translated (26, 0), proc.rack.master.load(), shownL, shownR, accent);
    g.setFont (font (10.0f));
    g.setColour (n7);
    g.drawText (juce::String ((int) std::lround (proc.rack.master.load() * 100)) + "%", m.getX() + 66, m.getBottom() - 24, 42, 12, juce::Justification::centred, false);
    if (proc.mixerTall.load() && ! proc.mixerInside.load())
    {
        // Every sound's load added up: what the whole plugin is using
        float sum = 0;
        for (auto& b : proc.rack.boards) sum += b->load.load();
        const int total = (int) std::lround (sum * 100.0f);
        caption (g, "Total", { m.getX() + 60, m.getY() + 36, 50, 12 }, juce::Justification::centred);
        g.setFont (font (8.5f, Weight::Medium, 0.3f));
        g.setColour (total >= 70 ? bandOrange : n7);
        g.drawText ("LOAD " + juce::String (juce::jmin (999, total)) + "%", m.getX() + 56, m.getY() + 51, 58, 11, juce::Justification::centred, false);
    }
}

void MixerStrip::mouseDown (const juce::MouseEvent& e)
{
    dragChannel = -2; dragPan = false; dragUid = 0; dragParam = -1;
    const auto m = masterArea();
    if (m.contains (e.getPosition())) { proc.history.beforeMaster(); dragChannel = -1; mouseDrag (e); return; }   // mouseUp drops the step if it didn't move
    if (e.x >= m.getX() - 10) return;
    if (proc.mixerInside.load())
    {
        if (e.x < 12 + kInsideHeadW) return;
        const auto list = insideList();
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto ch = insideStrip (i);
            if (! ch.contains (e.getPosition())) continue;
            if (insideName (ch).contains (e.getPosition())) { if (onShowUnit) onShowUnit (list[(size_t) i].uid); return; }
            if (! insideFader (ch).expanded (8, 4).contains (e.getPosition())) return;
            auto* view = boardFor ? boardFor() : nullptr;
            if (view == nullptr || view->board != insideBoard()) return;
            view->willChange();                 // one Undo step for the whole drag; mouseUp drops it if nothing moved
            dragUid = list[(size_t) i].uid; dragParam = list[(size_t) i].param; dragChannel = i;
            mouseDrag (e);
            return;
        }
        return;
    }
    for (int i = 0; i < (int) proc.rack.boards.size(); ++i)
    {
        const auto ch = channel (i);
        if (proc.mixerTall.load() && panIn (ch).expanded (4, 6).contains (e.getPosition())) { proc.history.before (*proc.rack.boards[(size_t) i]); dragChannel = i; dragPan = true; mouseDrag (e); return; }
        if (! ch.contains (e.getPosition())) continue;
        auto& b = *proc.rack.boards[(size_t) i];
        if (outIn (ch).contains (e.getPosition())) { outMenu (i); return; }
        proc.history.before (b);
        if (muteIn (ch).contains (e.getPosition())) b.mute.store (! b.mute.load());
        else if (soloIn (ch).contains (e.getPosition())) b.solo.store (! b.solo.load());
        else if (faderIn (ch).expanded (8, 4).contains (e.getPosition())) { dragChannel = i; mouseDrag (e); }
        else if (onSelect) onSelect (i);
        repaint();
        return;
    }
}

void MixerStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragChannel == -2) return;
    if (dragUid != 0)
    {
        // the strip is found again by its unit and knob: the list may have changed under the drag
        const auto list = insideList();
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].uid == dragUid && list[(size_t) i].param == dragParam)
            {
                const auto f = insideFader (insideStrip (i));
                if (turnInside (dragUid, dragParam, 1.0f - (float) (e.y - f.getY()) / (float) f.getHeight())) return;
            }
        dragUid = 0; dragChannel = -2;
        return;
    }
    if (dragPan)
    {
        const auto bar = panIn (channel (dragChannel));
        setPan (dragChannel, ((float) (e.x - bar.getCentreX())) / ((float) bar.getWidth() * 0.5f - 4.0f));
        return;
    }
    const auto f = dragChannel == -1 ? faderIn (masterArea()) : faderIn (channel (dragChannel));
    const float v = juce::jlimit (0.0f, kFaderTop, (1.0f - (float) (e.y - f.getY()) / (float) f.getHeight()) * kFaderTop);
    if (dragChannel == -1) proc.rack.master.store (v);
    else if (dragChannel < (int) proc.rack.boards.size()) proc.rack.boards[(size_t) dragChannel]->level.store (v);
    repaint();
}

void MixerStrip::mouseUp (const juce::MouseEvent&) { proc.history.discardIfUnchanged(); dragChannel = -2; dragPan = false; dragUid = 0; dragParam = -1; repaint(); }

void MixerStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (masterArea().contains (e.getPosition())) { proc.history.beforeMaster(); proc.rack.master.store (0.8f); proc.history.discardIfUnchanged(); repaint(); return; }
    if (proc.mixerInside.load())
    {
        // a double-click puts the knob back where it starts, as it does on the board
        const auto list = insideList();
        auto* b = insideBoard();
        for (int i = 0; i < (int) list.size(); ++i)
            if (insideFader (insideStrip (i)).expanded (8, 4).contains (e.getPosition()) && e.x < masterArea().getX() - 10)
                if (auto* m = b->find (list[(size_t) i].uid))
                    moveInside (list[(size_t) i], paramToNorm (m->info->params[(size_t) list[(size_t) i].param], m->info->params[(size_t) list[(size_t) i].param].def));
        return;
    }
    if (proc.mixerTall.load()) for (int i = 0; i < (int) proc.rack.boards.size(); ++i) if (panIn (channel (i)).expanded (4, 6).contains (e.getPosition())) { setPan (i, 0.0f); return; }
    for (int i = 0; i < (int) proc.rack.boards.size(); ++i)
        if (faderIn (channel (i)).expanded (8, 4).contains (e.getPosition())) { proc.rack.boards[(size_t) i]->level.store (0.8f); repaint(); }
}

void MixerStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (proc.mixerInside.load())
    {
        // more strips than fit: the wheel moves along them, as it does along the sounds
        const int all = (int) insideList().size() * (kChanW + 6), space = masterArea().getX() - 10 - 12 - kInsideHeadW - 6;
        scrollIn = juce::jlimit (0, juce::jmax (0, all - space), scrollIn - (int) ((w.deltaY + w.deltaX) * 240.0f));
        repaint();
        return;
    }
    const int total = (int) proc.rack.boards.size() * (kChanW + 6);
    const int room = masterArea().getX() - 10 - 12;
    scroll = juce::jlimit (0, juce::jmax (0, total - room), scroll - (int) ((w.deltaY + w.deltaX) * 240.0f));
    repaint();
}

void MixerStrip::tick()
{
    const int n = (int) proc.rack.boards.size();
    shown.resize ((size_t) n, 0.0f);
    bool moved = false;
    for (int i = 0; i < n; ++i)
    {
        const float p = proc.rack.boards[(size_t) i]->peak.exchange (0.0f);
        const float v = juce::jmax (p, shown[(size_t) i] * 0.85f);
        moved = moved || std::abs (v - shown[(size_t) i]) > 0.002f;
        shown[(size_t) i] = v < 0.001f ? 0.0f : v;
    }
    const float l = juce::jmax (proc.rack.peakL.exchange (0.0f), shownL * 0.85f), r = juce::jmax (proc.rack.peakR.exchange (0.0f), shownR * 0.85f);
    moved = moved || std::abs (l - shownL) > 0.002f || std::abs (r - shownR) > 0.002f;
    shownL = l < 0.001f ? 0.0f : l; shownR = r < 0.001f ? 0.0f : r;
    if (proc.mixerInside.load())
    {
        // Inside: follow the knobs. Anything that changes what is drawn (a knob turned on the board, by the timeline
        // or by Undo, a unit added or removed, another sound selected or renamed) changes this number.
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h] (uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        if (auto* b = insideBoard())
        {
            mix ((uint64_t) (size_t) b); mix ((uint64_t) std::hash<std::string>() (b->name)); mix ((uint64_t) b->colour);
            for (auto& s : insideStrips (*b))
                if (auto* m = b->find (s.uid))
                {
                    uint32_t bits = 0; const float v = m->P (s.param); std::memcpy (&bits, &v, sizeof (bits));
                    mix ((uint64_t) s.uid); mix ((uint64_t) s.param); mix (bits); mix (m->live ? 1u : 0u); mix ((uint64_t) (size_t) m->info);
                }
        }
        if (h != insideSeen) { insideSeen = h; moved = true; }
    }
    if (moved) repaint();
}

//==============================================================================
AddPanel::AddPanel()
{
    addAndMakeVisible (add); addAndMakeVisible (cancel);
    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Search units...", n7);
    search.setFont (font (14.0f));
    search.onTextChange = [this] { setSearch (search.getText()); };
    search.onEscapeKey = [this] { if (search.isEmpty()) setVisible (false); else search.clear(); };
    cancel.onClick = [this] { setVisible (false); };
    add.onClick = [this]
    {
        auto picked = ticked;
        setVisible (false);
        if (onAdd && ! picked.empty()) onAdd (picked);
    };
    setVisible (false);
}

void AddPanel::open() { hovered = nullptr; ticked.clear(); add.setEnabled (false); search.setText ({}, false); query.clear(); setVisible (true); toFront (true); search.grabKeyboardFocus(); repaint(); }

static constexpr int kRowH = 35, kHeadH = 27, kCardW = 1216, kAddCols = 4;

juce::Rectangle<int> AddPanel::card() const
{
    int total = 0;
    for (auto* s : shelves()) { juce::ignoreUnused (s); total += kHeadH; }
    total += (int) allTypes().size() * kRowH;
    const int h = juce::jmin (getHeight() - 24, total / kAddCols + 2 * kHeadH + 3 * kRowH + 120);
    return juce::Rectangle<int> (kCardW, h).withCentre (getLocalBounds().getCentre());
}

// Favourites live in a small text file beside the list of plugins used before
static juce::File favouritesFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Back2You").getChildFile ("Nebula2 Modulation").getChildFile ("favourites.txt");
}
std::vector<std::string> AddPanel::favourites()
{
    std::vector<std::string> out;
    juce::StringArray lines;
    lines.addLines (favouritesFile().loadFileAsString());
    for (auto& l : lines) if (l.trim().isNotEmpty() && findType (l.trim().toStdString()) != nullptr) out.push_back (l.trim().toStdString());
    return out;
}
void AddPanel::setFavourite (const std::string& type, bool on)
{
    auto now = favourites();
    now.erase (std::remove (now.begin(), now.end(), type), now.end());
    if (on) now.push_back (type);
    juce::StringArray lines;
    for (auto& t : now) lines.add (t);
    favouritesFile().getParentDirectory().createDirectory();
    favouritesFile().replaceWithText (lines.joinIntoString ("\n"));
}
juce::Rectangle<int> AddPanel::starIn (juce::Rectangle<int> row) { return { row.getRight() - 26, row.getY() + 6, 22, 22 }; }
void AddPanel::setSearch (const juce::String& text) { query = text.trim(); hovered = nullptr; repaint(); }

std::vector<AddPanel::Row> AddPanel::rows() const
{
    // Columns of headings; a heading never splits across columns. Favourites come first.
    // With letters typed in the search box, only units that have them in their name, hint or heading are listed.
    std::vector<Row> out;
    const auto c = card();
    const auto types = allTypes();
    const auto favs = favourites();
    auto matches = [this] (const Info* t) { return query.isEmpty() || juce::String (t->name).containsIgnoreCase (query) || juce::String (t->hint).containsIgnoreCase (query) || juce::String (t->shelf).containsIgnoreCase (query); };
    struct Block { juce::String heading; std::vector<const Info*> units; };
    std::vector<Block> blocks;
    if (! favs.empty())
    {
        Block f { "Favourites", {} };
        for (auto& id : favs) for (auto* t : types) if (id == t->type && matches (t)) f.units.push_back (t);
        if (! f.units.empty()) blocks.push_back (f);
    }
    for (auto* s : shelves())
    {
        Block bl { s, {} };
        for (auto* t : types) if (std::string (t->shelf) == s && matches (t)) bl.units.push_back (t);
        if (! bl.units.empty()) blocks.push_back (bl);
    }
    int total = 0;
    for (auto& bl : blocks) total += kHeadH + (int) bl.units.size() * kRowH;
    const int colW = (kCardW - 40 - (kAddCols - 1) * 12) / kAddCols;
    const int top = c.getY() + 62;
    int col = 0, y = top, used = 0;
    for (auto& bl : blocks)
    {
        const int block = kHeadH + (int) bl.units.size() * kRowH;
        if (col < kAddCols - 1 && used > 0 && (used + block / 2 > total * (col + 1) / kAddCols || y + block > c.getBottom() - 56)) { ++col; y = top; }
        const int x = c.getX() + 20 + col * (colW + 12);
        out.push_back ({ { x, y, colW, kHeadH }, nullptr, bl.heading });
        y += kHeadH;
        for (auto* t : bl.units) { out.push_back ({ { x, y, colW, kRowH }, t, bl.heading }); y += kRowH; }
        used += block;
    }
    return out;
}

void AddPanel::resized()
{
    const auto c = card();
    add.setBounds (c.getRight() - 20 - 130, c.getBottom() - 20 - 28, 130, 28);
    cancel.setBounds (add.getX() - 8 - 84, add.getY(), 84, 28);
    search.setBounds (c.getRight() - 20 - 260, c.getY() + 14, 260, 30);
}

void AddPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto c = card();
    raised (g, c.toFloat(), radiusPanel);
    g.setFont (titleFont());
    g.setColour (juce::Colours::white);
    g.drawText ("Add units", c.getX() + 20, c.getY() + 14, 300, 20, juce::Justification::centredLeft, false);
    g.setFont (helpFont());
    g.setColour (n7);
    g.setFont (font (12.5f));
    g.setColour (n8);
    g.drawText ("Tick one or several, then Add. They arrive on the board unconnected. Click a star to keep a unit in Favourites.", c.getX() + 20, c.getY() + 34, c.getWidth() - 320, 18, juce::Justification::centredLeft, true);

    const auto favs = favourites();
    for (auto& r : rows())
    {
        if (r.info == nullptr)
        {
            caption (g, r.heading, r.r.withTrimmedTop (10).withTrimmedLeft (2));
            dividerH (g, r.r.getX(), r.r.getRight(), r.r.getBottom() - 3);
            continue;
        }
        const bool on = std::find (ticked.begin(), ticked.end(), r.info->type) != ticked.end();
        if (on) { g.setColour (accent.withAlpha (0.07f)); g.fillRoundedRectangle (r.r.toFloat().reduced (0.0f, 1.0f), radiusControl); }
        const auto box = juce::Rectangle<float> ((float) r.r.getX() + 6.0f, (float) r.r.getCentreY() - 8.0f, 16.0f, 16.0f);
        sunken (g, box, radiusChip);
        if (on)
        {
            juce::Path t;
            t.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
            t.lineTo (box.getX() + 7.0f, box.getBottom() - 4.5f);
            t.lineTo (box.getRight() - 3.5f, box.getY() + 4.5f);
            g.setColour (accent);
            g.strokePath (t, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // (bigger and brighter than before: the names were hard to read)
        g.setFont (font (13.5f, Weight::SemiBold));
        g.setColour (juce::Colours::white.withAlpha (0.95f));
        g.drawText (r.info->name, r.r.getX() + 32, r.r.getY() + 2, r.r.getWidth() - 62, 17, juce::Justification::centredLeft, true);
        g.setFont (font (11.5f));
        g.setColour (n8);
        g.drawText (r.info->hint, r.r.getX() + 32, r.r.getY() + 18, r.r.getWidth() - 62, 14, juce::Justification::centredLeft, true);
        // the favourite star, at the end of the row
        {
            const bool fav = std::find (favs.begin(), favs.end(), r.info->type) != favs.end();
            juce::Path star;
            star.addStar (starIn (r.r).toFloat().getCentre(), 5, 3.4f, 8.0f);
            g.setColour (fav ? bandYellow : n5);
            if (fav) g.fillPath (star); else g.strokePath (star, juce::PathStrokeType (1.2f));
        }
    }

    // The unit under the mouse, pictured beside its row: to the right where there is room, otherwise to the left
    if (hovered != nullptr && preview)
        for (auto& r : rows())
            if (r.info == hovered)
            {
                const auto img = preview (*hovered);
                if (! img.isValid()) break;
                const float scale = juce::jmin (0.85f, 420.0f / (float) img.getWidth(), 330.0f / (float) img.getHeight());
                const int w = juce::roundToInt ((float) img.getWidth() * scale), h = juce::roundToInt ((float) img.getHeight() * scale);
                int x = r.r.getRight() + 6;
                if (x + w > c.getRight() - 8) x = r.r.getX() - w - 6;
                const int y = juce::jlimit (c.getY() + 8, juce::jmax (c.getY() + 8, c.getBottom() - h - 8), r.r.getCentreY() - h / 2);
                const juce::Rectangle<int> box (x, y, w, h);
                juce::DropShadow (juce::Colours::black.withAlpha (0.7f), 24, { 0, 6 }).drawForRectangle (g, box);
                g.setColour (windowBottom);
                g.fillRoundedRectangle (box.toFloat().expanded (6.0f), radiusCard);
                g.setColour (accent.withAlpha (0.35f));
                g.drawRoundedRectangle (box.toFloat().expanded (6.0f), radiusCard, 1.0f);
                g.setOpacity (1.0f);
                g.drawImage (img, box.toFloat());
                break;
            }
}

void AddPanel::mouseMove (const juce::MouseEvent& e)
{
    const Info* now = nullptr;
    for (auto& r : rows()) if (r.info != nullptr && r.r.contains (e.getPosition())) now = r.info;
    if (now != hovered) { hovered = now; repaint(); }
}

void AddPanel::mouseExit (const juce::MouseEvent&) { if (hovered != nullptr) { hovered = nullptr; repaint(); } }

void AddPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition())) { setVisible (false); return; }
    for (auto& r : rows())
        if (r.info != nullptr && r.r.contains (e.getPosition()))
        {
            if (starIn (r.r).contains (e.getPosition()))
            {
                const auto favs = favourites();
                setFavourite (r.info->type, std::find (favs.begin(), favs.end(), r.info->type) == favs.end());
                repaint();
                return;
            }
            auto it = std::find (ticked.begin(), ticked.end(), r.info->type);
            if (it == ticked.end()) ticked.push_back (r.info->type); else ticked.erase (it);
            add.setEnabled (! ticked.empty());
            add.setButtonText (ticked.size() > 1 ? "Add " + juce::String ((int) ticked.size()) + " to board" : "Add to board");
            repaint();
            return;
        }
}
} // namespace n2m

//==============================================================================
using namespace n2m;
using namespace n2m::look;

//==============================================================================
// The song page
SongPanel::SongPanel (Nebula2ModulationProcessor& p) : proc (p)
{
    for (auto* b : { &onOff, &fewer, &more, &barsButton, &allOn, &buildUp, &close }) addAndMakeVisible (b);
    close.onClick = [this] { setVisible (false); };
    onOff.onClick = [this] { setOn (! proc.rack.song.on.load()); };
    fewer.onClick = [this] { setSections (proc.rack.song.sections.load() - 1); };
    more.onClick = [this] { setSections (proc.rack.song.sections.load() + 1); };
    allOn.onClick = [this] { fill (0); };
    buildUp.onClick = [this] { fill (1); };
    barsButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("How long each section is");
        for (int v : { 1, 2, 4, 8, 16 }) menu.addItem (v, juce::String (v) + (v == 1 ? " bar" : " bars"), true, proc.rack.song.bars.load() == v);
        juce::Component::SafePointer<SongPanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&barsButton), [safe] (int r) { if (safe != nullptr && r > 0) safe->setBars (r); });
    };
    setVisible (false);
}

void SongPanel::open() { sync(); setVisible (true); toFront (true); resized(); repaint(); }
// (the panel is rebuilt from the rack each time it is drawn, so after Undo it only needs drawing again)
void SongPanel::sync()
{
    const bool on = proc.rack.song.on.load();
    onOff.setButtonText (on ? "Song page: on" : "Song page: off");
    onOff.setToggleState (on, juce::dontSendNotification);
    const int bars = proc.rack.song.bars.load();
    barsButton.setButtonText (juce::String (bars) + (bars == 1 ? " bar each" : " bars each"));
    fewer.setEnabled (proc.rack.song.sections.load() > 2);
    more.setEnabled (proc.rack.song.sections.load() < kSongSections);
    repaint();
}
void SongPanel::setOn (bool on) { proc.history.beforeSong(); proc.rack.song.on.store (on); sync(); }
void SongPanel::setBars (int n) { proc.history.beforeSong(); proc.rack.song.bars.store (juce::jlimit (1, 16, n)); sync(); }
void SongPanel::setSections (int n)
{
    n = juce::jlimit (2, kSongSections, n);
    if (n == proc.rack.song.sections.load()) return;
    proc.history.beforeSong();
    proc.rack.song.sections.store (n);
    sync();
}
void SongPanel::setCell (int tab, int section, bool on)
{
    if (tab < 0 || tab >= (int) proc.rack.boards.size() || section < 0 || section >= kSongSections) return;
    auto& plays = proc.rack.boards[(size_t) tab]->plays;
    const uint32_t bit = 1u << section, now = plays.load();
    plays.store (on ? (now | bit) : (now & ~bit));
    repaint();
}
void SongPanel::fill (int kind)
{
    proc.history.beforeSong();
    const int sections = proc.rack.song.sections.load(), count = (int) proc.rack.boards.size();
    for (int t = 0; t < count; ++t)
    {
        // Build up: the first sound plays from the start and one more joins each section; all play by the last
        const int from = kind == 1 ? juce::jmin (t, sections - 1) : 0;
        uint32_t bits = 0;
        for (int s = from; s < kSongSections; ++s) bits |= 1u << s;
        proc.rack.boards[(size_t) t]->plays.store (bits);
    }
    if (kind == 1 && ! proc.rack.song.on.load()) proc.rack.song.on.store (true);
    sync();
}

// ---- The timeline: bars along the top, a row per sound, and under a sound a row for each knob that moves ----
int SongPanel::contentH() const
{
    int h = 0;
    for (auto& b : proc.rack.boards) h += rowH + (int) b->autos.size() * laneH;
    return juce::jmax (rowH, h);
}
juce::Rectangle<int> SongPanel::card() const
{
    const int w = juce::jmin (getWidth() - 40, 1180), h = juce::jmin (getHeight() - 40, 136 + contentH() + 60);
    return juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
}
juce::Rectangle<int> SongPanel::rows() const { const auto c = card(); return { c.getX() + 20, c.getY() + 136, c.getWidth() - 40, c.getHeight() - 136 - 56 }; }
int SongPanel::timeX() const { return card().getX() + 20 + labelW; }
int SongPanel::timeW() const { return card().getWidth() - 40 - labelW; }
float SongPanel::barW() const { return (float) timeW() / (float) juce::jmax (1, proc.rack.songBars()); }
int SongPanel::rowY (int tab) const
{
    int y = rows().getY() - scroll;
    for (int t = 0; t < tab && t < (int) proc.rack.boards.size(); ++t) y += rowH + (int) proc.rack.boards[(size_t) t]->autos.size() * laneH;
    return y;
}
juce::Rectangle<int> SongPanel::cell (int tab, int section) const
{
    const float w = barW() * (float) proc.rack.song.bars.load();
    const int x0 = timeX() + juce::roundToInt (w * (float) section), x1 = timeX() + juce::roundToInt (w * (float) (section + 1));
    return { x0, rowY (tab), x1 - x0, rowH };
}
juce::Rectangle<int> SongPanel::laneArea (int tab, int lane) const { return { timeX(), rowY (tab) + rowH + lane * laneH, timeW(), laneH }; }
juce::Rectangle<int> SongPanel::knobChip (int tab) const { return { timeX() - 8 - 62, rowY (tab) + 4, 62, rowH - 8 }; }
juce::Rectangle<int> SongPanel::removeChip (int tab, int lane) const { return { timeX() - 8 - 20, laneArea (tab, lane).getCentreY() - 10, 20, 20 }; }
juce::Point<int> SongPanel::pointAt (int tab, int lane, float bar, float value) const
{
    const auto a = laneArea (tab, lane);
    return { a.getX() + juce::roundToInt (bar * barW()), a.getBottom() - 6 - juce::roundToInt (juce::jlimit (0.0f, 1.0f, value) * (float) (laneH - 12)) };
}
int SongPanel::pointNear (int tab, int lane, juce::Point<int> p) const
{
    if (tab < 0 || tab >= (int) proc.rack.boards.size() || lane < 0 || lane >= (int) proc.rack.boards[(size_t) tab]->autos.size()) return -1;
    auto& a = *proc.rack.boards[(size_t) tab]->autos[(size_t) lane];
    int best = -1; float bestD = 9.0f;
    for (int k = 0; k < a.count.load(); ++k)
    {
        const float d = pointAt (tab, lane, a.bar[(size_t) k].load(), a.val[(size_t) k].load()).toFloat().getDistanceFrom (p.toFloat());
        if (d < bestD) { bestD = d; best = k; }
    }
    return best;
}
// Half way along a stretch, on the line itself: the handle that bends it. None where the stretch is too short
// to grab or is flat (a flat line has nothing to bend).
juce::Point<int> SongPanel::bendHandle (int tab, int lane, int k) const
{
    auto& a = *proc.rack.boards[(size_t) tab]->autos[(size_t) lane];
    if (k < 0 || k + 1 >= a.count.load()) return { -1000, -1000 };
    const float b0 = a.bar[(size_t) k].load(), b1 = a.bar[(size_t) (k + 1)].load(), v0 = a.val[(size_t) k].load(), v1 = a.val[(size_t) (k + 1)].load();
    if ((b1 - b0) * barW() < 28.0f || std::abs (v1 - v0) < 0.02f) return { -1000, -1000 };
    return pointAt (tab, lane, 0.5f * (b0 + b1), v0 + (v1 - v0) * AutoLane::shape (0.5f, a.curve[(size_t) k].load()));
}
int SongPanel::bendNear (int tab, int lane, juce::Point<int> p) const
{
    if (tab < 0 || tab >= (int) proc.rack.boards.size() || lane < 0 || lane >= (int) proc.rack.boards[(size_t) tab]->autos.size()) return -1;
    const int n = proc.rack.boards[(size_t) tab]->autos[(size_t) lane]->count.load();
    for (int k = 0; k + 1 < n; ++k) if (bendHandle (tab, lane, k).getDistanceFrom (p) <= 8) return k;
    return -1;
}
// Double-click a bend handle: that stretch goes straight again
void SongPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int t = 0; t < (int) proc.rack.boards.size(); ++t)
        for (int l = 0; l < (int) proc.rack.boards[(size_t) t]->autos.size(); ++l)
        {
            const int k = bendNear (t, l, e.getPosition());
            if (k < 0) continue;
            proc.history.before (*proc.rack.boards[(size_t) t]);
            proc.rack.boards[(size_t) t]->autos[(size_t) l]->bend (k, 0.0f);
            proc.history.discardIfUnchanged();
            repaint();
            return;
        }
}
bool SongPanel::hit (juce::Point<int> p, int& tab, int& section) const
{
    if (! rows().contains (p)) return false;
    const int sections = proc.rack.song.sections.load();
    for (int t = 0; t < (int) proc.rack.boards.size(); ++t)
        for (int s = 0; s < sections; ++s)
            if (cell (t, s).contains (p)) { tab = t; section = s; return true; }
    return false;
}
void SongPanel::resized()
{
    const auto c = card();
    const int y = c.getY() + 62;
    onOff.setBounds (c.getX() + 20, y, 140, 28);
    fewer.setBounds (c.getX() + 252, y, 28, 28);
    more.setBounds (c.getX() + 316, y, 28, 28);
    barsButton.setBounds (c.getX() + 360, y, 104, 28);
    buildUp.setBounds (c.getRight() - 20 - 90, y, 90, 28);
    allOn.setBounds (buildUp.getX() - 8 - 76, y, 76, 28);
    close.setBounds (c.getRight() - 20 - 84, c.getBottom() - 14 - 28, 84, 28);
    scroll = juce::jlimit (0, juce::jmax (0, contentH() - rows().getHeight()), scroll);
}

int SongPanel::addLane (int tab, int uid, int param)
{
    if (tab < 0 || tab >= (int) proc.rack.boards.size()) return -1;
    auto& b = *proc.rack.boards[(size_t) tab];
    proc.history.before (b);
    const int lane = proc.rack.addAuto (b, uid, param);
    proc.history.discardIfUnchanged();
    resized(); repaint();
    return lane;
}
void SongPanel::removeLane (int tab, int lane)
{
    if (tab < 0 || tab >= (int) proc.rack.boards.size()) return;
    auto& b = *proc.rack.boards[(size_t) tab];
    proc.history.before (b);
    proc.rack.removeAuto (b, lane);
    proc.history.discardIfUnchanged();
    resized(); repaint();
}
// + Knob: every knob of that sound, unit by unit. Knobs that already have a line are ticked.
void SongPanel::knobMenu (int tab)
{
    if (tab < 0 || tab >= (int) proc.rack.boards.size()) return;
    auto& b = *proc.rack.boards[(size_t) tab];
    juce::PopupMenu m;
    m.addSectionHeader ("Move a knob of " + juce::String (b.name) + " through the song");
    std::map<std::string, int> seen;
    for (auto& mod : b.modules)
    {
        juce::PopupMenu sub;
        for (int k = 0; k < (int) mod->info->params.size(); ++k)
            if (mod->info->params[(size_t) k].choices.empty())
                sub.addItem (mod->uid * 100 + k + 1, mod->info->params[(size_t) k].name, true, b.autoIndex (mod->uid, k) >= 0);
        if (sub.getNumItems() == 0) continue;
        const int nth = ++seen[mod->info->type];
        m.addSubMenu (juce::String (mod->info->name) + (nth > 1 ? " " + juce::String (nth) : juce::String()), sub);
    }
    juce::Component::SafePointer<SongPanel> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (knobChip (tab))), [safe, tab] (int r)
    {
        if (safe != nullptr && r > 0) safe->addLane (tab, r / 100, r % 100 - 1);
    });
}

void SongPanel::mouseDown (const juce::MouseEvent& e)
{
    drag = {};
    const auto p = e.getPosition();
    if (! card().contains (p)) { setVisible (false); return; }
    if (! rows().contains (p)) return;
    for (int t = 0; t < (int) proc.rack.boards.size(); ++t)
    {
        auto& b = *proc.rack.boards[(size_t) t];
        if (knobChip (t).contains (p)) { knobMenu (t); return; }
        for (int l = 0; l < (int) b.autos.size(); ++l)
        {
            if (removeChip (t, l).contains (p)) { removeLane (t, l); return; }
            const auto area = laneArea (t, l);
            if (! area.expanded (4, 0).contains (p)) continue;
            auto& a = *b.autos[(size_t) l];
            proc.history.before (b);
            int point = pointNear (t, l, p);
            if (e.mods.isPopupMenu())
            {
                // right-click a point: it goes (a line keeps at least one)
                if (point >= 0 && a.count.load() > 1) a.remove (point);
                proc.history.discardIfUnchanged();
                repaint();
                return;
            }
            // the handle in the middle of a stretch bends it
            if (point < 0) { const int k = bendNear (t, l, p); if (k >= 0) { drag = { 3, t, l, k }; repaint(); return; } }
            if (point < 0)
            {
                // a click on the line's row puts a new point there, already in the hand
                const float bar = juce::jlimit (0.0f, (float) proc.rack.songBars(), (float) (p.x - area.getX()) / barW());
                const float value = juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - 6 - p.y) / (float) (laneH - 12));
                point = a.add (e.mods.isShiftDown() ? bar : std::round (bar * 4.0f) / 4.0f, value);
            }
            if (point >= 0) drag = { 2, t, l, point };
            repaint();
            return;
        }
    }
    int t = 0, s = 0;
    if (! hit (p, t, s)) return;
    proc.history.beforeSong();
    paintTo = ((proc.rack.boards[(size_t) t]->plays.load() >> s) & 1u) == 0;
    drag.kind = 1;
    setCell (t, s, paintTo);
}
void SongPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.kind == 1)
    {
        int t = 0, s = 0;
        if (hit (e.getPosition(), t, s)) setCell (t, s, paintTo);
    }
    if (drag.kind == 3 && drag.tab < (int) proc.rack.boards.size() && drag.lane < (int) proc.rack.boards[(size_t) drag.tab]->autos.size())
    {
        // Bending: the middle of the stretch follows the mouse up and down
        auto& a = *proc.rack.boards[(size_t) drag.tab]->autos[(size_t) drag.lane];
        if (drag.point + 1 < a.count.load())
        {
            const auto area = laneArea (drag.tab, drag.lane);
            const float v0 = a.val[(size_t) drag.point].load(), v1 = a.val[(size_t) (drag.point + 1)].load();
            const float value = (float) (area.getBottom() - 6 - e.y) / (float) (laneH - 12);
            if (std::abs (v1 - v0) > 0.0001f) a.bend (drag.point, AutoLane::curveFor ((value - v0) / (v1 - v0)));
        }
        repaint();
        return;
    }
    if (drag.kind != 2 || drag.tab >= (int) proc.rack.boards.size() || drag.lane >= (int) proc.rack.boards[(size_t) drag.tab]->autos.size()) return;
    // Dragging a point: sideways moves it through the song (to the nearest quarter bar; Shift for anywhere),
    // up and down moves the knob
    const auto area = laneArea (drag.tab, drag.lane);
    const float bar = juce::jlimit (0.0f, (float) proc.rack.songBars(), (float) (e.x - area.getX()) / barW());
    const float value = (float) (area.getBottom() - 6 - e.y) / (float) (laneH - 12);
    proc.rack.boards[(size_t) drag.tab]->autos[(size_t) drag.lane]->move (drag.point, e.mods.isShiftDown() ? bar : std::round (bar * 4.0f) / 4.0f, value);
    repaint();
}
void SongPanel::mouseUp (const juce::MouseEvent&)
{
    if (drag.kind == 2 || drag.kind == 3) proc.history.discardIfUnchanged();
    drag = {};
}
void SongPanel::mouseMove (const juce::MouseEvent& e) { if (rows().contains (e.getPosition())) repaint (rows()); }
void SongPanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    scroll = juce::jlimit (0, juce::jmax (0, contentH() - rows().getHeight()), scroll - juce::roundToInt (w.deltaY * 200.0f));
    repaint();
}
void SongPanel::tick()
{
    if (! isVisible()) return;
    const int now = proc.rack.song.on.load() && proc.isPlaying() ? proc.rack.song.now.load() : -1;
    const float bar = proc.isPlaying() ? proc.rack.song.bar.load() : -1.0f;
    if (now != shownSection || std::abs (bar - shownBar) > 0.02f) { shownSection = now; shownBar = bar; repaint(); }
}
void SongPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto c = card();
    raised (g, c.toFloat(), radiusPanel);
    const bool on = proc.rack.song.on.load();
    const int sections = proc.rack.song.sections.load(), bars = proc.rack.song.bars.load(), total = proc.rack.songBars();
    g.setFont (titleFont());
    g.setColour (juce::Colours::white);
    g.drawText ("Song", c.getX() + 20, c.getY() + 14, 300, 20, juce::Justification::centredLeft, false);
    g.setFont (helpFont());
    g.setColour (n7);
    g.drawText (on ? "A filled block means that sound plays in that section: click or drag to change. + Knob gives a knob a line: click the line to add a point, drag points, right-click one to remove it. Drag a diamond to bend the line."
                   : "Sections are off: every sound plays all the time. + Knob gives a knob a line through the song: click the line to add a point, drag points, right-click one to remove it. Drag a diamond to bend the line.",
                c.getX() + 20, c.getY() + 34, c.getWidth() - 40, 16, juce::Justification::centredLeft, true);
    caption (g, "Sections", { c.getX() + 180, c.getY() + 62, 68, 28 });
    g.setFont (controlFont());
    g.setColour (n8);
    g.drawText (juce::String (sections), fewer.getRight(), fewer.getY(), more.getX() - fewer.getRight(), 28, juce::Justification::centred, false);
    g.setFont (helpFont());
    g.setColour (n7);
    g.drawText ("The whole song: " + juce::String (total) + " bars, then round again", barsButton.getRight() + 14, barsButton.getY(), 280, 28, juce::Justification::centredLeft, false);

    // The ruler: section numbers, and bar numbers under them
    const int x0 = timeX(), ry = c.getY() + 100;
    const float bw = barW();
    const int every = bw >= 22.0f ? 1 : bw >= 11.0f ? 2 : 4;
    for (int s = 0; s < sections; ++s)
    {
        const auto r = cell (0, s).withY (ry).withHeight (16);
        g.setFont (controlFont());
        g.setColour (s == shownSection ? noteAccent : n8);
        g.drawText (juce::String (s + 1), r.withTrimmedLeft (4), juce::Justification::centredLeft, false);
    }
    for (int b = 0; b < total; ++b)
    {
        const float x = (float) x0 + (float) b * bw;
        const bool edge = b % bars == 0;
        g.setColour (juce::Colours::white.withAlpha (edge ? 0.28f : 0.10f));
        g.fillRect (x, (float) ry + (edge ? 2.0f : 18.0f), 1.0f, edge ? 30.0f : 14.0f);
        if (b % every != 0) continue;
        g.setFont (font (9.0f));
        g.setColour (n7);
        g.drawText (juce::String (b + 1), juce::roundToInt (x) + 3, ry + 18, 30, 12, juce::Justification::centredLeft, false);
    }

    const auto view = rows();
    const auto mouse = getMouseXYRelative();
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (view);
        for (int t = 0; t < (int) proc.rack.boards.size(); ++t)
        {
            auto& b = *proc.rack.boards[(size_t) t];
            const auto col = boardColour (b.colour);
            const int y = rowY (t);
            if (y > view.getBottom()) break;
            g.setColour (col);
            g.fillEllipse ((float) c.getX() + 22.0f, (float) y + (float) rowH * 0.5f - 4.0f, 8.0f, 8.0f);
            g.setFont (controlFont());
            g.setColour (n8);
            g.drawText (b.name, c.getX() + 38, y, labelW - 38 - 78, rowH, juce::Justification::centredLeft, true);
            {
                const auto chip = knobChip (t);
                raised (g, chip.toFloat(), radiusChip, chip.contains (mouse));
                g.setFont (font (10.5f, Weight::Medium));
                g.setColour (n8);
                g.drawText ("+ Knob", chip, juce::Justification::centred, false);
            }
            const uint32_t plays = b.plays.load();
            for (int s = 0; s < sections; ++s)
            {
                const auto r = cell (t, s).toFloat().reduced (1.5f, 3.0f);
                if (((plays >> s) & 1u) != 0)
                {
                    g.setColour (col.withAlpha (on ? (s == shownSection ? 1.0f : 0.78f) : 0.28f));
                    g.fillRoundedRectangle (r, radiusChip);
                }
                else sunken (g, r, radiusChip);
            }
            // its knob lines
            for (int l = 0; l < (int) b.autos.size(); ++l)
            {
                auto& a = *b.autos[(size_t) l];
                const auto area = laneArea (t, l);
                auto* mod = b.find (a.uid);
                sunken (g, area.toFloat().reduced (0.0f, 2.0f), radiusChip);
                g.setFont (font (10.5f, Weight::SemiBold, 0.2f));
                g.setColour (n8);
                g.drawFittedText (mod != nullptr ? juce::String (mod->info->name).toUpperCase() : juce::String ("?"), c.getX() + 38, area.getY() + 10, labelW - 38 - 34, 14, juce::Justification::centredLeft, 1, 0.7f);
                g.setFont (font (10.5f));
                g.setColour (n7);
                const bool ok = mod != nullptr && a.param >= 0 && a.param < (int) mod->info->params.size();
                const float here = shownBar >= 0.0f ? a.at (shownBar) : (ok ? paramToNorm (mod->info->params[(size_t) a.param], mod->P (a.param)) : 0.0f);
                if (ok) g.drawFittedText (juce::String (mod->info->params[(size_t) a.param].name) + "   " + minus (paramText (mod->info->params[(size_t) a.param], paramFromNorm (mod->info->params[(size_t) a.param], here))),
                                          c.getX() + 38, area.getY() + 26, labelW - 38 - 34, 14, juce::Justification::centredLeft, 1, 0.7f);
                {
                    const auto chip = removeChip (t, l);
                    if (chip.contains (mouse)) raised (g, chip.toFloat(), radiusChip, true);
                    g.setFont (font (13.0f));
                    g.setColour (chip.contains (mouse) ? juce::Colours::white : n7);
                    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")), chip, juce::Justification::centred, false);
                }
                // the line: held flat to both edges, straight between points, filled underneath
                const int n = a.count.load();
                if (n <= 0) continue;
                juce::Path line;
                line.startNewSubPath ((float) area.getX(), (float) pointAt (t, l, 0.0f, a.val[0].load()).y);
                for (int k = 0; k < n; ++k)
                {
                    // a bent stretch is drawn as the curve the knob will really follow
                    if (k > 0 && a.curve[(size_t) (k - 1)].load() != 0.0f)
                    {
                        const float b0 = a.bar[(size_t) (k - 1)].load(), b1 = a.bar[(size_t) k].load(), v0 = a.val[(size_t) (k - 1)].load(), v1 = a.val[(size_t) k].load(), cv = a.curve[(size_t) (k - 1)].load();
                        const int steps = juce::jlimit (8, 96, juce::roundToInt ((b1 - b0) * barW() / 2.0f));
                        // (worked out to a fraction of a pixel, so the curve is smooth rather than stepped)
                        for (int s = 1; s < steps; ++s)
                        {
                            const float u = (float) s / (float) steps;
                            line.lineTo ((float) area.getX() + (b0 + (b1 - b0) * u) * barW(), (float) area.getBottom() - 6.0f - juce::jlimit (0.0f, 1.0f, v0 + (v1 - v0) * AutoLane::shape (u, cv)) * (float) (laneH - 12));
                        }
                    }
                    line.lineTo (pointAt (t, l, a.bar[(size_t) k].load(), a.val[(size_t) k].load()).toFloat());
                }
                line.lineTo ((float) area.getRight(), (float) pointAt (t, l, 0.0f, a.val[(size_t) (n - 1)].load()).y);
                juce::Path fill (line);
                fill.lineTo ((float) area.getRight(), (float) area.getBottom() - 3.0f); fill.lineTo ((float) area.getX(), (float) area.getBottom() - 3.0f); fill.closeSubPath();
                g.setColour (col.withAlpha (0.14f));
                g.fillPath (fill);
                g.setColour (col);
                g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                const int closest = area.expanded (4, 0).contains (mouse) ? pointNear (t, l, mouse) : -1;
                // bend handles: a small diamond half way along each stretch
                {
                    const int over = closest < 0 && area.expanded (4, 0).contains (mouse) ? bendNear (t, l, mouse) : -1;
                    for (int k = 0; k + 1 < n; ++k)
                    {
                        const auto h = bendHandle (t, l, k).toFloat();
                        if (h.x < -500.0f) continue;
                        const bool hot = k == over || (drag.kind == 3 && drag.tab == t && drag.lane == l && drag.point == k);
                        const float r = hot ? 5.5f : 4.0f;
                        juce::Path d; d.startNewSubPath (h.x, h.y - r); d.lineTo (h.x + r, h.y); d.lineTo (h.x, h.y + r); d.lineTo (h.x - r, h.y); d.closeSubPath();
                        g.setColour (sunkenLo); g.fillPath (d);
                        g.setColour (hot ? juce::Colours::white : col.withAlpha (a.curve[(size_t) k].load() != 0.0f ? 1.0f : 0.55f));
                        g.strokePath (d, juce::PathStrokeType (1.4f));
                    }
                }
                for (int k = 0; k < n; ++k)
                {
                    const auto pt = pointAt (t, l, a.bar[(size_t) k].load(), a.val[(size_t) k].load()).toFloat();
                    const bool hot = k == closest || (drag.kind == 2 && drag.tab == t && drag.lane == l && drag.point == k);
                    g.setColour (sunkenLo);
                    g.fillEllipse (juce::Rectangle<float> (hot ? 11.0f : 8.0f, hot ? 11.0f : 8.0f).withCentre (pt));
                    g.setColour (hot ? juce::Colours::white : col);
                    g.drawEllipse (juce::Rectangle<float> (hot ? 11.0f : 8.0f, hot ? 11.0f : 8.0f).withCentre (pt), 1.8f);
                }
            }
        }
        // where the song is now
        if (shownBar >= 0.0f)
        {
            g.setColour (noteAccent);
            g.fillRect ((float) x0 + shownBar * bw - 0.75f, (float) view.getY(), 1.5f, (float) juce::jmin (view.getHeight(), contentH()));
        }
    }
    if (shownBar >= 0.0f)
    {
        juce::Path head;
        const float x = (float) x0 + shownBar * bw;
        head.addTriangle (x - 5.0f, (float) ry + 22.0f, x + 5.0f, (float) ry + 22.0f, x, (float) ry + 32.0f);
        g.setColour (noteAccent);
        g.fillPath (head);
    }
}

Nebula2ModulationEditor::Nebula2ModulationEditor (Nebula2ModulationProcessor& p)
    : AudioProcessorEditor (p), songPanel (p), bigPanel (p), proc (p), styleBar (p), tabs (p), boardView (p), allView (p), mixer (p)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    for (auto* b : { &play, &addUnitsButton, &tidyButton, &undoButton, &redoButton }) addAndMakeVisible (b);
    undoButton.onClick = [this] { stepHistory (false); };
    redoButton.onClick = [this] { stepHistory (true); };
    play.onClick = [this] { if (! proc.hostPlaying.load()) proc.preview.store (! proc.preview.load()); };
    addUnitsButton.onClick = [this] { addPanel.open(); };
    tidyButton.onClick = [this] { if (auto* b = current()) { proc.history.before (*b); tidy (*b); proc.history.discardIfUnchanged(); refreshBoards(); if (! proc.showAll.load()) viewport.setViewPosition (0, 0); } };

    for (int i = 0; i < 12; ++i) keyBox.addItem (kNoteNames[i], i + 1);
    for (int i = 0; i < (int) scales().size(); ++i) scaleBox.addItem (scales()[(size_t) i].name, i + 1);
    addAndMakeVisible (keyBox); addAndMakeVisible (scaleBox);
    // The key is shared by every tab. Changing it moves the notes and the chords with it.
    keyBox.onChange = [this] { proc.history.beforeChords(); proc.rack.setKey (keyBox.getSelectedId() - 1, proc.rack.scale.load()); proc.chordsVersion.fetch_add (1); boardView.repaint(); allView.repaintAll(); };
    scaleBox.onChange = [this] { proc.history.beforeChords(); proc.rack.setKey (proc.rack.root.load(), scaleBox.getSelectedId() - 1); proc.chordsVersion.fetch_add (1); boardView.repaint(); allView.repaintAll(); };

    addAndMakeVisible (styleBar);
    // Each of these changes many boards at once: let go of what is on screen, do it, then draw it all again
    styleBar.onMake = [this] (const juce::String& style) { hostOverlay.close(); selectTab (-1); proc.makeTrack (style, proc.addMode.load()); afterWholeChange (proc.selectedTab.load()); };
    styleBar.onVariation = [this] { selectTab (-1); proc.variation(); afterWholeChange (proc.selectedTab.load()); };
    styleBar.onRandomKnobs = [this] { selectTab (-1); proc.randomiseKnobs(); afterWholeChange (proc.selectedTab.load()); };
    styleBar.onPart = [this] (const juce::String& kind) { hostOverlay.close(); selectTab (-1); afterWholeChange (proc.remakePart (kind, proc.addMode.load())); };

    addAndMakeVisible (tabs);
    tabs.onSelect = [this] (int i) { selectTab (i); };
    tabs.onShow = [this] (int i) { reshow (i); };
    tabs.onAll = [this] { if (proc.showAll.load()) selectTab (proc.selectedTab.load()); else showAllSounds(); };
    tabs.onChanged = [this] { tabs.repaint(); mixer.repaint(); styleBar.repaint(); };

    addAndMakeVisible (viewport);
    boardHolder.addAndMakeVisible (boardView);
    boardHolder.addChildComponent (allView);
    viewport.setViewedComponent (&boardHolder, false);
    configureBoard (boardView);
    allView.configure = [this] (BoardView& v) { configureBoard (v); };
    allView.onZoomStep = [this] (int d) { stepZoom (d); };
    // Clicking a sound on the All view makes it the selected tab, for Add units, Change sound and Tidy
    allView.onFocus = [this] (int i) { proc.selectedTab.store (i); tabs.showSelected(); tabs.repaint(); mixer.repaint(); allView.repaint(); };
    allView.onOpen = [this] (int i) { selectTab (i); tabs.showSelected(); };
    for (auto* b : { &zoomOut, &zoomIn }) addAndMakeVisible (b);
    zoomOut.onClick = [this] { stepZoom (-1); };
    zoomIn.onClick = [this] { stepZoom (1); };
    addPanel.preview = [this] (const Info& i) { return previewOf (i); };
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (songButton);
    addAndMakeVisible (soundButton);
    soundButton.onClick = [this] { tabs.soundMenu (proc.selectedTab.load(), &soundButton); };
    soundButton.setToggleState (true, juce::dontSendNotification);      // drawn lit, so it stands out on the board
    songButton.onClick = [this] { songPanel.open(); };

    addAndMakeVisible (mixer);
    mixer.onSelect = [this] (int i) { reshow (i); tabs.showSelected(); };
    mixer.onResize = [this] { resized(); repaint(); };
    // The desk's own tab, sitting on its top edge above the master: opens it up and closes it again
    addAndMakeVisible (deskButton);
    deskButton.onClick = [this] { mixer.setTall (! proc.mixerTall.load()); };
    // Beside it: what the desk mixes. Sounds = one channel per sound; Inside = the levels inside the selected sound
    for (auto* b : { &deskSounds, &deskInside }) addAndMakeVisible (b);
    deskSounds.onClick = [this] { mixer.setInside (false); };
    deskInside.onClick = [this] { mixer.setInside (true); };
    mixer.boardFor = [this] { return shownBoard(); };
    mixer.onShowUnit = [this] (int uid) { showUnit (uid); };

    addChildComponent (hostOverlay);
    // A unit's bigger view. What it changes shows on the unit underneath; Undo, Redo and Play are on it too,
    // because it covers the ones along the top.
    addChildComponent (bigPanel);
    bigPanel.onEdited = [this] { boardView.repaintAll(); allView.repaintAll(); };
    bigPanel.onUndo = [this] (bool redo) { stepHistory (redo); };
    bigPanel.onPlay = [this] { if (! proc.hostPlaying.load()) proc.preview.store (! proc.preview.load()); };

    addChildComponent (songPanel);
    addChildComponent (addPanel);
    addPanel.onAdd = [this] (const std::vector<std::string>& types) { addUnits (types); };

    for (auto* b : { &focusButton, &fullButton }) addAndMakeVisible (b);
    focusButton.onClick = [this] { setFocus (! proc.focusView.load()); };
    fullButton.onClick = [this] { setBigWindow (! proc.bigWindow.load()); };
    setSize (windowW, windowH);
    applyWindowSize();
    stepZoom (0);
    timerCallback();
    startTimerHz (30);
}

Nebula2ModulationEditor::~Nebula2ModulationEditor()
{
    stopTimer();
    viewport.setViewedComponent (nullptr, false);
    previews.clear();
    setLookAndFeel (nullptr);
}

Board* Nebula2ModulationEditor::current() const
{
    const int i = proc.selectedTab.load();
    return i >= 0 && i < (int) proc.rack.boards.size() ? proc.rack.boards[(size_t) i].get() : nullptr;
}

void Nebula2ModulationEditor::configureBoard (BoardView& v)
{
    v.onZoomStep = [this] (int d) { stepZoom (d); };
    v.onAddMenu = [this] { addPanel.open(); };
    v.onAddTypes = [this] (const std::vector<std::string>& types) { addUnits (types); };
    v.onOpenPlugin = [this, &v] (HostedModule& m) { if (v.board != nullptr) hostOverlay.open (m, v.board->name); };
    v.onExpand = [this] (Board& b, Module& m) { bigPanel.open (b, m); };
    v.onRebuilt = [this] { bigPanel.check(); };          // a unit was deleted, or Undo took it away: its bigger view closes
    v.onDrawKnob = [this] (Board& b, int uid, int param)
    {
        for (int t = 0; t < (int) proc.rack.boards.size(); ++t)
            if (proc.rack.boards[(size_t) t].get() == &b) { songPanel.open(); songPanel.addLane (t, uid, param); boardView.repaintAll(); allView.repaintAll(); }
    };
}

void Nebula2ModulationEditor::letGo() { boardView.setBoard (nullptr); allView.clear(); }

// Draws the boards for whichever view is up: every sound on one board, or the selected tab alone
void Nebula2ModulationEditor::show()
{
    proc.selectedTab.store (juce::jlimit (0, juce::jmax (0, (int) proc.rack.boards.size() - 1), proc.selectedTab.load()));
    const bool all = proc.showAll.load();
    boardView.setVisible (! all);
    allView.setVisible (all);
    if (all) { boardView.setBoard (nullptr); allView.rebuild(); }
    else { allView.clear(); boardView.setBoard (current()); }
    tabs.repaint(); mixer.repaint(); styleBar.repaint();
    repaint (0, 0, getWidth(), topbarH);
    bigPanel.check();                // the unit a bigger view is showing may have gone with the change
}

void Nebula2ModulationEditor::selectTab (int index)
{
    // (letting go of the boards means a sound is about to be changed, replaced or deleted: a bigger view closes.
    // Undo and Redo let go without closing it, and it stays if its unit is still there afterwards.)
    if (index < 0) { bigPanel.close(); letGo(); return; }
    proc.showAll.store (false);
    proc.selectedTab.store (index);
    show();
    viewport.setViewPosition (0, 0);
}

void Nebula2ModulationEditor::showAllSounds()
{
    proc.showAll.store (true);
    show();
    viewport.setViewPosition (0, 0);
}

void Nebula2ModulationEditor::reshow (int index)
{
    if (index < 0) { bigPanel.close(); letGo(); return; }
    proc.selectedTab.store (index);
    const auto pos = viewport.getViewPosition();
    show();
    if (proc.showAll.load()) { viewport.setViewPosition (pos); scrollToTab (proc.selectedTab.load()); }
    else viewport.setViewPosition (0, 0);
}

// On the All view: bring a sound into view if it isn't
void Nebula2ModulationEditor::scrollToTab (int index)
{
    if (! proc.showAll.load()) return;
    const float z = allView.zoomNow();
    const auto a = allView.areaOf (index).toFloat() * z;
    const auto seen = viewport.getViewArea().toFloat();
    if (a.isEmpty() || seen.contains (a)) return;
    viewport.setViewPosition (juce::jmax (0, juce::roundToInt (a.getX() - 20.0f)), juce::jmax (0, juce::roundToInt (a.getY() - 20.0f)));
}

BoardView* Nebula2ModulationEditor::shownBoard() { return proc.showAll.load() ? allView.viewOf (proc.selectedTab.load()) : &boardView; }

// Bring one unit of the selected sound into view (if it isn't) and outline it for a moment
void Nebula2ModulationEditor::showUnit (int uid)
{
    auto* view = shownBoard();
    auto* comp = view != nullptr ? view->compFor (uid) : nullptr;
    if (comp == nullptr) return;
    const auto a = boardHolder.getLocalArea (comp, comp->getLocalBounds());
    if (! viewport.getViewArea().contains (a))
        viewport.setViewPosition (juce::jmax (0, a.getCentreX() - viewport.getViewWidth() / 2), juce::jmax (0, a.getCentreY() - viewport.getViewHeight() / 2));
    view->flash (uid);
}

void Nebula2ModulationEditor::refreshBoards()
{
    if (! proc.showAll.load()) { boardView.rebuild(); return; }
    if (auto* v = allView.viewOf (proc.selectedTab.load())) v->rebuild();
}

float Nebula2ModulationEditor::zoomInUse() const { return proc.showAll.load() ? allView.zoomNow() : proc.zoom.load(); }
juce::Rectangle<int> Nebula2ModulationEditor::zoomLabel() const { return { zoomOut.getRight(), 0, zoomIn.getX() - zoomOut.getRight(), topbarH }; }

// Clicking the zoom figure: on the All view, fit everything in; on a tab, back to full size
void Nebula2ModulationEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! zoomLabel().contains (e.getPosition())) return;
    if (proc.showAll.load()) { proc.zoomAll.store (0.0f); allView.layout(); viewport.setViewPosition (0, 0); }
    else { proc.zoom.store (1.0f); boardView.fitToContent(); }
    stepZoom (0);
}

void Nebula2ModulationEditor::afterWholeChange (int showTab)
{
    reshow (juce::jmax (0, showTab));
    keyBox.setSelectedId (proc.rack.root.load() + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (proc.rack.scale.load() + 1, juce::dontSendNotification);
    proc.chordsVersion.fetch_add (1);
    tabs.showSelected();
    styleBar.repaint(); tabs.repaint(); mixer.repaint(); repaint();
    if (songPanel.isVisible()) songPanel.open();
}

// Zoom steps: small enough to see a whole busy board, up to a little larger than life
void Nebula2ModulationEditor::stepZoom (int direction)
{
    static const float levels[10] = { 0.2f, 0.25f, 0.33f, 0.4f, 0.5f, 0.67f, 0.8f, 1.0f, 1.25f, 1.5f };
    const bool all = proc.showAll.load();
    const int lowest = all ? 0 : 4;                 // one sound alone never needs to be smaller than half size
    const float before = zoomInUse();
    if (direction != 0)
    {
        // the next level up or down from where it is now (the All view's "fit" can sit between two levels)
        int at = direction > 0 ? 9 : lowest;
        if (direction > 0) { for (int i = 9; i >= lowest; --i) if (levels[i] > before + 0.011f) at = i; }
        else for (int i = lowest; i <= 9; ++i) if (levels[i] < before - 0.011f) at = i;
        (all ? proc.zoomAll : proc.zoom).store (levels[at]);
    }
    // Keep the same spot of the board at the top left while the size changes
    const auto pos = viewport.getViewPosition();
    if (all) allView.layout(); else boardView.fitToContent();
    const float now = zoomInUse();
    viewport.setViewPosition (juce::roundToInt ((float) pos.x / before * now), juce::roundToInt ((float) pos.y / before * now));
    zoomOut.setEnabled (now > levels[lowest] + 0.011f); zoomIn.setEnabled (now < levels[9] - 0.011f);
    repaint (0, 0, getWidth(), topbarH);
}

// A picture of a unit as it will look on a board, for the Add units menu. Made once per kind and kept.
juce::Image Nebula2ModulationEditor::previewOf (const Info& info)
{
    auto it = previews.find (info.type);
    if (it != previews.end()) return it->second;
    Board temp;
    temp.colour = current() != nullptr ? current()->colour : 0;
    temp.type = current() != nullptr ? current()->type : "empty";
    juce::Image img;
    if (auto* m = temp.add (info.type, 0, 0, proc.rack.sr))
    {
        m->live = true;
        if (info.seq == Seq::Drum) setDrum (*m, drumIdeas (std::string (info.type) == "gate" ? "gate" : "kick")[0].pat);
        if (info.seq == Seq::Note) setNotes (*m, noteIdeas ("lead")[0].gates, noteIdeas ("lead")[0].deg);
        if (info.seq == Seq::Value) setValues (*m, valueIdeas()[2].v);
        ModuleComp comp (boardView, proc, temp, *m);
        comp.setAlpha (1.0f);
        img = comp.createComponentSnapshot (comp.getLocalBounds(), true, 1.0f);
    }
    previews[info.type] = img;
    return img;
}

// Undo or redo one step, then redraw from what the rack now holds
void Nebula2ModulationEditor::stepHistory (bool redo)
{
    if (redo ? ! proc.history.canRedo() : ! proc.history.canUndo()) return;
    hostOverlay.close();
    letGo();                                 // the boards on screen may be about to change or go
    proc.history.showTab = -1;
    if (redo) proc.history.redo(); else proc.history.undo();
    if (proc.history.showTab >= 0) proc.selectedTab.store (proc.history.showTab);
    reshow (juce::jlimit (0, juce::jmax (0, (int) proc.rack.boards.size() - 1), proc.selectedTab.load()));
    keyBox.setSelectedId (proc.rack.root.load() + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (proc.rack.scale.load() + 1, juce::dontSendNotification);
    proc.chordsVersion.fetch_add (1);
    tabs.showSelected();
    styleBar.repaint(); tabs.repaint(); mixer.repaint(); repaint();
    if (songPanel.isVisible()) songPanel.open();
}

void Nebula2ModulationEditor::addUnits (const std::vector<std::string>& types)
{
    auto* b = current();
    if (b == nullptr || b->frozen.load()) return;         // a frozen sound is locked
    proc.history.before (*b);
    // New units go in a row under everything already on the board, unconnected
    int bottom = 0;
    for (auto& m : b->modules) bottom = juce::jmax (bottom, (int) m->y + lay::height (*m->info));
    int x = 16;
    const int y = bottom + 18;
    for (auto& t : types)
        if (auto* m = proc.rack.addModule (*b, t, (float) x, (float) y))
            x += lay::width (*m->info) + 6;
    refreshBoards();
    if (! proc.showAll.load()) viewport.setViewPosition (0, juce::jmax (0, juce::roundToInt ((float) (y - 60) * proc.zoom.load())));
    else scrollToTab (proc.selectedTab.load());
}

void Nebula2ModulationEditor::timerCallback()
{
    const int v = proc.stateVersion.load();
    if (v != seenVersion)
    {
        // First open, or a project load replaced the rack
        seenVersion = v;
        keyBox.setSelectedId (proc.rack.root.load() + 1, juce::dontSendNotification);
        scaleBox.setSelectedId (proc.rack.scale.load() + 1, juce::dontSendNotification);
        letGo();
        show();
        viewport.setViewPosition (0, 0);
        stepZoom (0);                    // a loaded project brings its zoom with it
        applyWindowSize();               // and its window size, Focus, and whether its mixing desk was opened up
        tabs.showSelected();
        repaint();
    }
    // Choosing a mood on the Chord generator can change the scale: keep the box at the top in step
    if (scaleBox.getSelectedId() != proc.rack.scale.load() + 1)
    {
        scaleBox.setSelectedId (proc.rack.scale.load() + 1, juce::dontSendNotification);
        boardView.repaint(); allView.repaintAll();
    }
    undoButton.setEnabled (proc.history.canUndo());
    redoButton.setEnabled (proc.history.canRedo());
    const bool host = proc.hostPlaying.load(), on = proc.isPlaying();
    const juce::String text = host ? "With Cubase" : on ? "Stop" : "Play";
    if (play.getButtonText() != text || play.getToggleState() != on) { play.setButtonText (text); play.setToggleState (on, juce::dontSendNotification); }
    boardView.tick();
    allView.tick();
    mixer.tick();
    songPanel.tick();
    bigPanel.tick();
    if (songButton.getToggleState() != proc.rack.song.on.load()) songButton.setToggleState (proc.rack.song.on.load(), juce::dontSendNotification);
    static int shownBpm = 0;
    const int b = (int) std::lround (proc.bpm.load() * 10.0);
    if (b != shownBpm) { shownBpm = b; repaint (0, 0, getWidth(), topbarH); }
}

bool Nebula2ModulationEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { stepHistory (false); return true; }
    if (k == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)
        || k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { stepHistory (true); return true; }
    if (k == juce::KeyPress::spaceKey && ! proc.hostPlaying.load()) { proc.preview.store (! proc.preview.load()); return true; }
    if (k == juce::KeyPress::escapeKey && bigPanel.isOpen()) { bigPanel.close(); return true; }
    if (k == juce::KeyPress::escapeKey && songPanel.isVisible()) { songPanel.setVisible (false); return true; }
    if (k == juce::KeyPress::escapeKey && addPanel.isVisible()) { addPanel.setVisible (false); return true; }
    if (k == juce::KeyPress::escapeKey && hostOverlay.isOpen()) { hostOverlay.close(); return true; }
    return false;
}

void Nebula2ModulationEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (windowTop, 0, 0, windowBottom, 0, (float) getHeight(), false));
    g.fillAll();
    g.setFont (titleFont());
    g.setColour (n8);
    g.drawText ("NEBULA2 MODULATION", space4, 0, 220, topbarH, juce::Justification::centredLeft, false);
    dividerH (g, tabs.getRight(), getWidth(), tabs.getBottom() - 2);
    caption (g, "Tempo", { 336, 0, 44, topbarH });
    g.setFont (controlFont());
    g.setColour (n8);
    g.drawText (juce::String (proc.bpm.load(), 1), 380, 0, 60, topbarH, juce::Justification::centredLeft, false);
    caption (g, "Key", { 452, 0, 30, topbarH });
    g.setFont (controlFont());
    g.setColour (n7);
    g.drawText (proc.showAll.load() && proc.zoomAll.load() < 0.01f ? juce::String ("Fit") : juce::String (juce::roundToInt (zoomInUse() * 100.0f)) + "%", zoomLabel(), juce::Justification::centred, false);
    dividerH (g, 0, getWidth(), topbarH - 2);
}

// Full screen: as big as the monitor the plugin is on allows, leaving room for the host's own window frame and
// title strip around ours. Off: the standard size.
void Nebula2ModulationEditor::applyWindowSize()
{
    int w = windowW, h = windowH;
    if (proc.bigWindow.load())
    {
        juce::Rectangle<int> area (0, 0, 1920, 1080);                       // with no screen to ask (the tests): a common one
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) area = d->userArea;
        w = juce::jmax (windowW, area.getWidth() - 40);
        h = juce::jmax (windowH, area.getHeight() - 130);
    }
    if (w != getWidth() || h != getHeight()) setSize (w, h); else resized();
}
void Nebula2ModulationEditor::setBigWindow (bool on) { proc.bigWindow.store (on); applyWindowSize(); repaint(); }
void Nebula2ModulationEditor::setFocus (bool on) { proc.focusView.store (on); resized(); repaint(); }

void Nebula2ModulationEditor::resized()
{
    const int cy = (topbarH - 28) / 2;
    play.setBounds (236, cy, 84, 28);
    zoomOut.setBounds (856, cy, 28, 28);
    zoomIn.setBounds (926, cy, 28, 28);
    songButton.setBounds (966, cy, 66, 28);
    undoButton.setBounds (722, cy, 60, 28);
    redoButton.setBounds (786, cy, 60, 28);
    keyBox.setBounds (484, cy, 68, 28);
    scaleBox.setBounds (558, cy, 154, 28);
    tidyButton.setBounds (getWidth() - space4 - 70, cy, 70, 28);
    addUnitsButton.setBounds (tidyButton.getX() - 8 - 104, cy, 104, 28);
    // Focus: the style bar and the mixing desk step aside and the board takes their room
    const bool focus = proc.focusView.load();
    const int top = topbarH + (focus ? 0 : StyleBar::height);
    styleBar.setVisible (! focus); mixer.setVisible (! focus); deskButton.setVisible (! focus);
    deskSounds.setVisible (! focus); deskInside.setVisible (! focus);
    styleBar.setBounds (0, topbarH, getWidth(), StyleBar::height);
    tabs.setBounds (0, top, getWidth() - 136 - 176, kTabsH);
    fullButton.setBounds (getWidth() - 14 - 118 - 8 - 94, top + 6, 94, 26);
    focusButton.setBounds (fullButton.getX() - 6 - 66, top + 6, 66, 26);
    focusButton.setToggleState (focus, juce::dontSendNotification);
    fullButton.setToggleState (proc.bigWindow.load(), juce::dontSendNotification);
    fullButton.setButtonText (proc.bigWindow.load() ? "Normal size" : "Full screen");
    const int deskH = focus ? 0 : MixerStrip::heightFor (proc.mixerTall.load());
    mixer.setBounds (0, getHeight() - deskH, getWidth(), deskH);
    viewport.setBounds (0, top + kTabsH, getWidth(), getHeight() - top - kTabsH - deskH);
    deskButton.setButtonText (proc.mixerTall.load() ? "Less" : "More");
    deskButton.setToggleState (proc.mixerTall.load(), juce::dontSendNotification);
    deskButton.setBounds (getWidth() - 14 - 76, mixer.getY() - 22, 76, 22);
    // Sounds / Inside sit on the same edge, to its left
    deskInside.setBounds (deskButton.getX() - 10 - 66, mixer.getY() - 22, 66, 22);
    deskSounds.setBounds (deskInside.getX() - 2 - 70, mixer.getY() - 22, 70, 22);
    deskSounds.setToggleState (! proc.mixerInside.load(), juce::dontSendNotification);
    deskInside.setToggleState (proc.mixerInside.load(), juce::dontSendNotification);
    // Change sound sits at the top right of the board, over it, for whichever tab is showing
    soundButton.setBounds (getWidth() - 14 - 118, top + 6, 118, 26);
    addPanel.setBounds (getLocalBounds());
    songPanel.setBounds (getLocalBounds());
    hostOverlay.setBounds (getLocalBounds());
    bigPanel.setBounds (getLocalBounds());
    if (proc.showAll.load()) allView.layout(); else boardView.fitToContent();
}
