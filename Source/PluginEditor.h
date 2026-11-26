#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Look.h"

namespace n2m
{
juce::Colour boardColour (int index);

// Buttons in the house style
struct N2MLook : look::SoftLookAndFeel
{
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
};

// One knob or switch on a module
class ParamCell : public juce::Component
{
public:
    ParamCell (Nebula2ModulationProcessor&, Module& m, int index);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
private:
    void setNorm (float n);
    void changed();          // after any change: linked knobs and patterns follow
    void linkMenu();
    Nebula2ModulationProcessor& proc;
    Module& module; int idx; const Param& param;
    float dragStart = 0; bool dragging = false;
};

// The step grid on a sequencer module, with its Ideas and Random buttons
class SeqView : public juce::Component
{
public:
    SeqView (Nebula2ModulationProcessor&, Board&, Module&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void tick();   // called by the editor's timer: moves the playing-step light
private:
    juce::Rectangle<int> grid() const;
    int stepAtX (int x) const;
    void setDegreeFromY (int step, int y);
    void showIdeas();
    Nebula2ModulationProcessor& proc; Board& board; Module& module;
    juce::Rectangle<int> pill (int bar) const;
    int bars() const;
    void edited();           // after any change to the pattern: linked sequencers follow
    std::string ideaKind() const;
    juce::TextButton ideas { "Ideas" }, random { "Random" };
    int shownStep = -1, dragStep = -1, page = 0;
};

// The song's chords, on the Chord generator: mood, Generate, and a tile per chord
class ChordView : public juce::Component
{
public:
    ChordView (Nebula2ModulationProcessor&, Module&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    void refreshLabels();
    Nebula2ModulationProcessor& proc; Module& module;
    juce::TextButton mood, generate { "Generate" }, length, each;
    int shown = -1, seenVersion = -1;
};

class BoardView;

// On a Plugin slot: which plugin is in it, Load and Open
class HostView : public juce::Component
{
public:
    HostView (BoardView&, HostedModule&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void loadFile (const juce::String& path);
private:
    void showLoadMenu();
    void refresh();
    BoardView& owner; HostedModule& module;
    juce::TextButton load { "Load" }, open { "Open" };
    std::unique_ptr<juce::FileChooser> chooser;
};

// A hosted plugin's own window, shown over the whole screen with a way back
class HostOverlay : public juce::Component
{
public:
    HostOverlay();
    ~HostOverlay() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void open (HostedModule&, const juce::String& tabName);
    void close();
    HostedModule* showing = nullptr;
private:
    juce::TextButton back { "Back" };
    juce::Viewport viewport;
    std::unique_ptr<juce::AudioProcessorEditor> pluginEditor;
    juce::String title;
};

// A module on the board: panel, knobs, jacks
class ModuleComp : public juce::Component
{
public:
    ModuleComp (BoardView&, Nebula2ModulationProcessor&, Board&, Module&);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    juce::Point<int> jackCentre (bool isOut, int port) const;   // in this component
    juce::Rectangle<int> linkArea() const;                       // on a sequencer: the Link label in the header
    void linkMenu();
    bool jackAt (juce::Point<int> p, bool& isOut, int& port) const;
    Module& module;
    std::unique_ptr<SeqView> seq;
    std::unique_ptr<ChordView> chords;
    std::unique_ptr<HostView> host;
private:
    BoardView& owner; Nebula2ModulationProcessor& proc; Board& board;
    juce::OwnedArray<ParamCell> cells;
    juce::Point<int> dragOffset; bool moving = false, cabling = false, movedYet = false;
};

// The board for the selected tab: its modules and the cables between them
class BoardView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    BoardView (Nebula2ModulationProcessor&);
    void setBoard (Board*);
    void rebuild();              // after modules are added or removed
    void refreshLive();          // after cables change: dim what can't be heard
    void willChange (int removingUid = 0);   // call just before changing this board, so Undo can put it back
    void settled();              // call when a gesture ends: drops the undo step if nothing changed
    void repaintAll();           // after a linked knob or pattern changed others on this board
    void fitToContent();
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void tick();
    // Drag the empty background to move around the board
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Cable dragging, driven by the module the drag started on
    void beginCable (ModuleComp&, bool isOut, int port, juce::Point<int> at);
    void dragCable (juce::Point<int> at);
    void endCable (juce::Point<int> at);
    ModuleComp* compFor (int uid) const;

    // Dropping a .vst3 file on the board puts it in a new Plugin slot there
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    Board* board = nullptr;
    std::function<void()> onChanged;
    std::function<void (HostedModule&)> onOpenPlugin;
private:
    bool findJack (juce::Point<int> at, ModuleComp*& comp, bool& isOut, int& port) const;
    void drawCable (juce::Graphics&, juce::Point<float> a, juce::Point<float> b, juce::Colour, bool ghost) const;
    Nebula2ModulationProcessor& proc;
    juce::OwnedArray<ModuleComp> comps;
    struct Drag { bool active = false, fromOut = true; int uid = 0, port = 0; juce::Point<int> at; } drag;
    juce::Point<int> panStart;
};

// The tabs along the top: one per sound, plus the + button
class TabBar : public juce::Component
{
public:
    TabBar (Nebula2ModulationProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void showSelected();         // scroll so the selected tab is in view
    std::function<void (int)> onSelect;
    std::function<void()> onChanged;   // tabs added, removed, renamed or moved
private:
    struct Slot { juce::Rectangle<int> r; int index; };
    std::vector<Slot> slots() const;
    int totalWidth() const;
    juce::Rectangle<int> plusArea() const, leftArrow() const, rightArrow() const, tabsArea() const;
    bool overflowing() const;
    void addMenu();
    void tabMenu (int index);
    void rename (int index);
    Nebula2ModulationProcessor& proc;
    int scroll = 0;
    std::unique_ptr<juce::TextEditor> editor;
};

// The mixing desk along the bottom: one small channel per tab, and the master
class MixerStrip : public juce::Component
{
public:
    MixerStrip (Nebula2ModulationProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void tick();
    std::function<void (int)> onSelect;
private:
    juce::Rectangle<int> channel (int i) const, masterArea() const;
    static juce::Rectangle<int> faderIn (juce::Rectangle<int> ch), muteIn (juce::Rectangle<int> ch), soloIn (juce::Rectangle<int> ch), outIn (juce::Rectangle<int> ch);
    void outMenu (int channel);
    Nebula2ModulationProcessor& proc;
    std::vector<float> shown;
    float shownL = 0, shownR = 0;
    int scroll = 0, dragChannel = -2;
};

// The Add units menu: every unit type under its heading. Tick one or several, then Add.
class AddPanel : public juce::Component
{
public:
    AddPanel();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void open();
    std::function<void (const std::vector<std::string>&)> onAdd;
private:
    struct Row { juce::Rectangle<int> r; const Info* info; };
    juce::Rectangle<int> card() const;
    std::vector<Row> rows() const;
    std::vector<std::string> ticked;
    juce::TextButton add { "Add to board" }, cancel { "Cancel" };
};
} // namespace n2m

class Nebula2ModulationEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Nebula2ModulationEditor (Nebula2ModulationProcessor&);
    ~Nebula2ModulationEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void selectTab (int index);
    n2m::AddPanel addPanel;      // public so the snapshot test can open it
    n2m::HostOverlay hostOverlay;
    n2m::BoardView& board() { return boardView; }   // for the tests
    void stepHistory (bool redo);                   // Undo (false) or Redo (true)
    void addUnits (const std::vector<std::string>& types);
private:
    void timerCallback() override;
    n2m::Board* current() const;

    Nebula2ModulationProcessor& proc;
    n2m::N2MLook lookAndFeel;
    juce::TextButton play { "Play" }, addUnitsButton { "Add units" }, tidyButton { "Tidy" }, undoButton { "Undo" }, redoButton { "Redo" };
    juce::ComboBox keyBox, scaleBox;
    n2m::TabBar tabs;
    juce::Viewport viewport;
    n2m::BoardView boardView;
    n2m::MixerStrip mixer;
    int seenVersion = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Nebula2ModulationEditor)
};
