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

void N2MLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (controlFont());
    g.setColour (! b.isEnabled() ? n6 : b.getToggleState() ? accent : n8);
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
ParamCell::ParamCell (Nebula2ModulationProcessor& p, Module& m, int index) : proc (p), module (m), idx (index), param (m.info->params[(size_t) index])
{
    setSize (lay::cell, lay::cellH);
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
    juce::Component::SafePointer<ParamCell> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        boardWillChange (*safe);
        safe->proc.rack.linkParam (safe->module, safe->idx, r - 1);
        safe->repaint();
    });
}

void ParamCell::paint (juce::Graphics& g)
{
    const float v = module.P (idx);
    g.setFont (smallCaps());
    g.setColour (n7);
    g.drawText (juce::String (param.name).toUpperCase(), 0, 1, getWidth(), 11, juce::Justification::centred, false);

    // A linked knob wears its group's letter
    if (const int group = module.paramLink[(size_t) idx]; group > 0)
    {
        const auto badge = juce::Rectangle<float> ((float) getWidth() - 14.0f, 12.0f, 12.0f, 12.0f);
        g.setColour (groupColour (group));
        g.fillEllipse (badge);
        g.setFont (font (8.5f, Weight::SemiBold));
        g.setColour (n0);
        g.drawText (groupLetter (group), badge.toNearestInt(), juce::Justification::centred, false);
    }

    if (! param.choices.empty())
    {
        const auto r = juce::Rectangle<float> (3.0f, 20.0f, (float) getWidth() - 6.0f, 24.0f);
        raised (g, r, radiusControl, isMouseOver());
        g.setFont (font (10.0f, Weight::Medium));
        g.setColour (n8);
        g.drawFittedText (paramText (param, v), r.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
        return;
    }

    const float norm = paramToNorm (param, v);
    const juce::Point<float> c ((float) getWidth() * 0.5f, 31.0f);
    const float D = 28.0f, a0 = -2.36f, a1 = 2.36f, va = a0 + norm * (a1 - a0);
    const bool bipolar = param.lo < 0.0f && param.hi > 0.0f;
    const float mid = bipolar ? a0 + paramToNorm (param, 0.0f) * (a1 - a0) : a0;
    for (int i = 0; i < 17; ++i)
    {
        const float a = a0 + (float) i / 16.0f * (a1 - a0);
        const bool lit = std::abs (va - mid) > 0.02f && a >= std::min (mid, va) - 0.001f && a <= std::max (mid, va) + 0.001f;
        g.setColour (lit ? (dragging ? accent.brighter (0.2f) : accent) : juce::Colours::white.withAlpha (isMouseOver() ? 0.18f : 0.10f));
        g.drawLine ({ { c.x + 17.0f * std::sin (a), c.y - 17.0f * std::cos (a) }, { c.x + 19.5f * std::sin (a), c.y - 19.5f * std::cos (a) } }, 1.4f);
    }
    const auto body = juce::Rectangle<float> (D, D).withCentre (c);
    juce::Path bp; bp.addEllipse (body);
    shadow (g, bp, juce::Colours::black.withAlpha (0.55f), 0.4f * D, 0.06f * D, 0.14f * D);
    g.setGradientFill (juce::ColourGradient (isMouseOver() ? knobHiHover : knobHi, body.getTopLeft(), knobLo, body.getBottomRight(), false));
    g.fillPath (bp);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), body.getTopLeft(), juce::Colours::black.withAlpha (0.35f), body.getBottomRight(), false));
    g.strokePath (bp, juce::PathStrokeType (1.0f));
    const juce::Point<float> dot (c.x + D * 0.31f * std::sin (va), c.y - D * 0.31f * std::cos (va));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    g.setColour (accent.withAlpha (dragging ? 1.0f : 0.7f));
    g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (dot));

    g.setFont (font (10.0f));
    g.setColour (n8);
    g.drawText (minus (paramText (param, v)), -4, 49, getWidth() + 8, 12, juce::Justification::centred, false);
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

SeqView::SeqView (Nebula2ModulationProcessor& p, Board& b, Module& m) : proc (p), board (b), module (m)
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
    setSize (lay::seqW, lay::seqHeight (*m.info));
}

void SeqView::resized() { ideas.setBounds (0, 0, 58, 20); random.setBounds (62, 0, 66, 20); }

// Which set of pattern ideas suits this unit: a trance gate has its own, otherwise the tab's kind of sound decides
std::string SeqView::ideaKind() const { return std::string (module.info->type) == "gate" ? "gate" : board.type; }

void SeqView::edited()
{
    repaint();
    if (module.patternLink <= 0) return;
    proc.rack.pushPattern (module);
    if (auto* view = findParentComponentOfClass<BoardView>()) view->repaintAll();
}

juce::Rectangle<int> SeqView::grid() const { return { 0, 24, lay::seqW, getHeight() - 24 - 4 }; }
// The step under the mouse, on the bar being shown
int SeqView::stepAtX (int x) const { return page * kSteps + juce::jlimit (0, kSteps - 1, x / 20); }
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
        if (safe == nullptr || r <= 0) return;
        boardWillChange (*safe);
        if (drum) setDrum (safe->module, drumIdeas (safe->ideaKind())[(size_t) (r - 1)].pat);
        else if (value) setValues (safe->module, valueIdeas()[(size_t) (r - 1)].v);
        else { auto& d = noteIdeas (safe->board.type)[(size_t) (r - 1)]; setNotes (safe->module, d.gates, d.deg); }
        safe->edited();
    });
}

void SeqView::paint (juce::Graphics& g)
{
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
    const float barH = (float) gr.getHeight() - 18.0f;
    const float rowH = barH / (float) (kDegHi - kDegLo + 1);
    const int deg = juce::jlimit (kDegLo, kDegHi, kDegLo + (int) (((float) gr.getY() + barH - (float) y) / rowH));
    module.data[(size_t) (kMaxSteps + step)].store ((float) deg);
    module.data[(size_t) step].store (1.0f);
}

void SeqView::mouseDown (const juce::MouseEvent& e)
{
    const auto gr = grid();
    dragStep = -1;
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
}

//==============================================================================
ChordView::ChordView (Nebula2ModulationProcessor& p, Module& m) : proc (p), module (m)
{
    for (auto* b : { &mood, &generate, &length, &each }) addAndMakeVisible (b);
    auto changed = [this] { proc.chordsVersion.fetch_add (1); };
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
    setSize (lay::chordW, lay::chordSeqH);
    refreshLabels();
}

void ChordView::refreshLabels()
{
    const auto& h = proc.rack.harmony;
    mood.setButtonText (n2c::moods()[(size_t) n2c::moodIndex (h.mood)].name);
    length.setButtonText (juce::String (h.lenBars) + " bars");
    each.setButtonText (h.chordBars == 1 ? "1 bar each" : juce::String (h.chordBars) + " bars each");
}

void ChordView::resized()
{
    mood.setBounds (0, 0, 150, 22);
    generate.setBounds (154, 0, 88, 22);
    length.setBounds (246, 0, 76, 22);
    each.setBounds (326, 0, 100, 22);
}

void ChordView::paint (juce::Graphics& g)
{
    const auto& h = proc.rack.harmony;
    for (int i = 0; i < (int) h.names.size() && i < 32; ++i)
    {
        const auto r = juce::Rectangle<float> ((float) ((i % 8) * 60 + 1), (float) (28 + (i / 8) * lay::chordTileH), 58.0f, (float) lay::chordTileH - 2.0f);
        const auto col = degreeColour (h.seq[(size_t) i].deg);
        const bool now = i == shown;
        g.setColour (col.withAlpha (now ? 0.95f : 0.16f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col.withAlpha (now ? 0.0f : 0.8f));
        g.fillRoundedRectangle (r.getX() + 6.0f, r.getY() + 2.0f, r.getWidth() - 12.0f, 1.5f, 0.75f);
        g.setFont (font (10.0f, Weight::Medium));
        g.setColour (now ? n0 : n8);
        g.drawFittedText (h.names[(size_t) i], r.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.7f);
    }
}

void ChordView::tick()
{
    const int v = proc.chordsVersion.load(), s = module.playStep.load();
    if (v != seenVersion) { seenVersion = v; refreshLabels(); repaint(); }
    if (s != shown) { shown = s; repaint(); }
}

//==============================================================================
HostView::HostView (BoardView& o, HostedModule& m) : owner (o), module (m)
{
    addAndMakeVisible (load); addAndMakeVisible (open);
    load.onClick = [this] { showLoadMenu(); };
    open.onClick = [this] { if (module.hasPlugin() && owner.onOpenPlugin) owner.onOpenPlugin (module); };
    setSize (lay::hostW, lay::hostSeqH);
    refresh();
}

void HostView::refresh() { open.setEnabled (module.hasPlugin()); repaint(); }
void HostView::resized() { load.setBounds (0, 30, 80, 24); open.setBounds (86, 30, 80, 24); }

void HostView::paint (juce::Graphics& g)
{
    const bool failed = module.status.isNotEmpty();
    g.setFont (font (13.0f, Weight::SemiBold));
    g.setColour (module.hasPlugin() ? juce::Colours::white : n7);
    g.drawText (module.hasPlugin() ? module.pluginName() : "Empty", 0, 2, getWidth(), 18, juce::Justification::centredLeft, true);
    g.setFont (helpFont());
    g.setColour (failed ? bandOrange : n7);
    g.drawFittedText (failed ? module.status : module.hasPlugin() ? "Open shows its own window" : "Load a plugin, or drop a .vst3 file on the board",
                      172, 30, getWidth() - 172, 24, juce::Justification::centredLeft, 2, 0.8f);
}

void HostView::loadFile (const juce::String& path)
{
    const auto found = pluginsInFile (path);
    if (found.empty()) module.status = "That file isn't a VST3 plugin this can load";
    else module.load (found[0]);
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
        if (r == 1001) { safe->module.unload(); safe->refresh(); return; }
        if (r == 1002) { installedPlugins (true); return; }
        if (r >= 2000) { if (r - 2000 < (int) installed.size()) safe->loadFile (installed[(size_t) (r - 2000)].second); return; }
        if (r < 1000) { safe->module.load (recent[(size_t) (r - 1)]); safe->refresh(); return; }
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
    back.setButtonText ("Back to " + tabName);
    viewport.setViewedComponent (pluginEditor.get(), false);
    setVisible (true);
    toFront (true);
    resized();
}

void HostOverlay::close()
{
    viewport.setViewedComponent (nullptr, false);
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
    if (info.seq == Seq::Host)
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
    const int cols = lay::cols (info);
    const int x0 = (getWidth() - cols * lay::cell) / 2, y0 = lay::header + lay::seqHeight (info);
    for (int i = 0; i < (int) info.params.size(); ++i)
    {
        auto* c = cells.add (new ParamCell (p, m, i));
        addAndMakeVisible (c);
        c->setTopLeftPosition (x0 + (i % lay::rowLen (info)) * lay::cell, y0 + (i / lay::rowLen (info)) * lay::cellH);
    }
    setAlpha (m.live ? 1.0f : 0.45f);
}

juce::Point<int> ModuleComp::jackCentre (bool isOut, int port) const
{
    const int y = getHeight() - lay::pad - lay::jackH + 29;
    if (! isOut) return { lay::pad + port * lay::jackW + lay::jackW / 2, y };
    const int nOut = (int) module.info->outs.size();
    return { getWidth() - lay::pad - (nOut - port) * lay::jackW + lay::jackW / 2, y };
}

static juce::String linkText (const Module& m)
{
    if (m.patternLink <= 0) return "LINK";
    return "LINK " + groupLetter (m.patternLink) + (m.patternShare == 1 ? ": RHYTHM" : m.patternShare == 2 ? ": NOTES" : m.patternShare == 4 ? ": FOLLOWS" : ": RHYTHM + NOTES");
}

juce::Rectangle<int> ModuleComp::linkArea() const
{
    if (module.info->seq != Seq::Drum && module.info->seq != Seq::Note) return {};
    const int nameW = juce::GlyphArrangement::getStringWidthInt (font (11.5f, Weight::SemiBold), module.info->name);
    const int w = juce::GlyphArrangement::getStringWidthInt (font (9.0f, Weight::SemiBold, 0.4f), linkText (module)) + 14;
    return { 23 + nameW + 10, 5, w, 16 };
}

// A sequencer's Link label: put its pattern in a group. Sequencers in the same group, on any tab, share
// their rhythm, their notes, or both.
void ModuleComp::linkMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Link this pattern");
    m.addItem (1, "Not linked", true, module.patternLink == 0);
    for (int g = 1; g <= 4; ++g)
    {
        const int n = proc.rack.membersOf (g, true);
        m.addItem (g + 1, "Group " + groupLetter (g) + (n > 0 ? "   (" + juce::String (n) + (n == 1 ? " pattern)" : " patterns)") : ""), true, module.patternLink == g);
    }
    m.addSeparator();
    m.addSectionHeader ("Follow");
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
            if (jackCentre (pass == 1, i).getDistanceFrom (p) <= 14) { isOut = pass == 1; port = i; return true; }
    }
    return false;
}

void ModuleComp::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto col = boardColour (board.colour);
    raised (g, r, radiusCard);
    g.setColour (col.withAlpha (0.85f));
    g.fillEllipse (11.0f, 10.0f, 6.0f, 6.0f);
    g.setFont (font (11.5f, Weight::SemiBold));
    g.setColour (n8);
    g.drawText (module.info->name, 23, 0, getWidth() - 30, lay::header, juce::Justification::centredLeft, true);
    if (const auto la = linkArea(); ! la.isEmpty())
    {
        const bool on = module.patternLink > 0;
        const auto lc = groupColour (module.patternLink);
        g.setColour (on ? lc.withAlpha (0.9f) : n4);
        g.fillRoundedRectangle (la.toFloat(), 4.0f);
        g.setFont (font (9.0f, Weight::SemiBold, 0.4f));
        g.setColour (on ? n0 : n7);
        g.drawText (linkText (module), la, juce::Justification::centred, false);
    }

    const int jy = getHeight() - lay::pad - lay::jackH;
    dividerH (g, lay::pad, getWidth() - lay::pad, jy - 2);
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto& ports = pass == 0 ? module.info->ins : module.info->outs;
        for (int i = 0; i < (int) ports.size(); ++i)
        {
            const auto c = jackCentre (pass == 1, i).toFloat();
            g.setFont (font (8.5f, Weight::Medium, 0.3f));
            g.setColour (pass == 1 ? n8 : n7);
            g.drawText (juce::String (ports[(size_t) i].name).toUpperCase(), (int) c.x - lay::jackW / 2 - 4, jy + 4, lay::jackW + 8, 10, juce::Justification::centred, false);
            juce::Path ring; ring.addEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (c));
            sunken (g, ring, 20.0f);
            // Outputs carry a ring in the sound's colour; inputs are plain
            g.setColour (pass == 1 ? col.withAlpha (0.9f) : n6);
            g.drawEllipse (juce::Rectangle<float> (15.0f, 15.0f).withCentre (c), 1.6f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c));
        }
    }
}

void ModuleComp::paintOverChildren (juce::Graphics& g)
{
    if (module.live) return;
    g.setFont (font (9.0f, Weight::Medium, 0.3f));
    g.setColour (bandOrange);
    g.drawText ("NOT CONNECTED", 0, 0, getWidth() - 10, lay::header, juce::Justification::centredRight, false);
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
    if (linkArea().contains (e.getPosition())) { linkMenu(); return; }
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
    moving = cabling = false;
}

//==============================================================================
BoardView::BoardView (Nebula2ModulationProcessor& p) : proc (p) {}

void BoardView::setBoard (Board* b) { board = b; rebuild(); }

void BoardView::rebuild()
{
    drag.active = false;
    comps.clear();
    if (board != nullptr)
        for (auto& m : board->modules)
            addAndMakeVisible (comps.add (new ModuleComp (*this, proc, *board, *m)));
    fitToContent();
    repaint();
    if (onChanged) onChanged();
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
    w += 500; h += 320;        // always some room to scroll into and put new units
    const float zoom = juce::jlimit (0.5f, 1.25f, proc.zoom.load());
    if (auto* vp = findParentComponentOfClass<juce::Viewport>())
    {
        // Leave room for the scroll bars, so one appearing doesn't summon the other
        w = juce::jmax (w, (int) std::ceil ((float) (vp->getWidth() - vp->getScrollBarThickness()) / zoom));
        h = juce::jmax (h, (int) std::ceil ((float) (vp->getHeight() - vp->getScrollBarThickness()) / zoom));
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
    g.setColour (juce::Colours::black.withAlpha (ghost ? 0.15f : 0.3f));
    g.strokePath (p, juce::PathStrokeType (4.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col.withAlpha (ghost ? 0.45f : 0.8f));
    g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (a));
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (b));
}

void BoardView::paintOverChildren (juce::Graphics& g)
{
    if (board == nullptr) return;
    for (auto& c : board->cables)
    {
        auto* a = compFor (c.src); auto* b = compFor (c.dst);
        if (a == nullptr || b == nullptr) continue;
        const bool dead = ! (a->module.live && b->module.live);
        drawCable (g, (a->getPosition() + a->jackCentre (true, c.srcPort)).toFloat(),
                   (b->getPosition() + b->jackCentre (false, c.dstPort)).toFloat(),
                   boardColour (board->colour + c.src), dead);
    }
    if (drag.active)
        if (auto* a = compFor (drag.uid))
            drawCable (g, (a->getPosition() + a->jackCentre (drag.fromOut, drag.port)).toFloat(), drag.at.toFloat(),
                       drag.fromOut ? boardColour (board->colour + drag.uid) : n8, true);
}

bool BoardView::findJack (juce::Point<int> at, ModuleComp*& comp, bool& isOut, int& port) const
{
    for (int i = getNumChildComponents(); --i >= 0;)
        if (auto* c = dynamic_cast<ModuleComp*> (getChildComponent (i)))
            if (c->jackAt (at - c->getPosition(), isOut, port)) { comp = c; return true; }
    return false;
}

void BoardView::willChange (int removingUid) { if (board != nullptr) proc.history.before (*board, removingUid); }
void BoardView::settled() { proc.history.discardIfUnchanged(); }

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
        if (drag.fromOut) proc.rack.connect (*board, drag.uid, drag.port, comp->module.uid, port);
        else proc.rack.connect (*board, comp->module.uid, port, drag.uid, drag.port);
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
    if (auto* vp = findParentComponentOfClass<juce::Viewport>()) panStart = vp->getViewPosition();
    juce::ignoreUnused (e);
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
    for (auto& f : files) if (f.endsWithIgnoreCase (".vst3")) return board != nullptr;
    return false;
}

void BoardView::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (board == nullptr) return;
    willChange();
    for (auto& f : files)
    {
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
    for (auto* c : comps)
    {
        if (c->seq != nullptr) c->seq->tick();
        if (c->chords != nullptr) c->chords->tick();
    }
}

//==============================================================================
TabBar::TabBar (Nebula2ModulationProcessor& p) : proc (p) {}

int TabBar::totalWidth() const
{
    int w = 0;
    for (auto& b : proc.rack.boards) w += juce::jmax (84, juce::GlyphArrangement::getStringWidthInt (controlFont(), b->name) + 44) + 4;
    return w;
}
bool TabBar::overflowing() const { return totalWidth() > getWidth() - 24 - 40; }
juce::Rectangle<int> TabBar::plusArea() const
{
    if (overflowing()) return { getWidth() - 12 - 32, 5, 32, 28 };
    return { 12 + totalWidth() + 2, 5, 32, 28 };
}
juce::Rectangle<int> TabBar::leftArrow() const { return { getWidth() - 12 - 32 - 6 - 56, 5, 26, 28 }; }
juce::Rectangle<int> TabBar::rightArrow() const { return { getWidth() - 12 - 32 - 6 - 28, 5, 26, 28 }; }
juce::Rectangle<int> TabBar::tabsArea() const
{
    return { 12, 0, overflowing() ? leftArrow().getX() - 6 - 12 : getWidth() - 24, getHeight() };
}

std::vector<TabBar::Slot> TabBar::slots() const
{
    std::vector<Slot> s;
    int x = 12 - (overflowing() ? scroll : 0), i = 0;
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
                raised (g, r, radiusControl);
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
    juce::Component::SafePointer<TabBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (plusArea())), [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& rack = safe->proc.rack;
        auto b = makeBoard (boardTypes()[(size_t) (r - 1)].id, rack.sr);
        // A second Kick is "Kick 2", so tabs and mixer channels can be told apart
        int same = 0;
        for (auto& o : rack.boards) if (o->type == b->type) ++same;
        if (same > 0) b->name += " " + std::to_string (same + 1);
        rack.addBoard (std::move (b));
        safe->proc.history.tabAdded ((int) rack.boards.size() - 1);
        if (safe->onChanged) safe->onChanged();
        if (safe->onSelect) safe->onSelect ((int) rack.boards.size() - 1);
        safe->showSelected();
    });
}

void TabBar::tabMenu (int index)
{
    juce::PopupMenu m;
    const int n = (int) proc.rack.boards.size();
    m.addItem (1, "Rename");
    m.addItem (2, "Duplicate");
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
            if (safe->onSelect) safe->onSelect (-1);
            safe->proc.history.tabRemoving (index);
            rack.removeBoard (index);
            select = juce::jmin (index, (int) rack.boards.size() - 1);
        }
        if (safe->onChanged) safe->onChanged();
        if (safe->onSelect) safe->onSelect (select);
        safe->showSelected();
    });
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

juce::Rectangle<int> MixerStrip::masterArea() const { return { getWidth() - 12 - kMasterW, 8, kMasterW, getHeight() - 16 }; }
juce::Rectangle<int> MixerStrip::channel (int i) const { return { 12 + i * (kChanW + 6) - scroll, 8, kChanW, getHeight() - 16 }; }
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
    for (int k = 1; k < kOuts; ++k)
        m.addItem (k + 1, "Out " + juce::String (k + 1) + ((on & (1 << k)) != 0 ? "" : "   (not switched on in Cubase)"), true, board->out.load() == k);
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
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (0, 0, masterArea().getX() - 10, getHeight());
        for (int i = 0; i < n; ++i)
        {
            auto& b = *proc.rack.boards[(size_t) i];
            const auto ch = channel (i);
            const auto col = boardColour (b.colour);
            if (i == sel) raised (g, ch.toFloat(), radiusCard);
            else { g.setColour (juce::Colours::white.withAlpha (0.025f)); g.fillRoundedRectangle (ch.toFloat(), radiusCard); }
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
}

void MixerStrip::mouseDown (const juce::MouseEvent& e)
{
    dragChannel = -2;
    const auto m = masterArea();
    if (m.contains (e.getPosition())) { dragChannel = -1; mouseDrag (e); return; }
    if (e.x >= m.getX() - 10) return;
    for (int i = 0; i < (int) proc.rack.boards.size(); ++i)
    {
        const auto ch = channel (i);
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
    const auto f = dragChannel == -1 ? faderIn (masterArea()) : faderIn (channel (dragChannel));
    const float v = juce::jlimit (0.0f, kFaderTop, (1.0f - (float) (e.y - f.getY()) / (float) f.getHeight()) * kFaderTop);
    if (dragChannel == -1) proc.rack.master.store (v);
    else if (dragChannel < (int) proc.rack.boards.size()) proc.rack.boards[(size_t) dragChannel]->level.store (v);
    repaint();
}

void MixerStrip::mouseUp (const juce::MouseEvent&) { proc.history.discardIfUnchanged(); dragChannel = -2; repaint(); }

void MixerStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (masterArea().contains (e.getPosition())) { proc.rack.master.store (0.8f); repaint(); return; }
    for (int i = 0; i < (int) proc.rack.boards.size(); ++i)
        if (faderIn (channel (i)).expanded (8, 4).contains (e.getPosition())) { proc.rack.boards[(size_t) i]->level.store (0.8f); repaint(); }
}

void MixerStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
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
    if (moved) repaint();
}

//==============================================================================
AddPanel::AddPanel()
{
    addAndMakeVisible (add); addAndMakeVisible (cancel);
    cancel.onClick = [this] { setVisible (false); };
    add.onClick = [this]
    {
        auto picked = ticked;
        setVisible (false);
        if (onAdd && ! picked.empty()) onAdd (picked);
    };
    setVisible (false);
}

void AddPanel::open() { hovered = nullptr; ticked.clear(); add.setEnabled (false); setVisible (true); toFront (true); repaint(); }

static constexpr int kRowH = 35, kHeadH = 27, kCardW = 1216, kAddCols = 4;

juce::Rectangle<int> AddPanel::card() const
{
    int total = 0;
    for (auto* s : shelves()) { juce::ignoreUnused (s); total += kHeadH; }
    total += (int) allTypes().size() * kRowH;
    const int h = juce::jmin (getHeight() - 24, total / kAddCols + 2 * kHeadH + 3 * kRowH + 120);
    return juce::Rectangle<int> (kCardW, h).withCentre (getLocalBounds().getCentre());
}

std::vector<AddPanel::Row> AddPanel::rows() const
{
    // Three columns of headings; a heading never splits across columns
    std::vector<Row> out;
    const auto c = card();
    int total = 0;
    const auto types = allTypes();
    for (auto* s : shelves()) { total += kHeadH; for (auto* t : types) if (std::string (t->shelf) == s) total += kRowH; }
    const int colW = (kCardW - 40 - (kAddCols - 1) * 12) / kAddCols;
    int col = 0, y = c.getY() + 56, used = 0;
    for (auto* s : shelves())
    {
        int block = kHeadH;
        for (auto* t : types) if (std::string (t->shelf) == s) block += kRowH;
        if (col < kAddCols - 1 && used > 0 && used + block / 2 > total * (col + 1) / kAddCols) { ++col; y = c.getY() + 56; }
        const int x = c.getX() + 20 + col * (colW + 12);
        out.push_back ({ { x, y, colW, kHeadH }, nullptr });
        y += kHeadH;
        for (auto* t : types)
            if (std::string (t->shelf) == s) { out.push_back ({ { x, y, colW, kRowH }, t }); y += kRowH; }
        used += block;
    }
    return out;
}

void AddPanel::resized()
{
    const auto c = card();
    add.setBounds (c.getRight() - 20 - 130, c.getBottom() - 20 - 28, 130, 28);
    cancel.setBounds (add.getX() - 8 - 84, add.getY(), 84, 28);
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
    g.drawText ("Tick one or several. They arrive on the board unconnected, ready for you to cable in.", c.getX() + 20, c.getY() + 34, c.getWidth() - 40, 16, juce::Justification::centredLeft, true);

    size_t shelf = 0;
    for (auto& r : rows())
    {
        if (r.info == nullptr)
        {
            caption (g, shelves()[shelf++], r.r.withTrimmedTop (10).withTrimmedLeft (2));
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
        g.setFont (controlFont());
        g.setColour (n8);
        g.drawText (r.info->name, r.r.getX() + 32, r.r.getY() + 3, r.r.getWidth() - 36, 15, juce::Justification::centredLeft, true);
        g.setFont (font (10.0f));
        g.setColour (n7);
        g.drawText (r.info->hint, r.r.getX() + 32, r.r.getY() + 18, r.r.getWidth() - 34, 13, juce::Justification::centredLeft, true);
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

Nebula2ModulationEditor::Nebula2ModulationEditor (Nebula2ModulationProcessor& p)
    : AudioProcessorEditor (p), proc (p), tabs (p), boardView (p), mixer (p)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    for (auto* b : { &play, &addUnitsButton, &tidyButton, &undoButton, &redoButton }) addAndMakeVisible (b);
    undoButton.onClick = [this] { stepHistory (false); };
    redoButton.onClick = [this] { stepHistory (true); };
    play.onClick = [this] { if (! proc.hostPlaying.load()) proc.preview.store (! proc.preview.load()); };
    addUnitsButton.onClick = [this] { addPanel.open(); };
    tidyButton.onClick = [this] { if (auto* b = current()) { proc.history.before (*b); tidy (*b); proc.history.discardIfUnchanged(); boardView.rebuild(); viewport.setViewPosition (0, 0); } };

    for (int i = 0; i < 12; ++i) keyBox.addItem (kNoteNames[i], i + 1);
    for (int i = 0; i < (int) scales().size(); ++i) scaleBox.addItem (scales()[(size_t) i].name, i + 1);
    addAndMakeVisible (keyBox); addAndMakeVisible (scaleBox);
    // The key is shared by every tab. Changing it moves the notes and the chords with it.
    keyBox.onChange = [this] { proc.history.beforeChords(); proc.rack.setKey (keyBox.getSelectedId() - 1, proc.rack.scale.load()); proc.chordsVersion.fetch_add (1); boardView.repaint(); };
    scaleBox.onChange = [this] { proc.history.beforeChords(); proc.rack.setKey (proc.rack.root.load(), scaleBox.getSelectedId() - 1); proc.chordsVersion.fetch_add (1); boardView.repaint(); };

    addAndMakeVisible (tabs);
    tabs.onSelect = [this] (int i) { selectTab (i); };
    tabs.onChanged = [this] { tabs.repaint(); mixer.repaint(); };

    addAndMakeVisible (viewport);
    boardHolder.addAndMakeVisible (boardView);
    viewport.setViewedComponent (&boardHolder, false);
    boardView.onZoomStep = [this] (int d) { stepZoom (d); };
    for (auto* b : { &zoomOut, &zoomIn }) addAndMakeVisible (b);
    zoomOut.onClick = [this] { stepZoom (-1); };
    zoomIn.onClick = [this] { stepZoom (1); };
    addPanel.preview = [this] (const Info& i) { return previewOf (i); };
    viewport.setScrollBarThickness (10);

    addAndMakeVisible (mixer);
    mixer.onSelect = [this] (int i) { selectTab (i); tabs.showSelected(); };

    addChildComponent (hostOverlay);
    boardView.onOpenPlugin = [this] (HostedModule& m) { if (auto* b = current()) hostOverlay.open (m, b->name); };

    addChildComponent (addPanel);
    addPanel.onAdd = [this] (const std::vector<std::string>& types) { addUnits (types); };

    setSize (windowW, windowH);
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

void Nebula2ModulationEditor::selectTab (int index)
{
    if (index < 0) { boardView.setBoard (nullptr); return; }
    proc.selectedTab.store (juce::jlimit (0, juce::jmax (0, (int) proc.rack.boards.size() - 1), index));
    boardView.setBoard (current());
    viewport.setViewPosition (0, 0);
    tabs.repaint(); mixer.repaint();
}

// Zoom steps: small enough to see a whole busy board, up to a little larger than life
void Nebula2ModulationEditor::stepZoom (int direction)
{
    static const float levels[5] = { 0.5f, 0.67f, 0.8f, 1.0f, 1.25f };
    int at = 3;
    for (int i = 0; i < 5; ++i) if (std::abs (levels[i] - proc.zoom.load()) < 0.02f) at = i;
    at = juce::jlimit (0, 4, at + direction);
    // Keep the same spot of the board at the top left while the size changes
    const float before = proc.zoom.load();
    const auto pos = viewport.getViewPosition();
    proc.zoom.store (levels[at]);
    boardView.fitToContent();
    viewport.setViewPosition (juce::roundToInt ((float) pos.x / before * levels[at]), juce::roundToInt ((float) pos.y / before * levels[at]));
    zoomOut.setEnabled (at > 0); zoomIn.setEnabled (at < 4);
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
    boardView.setBoard (nullptr);            // the board on screen may be about to change or go
    proc.history.showTab = -1;
    if (redo) proc.history.redo(); else proc.history.undo();
    if (proc.history.showTab >= 0) proc.selectedTab.store (proc.history.showTab);
    proc.selectedTab.store (juce::jlimit (0, juce::jmax (0, (int) proc.rack.boards.size() - 1), proc.selectedTab.load()));
    boardView.setBoard (current());
    keyBox.setSelectedId (proc.rack.root.load() + 1, juce::dontSendNotification);
    scaleBox.setSelectedId (proc.rack.scale.load() + 1, juce::dontSendNotification);
    proc.chordsVersion.fetch_add (1);
    tabs.showSelected();
    tabs.repaint(); mixer.repaint(); repaint();
}

void Nebula2ModulationEditor::addUnits (const std::vector<std::string>& types)
{
    auto* b = current();
    if (b == nullptr) return;
    proc.history.before (*b);
    // New units go in a row under everything already on the board, unconnected
    int bottom = 0;
    for (auto& m : b->modules) bottom = juce::jmax (bottom, (int) m->y + lay::height (*m->info));
    int x = 24;
    const int y = bottom + 28;
    for (auto& t : types)
        if (auto* m = proc.rack.addModule (*b, t, (float) x, (float) y))
            x += lay::width (*m->info) + 20;
    boardView.rebuild();
    viewport.setViewPosition (0, juce::jmax (0, juce::roundToInt ((float) (y - 60) * proc.zoom.load())));
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
        selectTab (proc.selectedTab.load());
        stepZoom (0);                    // a loaded project brings its zoom with it
        tabs.showSelected();
        repaint();
    }
    // Choosing a mood on the Chord generator can change the scale: keep the box at the top in step
    if (scaleBox.getSelectedId() != proc.rack.scale.load() + 1)
    {
        scaleBox.setSelectedId (proc.rack.scale.load() + 1, juce::dontSendNotification);
        boardView.repaint();
    }
    undoButton.setEnabled (proc.history.canUndo());
    redoButton.setEnabled (proc.history.canRedo());
    const bool host = proc.hostPlaying.load(), on = proc.isPlaying();
    const juce::String text = host ? "Playing" : on ? "Stop" : "Play";
    if (play.getButtonText() != text || play.getToggleState() != on) { play.setButtonText (text); play.setToggleState (on, juce::dontSendNotification); }
    boardView.tick();
    mixer.tick();
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
    if (k == juce::KeyPress::escapeKey && addPanel.isVisible()) { addPanel.setVisible (false); return true; }
    if (k == juce::KeyPress::escapeKey && hostOverlay.isVisible()) { hostOverlay.close(); return true; }
    return false;
}

void Nebula2ModulationEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (windowTop, 0, 0, windowBottom, 0, (float) getHeight(), false));
    g.fillAll();
    g.setFont (titleFont());
    g.setColour (n8);
    g.drawText ("NEBULA2 MODULATION", space4, 0, 220, topbarH, juce::Justification::centredLeft, false);
    caption (g, "Tempo", { 336, 0, 44, topbarH });
    g.setFont (controlFont());
    g.setColour (n8);
    g.drawText (juce::String (proc.bpm.load(), 1), 380, 0, 60, topbarH, juce::Justification::centredLeft, false);
    caption (g, "Key", { 452, 0, 30, topbarH });
    g.setFont (controlFont());
    g.setColour (n7);
    g.drawText (juce::String (juce::roundToInt (proc.zoom.load() * 100.0f)) + "%", zoomOut.getRight(), 0, zoomIn.getX() - zoomOut.getRight(), topbarH, juce::Justification::centred, false);
    dividerH (g, 0, getWidth(), topbarH - 2);
}

void Nebula2ModulationEditor::resized()
{
    const int cy = (topbarH - 28) / 2;
    play.setBounds (236, cy, 84, 28);
    zoomOut.setBounds (896, cy, 28, 28);
    zoomIn.setBounds (972, cy, 28, 28);
    undoButton.setBounds (744, cy, 64, 28);
    redoButton.setBounds (812, cy, 64, 28);
    keyBox.setBounds (484, cy, 68, 28);
    scaleBox.setBounds (558, cy, 170, 28);
    tidyButton.setBounds (getWidth() - space4 - 70, cy, 70, 28);
    addUnitsButton.setBounds (tidyButton.getX() - 8 - 104, cy, 104, 28);
    tabs.setBounds (0, topbarH, getWidth(), kTabsH);
    mixer.setBounds (0, getHeight() - kMixerH, getWidth(), kMixerH);
    viewport.setBounds (0, topbarH + kTabsH, getWidth(), getHeight() - topbarH - kTabsH - kMixerH);
    addPanel.setBounds (getLocalBounds());
    hostOverlay.setBounds (getLocalBounds());
    boardView.fitToContent();
}
