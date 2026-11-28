#pragma once
// Nebula2 Modulation: the bigger views. A unit with an Expand label in its header opens one of these over the
// board: the same pattern, chords, wave or curve as on the unit, with room to do more.
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Look.h"

namespace n2m
{
// What every bigger view is. It shows one unit of one board, and it never trusts that the unit is still there:
// alive() looks the unit up in the rack by address (without touching it), and nothing is drawn or changed unless
// it is found. The panel closes a view whose unit has gone.
class BigView : public juce::Component
{
public:
    BigView (Nebula2ModulationProcessor&, Board&, Module&);
    bool alive() const;
    void tick();                                    // called by the editor's timer through the panel
    virtual juce::String help() const = 0;          // one line under the title
    std::function<void()> onEdited;                 // something changed: the unit on the board needs drawing again
    int unitId() const { return moduleUid; }

    void paint (juce::Graphics&) final;
    void mouseDown (const juce::MouseEvent&) final;
    void mouseDrag (const juce::MouseEvent&) final;
    void mouseUp (const juce::MouseEvent&) final;
    void mouseDoubleClick (const juce::MouseEvent&) final;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) final;

protected:
    virtual void draw (juce::Graphics&) = 0;
    virtual void down (const juce::MouseEvent&) {}
    virtual void dragged (const juce::MouseEvent&) {}
    virtual void up (const juce::MouseEvent&) {}
    virtual void doubleClicked (const juce::MouseEvent&) {}
    virtual void wheel (const juce::MouseEvent&, const juce::MouseWheelDetails&) {}
    virtual uint64_t signature() const { return 0; }   // what is on show, as one number: drawn again when it changes
    virtual void ticked() {}
    void begin();                                   // just before a change, so Undo can put it back
    void end();                                     // when the change is made: linked units follow, the board is drawn again
    void setKnob (int param, float value);          // a knob of the unit, kept in its range; linked knobs follow
    Nebula2ModulationProcessor& proc; Board& board; Module& module;

private:
    Board* boardPtr; Module* modulePtr; int moduleUid;
    uint64_t seen = 0;
};

// What the three pattern views share: how many bars, which bar is picked, and copy / paste / clear of a bar
class BigPattern : public BigView
{
public:
    BigPattern (Nebula2ModulationProcessor&, Board&, Module&);
    void resized() override;
    int bars() const;                               // 1, 2, 4 or 8
    int length() const;                             // steps before the pattern goes round
    void setBars (int index);                       // 0..3: 1, 2, 4 or 8 bars
    void selectBar (int bar);
    int selectedBar() const { return juce::jlimit (0, bars() - 1, picked); }
    void copyBarAt (int bar);
    bool pasteBarAt (int bar);                      // false: nothing copied yet, or it came from another kind of unit
    void clearBarAt (int bar);
    bool canPaste() const;
protected:
    void ticked() override;
    uint64_t signature() const override;
    juce::Rectangle<int> toolbar() const { return { 0, 0, getWidth(), 28 }; }
    juce::Rectangle<int> body() const { return { 0, 38, getWidth(), getHeight() - 38 }; }
    void drawToolbar (juce::Graphics&);
    int shownStep = -1;
private:
    juce::TextButton barButtons[4], copy { "Copy bar" }, paste { "Paste bar" }, clear { "Clear bar" };
    int picked = 0;
};

// 1. Note sequencer: a piano roll. Every bar of the pattern, three octaves of the key, a length for each note.
class BigNoteRoll : public BigPattern
{
public:
    using BigPattern::BigPattern;
    juce::String help() const override;
    static constexpr int lo = -7, hi = 14;          // the rows: the same three octaves as on the unit
    void setNote (int step, int degree);            // puts the step's note there (adds it if the step was empty)
    void removeNote (int step);
    void setNoteLength (int step, int steps);       // 0 = no length of its own: the Length knob sets it
    juce::Rectangle<int> grid() const;
    juce::Rectangle<float> cell (int step, int degree) const;
    juce::Rectangle<float> noteRect (int step) const;   // where a note is drawn, at the length it will sound
protected:
    void draw (juce::Graphics&) override;
    void down (const juce::MouseEvent&) override;
    void dragged (const juce::MouseEvent&) override;
    void up (const juce::MouseEvent&) override;
private:
    int stepAt (float x) const, degreeAt (float y) const, noteAt (juce::Point<float>) const;
    int dragStep = -1, dragKind = 0;                // kind: 1 moving up and down, 2 pulling its end, 3 a new note
    bool movedYet = false;
};

// 2. Drum sequencer: every bar as a row of big steps, each with a bar underneath to drag for how hard it hits
class BigDrumGrid : public BigPattern
{
public:
    using BigPattern::BigPattern;
    juce::String help() const override;
    void toggleHit (int step);
    void setVelocity (int step, float vel);         // 0.1 .. 1; a step with no hit is left alone
    juce::Rectangle<int> row (int bar) const;
    juce::Rectangle<float> tile (int step) const, velLane (int step) const;
protected:
    void draw (juce::Graphics&) override;
    void down (const juce::MouseEvent&) override;
    void dragged (const juce::MouseEvent&) override;
    void up (const juce::MouseEvent&) override;
private:
    int stepAt (juce::Point<float>, bool& inVel) const;
    bool dragVel = false;
};

// 6. Step modulator: every bar in one row; drag across the steps to draw a shape
class BigStepMod : public BigPattern
{
public:
    using BigPattern::BigPattern;
    juce::String help() const override;
    static constexpr int lo = -7, hi = 14;
    void setValue (int step, int level);            // lo .. hi; switches the step on
    void drawLine (int stepA, int levelA, int stepB, int levelB);   // what one stroke of the pencil does
    void setHold (int step);                        // switches the step off: it holds the value before it
    juce::Rectangle<int> grid() const;
    juce::Point<float> pointFor (int step, int level) const;
protected:
    void draw (juce::Graphics&) override;
    void down (const juce::MouseEvent&) override;
    void dragged (const juce::MouseEvent&) override;
    void up (const juce::MouseEvent&) override;
private:
    void rawLine (int stepA, int levelA, int stepB, int levelB, bool hold);
    int stepAt (float x) const, levelAt (float y) const;
    int lastStep = -1, lastLevel = 0;
};

// 3. Chord generator: every chord of the song with its notes on a keyboard, and the picked chord's choices as buttons
class BigChords : public BigView
{
public:
    BigChords (Nebula2ModulationProcessor&, Board&, Module&);
    juce::String help() const override;
    void select (int chord);
    int selected() const;
    // The picked chord's choices. Each is one Undo step.
    void setColour (int index);                     // one of colourNames()
    void setBass (int inv);
    void setDegree (int deg);                       // which chord of the key
    void setBars (int bars);
    void toggleLock();
    std::vector<juce::String> colourNames() const, bassNames() const, degreeNames() const;
    struct Chip { juce::Rectangle<int> r; int kind, value; juce::String text; bool on; };   // kind: 0 colour, 1 bass, 2 change to, 3 length, 4 lock
    std::vector<Chip> chips() const;
    juce::Rectangle<int> tile (int chord) const;
protected:
    void draw (juce::Graphics&) override;
    void down (const juce::MouseEvent&) override;
    void ticked() override;
    uint64_t signature() const override;
private:
    int count() const, perRow() const, tilesBottom() const;
    void changed();
    int picked = 0, shown = -1;
};

// 4. Sample player: the wave drawn large. Drag the start and the end; zoom in and out; the playhead moves over it.
class BigSample : public BigView
{
public:
    BigSample (Nebula2ModulationProcessor&, Board&, Module&);
    juce::String help() const override;
    void resized() override;
    void setRegion (float from, float to);          // both 0..1 through the file
    float regionStart() const, regionEnd() const;
    void zoomBy (float factor, float around);       // above 1 zooms in; around: the place (0..1 of the file) that stays put
    void showWhole();
    float viewStart() const { return v0; }
    float viewEnd() const { return v1; }
    juce::Rectangle<int> well() const;
    float xFor (float at) const, atFor (float x) const;
protected:
    void draw (juce::Graphics&) override;
    void down (const juce::MouseEvent&) override;
    void dragged (const juce::MouseEvent&) override;
    void up (const juce::MouseEvent&) override;
    void wheel (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void ticked() override;
    uint64_t signature() const override;
private:
    void rawRegion (float from, float to);
    juce::TextButton zoomIn { "Zoom in" }, zoomOut { "Zoom out" }, whole { "Whole file" };
    float v0 = 0.0f, v1 = 1.0f, shownHead = -1.0f;
    int dragEnd = 0;                                // 1 the start, 2 the end
};

// 5. Parametric EQ: the curve drawn large, each band a point to drag
class BigEq : public BigView
{
public:
    using BigView::BigView;
    juce::String help() const override;
    void setBand (int band, float freq, float gainDb);   // freq: the knob's 0..1 (20 Hz to 20 kHz)
    void setWidth (int band, float width);
    juce::Rectangle<int> plot() const;
    juce::Point<float> pointOf (int band) const;
    float curveDb (float freq) const;               // what the four bands add up to at this place (0..1 as above)
    static constexpr float topDb = 20.0f;
protected:
    void draw (juce::Graphics&) override;
    void down (const juce::MouseEvent&) override;
    void dragged (const juce::MouseEvent&) override;
    void up (const juce::MouseEvent&) override;
    void doubleClicked (const juce::MouseEvent&) override;
    void wheel (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    uint64_t signature() const override;
private:
    int bandNear (juce::Point<float>) const;
    int dragBand = -1, lastBand = 0; bool dragWidth = false; float widthStart = 0;
    juce::uint32 wheelTime = 0;
};

// 7. Scope and tuner: a large scope, with the note, how far off it is and the frequency in large letters
class BigScope : public BigView
{
public:
    BigScope (Nebula2ModulationProcessor&, Board&, Module&);
    juce::String help() const override;
    void resized() override;
    void setFrozen (bool);
    bool frozen() const { return held; }
protected:
    void draw (juce::Graphics&) override;
    void ticked() override;
private:
    juce::TextButton freeze { "Freeze" };
    bool held = false, wasQuiet = true;
    std::vector<float> kept; float keptHz = 0;
};

// The panel a bigger view opens in: over the whole plugin window, a card in the middle, Close at the top right
class BigPanel : public juce::Component
{
public:
    explicit BigPanel (Nebula2ModulationProcessor&);
    static bool has (const Info&);                  // does this kind of unit have a bigger view
    bool open (Board&, Module&);
    void close();
    bool isOpen() const { return view != nullptr; }
    void check();                                   // closes it if its unit has gone; otherwise draws it again
    void tick();
    BigView* content() const { return view.get(); }
    template <typename T> T* as() const { return dynamic_cast<T*> (view.get()); }
    juce::Rectangle<int> card() const;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onEdited;                 // the board underneath needs drawing again
    std::function<void (bool redo)> onUndo;
    std::function<void()> onPlay;
private:
    Nebula2ModulationProcessor& proc;
    std::unique_ptr<BigView> view;
    juce::TextButton closeButton { "Close" }, undo { "Undo" }, redo { "Redo" }, play { "Play" };
    juce::String title;
};
} // namespace n2m
