#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
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
    void mouseDown (const juce::MouseEvent&) override;
    void tick();
    void chordMenu (int index);      // public so the tests can reach it
    static juce::Rectangle<int> tile (int index);
private:
    void refreshLabels();
    Nebula2ModulationProcessor& proc; Module& module;
    juce::TextButton mood, generate { "Generate" }, length, each, more { "More" };
    int shown = -1, seenVersion = -1;
};

class BoardView;

// On a Scope: the wave passing through, the note it is playing and how far off it is
class ScopeView : public juce::Component
{
public:
    ScopeView (BoardView&, Scope&);
    void paint (juce::Graphics&) override;
    void tick();
private:
    BoardView& owner; Scope& module;
    bool wasQuiet = true;
};

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
    void showKnobMenu();
    void showPresetMenu();
    void refresh();
    BoardView& owner; HostedModule& module;
    juce::TextButton load { "Load" }, open { "Open" }, knobs { "Knobs" }, presets { "Presets" };
    std::unique_ptr<juce::FileChooser> chooser;
};

// On a Sample player: the file's name and shape, and Load. An audio file can be dropped straight on it.
class SampleView : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    SampleView (BoardView&, SampleModule&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
private:
    BoardView& owner; SampleModule& module;
    juce::TextButton load { "Load" };
    std::unique_ptr<juce::FileChooser> chooser;
    float shownHead = -1.0f;
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
    bool isOpen() const { return showing != nullptr; }
private:
    std::unique_ptr<juce::DocumentWindow> window;      // the plugin's window, floating beside ours
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
    juce::Rectangle<int> midiArea() const;                       // on a sequencer or the chords: drag from here to put the MIDI in the host
    bool midiDrag = false;
    juce::Rectangle<int> linkArea() const;                       // on a sequencer: the Link label in the header
    void linkMenu();
    bool jackAt (juce::Point<int> p, bool& isOut, int& port) const;
    Module& module;
    std::unique_ptr<SeqView> seq;
    std::unique_ptr<ChordView> chords;
    std::unique_ptr<HostView> host;
    std::unique_ptr<SampleView> sample;
    std::unique_ptr<ScopeView> scope;
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
    // While a cable is being dragged: could it be dropped on a jack of this kind on this module?
    bool couldTake (int uid, bool jackIsOut) const { return drag.active && jackIsOut != drag.fromOut && uid != drag.uid; }
    void willChange (int removingUid = 0);   // call just before changing this board, so Undo can put it back
    void settled();              // call when a gesture ends: drops the undo step if nothing changed
    void repaintAll();           // after a linked knob or pattern changed others on this board
    void fitToContent();
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void tick();
    // Drag the empty background to move around the board; Ctrl + wheel zooms
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::function<void (int)> onZoomStep;        // +1 bigger, -1 smaller
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
    std::function<void()> onAddMenu;                                  // right-click on the board: open Add units
    std::function<void (const std::vector<std::string>&)> onAddTypes;   // ...or add a favourite straight away
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
    void added (int index);      // after a tab has been added: show it
public:
    void soundMenu (int index, juce::Component* target = nullptr);   // Change sound: presets, a sample, a plugin, a saved sound
private:
    Nebula2ModulationProcessor& proc;
    int scroll = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::TextEditor> editor;
};

// The style bar, under the top bar: pick a style, make a whole track in it, vary it, or make one sound again
class StyleBar : public juce::Component
{
public:
    explicit StyleBar (Nebula2ModulationProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    static constexpr int height = 64;
    // What a click asks for. The editor does it, because the boards on screen have to be let go first.
    std::function<void (const juce::String& style)> onMake;
    std::function<void()> onVariation, onRandomKnobs;
    std::function<void (const juce::String& kind)> onPart;
    // Where things are, for the tests
    juce::Rectangle<int> styleChip (int index) const, makeArea() const, variationArea() const, knobsArea() const, partChip (int index) const, modeArea (bool add) const;
    static const std::vector<std::pair<const char*, const char*>>& parts();      // kind, label
private:
    Nebula2ModulationProcessor& proc;
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
    std::function<void()> onResize;                 // MORE or LESS was pressed: the desk changes height
    static int heightFor (bool tall);
    void setTall (bool tall);
    void setPan (int channel, float pan);           // -1 left .. 1 right
private:
    juce::Rectangle<int> channel (int i) const, masterArea() const;
    static juce::Rectangle<int> panIn (juce::Rectangle<int> channel);
    bool dragPan = false;
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
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void open();
    void hover (const Info* info) { hovered = info; repaint(); }      // for the snapshot tool
    void setSearch (const juce::String& text);      // only units whose name, hint or heading has these letters
    // Favourite units: starred in this menu, kept between sessions, offered on a right-click of the board
    static std::vector<std::string> favourites();
    static void setFavourite (const std::string& type, bool on);
    std::function<void (const std::vector<std::string>&)> onAdd;
    std::function<juce::Image (const Info&)> preview;     // a picture of a unit, shown beside the row the mouse is on
private:
    const Info* hovered = nullptr;
    struct Row { juce::Rectangle<int> r; const Info* info; juce::String heading; };
    static juce::Rectangle<int> starIn (juce::Rectangle<int> row);
    juce::TextEditor search;
    juce::String query;
    juce::Rectangle<int> card() const;
    std::vector<Row> rows() const;
    std::vector<std::string> ticked;
    juce::TextButton add { "Add to board" }, cancel { "Cancel" };
};

// The song page: a grid of tabs down the side and sections along the top. A filled square = that sound plays there.
class SongPanel : public juce::Component
{
public:
    explicit SongPanel (Nebula2ModulationProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void open();
    void tick();
    void fill (int kind);                 // 0 = every sound everywhere, 1 = build up: one more sound each section
    void setCell (int tab, int section, bool on);
    void setSections (int n);
    void setBars (int n);
    void setOn (bool on);
private:
    juce::Rectangle<int> card() const;
    juce::Rectangle<int> cell (int tab, int section) const;
    int rowH() const;
    bool hit (juce::Point<int>, int& tab, int& section) const;
    void sync();
    Nebula2ModulationProcessor& proc;
    juce::TextButton onOff, fewer { "-" }, more { "+" }, barsButton, allOn { "All on" }, buildUp { "Build up" }, close { "Close" };
    bool paintTo = false; int shownSection = -2;
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
    n2m::SongPanel songPanel;
    n2m::HostOverlay hostOverlay;
    n2m::BoardView& board() { return boardView; }   // for the tests
    n2m::MixerStrip& desk() { return mixer; }
    void stepHistory (bool redo);                   // Undo (false) or Redo (true)
    void stepZoom (int direction);                  // +1 bigger, -1 smaller, 0 just apply
    void addUnits (const std::vector<std::string>& types);
    n2m::StyleBar& styles() { return styleBar; }    // for the tests
    void afterWholeChange (int showTab);            // after Make track and the like: draw everything again
private:
    void timerCallback() override;
    n2m::Board* current() const;

    Nebula2ModulationProcessor& proc;
    n2m::N2MLook lookAndFeel;
    juce::TextButton play { "Play" }, addUnitsButton { "Add units" }, tidyButton { "Tidy" }, undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, songButton { "Song" }, soundButton { "Change sound" }, deskButton { "More" };
    juce::Component boardHolder;                 // the board sits in this so it can be drawn bigger or smaller
    std::map<std::string, juce::Image> previews;
    juce::Image previewOf (const n2m::Info&);
    juce::ComboBox keyBox, scaleBox;
    n2m::StyleBar styleBar;
    n2m::TabBar tabs;
    juce::Viewport viewport;
    n2m::BoardView boardView;
    n2m::MixerStrip mixer;
    int seenVersion = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Nebula2ModulationEditor)
};
