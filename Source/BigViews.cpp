#include "PluginEditor.h"
#include <cstring>

// The bigger views: see BigViews.h. Everything here is the screen; what it changes lives in the engine.
namespace n2m
{
using namespace look;

static const juce::Colour kGlass (0xff07080b);

static uint64_t mix (uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
static uint64_t bitsOf (float f) { uint32_t u = 0; std::memcpy (&u, &f, sizeof u); return u; }

static juce::String barsText (int bars) { return bars == 1 ? juce::String ("1 bar") : juce::String (bars) + " bars"; }

//============================================================================== the base
BigView::BigView (Nebula2ModulationProcessor& p, Board& b, Module& m) : proc (p), board (b), module (m), boardPtr (&b), modulePtr (&m), moduleUid (m.uid)
{
    setOpaque (true);
}

// Is the unit this view shows still on its board, and the board still in the rack? Compares addresses only, so
// it is safe to ask after either has gone.
bool BigView::alive() const
{
    for (auto& b : proc.rack.boards)
        if (b.get() == boardPtr) return b->find (moduleUid) == modulePtr;
    return false;
}

void BigView::tick()
{
    if (! alive()) return;
    ticked();
    const auto s = signature();
    if (s != seen) { seen = s; repaint(); }
}

void BigView::paint (juce::Graphics& g)
{
    g.fillAll (raisedHi);
    if (alive()) draw (g);
}
void BigView::mouseDown (const juce::MouseEvent& e) { if (alive()) down (e); }
void BigView::mouseDrag (const juce::MouseEvent& e) { if (alive()) dragged (e); }
void BigView::mouseUp (const juce::MouseEvent& e) { if (alive()) up (e); }
void BigView::mouseDoubleClick (const juce::MouseEvent& e) { if (alive()) doubleClicked (e); }
void BigView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { if (alive()) wheel (e, w); }

void BigView::begin() { proc.history.before (board); }
void BigView::end()
{
    if (module.patternLink > 0) proc.rack.pushPattern (module);
    proc.history.discardIfUnchanged();
    repaint();
    if (onEdited) onEdited();
}

void BigView::setKnob (int param, float value)
{
    if (param < 0 || param >= (int) module.params.size()) return;
    const auto& p = module.info->params[(size_t) param];
    module.params[(size_t) param].store (juce::jlimit (p.lo, p.hi, value));
    proc.rack.pushParam (module, param);
}

//============================================================================== what the pattern views share
// One bar copied from any pattern view, and the kind of unit it came from
static BarClip& barClip() { static BarClip c; return c; }
static Seq& barClipKind() { static Seq k = Seq::None; return k; }

BigPattern::BigPattern (Nebula2ModulationProcessor& p, Board& b, Module& m) : BigView (p, b, m)
{
    static const char* const names[4] = { "1 bar", "2", "4", "8" };
    for (int k = 0; k < 4; ++k)
    {
        barButtons[k].setButtonText (names[k]);
        addAndMakeVisible (barButtons[k]);
        barButtons[k].onClick = [this, k] { setBars (k); };
    }
    for (auto* t : { &copy, &paste, &clear }) addAndMakeVisible (t);
    copy.onClick = [this] { copyBarAt (selectedBar()); };
    paste.onClick = [this] { pasteBarAt (selectedBar()); };
    clear.onClick = [this] { clearBarAt (selectedBar()); };
}

void BigPattern::resized()
{
    barButtons[0].setBounds (0, 0, 56, 26);
    for (int k = 1; k < 4; ++k) barButtons[k].setBounds (58 + (k - 1) * 34, 0, 32, 26);
    copy.setBounds (184, 0, 84, 26);
    paste.setBounds (272, 0, 88, 26);
    clear.setBounds (364, 0, 84, 26);
}

int BigPattern::bars() const { return 1 << juce::jlimit (0, 3, (int) std::lround (module.P (module.paramIndex ("bars")))); }
int BigPattern::length() const { return loopSteps (module.P (module.paramIndex ("length")), module.P (module.paramIndex ("bars"))); }

void BigPattern::setBars (int index)
{
    if (! alive()) return;
    const int k = module.paramIndex ("bars");
    if (k < 0) return;
    begin();
    module.params[(size_t) k].store ((float) juce::jlimit (0, 3, index));
    end();
}

void BigPattern::selectBar (int bar) { if (alive()) { picked = juce::jlimit (0, bars() - 1, bar); repaint(); } }

void BigPattern::copyBarAt (int bar)
{
    if (! alive() || bar < 0 || bar >= kMaxBars) return;
    barClip() = copyBar (module, bar);
    barClipKind() = module.info->seq;
    repaint();
}

bool BigPattern::canPaste() const { return barClip().full && barClipKind() == module.info->seq; }

bool BigPattern::pasteBarAt (int bar)
{
    if (! alive() || ! canPaste() || bar < 0 || bar >= kMaxBars) return false;
    begin();
    pasteBar (module, bar, barClip());
    end();
    return true;
}

void BigPattern::clearBarAt (int bar)
{
    if (! alive() || bar < 0 || bar >= kMaxBars) return;
    begin();
    clearBar (module, bar);
    end();
}

void BigPattern::ticked()
{
    const int s = module.playStep.load();
    if (s != shownStep) { shownStep = s; repaint(); }
    const int index = juce::jlimit (0, 3, (int) std::lround (module.P (module.paramIndex ("bars"))));
    for (int k = 0; k < 4; ++k) if (barButtons[k].getToggleState() != (k == index)) barButtons[k].setToggleState (k == index, juce::dontSendNotification);
    if (paste.isEnabled() != canPaste()) paste.setEnabled (canPaste());
}

uint64_t BigPattern::signature() const
{
    uint64_t h = 1469598103934665603ull;
    for (auto& d : module.data) h = mix (h, bitsOf (d.load (std::memory_order_relaxed)));
    for (auto& p : module.params) h = mix (h, bitsOf (p.load (std::memory_order_relaxed)));
    h = mix (h, (uint64_t) (proc.rack.root.load() * 16 + proc.rack.scale.load()));
    return mix (h, (uint64_t) picked);
}

void BigPattern::drawToolbar (juce::Graphics& g)
{
    g.setFont (helpFont());
    g.setColour (n7);
    const juce::String text = bars() > 1 ? "Bar " + juce::String (selectedBar() + 1) + " is picked. Click another bar's number to pick it."
                                         : juce::String ("One bar. Choose 2, 4 or 8 for a longer pattern.");
    g.drawText (text, 462, 0, getWidth() - 462, 26, juce::Justification::centredLeft, true);
}

//============================================================================== 1. the piano roll
juce::String BigNoteRoll::help() const
{
    return "Click to add a note; click a note to take it away. Drag a note up or down to change it, and drag its right-hand end for its own length. Right-click a note to go back to the Length knob.";
}

juce::Rectangle<int> BigNoteRoll::grid() const { const auto b = body(); return { 46, b.getY() + 20, b.getWidth() - 46, b.getHeight() - 20 }; }

juce::Rectangle<float> BigNoteRoll::cell (int step, int degree) const
{
    const auto gr = grid().toFloat();
    const float sw = gr.getWidth() / (float) (bars() * kSteps), rh = gr.getHeight() / (float) (hi - lo + 1);
    return { gr.getX() + (float) step * sw, gr.getBottom() - (float) (degree - lo + 1) * rh, sw, rh };
}

juce::Rectangle<float> BigNoteRoll::noteRect (int step) const
{
    const int deg = juce::jlimit (lo, hi, (int) std::lround (module.data[(size_t) (kMaxSteps + step)].load()));
    const auto c = cell (step, deg);
    const int own = ownNoteSteps (module, step, length());
    const float gl = module.P (module.paramIndex ("gatelen"));
    const float w = own > 0 ? (float) own * c.getWidth() - 2.0f : juce::jmax (4.0f, (gl >= 0.995f ? 1.0f : juce::jmin (0.96f, gl)) * c.getWidth() - 1.0f);
    const float right = juce::jmin (c.getX() + 1.0f + w, grid().toFloat().getRight() - 1.0f);
    return { c.getX() + 1.0f, c.getY() + 1.5f, juce::jmax (3.0f, right - c.getX() - 1.0f), c.getHeight() - 3.0f };
}

int BigNoteRoll::stepAt (float x) const
{
    const auto gr = grid().toFloat();
    return juce::jlimit (0, bars() * kSteps - 1, (int) std::floor ((x - gr.getX()) / (gr.getWidth() / (float) (bars() * kSteps))));
}
int BigNoteRoll::degreeAt (float y) const
{
    const auto gr = grid().toFloat();
    return juce::jlimit (lo, hi, lo + (int) std::floor ((gr.getBottom() - y) / (gr.getHeight() / (float) (hi - lo + 1))));
}

// The note under the mouse: on its row, anywhere along its length (or its whole step, if it is drawn shorter)
int BigNoteRoll::noteAt (juce::Point<float> p) const
{
    const int deg = degreeAt (p.y);
    int found = -1;
    for (int st = 0; st < bars() * kSteps; ++st)
    {
        if (module.data[(size_t) st].load() <= 0.5f) continue;
        if (juce::jlimit (lo, hi, (int) std::lround (module.data[(size_t) (kMaxSteps + st)].load())) != deg) continue;
        const auto r = noteRect (st);
        const float right = juce::jmax (r.getRight(), cell (st, deg).getRight());
        if (p.x >= r.getX() - 1.0f && p.x < right) found = st;
    }
    return found;
}

void BigNoteRoll::setNote (int step, int degree)
{
    if (! alive() || step < 0 || step >= kMaxSteps) return;
    begin();
    if (module.data[(size_t) step].load() <= 0.5f) module.data[(size_t) (kLenRow + step)].store (0.0f);
    module.data[(size_t) (kMaxSteps + step)].store ((float) juce::jlimit (lo, hi, degree));
    module.data[(size_t) step].store (1.0f);
    end();
}

void BigNoteRoll::removeNote (int step)
{
    if (! alive() || step < 0 || step >= kMaxSteps) return;
    begin();
    module.data[(size_t) step].store (0.0f);
    module.data[(size_t) (kLenRow + step)].store (0.0f);
    end();
}

void BigNoteRoll::setNoteLength (int step, int steps)
{
    if (! alive() || step < 0 || step >= kMaxSteps || module.data[(size_t) step].load() <= 0.5f) return;
    begin();
    module.data[(size_t) (kLenRow + step)].store ((float) juce::jlimit (0, kMaxSteps, steps));
    end();
}

void BigNoteRoll::down (const juce::MouseEvent& e)
{
    dragStep = -1; dragKind = 0; movedYet = false;
    const auto p = e.position;
    const auto gr = grid();
    if (p.y < (float) gr.getY() && p.y >= (float) body().getY() && p.x >= (float) gr.getX()) { selectBar (stepAt (p.x) / kSteps); return; }
    if (! gr.toFloat().contains (p)) return;
    const int hit = noteAt (p);
    if (e.mods.isPopupMenu()) { if (hit >= 0) setNoteLength (hit, 0); return; }
    begin();
    if (hit >= 0)
    {
        dragStep = hit;
        const auto r = noteRect (hit);
        const float grab = juce::jlimit (3.0f, 7.0f, r.getWidth() * 0.35f);
        // caught by its right-hand end: this drag is its length. Anywhere else, the way the mouse first goes decides.
        dragKind = p.x >= juce::jmax (r.getRight(), r.getX() + 4.0f) - grab ? 2 : 0;
        return;
    }
    const int st = stepAt (p.x);
    if (module.data[(size_t) st].load() <= 0.5f) module.data[(size_t) (kLenRow + st)].store (0.0f);
    module.data[(size_t) (kMaxSteps + st)].store ((float) degreeAt (p.y));
    module.data[(size_t) st].store (1.0f);
    dragStep = st; dragKind = 3; movedYet = true;
    repaint();
}

void BigNoteRoll::dragged (const juce::MouseEvent& e)
{
    if (dragStep < 0) return;
    if (dragKind == 0)
    {
        if (e.getDistanceFromDragStart() < 4) return;
        dragKind = std::abs (e.getDistanceFromDragStartX()) > std::abs (e.getDistanceFromDragStartY()) ? 2 : 1;
    }
    movedYet = true;
    if (dragKind == 1) module.data[(size_t) (kMaxSteps + dragStep)].store ((float) degreeAt (e.position.y));
    else
    {
        const auto c = cell (dragStep, 0);
        int steps = (int) std::lround ((e.position.x - c.getX()) / c.getWidth());
        // a new note stays the knob's length until it is pulled past its own step
        steps = dragKind == 3 && steps <= 1 ? 0 : juce::jlimit (1, length(), steps);
        module.data[(size_t) (kLenRow + dragStep)].store ((float) steps);
    }
    repaint();
}

void BigNoteRoll::up (const juce::MouseEvent&)
{
    if (dragStep < 0) return;
    if (! movedYet)        // a click on a note, not a drag: it goes
    {
        module.data[(size_t) dragStep].store (0.0f);
        module.data[(size_t) (kLenRow + dragStep)].store (0.0f);
    }
    dragStep = -1;
    end();
}

void BigNoteRoll::draw (juce::Graphics& g)
{
    drawToolbar (g);
    const auto col = boardColour (board.colour);
    const auto gr = grid().toFloat();
    const int nBars = bars(), total = nBars * kSteps, len = length();
    const float sw = gr.getWidth() / (float) total, rh = gr.getHeight() / (float) (hi - lo + 1);
    const int root = proc.rack.root.load();
    const int* sc = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, proc.rack.scale.load())].iv;
    const int octave = (int) std::lround (module.P (module.paramIndex ("octave")));

    g.setColour (kGlass);
    g.fillRoundedRectangle (gr, 4.0f);
    // the rows: one for each note of the key, the key note's rows a little lighter, each named at the left
    for (int deg = lo; deg <= hi; ++deg)
    {
        const auto row = cell (0, deg).withWidth (gr.getWidth());
        const bool home = ((deg % 7) + 7) % 7 == 0;
        if (home) { g.setColour (juce::Colours::white.withAlpha (0.055f)); g.fillRect (row); }
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.fillRect (row.getX() + 1.0f, row.getY(), row.getWidth() - 2.0f, 1.0f);
        const int midi = 60 + root + degreeToSemis (deg, sc) + 12 * octave;
        g.setFont (font (juce::jlimit (8.0f, 10.5f, rh * 0.62f), home ? Weight::SemiBold : Weight::Medium));
        g.setColour (home ? juce::Colours::white : n7);
        g.drawText (juce::String (kNoteNames[((midi % 12) + 12) % 12]) + juce::String ((int) std::floor (midi / 12.0) - 1), 0, (int) row.getY(), 40, (int) std::ceil (rh), juce::Justification::centredRight, false);
    }
    // the ruler: a number for each bar (click one to pick that bar), and lines for steps, beats and bars
    for (int b = 0; b < nBars; ++b)
    {
        const auto r = juce::Rectangle<float> (gr.getX() + (float) (b * kSteps) * sw + 1.0f, (float) body().getY(), (float) kSteps * sw - 2.0f, 16.0f);
        const bool sel = nBars > 1 && b == selectedBar();
        g.setColour (sel ? col.withAlpha (0.85f) : n4);
        g.fillRoundedRectangle (r, 4.0f);
        g.setFont (font (10.0f, Weight::SemiBold));
        g.setColour (sel ? n0 : n8);
        g.drawText (juce::String (b + 1), r.toNearestInt(), juce::Justification::centred, false);
    }
    for (int st = 1; st < total; ++st)
    {
        const bool barLine = st % kSteps == 0, beat = st % 4 == 0;
        if (! barLine && ! beat && sw < 5.0f) continue;
        g.setColour (juce::Colours::white.withAlpha (barLine ? 0.22f : beat ? 0.09f : 0.035f));
        g.fillRect (gr.getX() + (float) st * sw, gr.getY() + 1.0f, 1.0f, gr.getHeight() - 2.0f);
    }
    if (len < total)          // past the end of the pattern: dark
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (gr.getX() + (float) len * sw, gr.getY(), gr.getRight() - (gr.getX() + (float) len * sw), gr.getHeight());
    }
    if (shownStep >= 0 && shownStep < total)
    {
        g.setColour (noteAccent.withAlpha (0.16f));
        g.fillRect (gr.getX() + (float) shownStep * sw, gr.getY(), sw, gr.getHeight());
    }
    // the notes
    for (int st = 0; st < total; ++st)
    {
        if (module.data[(size_t) st].load() <= 0.5f) continue;
        const auto r = noteRect (st);
        const bool own = std::lround (module.data[(size_t) (kLenRow + st)].load()) > 0;
        const float alpha = st < len ? 1.0f : 0.3f;
        const bool sounding = shownStep >= 0 && shownStep >= st && (float) shownStep < (float) st + juce::jmax (1.0f, r.getWidth() / sw);
        g.setColour ((sounding ? col.brighter (0.5f) : col).withAlpha (alpha));
        g.fillRoundedRectangle (r, 2.5f);
        if (own && r.getWidth() > 8.0f)          // a note with its own length has a grip at its end
        {
            g.setColour (n0.withAlpha (0.55f * alpha));
            g.fillRoundedRectangle (r.getRight() - 4.5f, r.getY() + 2.0f, 2.0f, r.getHeight() - 4.0f, 1.0f);
        }
    }
    g.setColour (col.withAlpha (0.22f));
    g.drawRoundedRectangle (gr.reduced (0.5f), 4.0f, 1.0f);
}

//============================================================================== 2. the drum grid
juce::String BigDrumGrid::help() const
{
    return "Click a step to put a hit there or take it away. Drag in the bar under a hit to set how hard it hits: across the steps to shape a whole bar. Copy, Paste and Clear work on the picked bar.";
}

juce::Rectangle<int> BigDrumGrid::row (int bar) const
{
    const auto b = body();
    const int n = bars(), h = juce::jmin (190, b.getHeight() / n);
    return { 0, b.getY() + bar * h, b.getWidth(), h - 8 };
}

juce::Rectangle<float> BigDrumGrid::tile (int step) const
{
    const auto r = row (step / kSteps).toFloat();
    const float sw = (r.getWidth() - 52.0f) / (float) kSteps, th = std::floor (r.getHeight() * 0.42f);
    return { r.getX() + 52.0f + (float) (step % kSteps) * sw + 2.0f, r.getY(), sw - 4.0f, th };
}

juce::Rectangle<float> BigDrumGrid::velLane (int step) const
{
    const auto r = row (step / kSteps).toFloat();
    const auto t = tile (step);
    return { t.getX(), t.getBottom() + 4.0f, t.getWidth(), r.getBottom() - t.getBottom() - 4.0f };
}

int BigDrumGrid::stepAt (juce::Point<float> p, bool& inVel) const
{
    inVel = false;
    for (int b = 0; b < bars(); ++b)
    {
        const auto r = row (b).toFloat().expanded (0.0f, 4.0f);
        if (! r.contains (p) || p.x < 52.0f) continue;
        const float sw = (r.getWidth() - 52.0f) / (float) kSteps;
        const int st = b * kSteps + juce::jlimit (0, kSteps - 1, (int) std::floor ((p.x - 52.0f) / sw));
        inVel = p.y > tile (st).getBottom() + 2.0f;
        return st;
    }
    return -1;
}

void BigDrumGrid::toggleHit (int step)
{
    if (! alive() || step < 0 || step >= kMaxSteps) return;
    begin();
    module.data[(size_t) step].store (module.data[(size_t) step].load() > 0.5f ? 0.0f : 1.0f);
    end();
}

void BigDrumGrid::setVelocity (int step, float vel)
{
    if (! alive() || step < 0 || step >= kMaxSteps || module.data[(size_t) step].load() <= 0.5f) return;
    begin();
    module.data[(size_t) step].store (drumData (vel));
    end();
}

void BigDrumGrid::down (const juce::MouseEvent& e)
{
    dragVel = false;
    const auto p = e.position;
    for (int b = 0; b < bars(); ++b)
        if (p.x < 48.0f && row (b).toFloat().contains (p)) { selectBar (b); return; }
    bool inVel = false;
    const int st = stepAt (p, inVel);
    if (st < 0) return;
    if (! inVel) { toggleHit (st); return; }
    begin();
    dragVel = true;
    dragged (e);
}

void BigDrumGrid::dragged (const juce::MouseEvent& e)
{
    if (! dragVel) return;
    bool inVel = false;
    // the step under the mouse, in the bar the drag is over (or the one it started in, if it has strayed off the rows)
    int st = stepAt (e.position, inVel);
    if (st < 0) st = stepAt ({ e.position.x, e.mouseDownPosition.y }, inVel);
    if (st < 0 || module.data[(size_t) st].load() <= 0.5f) return;
    const auto lane = velLane (st);
    module.data[(size_t) st].store (drumData ((lane.getBottom() - 3.0f - e.position.y) / juce::jmax (1.0f, lane.getHeight() - 6.0f)));
    repaint();
}

void BigDrumGrid::up (const juce::MouseEvent&) { if (dragVel) { dragVel = false; end(); } }

void BigDrumGrid::draw (juce::Graphics& g)
{
    drawToolbar (g);
    const auto col = boardColour (board.colour);
    const int nBars = bars(), len = length();
    for (int b = 0; b < nBars; ++b)
    {
        const auto r = row (b);
        const bool sel = nBars > 1 && b == selectedBar();
        const bool playing = shownStep >= 0 && shownStep / kSteps == b;
        // the bar's label: click it to pick the bar
        const auto label = juce::Rectangle<float> (0.0f, (float) r.getY(), 44.0f, (float) r.getHeight());
        g.setColour (sel ? col.withAlpha (0.85f) : n4);
        g.fillRoundedRectangle (label, 4.0f);
        g.setFont (font (9.0f, Weight::SemiBold, 0.4f));
        g.setColour (sel ? n0 : n7);
        g.drawText ("BAR", label.toNearestInt().withHeight (r.getHeight() / 2).translated (0, 4), juce::Justification::centredBottom, false);
        g.setFont (font (16.0f, Weight::SemiBold));
        g.setColour (sel ? n0 : n8);
        g.drawText (juce::String (b + 1), label.toNearestInt().withTrimmedTop (r.getHeight() / 2).translated (0, 2), juce::Justification::centredTop, false);
        if (playing) { g.setColour (noteAccent); g.fillRoundedRectangle (label.getX() + 5.0f, label.getBottom() - 5.0f, label.getWidth() - 10.0f, 2.5f, 1.0f); }

        const auto first = velLane (b * kSteps), last = velLane (b * kSteps + kSteps - 1);
        g.setColour (kGlass);
        g.fillRoundedRectangle (first.getX() - 2.0f, first.getY(), last.getRight() - first.getX() + 4.0f, first.getHeight(), 4.0f);
        for (int i = 0; i < kSteps; ++i)
        {
            const int st = b * kSteps + i;
            const float alpha = st < len ? 1.0f : 0.3f;
            const float d = module.data[(size_t) st].load();
            const float vel = drumVel (d);
            const auto t = tile (st);
            if (d > 0.5f) g.setColour (col.withAlpha ((0.30f + 0.70f * vel) * alpha));
            else g.setColour ((i % 4 == 0 ? n5 : n4.withAlpha (0.55f)).withAlpha (alpha * (i % 4 == 0 ? 1.0f : 0.55f)));
            g.fillRoundedRectangle (t, 4.0f);
            g.setColour (materialRim);
            g.drawRoundedRectangle (t.reduced (0.5f), 4.0f, 1.0f);
            if (st == shownStep) { g.setColour (noteAccent); g.drawRoundedRectangle (t.expanded (1.0f), 5.0f, 1.8f); }
            const auto lane = velLane (st);
            if (d > 0.5f)
            {
                const float h = juce::jmax (3.0f, vel * (lane.getHeight() - 6.0f));
                g.setColour (col.withAlpha ((0.45f + 0.5f * vel) * alpha));
                g.fillRoundedRectangle (lane.getX() + 1.0f, lane.getBottom() - 3.0f - h, lane.getWidth() - 2.0f, h, 2.0f);
                if (t.getWidth() >= 26.0f)       // how hard it hits, as a figure
                {
                    g.setFont (font (10.0f, Weight::SemiBold));
                    g.setColour (n0.withAlpha (0.8f * alpha));
                    g.drawText (juce::String ((int) std::lround (vel * 100.0f)), t.toNearestInt(), juce::Justification::centred, false);
                }
            }
            else if (i % 4 == 0 && i > 0) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRect (lane.getX() - 2.5f, lane.getY() + 2.0f, 1.0f, lane.getHeight() - 4.0f); }
        }
    }
}

//============================================================================== 6. the step modulator
juce::String BigStepMod::help() const
{
    return "Drag across the steps to draw a shape, like a pencil. Right-drag switches steps off: a step that is off holds the value before it.";
}

juce::Rectangle<int> BigStepMod::grid() const { const auto b = body(); return { 40, b.getY() + 20, b.getWidth() - 40, b.getHeight() - 20 }; }
int BigStepMod::stepAt (float x) const
{
    const auto gr = grid().toFloat();
    return juce::jlimit (0, bars() * kSteps - 1, (int) std::floor ((x - gr.getX()) / (gr.getWidth() / (float) (bars() * kSteps))));
}
int BigStepMod::levelAt (float y) const
{
    const auto gr = grid().toFloat();
    return juce::jlimit (lo, hi, lo + (int) std::lround ((gr.getBottom() - 3.0f - y) / juce::jmax (1.0f, gr.getHeight() - 6.0f) * (float) (hi - lo)));
}
juce::Point<float> BigStepMod::pointFor (int step, int level) const
{
    const auto gr = grid().toFloat();
    const float sw = gr.getWidth() / (float) (bars() * kSteps);
    return { gr.getX() + ((float) step + 0.5f) * sw, gr.getBottom() - 3.0f - (float) (level - lo) / (float) (hi - lo) * (gr.getHeight() - 6.0f) };
}

void BigStepMod::rawLine (int a, int la, int b, int lb, bool hold)
{
    const int from = juce::jmin (a, b), to = juce::jmax (a, b);
    for (int st = juce::jmax (0, from); st <= juce::jmin (kMaxSteps - 1, to); ++st)
    {
        if (hold) { module.data[(size_t) st].store (0.0f); continue; }
        const float t = a == b ? 1.0f : (float) (st - a) / (float) (b - a);
        module.data[(size_t) (kMaxSteps + st)].store ((float) juce::jlimit (lo, hi, (int) std::lround ((float) la + ((float) lb - (float) la) * t)));
        module.data[(size_t) st].store (1.0f);
    }
}

void BigStepMod::setValue (int step, int level) { drawLine (step, level, step, level); }
void BigStepMod::drawLine (int a, int la, int b, int lb) { if (alive()) { begin(); rawLine (a, la, b, lb, false); end(); } }
void BigStepMod::setHold (int step) { if (alive()) { begin(); rawLine (step, 0, step, 0, true); end(); } }

void BigStepMod::down (const juce::MouseEvent& e)
{
    lastStep = -1;
    const auto p = e.position;
    const auto gr = grid();
    if (p.y < (float) gr.getY() && p.y >= (float) body().getY() && p.x >= (float) gr.getX()) { selectBar (stepAt (p.x) / kSteps); return; }
    if (! gr.toFloat().contains (p)) return;
    begin();
    lastStep = stepAt (p.x); lastLevel = levelAt (p.y);
    rawLine (lastStep, lastLevel, lastStep, lastLevel, e.mods.isPopupMenu());
    repaint();
}

void BigStepMod::dragged (const juce::MouseEvent& e)
{
    if (lastStep < 0) return;
    const int st = stepAt (e.position.x), lv = levelAt (e.position.y);
    rawLine (lastStep, lastLevel, st, lv, e.mods.isPopupMenu());      // every step the mouse passed over, even on a fast stroke
    lastStep = st; lastLevel = lv;
    repaint();
}

void BigStepMod::up (const juce::MouseEvent&) { if (lastStep >= 0) { lastStep = -1; end(); } }

void BigStepMod::draw (juce::Graphics& g)
{
    drawToolbar (g);
    const auto col = boardColour (board.colour);
    const auto gr = grid().toFloat();
    const int nBars = bars(), total = nBars * kSteps, len = length();
    const float sw = gr.getWidth() / (float) total;
    g.setColour (kGlass);
    g.fillRoundedRectangle (gr, 4.0f);
    for (int k = 0; k <= 4; ++k)       // 0, 25, 50, 75, 100
    {
        const float y = gr.getBottom() - 3.0f - (float) k / 4.0f * (gr.getHeight() - 6.0f);
        g.setColour (juce::Colours::white.withAlpha (k == 0 || k == 4 ? 0.0f : 0.05f));
        g.fillRect (gr.getX() + 1.0f, y, gr.getWidth() - 2.0f, 1.0f);
        g.setFont (font (9.5f, Weight::Medium));
        g.setColour (n7);
        g.drawText (juce::String (k * 25), 0, (int) y - 7, 34, 14, juce::Justification::centredRight, false);
    }
    for (int b = 0; b < nBars; ++b)
    {
        const auto r = juce::Rectangle<float> (gr.getX() + (float) (b * kSteps) * sw + 1.0f, (float) body().getY(), (float) kSteps * sw - 2.0f, 16.0f);
        const bool sel = nBars > 1 && b == selectedBar();
        g.setColour (sel ? col.withAlpha (0.85f) : n4);
        g.fillRoundedRectangle (r, 4.0f);
        g.setFont (font (10.0f, Weight::SemiBold));
        g.setColour (sel ? n0 : n8);
        g.drawText (juce::String (b + 1), r.toNearestInt(), juce::Justification::centred, false);
    }
    // the steps: a column up to each one's value. A step that is off is dim, at the value it holds.
    int held = lo;
    for (int st = total; --st >= 0;) if (st < len && module.data[(size_t) st].load() > 0.5f) { held = (int) std::lround (module.data[(size_t) (kMaxSteps + st)].load()); break; }   // the loop comes round to its last step
    juce::Path line;
    for (int st = 0; st < total; ++st)
    {
        const bool on = module.data[(size_t) st].load() > 0.5f;
        if (on) held = juce::jlimit (lo, hi, (int) std::lround (module.data[(size_t) (kMaxSteps + st)].load()));
        const float alpha = st < len ? 1.0f : 0.3f;
        const auto top = pointFor (st, held);
        const auto bar = juce::Rectangle<float> (gr.getX() + (float) st * sw + (sw > 5.0f ? 1.0f : 0.25f), top.y, sw - (sw > 5.0f ? 2.0f : 0.5f), juce::jmax (2.0f, gr.getBottom() - 3.0f - top.y + 2.0f));
        if (st == shownStep) { g.setColour (noteAccent.withAlpha (0.16f)); g.fillRect (gr.getX() + (float) st * sw, gr.getY(), sw, gr.getHeight()); }
        g.setColour (on ? col.withAlpha (0.55f * alpha) : n5.withAlpha (0.7f * alpha));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (on ? col.withAlpha (alpha) : n6.withAlpha (alpha));
        g.fillRoundedRectangle (bar.withHeight (3.0f), 1.5f);
        if (st < len) { if (line.isEmpty()) line.startNewSubPath (bar.getX(), top.y); else line.lineTo (bar.getX(), top.y); line.lineTo (bar.getRight(), top.y); }
    }
    for (int st = kSteps; st < total; st += kSteps) { g.setColour (juce::Colours::white.withAlpha (0.22f)); g.fillRect (gr.getX() + (float) st * sw, gr.getY() + 1.0f, 1.0f, gr.getHeight() - 2.0f); }
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.strokePath (line, juce::PathStrokeType (1.2f));
    if (len < total) { g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRect (gr.getX() + (float) len * sw, gr.getY(), gr.getRight() - (gr.getX() + (float) len * sw), gr.getHeight()); }
    g.setColour (col.withAlpha (0.22f));
    g.drawRoundedRectangle (gr.reduced (0.5f), 4.0f, 1.0f);
}

//============================================================================== 3. the chords
BigChords::BigChords (Nebula2ModulationProcessor& p, Board& b, Module& m) : BigView (p, b, m) {}

juce::String BigChords::help() const
{
    return "Every chord of the song, in order, with its notes on a keyboard. Click a chord to pick it, then click what to change underneath. The chords belong to the whole song, like the key.";
}

int BigChords::count() const { return juce::jmin (32, (int) proc.rack.harmony.seq.size(), (int) proc.rack.harmony.names.size()); }
int BigChords::perRow() const { return juce::jlimit (1, 8, count()); }
// Where the tiles end and the picked chord's choices begin: straight under the tiles, however many rows there are
int BigChords::tilesBottom() const { const int per = perRow(), rows = juce::jmax (1, (count() + per - 1) / per); return juce::jmin (getHeight() - 214, rows * 158); }
int BigChords::selected() const { return juce::jlimit (0, juce::jmax (0, count() - 1), picked); }

juce::Rectangle<int> BigChords::tile (int i) const
{
    const int per = perRow(), rows = juce::jmax (1, (count() + per - 1) / per);
    const int tw = juce::jmin (190, (getWidth() - (per - 1) * 8) / per);
    const int th = juce::jlimit (48, 150, (tilesBottom() - (rows - 1) * 8) / rows);
    return { (i % per) * (tw + 8), (i / per) * (th + 8), tw, th };
}

void BigChords::select (int chord) { if (alive() && chord >= 0 && chord < count()) { picked = chord; repaint(); } }

void BigChords::changed()
{
    proc.chordsVersion.fetch_add (1);
    repaint();
    if (onEdited) onEdited();
}

std::vector<juce::String> BigChords::colourNames() const
{
    std::vector<juce::String> v;
    if (! alive() || count() == 0) return v;
    const auto t = proc.rack.theory();
    const auto chord = proc.rack.harmony.seq[(size_t) selected()];
    for (auto& c : t.sortedColours (chord.deg)) { auto x = chord; x.t = c.s; x.inv = 0; v.push_back (juce::String (t.chordName (x)) + "  " + juce::String (n2c::colourLevelName (c.lv)).toLowerCase()); }
    return v;
}
std::vector<juce::String> BigChords::bassNames() const
{
    std::vector<juce::String> v;
    if (! alive() || count() == 0) return v;
    const auto t = proc.rack.theory();
    const auto chord = proc.rack.harmony.seq[(size_t) selected()];
    const int notes = (int) t.colourOf (chord).iv.size();
    for (int inv = 0; inv < juce::jmin (3, notes); ++inv) { auto x = chord; x.inv = inv; v.push_back (inv == 0 ? juce::String ("Root: ") + t.chordName (x) : juce::String (t.chordName (x))); }
    return v;
}
std::vector<juce::String> BigChords::degreeNames() const
{
    std::vector<juce::String> v;
    if (! alive()) return v;
    const auto t = proc.rack.theory();
    for (int d = 0; d < 7; ++d) { n2c::Chord c; c.deg = d; c.t = t.colourOf (c).s; v.push_back (t.chordName (c)); }
    return v;
}

void BigChords::setColour (int index)
{
    if (! alive() || count() == 0) return;
    const int i = selected();
    const auto cols = proc.rack.theory().sortedColours (proc.rack.harmony.seq[(size_t) i].deg);
    if (index < 0 || index >= (int) cols.size()) return;
    proc.history.beforeChords();
    proc.rack.setChordColour (i, cols[(size_t) index].s);
    changed();
}
void BigChords::setBass (int inv) { if (alive() && count() > 0 && inv >= 0 && inv <= 2) { proc.history.beforeChords(); proc.rack.setChordBass (selected(), inv); changed(); } }
void BigChords::setDegree (int deg) { if (alive() && count() > 0 && deg >= 0 && deg < 7) { proc.history.beforeChords(); proc.rack.setChordDegree (selected(), deg); changed(); } }
void BigChords::setBars (int b) { if (alive() && count() > 0 && b >= 1 && b <= 8) { proc.history.beforeChords(); proc.rack.setChordBars (selected(), b); changed(); } }
void BigChords::toggleLock() { if (alive() && count() > 0) { proc.history.beforeChords(); proc.rack.toggleChordLock (selected()); changed(); } }

// The picked chord's choices, laid out in rows under the tiles: a caption, then a button for each choice
std::vector<BigChords::Chip> BigChords::chips() const
{
    std::vector<Chip> v;
    if (! alive() || count() == 0) return v;
    const auto& rack = proc.rack;
    const auto t = rack.theory();
    const auto chord = rack.harmony.seq[(size_t) selected()];
    const auto f = controlFont();
    int y = tilesBottom() + 40;
    auto rowOf = [&] (int kind, const std::vector<juce::String>& names, const std::vector<int>& values, int current)
    {
        int x = 96;
        for (size_t k = 0; k < names.size(); ++k)
        {
            const int w = juce::GlyphArrangement::getStringWidthInt (f, names[k]) + 24;
            if (x + w > getWidth() && x > 96) { x = 96; y += 30; }
            v.push_back ({ { x, y, w, 26 }, kind, values[k], names[k], values[k] == current });
            x += w + 6;
        }
        y += 34;
    };
    const auto cols = t.sortedColours (chord.deg);
    int colNow = -1; std::vector<int> idx;
    for (size_t k = 0; k < cols.size(); ++k) { idx.push_back ((int) k); if (cols[k].s == t.colourOf (chord).s) colNow = (int) k; }
    rowOf (0, colourNames(), idx, colNow);
    const auto bass = bassNames();
    idx.clear(); for (size_t k = 0; k < bass.size(); ++k) idx.push_back ((int) k);
    rowOf (1, bass, idx, chord.inv);
    rowOf (2, degreeNames(), { 0, 1, 2, 3, 4, 5, 6 }, chord.deg);
    rowOf (3, { "1 bar", "2 bars", "4 bars", "8 bars" }, { 1, 2, 4, 8 }, chord.bars);
    rowOf (4, { chord.locked ? juce::String ("Locked: stays through Generate") : juce::String ("Lock (keep through Generate)") }, { 1 }, chord.locked ? 1 : 0);
    return v;
}

void BigChords::down (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    for (int i = 0; i < count(); ++i) if (tile (i).contains (p)) { select (i); return; }
    for (auto& c : chips())
        if (c.r.contains (p))
        {
            if (c.kind == 4) toggleLock();
            else if (c.on) return;                 // already so
            else if (c.kind == 0) setColour (c.value);
            else if (c.kind == 1) setBass (c.value);
            else if (c.kind == 2) setDegree (c.value);
            else if (c.kind == 3) setBars (c.value);
            return;
        }
}

void BigChords::ticked()
{
    const int s = module.playStep.load();
    if (s != shown) { shown = s; repaint(); }
}

uint64_t BigChords::signature() const
{
    uint64_t h = mix (1469598103934665603ull, (uint64_t) proc.chordsVersion.load());
    h = mix (h, (uint64_t) proc.rack.harmony.seq.size());
    for (auto& p : module.params) h = mix (h, bitsOf (p.load (std::memory_order_relaxed)));
    return mix (h, (uint64_t) picked);
}

void BigChords::draw (juce::Graphics& g)
{
    const auto& h = proc.rack.harmony;
    const auto& tl = proc.rack.timeline;
    const int n = count(), sel = selected();
    const int oct = (int) std::lround (module.P (module.paramIndex ("octave")));
    for (int i = 0; i < n; ++i)
    {
        const auto r = tile (i).toFloat();
        const auto col = degreeColour (h.seq[(size_t) i].deg);
        const bool now = i == shown;
        g.setColour (juce::Colour (0xff101116).interpolatedWith (col, i == sel ? 0.16f : 0.08f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (col.withAlpha (0.9f));
        g.fillRoundedRectangle (r.getX() + 5.0f, r.getY() + 2.0f, r.getWidth() - 10.0f, 2.5f, 1.2f);
        g.setFont (font (9.0f, Weight::Medium));
        g.setColour (n7);
        g.drawText (juce::String (i + 1), (int) r.getX() + 7, (int) r.getY() + 7, 24, 10, juce::Justification::centredLeft, false);
        const bool roomy = r.getHeight() >= 84.0f;
        g.setFont (font (roomy ? 16.0f : 13.0f, Weight::SemiBold));
        g.setColour (juce::Colours::white);
        g.drawFittedText (h.names[(size_t) i], (int) r.getX() + 6, (int) r.getY() + (roomy ? 14 : 6), (int) r.getWidth() - 12, roomy ? 20 : 15, juce::Justification::centred, 1, 0.7f);
        g.setFont (font (roomy ? 10.5f : 9.0f));
        g.setColour (n7);
        g.drawText (barsText (h.seq[(size_t) i].bars), (int) r.getX(), (int) r.getY() + (roomy ? 35 : 21), (int) r.getWidth(), 12, juce::Justification::centred, false);
        if (h.seq[(size_t) i].locked)
        {
            g.setColour (noteAccent);
            g.fillEllipse (r.getRight() - 12.0f, r.getY() + 8.0f, 5.5f, 5.5f);
        }
        // its notes on two octaves of keyboard
        if (i < (int) tl.slots.size())
        {
            const auto& slot = tl.slots[(size_t) i];
            std::vector<int> notes { 60 + (int) std::lround (slot.bass * 12.0f) };
            for (int k = 0; k < 4; ++k) notes.push_back (60 + (int) std::lround (slot.voice[k] * 12.0f) + 12 * oct);
            const int lowC = 48;
            auto lit = [&notes, lowC] (int note)
            {
                for (int nn : notes) { while (nn < lowC) nn += 12; while (nn >= lowC + 24) nn -= 12; if (nn == note) return true; }
                return false;
            };
            const float top = r.getY() + (roomy ? 52.0f : 34.0f);
            const auto kb = juce::Rectangle<float> (r.getX() + 6.0f, top, r.getWidth() - 12.0f, juce::jmin (84.0f, r.getBottom() - 8.0f - top));
            if (kb.getHeight() >= 10.0f) drawKeyboard (g, kb, lowC, 14, lit, now ? noteAccent : col);
        }
        if (i == sel) { g.setColour (accent); g.drawRoundedRectangle (r.reduced (1.0f), 6.0f, 2.0f); }
        if (now) { g.setColour (noteAccent); g.drawRoundedRectangle (r.reduced (i == sel ? 3.5f : 1.0f), 5.0f, 1.6f); }
    }
    if (n == 0) return;

    // the picked chord, and its choices
    const int y0 = tilesBottom() + 8;
    dividerH (g, 0, getWidth(), y0 - 6);
    g.setFont (titleFont());
    g.setColour (juce::Colours::white);
    g.drawText ("Chord " + juce::String (sel + 1) + ": " + juce::String (h.names[(size_t) sel]) + "   " + barsText (h.seq[(size_t) sel].bars), 0, y0, getWidth(), 22, juce::Justification::centredLeft, false);
    static const char* const captions[5] = { "Colour", "Bass note", "Change to", "Length", "Lock" };
    int lastKind = -1;
    for (auto& c : chips())
    {
        if (c.kind != lastKind) { lastKind = c.kind; caption (g, captions[c.kind], { 0, c.r.getY(), 90, c.r.getHeight() }); }
        if (c.on) sunken (g, c.r.toFloat().reduced (0.5f), radiusControl);
        else { g.setColour (n4); g.fillRoundedRectangle (c.r.toFloat().reduced (0.5f), radiusControl); g.setColour (materialRim); g.drawRoundedRectangle (c.r.toFloat().reduced (0.5f), radiusControl, 1.0f); }
        g.setFont (controlFont());
        g.setColour (c.on ? accent : n8);
        g.drawText (c.text, c.r, juce::Justification::centred, false);
    }
}

//============================================================================== 4. the sample
BigSample::BigSample (Nebula2ModulationProcessor& p, Board& b, Module& m) : BigView (p, b, m)
{
    for (auto* t : { &zoomIn, &zoomOut, &whole }) addAndMakeVisible (t);
    zoomIn.onClick = [this] { zoomBy (2.0f, 0.5f * (regionStart() + regionEnd())); };
    zoomOut.onClick = [this] { zoomBy (0.5f, 0.5f * (v0 + v1)); };
    whole.onClick = [this] { showWhole(); };
}

juce::String BigSample::help() const
{
    return "Drag in the wave to move whichever is nearer, the start or the end. The mouse wheel zooms in and out where the mouse is.";
}

void BigSample::resized() { zoomIn.setBounds (0, 0, 84, 26); zoomOut.setBounds (88, 0, 90, 26); whole.setBounds (182, 0, 96, 26); }
juce::Rectangle<int> BigSample::well() const { return { 0, 38, getWidth(), getHeight() - 38 - 24 }; }
float BigSample::xFor (float at) const { const auto w = well().toFloat(); return w.getX() + (at - v0) / juce::jmax (1.0e-6f, v1 - v0) * w.getWidth(); }
float BigSample::atFor (float x) const { const auto w = well().toFloat(); return v0 + (x - w.getX()) / juce::jmax (1.0f, w.getWidth()) * (v1 - v0); }
float BigSample::regionStart() const { return alive() ? module.P (0) : 0.0f; }
float BigSample::regionEnd() const { if (! alive()) return 1.0f; const float s = module.P (0); return s + module.P (1) * (1.0f - s); }

void BigSample::rawRegion (float from, float to)
{
    from = juce::jlimit (0.0f, 0.98f, from);
    to = juce::jlimit (from + 0.002f, 1.0f, to);
    setKnob (0, from);
    setKnob (1, (to - from) / juce::jmax (1.0e-6f, 1.0f - from));      // Length is a share of what is left after Start
}

void BigSample::setRegion (float from, float to) { if (alive()) { begin(); rawRegion (from, to); end(); } }

void BigSample::zoomBy (float factor, float around)
{
    const float span = v1 - v0, fresh = juce::jlimit (0.002f, 1.0f, span / juce::jmax (0.01f, factor));
    around = juce::jlimit (0.0f, 1.0f, around);
    float a = around - (around - v0) * (fresh / span);
    a = juce::jlimit (0.0f, 1.0f - fresh, a);
    v0 = a; v1 = a + fresh;
    repaint();
}
void BigSample::showWhole() { v0 = 0.0f; v1 = 1.0f; repaint(); }

void BigSample::down (const juce::MouseEvent& e)
{
    dragEnd = 0;
    auto* sm = dynamic_cast<SampleModule*> (&module);
    if (sm == nullptr || ! sm->hasSample() || ! well().toFloat().contains (e.position)) return;
    const float s = regionStart(), en = regionEnd();
    dragEnd = std::abs (e.position.x - xFor (s)) <= std::abs (e.position.x - xFor (en)) ? 1 : 2;
    begin();
    dragged (e);
}

void BigSample::dragged (const juce::MouseEvent& e)
{
    if (dragEnd == 0) return;
    const float at = juce::jlimit (0.0f, 1.0f, atFor (e.position.x));
    if (dragEnd == 1) rawRegion (juce::jmin (at, regionEnd() - 0.002f), regionEnd());
    else rawRegion (regionStart(), juce::jmax (at, regionStart() + 0.002f));
    repaint();
}

void BigSample::up (const juce::MouseEvent&) { if (dragEnd != 0) { dragEnd = 0; end(); } }

void BigSample::wheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (w.deltaY != 0.0f) zoomBy (w.deltaY > 0.0f ? 1.25f : 0.8f, atFor (e.position.x));
}

void BigSample::ticked()
{
    auto* sm = dynamic_cast<SampleModule*> (&module);
    const float h = sm != nullptr ? sm->playhead.load() : -1.0f;
    if (std::abs (h - shownHead) > 0.0005f) { shownHead = h; repaint(); }
}

uint64_t BigSample::signature() const
{
    uint64_t h = 1469598103934665603ull;
    for (auto& p : module.params) h = mix (h, bitsOf (p.load (std::memory_order_relaxed)));
    if (auto* sm = dynamic_cast<SampleModule*> (&module)) h = mix (h, (uint64_t) sm->fine.size() + (uint64_t) sm->fileName().hashCode64());
    return h;
}

void BigSample::draw (juce::Graphics& g)
{
    auto* sm = dynamic_cast<SampleModule*> (&module);
    if (sm == nullptr) return;
    const auto col = boardColour (board.colour);
    const auto w = well().toFloat();
    const bool failed = sm->status.isNotEmpty();
    const double secs = sm->seconds();
    g.setFont (controlFont());
    g.setColour (failed ? bandOrange : sm->hasSample() ? n8 : n7);
    const float s = regionStart(), en = regionEnd();
    const juce::String text = failed ? sm->status
                            : sm->hasSample() ? sm->fileName() + "   " + juce::String (secs, 2) + " s      Plays from " + juce::String (s * secs, 3) + " s to " + juce::String (en * secs, 3) + " s  (" + juce::String ((en - s) * secs, 3) + " s)"
                            : juce::String ("No file yet: use Load on the unit, or drop an audio file on it");
    g.drawText (text, 292, 0, getWidth() - 292, 26, juce::Justification::centredLeft, true);
    sunken (g, w, radiusControl);
    if (sm->fine.empty()) return;

    // the part that plays, shaded, with the rest dim
    const float xs = juce::jlimit (w.getX(), w.getRight(), xFor (s)), xe = juce::jlimit (w.getX(), w.getRight(), xFor (en));
    g.setColour (col.withAlpha (0.07f));
    g.fillRect (xs, w.getY() + 1.0f, juce::jmax (0.0f, xe - xs), w.getHeight() - 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (w.getX() + 2.0f, w.getCentreY(), w.getWidth() - 4.0f, 1.0f);
    const int n = (int) sm->fine.size(), px = (int) w.getWidth();
    for (int x = 1; x < px - 1; ++x)
    {
        const float a0 = atFor (w.getX() + (float) x), a1 = atFor (w.getX() + (float) x + 1.0f);
        const int k0 = juce::jlimit (0, n - 1, (int) std::floor (a0 * (float) n)), k1 = juce::jlimit (k0, n - 1, (int) std::ceil (a1 * (float) n) - 1);
        float pk = 0.0f;
        for (int k = k0; k <= k1; ++k) pk = juce::jmax (pk, sm->fine[(size_t) k]);
        const float hgt = juce::jmax (1.0f, juce::jmin (1.0f, pk) * (w.getHeight() - 12.0f));
        const float at = 0.5f * (a0 + a1);
        g.setColour (at >= s && at <= en ? col.withAlpha (0.9f) : n5);
        g.fillRect (w.getX() + (float) x, w.getCentreY() - hgt * 0.5f, 1.0f, hgt);
    }
    // the start and the end: a line each, with a flag
    for (int k = 0; k < 2; ++k)
    {
        const float at = k == 0 ? s : en, x = xFor (at);
        if (x < w.getX() - 1.0f || x > w.getRight() + 1.0f) continue;
        g.setColour (accent);
        g.fillRect (x - 0.75f, w.getY() + 2.0f, 1.5f, w.getHeight() - 4.0f);
        const auto flag = k == 0 ? juce::Rectangle<float> (x, w.getY() + 2.0f, 40.0f, 15.0f) : juce::Rectangle<float> (x - 32.0f, w.getY() + 2.0f, 32.0f, 15.0f);
        g.fillRoundedRectangle (flag, 3.0f);
        g.setFont (font (8.5f, Weight::SemiBold, 0.3f));
        g.setColour (n0);
        g.drawText (k == 0 ? "START" : "END", flag.toNearestInt(), juce::Justification::centred, false);
    }
    if (shownHead >= 0.0f)
    {
        const float x = xFor (shownHead);
        if (x >= w.getX() && x <= w.getRight()) { g.setColour (noteAccent); g.fillRect (x - 0.75f, w.getY() + 2.0f, 1.5f, w.getHeight() - 4.0f); }
    }
    // what is in view, in seconds
    g.setFont (helpFont());
    g.setColour (n7);
    const int ty = (int) w.getBottom() + 4;
    g.drawText (juce::String (v0 * secs, 3) + " s", 0, ty, 120, 16, juce::Justification::centredLeft, false);
    g.drawText (juce::String (v1 * secs, 3) + " s", getWidth() - 120, ty, 120, 16, juce::Justification::centredRight, false);
    g.drawText (v1 - v0 >= 0.999f ? juce::String ("The whole file") : "Zoomed in " + juce::String (1.0f / (v1 - v0), 1) + " times", getWidth() / 2 - 150, ty, 300, 16, juce::Justification::centred, false);
}

//============================================================================== 5. the EQ
static const juce::Colour& eqColour (int band)
{
    static const juce::Colour c[4] = { bandMint, bandBlue, bandPurple, bandOrange };
    return c[juce::jlimit (0, 3, band)];
}

juce::String BigEq::help() const
{
    return "Drag a band's point: left and right is where, up and down is how much. The mouse wheel, or a right-button drag up and down, sets how wide it is. Double-click a point to set it flat.";
}

juce::Rectangle<int> BigEq::plot() const { return { 46, 10, getWidth() - 46 - 14, getHeight() - 10 - 74 }; }

juce::Point<float> BigEq::pointOf (int band) const
{
    const auto p = plot().toFloat();
    const float f = alive() ? module.P (band * 3) : 0.5f, gn = alive() ? module.P (band * 3 + 1) : 0.0f;
    return { p.getX() + f * p.getWidth(), p.getCentreY() - gn / topDb * p.getHeight() * 0.5f };
}

// One band's gain, in dB, at a frequency: worked out from the very filter the unit runs
static float bandDb (const Module& m, int band, double hz, double sr)
{
    const float gn = m.P (band * 3 + 1);
    if (gn == 0.0f) return 0.0f;
    Biquad b;
    b.peak (cutoffHz (m.P (band * 3)), gn, 1.0 / std::max (0.1f, m.P (band * 3 + 2)) * 1.4, sr);
    const double w = 2.0 * kPi * std::min (sr * 0.499, hz) / sr, c1 = std::cos (w), c2 = std::cos (2.0 * w);
    const double num = (double) b.b0 * b.b0 + (double) b.b1 * b.b1 + (double) b.b2 * b.b2 + 2.0 * ((double) b.b0 * b.b1 + (double) b.b1 * b.b2) * c1 + 2.0 * (double) b.b0 * b.b2 * c2;
    const double den = 1.0 + (double) b.a1 * b.a1 + (double) b.a2 * b.a2 + 2.0 * ((double) b.a1 + (double) b.a1 * b.a2) * c1 + 2.0 * (double) b.a2 * c2;
    return (float) (10.0 * std::log10 (std::max (1.0e-12, num) / std::max (1.0e-12, den)));
}

float BigEq::curveDb (float freq) const
{
    if (! alive()) return 0.0f;
    float db = 0.0f;
    for (int k = 0; k < 4; ++k) db += bandDb (module, k, cutoffHz (freq), proc.rack.sr);
    return db;
}

void BigEq::setBand (int band, float freq, float gainDb)
{
    if (! alive() || band < 0 || band > 3) return;
    begin();
    setKnob (band * 3, freq); setKnob (band * 3 + 1, gainDb);
    end();
}

void BigEq::setWidth (int band, float width)
{
    if (! alive() || band < 0 || band > 3) return;
    begin();
    setKnob (band * 3 + 2, width);
    end();
}

int BigEq::bandNear (juce::Point<float> p) const
{
    int best = -1; float bestD = 20.0f;
    for (int k = 0; k < 4; ++k) { const float d = pointOf (k).getDistanceFrom (p); if (d < bestD) { bestD = d; best = k; } }
    return best;
}

void BigEq::down (const juce::MouseEvent& e)
{
    dragBand = bandNear (e.position);
    if (dragBand < 0) return;
    lastBand = dragBand;
    dragWidth = e.mods.isPopupMenu() || e.mods.isShiftDown();
    widthStart = paramToNorm (module.info->params[(size_t) (dragBand * 3 + 2)], module.P (dragBand * 3 + 2));
    begin();
    repaint();
}

void BigEq::dragged (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    const auto p = plot().toFloat();
    if (dragWidth) setKnob (dragBand * 3 + 2, paramFromNorm (module.info->params[(size_t) (dragBand * 3 + 2)], widthStart - (float) e.getDistanceFromDragStartY() / 180.0f));
    else
    {
        setKnob (dragBand * 3, juce::jlimit (0.0f, 1.0f, (e.position.x - p.getX()) / p.getWidth()));
        float gn = (p.getCentreY() - e.position.y) / (p.getHeight() * 0.5f) * topDb;
        if (std::abs (gn) < 0.25f) gn = 0.0f;                    // flat is easy to find
        setKnob (dragBand * 3 + 1, gn);
    }
    repaint();
}

void BigEq::up (const juce::MouseEvent&) { if (dragBand >= 0) { dragBand = -1; end(); } }

void BigEq::doubleClicked (const juce::MouseEvent& e)
{
    const int k = bandNear (e.position);
    if (k >= 0) setBand (k, module.P (k * 3), 0.0f);
}

void BigEq::wheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (w.deltaY == 0.0f) return;
    int k = bandNear (e.position);
    if (k < 0) k = lastBand;
    lastBand = k;
    const auto& p = module.info->params[(size_t) (k * 3 + 2)];
    // a run of wheel clicks is one Undo step
    const auto now = juce::Time::getMillisecondCounter();
    if (now - wheelTime > 600) begin();
    wheelTime = now;
    setKnob (k * 3 + 2, paramFromNorm (p, paramToNorm (p, module.P (k * 3 + 2)) + (w.deltaY > 0.0f ? 0.04f : -0.04f)));
    repaint();
    if (onEdited) onEdited();
}

uint64_t BigEq::signature() const
{
    uint64_t h = 1469598103934665603ull;
    for (auto& p : module.params) h = mix (h, bitsOf (p.load (std::memory_order_relaxed)));
    return h;
}

void BigEq::draw (juce::Graphics& g)
{
    const auto p = plot().toFloat();
    const double sr = proc.rack.sr;
    g.setColour (kGlass);
    g.fillRoundedRectangle (p, 4.0f);
    auto yFor = [&p] (float db) { return p.getCentreY() - juce::jlimit (-topDb, topDb, db) / topDb * p.getHeight() * 0.5f; };
    // the grid: frequencies across, dB up and down
    static const float marks[10] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    static const char* const markText[10] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    for (int k = 0; k < 10; ++k)
    {
        const float x = p.getX() + std::log (marks[k] / 20.0f) / std::log (1000.0f) * p.getWidth();
        if (k > 0 && k < 9) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRect (x, p.getY() + 1.0f, 1.0f, p.getHeight() - 2.0f); }
        g.setFont (font (9.5f, Weight::Medium));
        g.setColour (n7);
        g.drawText (markText[k], (int) x - 20, (int) p.getBottom() + 3, 40, 12, k == 0 ? juce::Justification::centredRight : k == 9 ? juce::Justification::centredLeft : juce::Justification::centred, false);
    }
    for (int db = -18; db <= 18; db += 6)
    {
        const float y = yFor ((float) db);
        g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.16f : 0.06f));
        g.fillRect (p.getX() + 1.0f, y, p.getWidth() - 2.0f, 1.0f);
        g.setFont (font (9.5f, Weight::Medium));
        g.setColour (n7);
        g.drawText (minus (juce::String (db > 0 ? "+" : "") + juce::String (db)), 0, (int) y - 7, 40, 14, juce::Justification::centredRight, false);
    }
    // each band's own shape, faint, then what they add up to
    const int px = (int) p.getWidth();
    for (int k = 0; k < 4; ++k)
    {
        if (module.P (k * 3 + 1) == 0.0f) continue;
        juce::Path one;
        for (int x = 0; x <= px; x += 2)
        {
            const float y = yFor (bandDb (module, k, cutoffHz ((float) x / (float) px), sr));
            if (x == 0) one.startNewSubPath (p.getX(), y); else one.lineTo (p.getX() + (float) x, y);
        }
        g.setColour (eqColour (k).withAlpha (0.35f));
        g.strokePath (one, juce::PathStrokeType (1.0f));
    }
    juce::Path sum, fill;
    for (int x = 0; x <= px; ++x)
    {
        const float y = yFor (curveDb ((float) x / (float) px));
        if (x == 0) { sum.startNewSubPath (p.getX(), y); fill.startNewSubPath (p.getX(), p.getCentreY()); fill.lineTo (p.getX(), y); }
        else { sum.lineTo (p.getX() + (float) x, y); fill.lineTo (p.getX() + (float) x, y); }
    }
    fill.lineTo (p.getRight(), p.getCentreY()); fill.closeSubPath();
    g.setColour (accent.withAlpha (0.10f));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (sum, juce::PathStrokeType (1.8f));
    // the points
    for (int k = 0; k < 4; ++k)
    {
        const auto c = pointOf (k);
        const bool held = k == dragBand;
        g.setColour (eqColour (k).withAlpha (held ? 0.35f : 0.18f));
        g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (c));
        g.setColour (eqColour (k));
        g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (c));
        g.setFont (font (10.5f, Weight::SemiBold));
        g.setColour (n0);
        g.drawText (juce::String (k + 1), juce::Rectangle<float> (18.0f, 18.0f).withCentre (c).toNearestInt(), juce::Justification::centred, false);
    }
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (p.reduced (0.5f), 4.0f, 1.0f);
    // each band in words
    const int colW = getWidth() / 4, ty = getHeight() - 48;
    for (int k = 0; k < 4; ++k)
    {
        const auto& info = *module.info;
        const int x = k * colW;
        g.setColour (eqColour (k));
        g.fillEllipse ((float) x + 4.0f, (float) ty + 5.0f, 10.0f, 10.0f);
        g.setFont (controlFont());
        g.setColour (n8);
        g.drawText ("Band " + juce::String (k + 1), x + 22, ty, colW - 24, 20, juce::Justification::centredLeft, false);
        g.setFont (valueFont());
        g.setColour (n7);
        g.drawText (minus (juce::String (paramText (info.params[(size_t) (k * 3)], module.P (k * 3))) + "     " + juce::String (paramText (info.params[(size_t) (k * 3 + 1)], module.P (k * 3 + 1))) + "     width " + juce::String (module.P (k * 3 + 2), 2)),
                    x + 22, ty + 20, colW - 24, 18, juce::Justification::centredLeft, true);
    }
}

//============================================================================== 7. the scope
BigScope::BigScope (Nebula2ModulationProcessor& p, Board& b, Module& m) : BigView (p, b, m)
{
    addAndMakeVisible (freeze);
    freeze.onClick = [this] { setFrozen (! held); };
}

juce::String BigScope::help() const { return "The sound passing through this unit, and the note it is playing. Freeze holds the picture still."; }
void BigScope::resized() { freeze.setBounds (getWidth() - 90, 0, 90, 26); }

void BigScope::setFrozen (bool on)
{
    if (! alive()) return;
    held = on;
    freeze.setToggleState (on, juce::dontSendNotification);
    if (auto* sc = dynamic_cast<Scope*> (&module); sc != nullptr && on)
    {
        // the picture as it is now, started from an upward crossing as the scope draws it
        const int n = Scope::size, head = sc->head.load();
        int start = (head + 1) % n;
        for (int k = 1; k < n / 2; ++k)
        {
            const float a = sc->ring[(size_t) ((head + k) % n)], b = sc->ring[(size_t) ((head + k + 1) % n)];
            if (a <= 0.0f && b > 0.0f) { start = (head + k + 1) % n; break; }
        }
        kept.resize ((size_t) (n / 2));
        for (int k = 0; k < n / 2; ++k) kept[(size_t) k] = sc->ring[(size_t) ((start + k) % n)];
        keptHz = sc->hz.load();
    }
    repaint();
}

void BigScope::ticked()
{
    auto* sc = dynamic_cast<Scope*> (&module);
    if (sc == nullptr || held) return;
    const bool quiet = sc->level.load() < 0.0005f;
    if (! quiet || ! wasQuiet) repaint();
    wasQuiet = quiet;
}

void BigScope::draw (juce::Graphics& g)
{
    auto* sc = dynamic_cast<Scope*> (&module);
    if (sc == nullptr) return;
    const auto col = boardColour (board.colour);
    // the tuner, across the top: the note in large letters, how far off it is on a scale, and the frequency
    const float hz = held ? keptHz : sc->hz.load();
    int cents = 0;
    const auto name = noteText (hz, &cents);
    const bool inTune = ! name.empty() && std::abs (cents) <= 5;
    g.setFont (font (64.0f, Weight::SemiBold));
    g.setColour (name.empty() ? n6 : inTune ? bandGreen : juce::Colours::white);
    g.drawText (name.empty() ? juce::String ("--") : juce::String (name), 0, 0, 220, 84, juce::Justification::centredLeft, false);
    const auto scale = juce::Rectangle<float> (240.0f, 40.0f, juce::jmin (420.0f, (float) getWidth() - 240.0f - 330.0f), 10.0f);
    if (scale.getWidth() > 120.0f)
    {
        sunken (g, scale, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (scale.getCentreX() - 0.5f, scale.getY() - 5.0f, 1.0f, scale.getHeight() + 10.0f);
        if (! name.empty())
        {
            const float x = scale.getCentreX() + (float) juce::jlimit (-50, 50, cents) / 50.0f * (scale.getWidth() * 0.5f - 6.0f);
            g.setColour (inTune ? bandGreen : noteAccent);
            g.fillRoundedRectangle (x - 4.0f, scale.getY() - 4.0f, 8.0f, scale.getHeight() + 8.0f, 3.0f);
        }
        g.setFont (font (9.5f, Weight::Medium));
        g.setColour (n7);
        g.drawText (minus ("-50"), (int) scale.getX() - 4, (int) scale.getBottom() + 6, 40, 12, juce::Justification::centredLeft, false);
        g.drawText ("+50", (int) scale.getRight() - 36, (int) scale.getBottom() + 6, 40, 12, juce::Justification::centredRight, false);
        g.setFont (font (20.0f, Weight::SemiBold));
        g.setColour (name.empty() ? n6 : inTune ? bandGreen : n8);
        g.drawText (name.empty() ? juce::String ("no note") : cents == 0 ? juce::String ("in tune") : minus (juce::String (cents > 0 ? "+" : "") + juce::String (cents) + " cents"),
                    (int) scale.getX(), 6, (int) scale.getWidth(), 26, juce::Justification::centred, false);
    }
    g.setFont (font (30.0f, Weight::Medium));
    g.setColour (name.empty() ? n6 : n8);
    g.drawText (name.empty() ? juce::String() : juce::String (hz, 1) + " Hz", getWidth() - 320, 22, 210, 40, juce::Justification::centredRight, false);

    const auto well = juce::Rectangle<float> (0.0f, 96.0f, (float) getWidth(), (float) getHeight() - 96.0f);
    g.setColour (kGlass);
    g.fillRoundedRectangle (well, 6.0f);
    for (int k = 1; k < 8; ++k) { g.setColour (juce::Colours::white.withAlpha (k == 4 ? 0.10f : 0.04f)); g.fillRect (well.getX() + 4.0f, well.getY() + well.getHeight() * (float) k / 8.0f, well.getWidth() - 8.0f, 1.0f); }
    for (int k = 1; k < 10; ++k) { g.setColour (juce::Colours::white.withAlpha (0.04f)); g.fillRect (well.getX() + well.getWidth() * (float) k / 10.0f, well.getY() + 4.0f, 1.0f, well.getHeight() - 8.0f); }

    const int n = Scope::size, shown = n / 2;
    std::vector<float> now;
    if (held && (int) kept.size() == shown) now = kept;
    else
    {
        const int head = sc->head.load();
        int start = (head + 1) % n;
        for (int k = 1; k < n / 2; ++k)
        {
            const float a = sc->ring[(size_t) ((head + k) % n)], b = sc->ring[(size_t) ((head + k + 1) % n)];
            if (a <= 0.0f && b > 0.0f) { start = (head + k + 1) % n; break; }
        }
        now.resize ((size_t) shown);
        for (int k = 0; k < shown; ++k) now[(size_t) k] = sc->ring[(size_t) ((start + k) % n)];
    }
    juce::Path p;
    for (int k = 0; k < shown; ++k)
    {
        const float v = juce::jlimit (-1.0f, 1.0f, now[(size_t) k]);
        const float x = well.getX() + 6.0f + (well.getWidth() - 12.0f) * (float) k / (float) (shown - 1);
        const float y = well.getCentreY() - v * (well.getHeight() * 0.5f - 10.0f);
        if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col.withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col.withAlpha (0.22f));
    g.drawRoundedRectangle (well.reduced (0.5f), 6.0f, 1.0f);
    g.setFont (helpFont());
    g.setColour (n7);
    g.drawText (held ? "Frozen" : "About 21 ms across", (int) well.getX() + 10, (int) well.getBottom() - 22, 200, 16, juce::Justification::centredLeft, false);
}

//============================================================================== the panel
BigPanel::BigPanel (Nebula2ModulationProcessor& p) : proc (p)
{
    for (auto* b : { &closeButton, &undo, &redo, &play }) addAndMakeVisible (b);
    closeButton.onClick = [this] { close(); };
    undo.onClick = [this] { if (onUndo) onUndo (false); };
    redo.onClick = [this] { if (onUndo) onUndo (true); };
    play.onClick = [this] { if (onPlay) onPlay(); };
    setVisible (false);
}

bool BigPanel::has (const Info& info)
{
    const std::string t = info.type;
    return t == "noteseq" || t == "drumseq" || t == "chords" || t == "sample" || t == "parameq" || t == "stepmod" || t == "scope";
}

bool BigPanel::open (Board& b, Module& m)
{
    close();
    if (! has (*m.info)) return false;
    const std::string t = m.info->type;
    if (t == "noteseq") view = std::make_unique<BigNoteRoll> (proc, b, m);
    else if (t == "drumseq") view = std::make_unique<BigDrumGrid> (proc, b, m);
    else if (t == "stepmod") view = std::make_unique<BigStepMod> (proc, b, m);
    else if (t == "chords") view = std::make_unique<BigChords> (proc, b, m);
    else if (t == "sample") { if (dynamic_cast<SampleModule*> (&m) != nullptr) view = std::make_unique<BigSample> (proc, b, m); }
    else if (t == "parameq") view = std::make_unique<BigEq> (proc, b, m);
    else if (t == "scope") { if (dynamic_cast<Scope*> (&m) != nullptr) view = std::make_unique<BigScope> (proc, b, m); }
    if (view == nullptr || ! view->alive()) { view.reset(); return false; }
    juce::Component::SafePointer<BigPanel> safe (this);
    view->onEdited = [safe] { if (safe != nullptr && safe->onEdited) safe->onEdited(); };
    title = juce::String (m.info->name) + ": " + juce::String (b.name);
    addAndMakeVisible (*view);
    setVisible (true);
    toFront (false);
    resized();
    tick();
    repaint();
    return true;
}

void BigPanel::close()
{
    view.reset();
    setVisible (false);
}

void BigPanel::check()
{
    if (view == nullptr) return;
    if (! view->alive()) { close(); return; }
    view->repaint();
}

void BigPanel::tick()
{
    if (view == nullptr) return;
    if (! view->alive()) { close(); return; }
    undo.setEnabled (proc.history.canUndo());
    redo.setEnabled (proc.history.canRedo());
    const bool host = proc.hostPlaying.load(), on = proc.isPlaying();
    const juce::String text = host ? "With Cubase" : on ? "Stop" : "Play";
    if (play.getButtonText() != text || play.getToggleState() != on) { play.setButtonText (text); play.setToggleState (on, juce::dontSendNotification); }
    view->tick();
}

juce::Rectangle<int> BigPanel::card() const
{
    const int w = juce::jmin (getWidth() - 40, 1800), h = juce::jmin (getHeight() - 40, 1040);
    return juce::Rectangle<int> (juce::jmax (200, w), juce::jmax (200, h)).withCentre (getLocalBounds().getCentre());
}

void BigPanel::resized()
{
    const auto c = card();
    const int y = c.getY() + 14;
    closeButton.setBounds (c.getRight() - 20 - 80, y, 80, 28);
    play.setBounds (closeButton.getX() - 16 - 104, y, 104, 28);
    redo.setBounds (play.getX() - 8 - 60, y, 60, 28);
    undo.setBounds (redo.getX() - 6 - 60, y, 60, 28);
    if (view != nullptr) view->setBounds (c.getX() + 20, c.getY() + 68, c.getWidth() - 40, c.getHeight() - 68 - 20);
}

void BigPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto c = card();
    raised (g, c.toFloat(), radiusPanel);
    g.setFont (titleFont());
    g.setColour (juce::Colours::white);
    g.drawText (title, c.getX() + 20, c.getY() + 12, undo.getX() - c.getX() - 40, 20, juce::Justification::centredLeft, true);
    g.setFont (helpFont());
    g.setColour (n7);
    if (view != nullptr) g.drawFittedText (view->help(), c.getX() + 20, c.getY() + 32, undo.getX() - c.getX() - 40, 30, juce::Justification::topLeft, 2, 0.9f);
}

void BigPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition())) close();
}
} // namespace n2m
