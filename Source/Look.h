#pragma once
// The ParaEQ design system: tokens, fonts, the two materials and the engraved divider.
// Values come from the system's tokens.json and Implementation notes. Change them here only. Shared with Nebula2 Chord.
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace n2m::look
{
// ---- Colour tokens ----
inline const juce::Colour windowTop     { 0xff292c36 };
inline const juce::Colour windowBottom  { 0xff1c1e26 };
inline const juce::Colour surface       { 0xff23262f };
// Controls are darker than the panel, with flat faces (design system, 24 Nov 2026)
inline const juce::Colour surfaceHover  { 0xff1f2129 };
inline const juce::Colour raisedHi      { 0xff191b22 };
inline const juce::Colour raisedHiHover = surfaceHover;
inline const juce::Colour raisedLo      { 0xff191b22 };
inline const juce::Colour sunkenHi      { 0xff16181d };
inline const juce::Colour sunkenLo      { 0xff16181d };
// Knob body keeps its own lit fade. The hover colour has no token: knobHi + 11 per channel, as ParaEQ.
inline const juce::Colour knobHi        { 0xff272a33 };
inline const juce::Colour knobHiHover   { 0xff32353e };
inline const juce::Colour knobLo        { 0xff1c1e25 };
inline const juce::Colour materialRim   { 0x33000000 };   // flat 1px rim, black 20%
inline const juce::Colour n0 { 0xff0a0b15 }, n4 { 0xff2a2c3c }, n5 { 0xff373b4c },
                          n6 { 0xff4b5362 }, n7 { 0xff8f959e }, n8 { 0xffc6d0d3 };
inline const juce::Colour accent        { 0xff74eefb };
inline const juce::Colour bandMint      { 0xff74fab3 };
inline const juce::Colour bandOrange    { 0xfffab476 };
inline const juce::Colour bandPurple    { 0xffb173f8 };
inline const juce::Colour bandPink      { 0xfffb758f };
inline const juce::Colour bandGreen     { 0xff8ffc78 };
inline const juce::Colour bandYellow    { 0xffebfd74 };

inline const juce::Colour bandBlue      { 0xff798ffb };
inline const juce::Colour bandMagenta   { 0xfffa74e9 };
inline const juce::Colour noteAccent    = bandYellow;   // whatever is playing now

// Each scale degree keeps one band colour, on its tile and its key (in D minor: D mint, E blue, F purple ...)
inline juce::Colour degreeColour (int d)
{
    static const juce::Colour c[7] = { bandMint, bandBlue, bandPurple, bandPink, bandOrange, bandYellow, bandGreen };
    return c[((d % 7) + 7) % 7];
}

// Keyboard: the only light part in the system
inline const juce::Colour whiteKeyTop { 0xffdce0e6 }, whiteKeyBottom { 0xffb4bac4 };
inline const juce::Colour blackKeyTop { 0xff2a2d36 }, blackKeyBottom { 0xff15171d };

// ---- Size tokens ----
constexpr int windowW = 1240, windowH = 780, topbarH = 44, controlH = 24;
constexpr int space1 = 4, space2 = 8, space3 = 12, space4 = 16, space6 = 24;
constexpr float radiusXs = 2.0f, radiusChip = 4.0f, radiusControl = 6.0f, radiusCard = 8.0f, radiusPanel = 10.0f;
constexpr int knobM = 48;
constexpr int tileW = 68, tileH = 72;

// ---- Type: Inter in three weights ----
enum class Weight { Regular, Medium, SemiBold };

inline juce::Typeface::Ptr typeface (Weight w)
{
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
    static juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_ttf, BinaryData::InterMedium_ttfSize);
    static juce::Typeface::Ptr semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
    switch (w) { case Weight::Medium: return medium; case Weight::SemiBold: return semibold; case Weight::Regular: break; }
    return regular;
}

// size is the design's pixel size (JUCE height matches the design's Inter widths); tracking in px
inline juce::Font font (float size, Weight w = Weight::Regular, float tracking = 0.0f)
{
    return juce::Font (juce::FontOptions (typeface (w)).withHeight (size).withKerningFactor (tracking / size));
}

inline juce::Font titleFont()   { return font (14.0f, Weight::SemiBold); }
inline juce::Font controlFont() { return font (12.0f, Weight::Medium); }
inline juce::Font valueFont()   { return font (12.0f, Weight::Regular); }
inline juce::Font labelFont()   { return font (10.0f, Weight::Medium, 0.6f); }
inline juce::Font helpFont()    { return font (11.0f, Weight::Regular); }

// Real minus sign for negative numbers
inline juce::String minus (juce::String s) { return s.replace ("-", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))); }

// ---- Materials (soft::raised / soft::sunken). s = part height, or diameter for round parts ----
inline void shadow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float radius, float dx, float dy)
{
    juce::DropShadow (c, juce::jmax (1, juce::roundToInt (radius)), { juce::roundToInt (dx), juce::roundToInt (dy) }).drawForPath (g, p);
}

inline void raised (juce::Graphics& g, const juce::Path& p, float s, bool hover = false)
{
    const auto b = p.getBounds();
    shadow (g, p, juce::Colours::black.withAlpha (0.55f), 0.45f * s, 0.07f * s, 0.15f * s);
    shadow (g, p, juce::Colours::black.withAlpha (0.35f), 0.10f * s, 0.0f, 0.04f * s);
    shadow (g, p, juce::Colours::white.withAlpha (0.045f), 0.30f * s, -0.06f * s, -0.09f * s);
    g.setGradientFill (juce::ColourGradient (hover ? raisedHiHover : raisedHi, b.getTopLeft(), raisedLo, b.getBottomRight(), false));
    g.fillPath (p);
    g.setColour (materialRim);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

inline void sunken (juce::Graphics& g, const juce::Path& p, float s)
{
    const auto b = p.getBounds();
    g.setGradientFill (juce::ColourGradient (sunkenHi, b.getTopLeft(), sunkenLo, b.getBottomRight(), false));
    g.fillPath (p);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (p);
        juce::Path outside;
        outside.addRectangle (b.expanded (s + 20.0f));
        outside.addPath (p);
        outside.setUsingNonZeroWinding (false);
        shadow (g, outside, juce::Colours::black.withAlpha (0.45f), 0.25f * s, 0.05f * s, 0.09f * s);
    }
    g.setColour (materialRim);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

inline juce::Path rounded (juce::Rectangle<float> r, float radius)
{
    juce::Path p;
    p.addRoundedRectangle (r, radius);
    return p;
}

inline void raised (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool hover = false)
{
    raised (g, rounded (r, radius), r.getHeight(), hover);
}

// Big wells keep a control-sized inner shadow
inline void sunken (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    sunken (g, rounded (r, radius), std::min (r.getHeight(), 40.0f));
}

// Engraved divider: 1px black 35% with 1px white 5% beside it
inline void dividerH (juce::Graphics& g, int x0, int x1, int y)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (x0, y, x1 - x0, 1);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (x0, y + 1, x1 - x0, 1);
}

inline void dividerV (juce::Graphics& g, int x, int y0, int y1)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (x, y0, 1, y1 - y0);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (x + 1, y0, 1, y1 - y0);
}

inline void caption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                     juce::Justification j = juce::Justification::centredLeft)
{
    g.setFont (labelFont());
    g.setColour (n7);
    g.drawText (text.toUpperCase(), r, j, false);
}

inline void led (juce::Graphics& g, juce::Point<float> c, bool on)
{
    const auto r = juce::Rectangle<float> (6.0f, 6.0f).withCentre (c);
    if (on)
    {
        juce::DropShadow (accent.withAlpha (0.8f), 6, {}).drawForRectangle (g, r.toNearestInt());
        g.setColour (accent);
    }
    else
        g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (r);
}

//==============================================================================
class SoftLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SoftLookAndFeel()
    {
        setDefaultSansSerifTypeface (typeface (Weight::Regular));
        setColour (juce::ComboBox::textColourId, n8);
        setColour (juce::PopupMenu::textColourId, n8);
        setColour (juce::PopupMenu::highlightedTextColourId, n8);
        setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::textColourId, juce::Colours::white);
        setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.25f));
        setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::CaretComponent::caretColourId, accent);
        setColour (juce::ScrollBar::thumbColourId, n6);
    }

    // ---- Knob: raised body, ticks outside, sunken dimple with an accent dot ----
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto c = juce::Rectangle<int> (x, y, w, h).toFloat().getCentre();
        const float D = (float) knobM;
        const bool bipolar = s.getProperties()["bipolar"];
        const bool dragging = s.isMouseButtonDown();
        const bool hover = s.isMouseOverOrDragging();

        // Ticks
        const float outer = D * 0.5f + (float) std::max (8, juce::roundToInt (0.17f * D));
        const float len = std::max (4.0f, 0.07f * D);
        const int n = 25;
        const float valueAngle = startAngle + pos * (endAngle - startAngle);
        const float centreAngle = (startAngle + endAngle) * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float a = startAngle + (float) i / (float) (n - 1) * (endAngle - startAngle);
            bool lit;
            if (bipolar)
            {
                const float lo = std::min (centreAngle, valueAngle), hi = std::max (centreAngle, valueAngle);
                lit = std::abs (valueAngle - centreAngle) > 0.01f && a >= lo - 0.001f && a <= hi + 0.001f;
            }
            else
                lit = pos > 0.001f && a <= valueAngle + 0.001f;

            juce::Colour col;
            if (lit) col = dragging ? accent.brighter (0.2f) : accent;
            else if (bipolar && i == n / 2) col = juce::Colours::white.withAlpha (0.28f);
            else col = juce::Colours::white.withAlpha (hover ? 0.18f : 0.10f);

            const juce::Point<float> p0 (c.x + (outer - len) * std::sin (a), c.y - (outer - len) * std::cos (a));
            const juce::Point<float> p1 (c.x + outer * std::sin (a), c.y - outer * std::cos (a));
            g.setColour (col);
            g.drawLine ({ p0, p1 }, 1.5f);
        }

        // Body
        const auto body = juce::Rectangle<float> (D, D).withCentre (c);
        juce::Path bp;
        bp.addEllipse (body);
        shadow (g, bp, juce::Colours::black.withAlpha (0.60f), 0.50f * D, 0.08f * D, 0.16f * D);
        shadow (g, bp, juce::Colours::black.withAlpha (0.35f), 0.08f * D, 0.02f * D, 0.04f * D);
        shadow (g, bp, juce::Colours::white.withAlpha (0.045f), 0.30f * D, -0.06f * D, -0.09f * D);
        g.setGradientFill (juce::ColourGradient (hover ? knobHiHover : knobHi, body.getTopLeft(), knobLo, body.getBottomRight(), false));
        g.fillPath (bp);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), body.getTopLeft(),
                                                 juce::Colours::black.withAlpha (0.35f), body.getBottomRight(), false));
        g.strokePath (bp, juce::PathStrokeType (1.0f));

        // Dimple
        const float dr = std::max (3.0f, 0.085f * D);
        const float dist = D * 0.5f * 0.62f;
        const juce::Point<float> dc (c.x + dist * std::sin (valueAngle), c.y - dist * std::cos (valueAngle));
        juce::Path dp;
        dp.addEllipse (juce::Rectangle<float> (dr * 2, dr * 2).withCentre (dc));
        sunken (g, dp, dr * 2);
        g.setColour (accent.withAlpha (dragging ? 1.0f : 0.55f));
        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (dc));
    }

    // ---- Dropdown ----
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        const bool open = box.isPopupActive();
        if (open) sunken (g, r, radiusControl);
        else raised (g, r, radiusControl, box.isMouseOver (true));

        const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
        juce::Path chev;
        if (open) { chev.startNewSubPath (cx - 4, cy + 2); chev.lineTo (cx, cy - 2); chev.lineTo (cx + 4, cy + 2); }
        else      { chev.startNewSubPath (cx - 4, cy - 2); chev.lineTo (cx, cy + 2); chev.lineTo (cx + 4, cy - 2); }
        g.setColour (open ? n8 : n7);
        g.strokePath (chev, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return controlFont(); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 0, box.getWidth() - 34, box.getHeight());
        label.setBorderSize ({});
        label.setFont (controlFont());
        label.setColour (juce::Label::textColourId, n8);
    }

    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& label) override
    {
        return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, label).withStandardItemHeight (28);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (surface);
        g.setGradientFill (juce::ColourGradient (raisedHi, 0, 0, raisedLo, (float) w, (float) h, false));
        g.fillRoundedRectangle (0, 0, (float) w, (float) h, radiusCard);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, radiusCard, 1.0f);
    }

    int getPopupMenuBorderSize() override { return 6; }
    juce::Font getPopupMenuFont() override { return controlFont(); }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool, bool highlighted,
                            bool ticked, bool, const juce::String& text, const juce::String&, const juce::Drawable*,
                            const juce::Colour*) override
    {
        if (isSeparator) { dividerH (g, area.getX() + 6, area.getRight() - 6, area.getCentreY()); return; }
        auto r = area.toFloat().reduced (2.0f, 1.0f);
        if (ticked) sunken (g, r, radiusControl);
        else if (highlighted) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (r, radiusControl); }
        auto t = area.reduced (10, 0);
        if (ticked)
        {
            juce::Path tick;
            const float tx = (float) t.getRight() - 8.0f, ty = (float) t.getCentreY();
            tick.startNewSubPath (tx - 4, ty);
            tick.lineTo (tx - 1, ty + 3);
            tick.lineTo (tx + 4, ty - 3);
            g.setColour (accent);
            g.strokePath (tick, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            t.removeFromRight (20);
        }
        g.setFont (controlFont());
        g.setColour (n8);
        g.drawText (text, t, juce::Justification::centredLeft, true);
    }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&) override
    {
        sunken (g, juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f), radiusControl);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& ed) override
    {
        if (ed.hasKeyboardFocus (true))
        {
            g.setColour (accent.withAlpha (0.6f));
            g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, radiusControl, 1.0f);
        }
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool) override
    {
        juce::ignoreUnused (x, y, w, h);
        const auto r = vertical ? juce::Rectangle<float> ((float) x + (float) w * 0.5f - 2.0f, (float) thumbStart, 4.0f, (float) thumbSize)
                                : juce::Rectangle<float> ((float) thumbStart, (float) y + (float) h * 0.5f - 2.0f, (float) thumbSize, 4.0f);
        g.setColour (over ? n7 : n6);
        g.fillRoundedRectangle (r, 2.0f);
    }
};
} // namespace n2m::look
