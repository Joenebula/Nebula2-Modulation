# Nebula2 Modulation: handover for Claude Code

Read this first. It's everything a new session needs to carry on.

## What it is

A modular synth plugin for Joe (Nebula II): a VST3 instrument for Cubase (Windows and Mac), built in JUCE 8.
A style bar makes a whole track in one press. Tabs along the top, one per sound, and an All tab that shows every
sound on one board. Each sound is its own Eurorack-style board of modules joined by cables. A menu adds units. A
timeline moves knobs through the song. A mixing desk along the bottom. Other VST3 plugins load inside it as sound sources.

- The design: `docs/SPEC.md`
- The order of work and what's done: `docs/BUILD-PLAN.md`. Tick things off there as they land.

Sister project: Nebula2 Chord (`Joenebula/Nebula2-Chord`). Same look, same build and release route. Its
`ChordEngine.h` is the planned chord generator here.

## How Joe wants work done

- Short, plain replies. No jargon.
- Do only what he asked. Don't restyle or reorganise anything he didn't mention.
- Discuss before big builds. One short multiple-choice question when there's a real choice.
- Check it works before saying it's done (tests, snapshots). Then say plainly what you checked.
- Everything stays in key.
- Keep it tidy: one tab per sound. The All tab shows every sound together, each in its own frame (Joe asked for this on 27 Nov 2026, after the Modurand video); a sound's units never mix with another's.
- Keep our look: dark, text buttons. Joe chose this over Modurand's light theme (27 Nov 2026). The window is a fixed size unless he presses Full screen (28 Nov 2026), so lay things out from `getWidth()` / `getHeight()`, never from `windowW` / `windowH`.
- The mixing desk is for mixing only (level, pan, mute, solo, output). Nothing that changes a sound goes there.

## Files

- `Source/Engine.h`: the rack. Rack > Board > Module + Cable. Running order, "can it be heard", the shared chords (Harmony, Timeline), save and load. No JUCE.
- `Source/ChordEngine.h`: the chord generator, copied unchanged from Nebula2 Chord. Don't edit it here; change it there and copy it over.
- `Source/Modules.h`: every module type, and `registry()`, the list the Add menu is drawn from. No JUCE.
- `Source/Presets.h`: the starting board for each kind of sound, pattern Ideas, in-style Random, Tidy, module sizes. No JUCE.
- `Source/Styles.h`: styles. A style is a recipe (sounds, presets, pattern ideas, chord mood). `makeTrack`, `addTrack`, `varyPatterns`, `randomKnobs`, `makeStylePart`. No JUCE.
- `Source/History.h`: undo and redo. The screen calls `history.before (board)` (or `beforeChords`, `tabAdded`, `tabRemoving`, `tabMoved`) just before a change. No JUCE.
- `Source/PluginHost.*`: the Plugin slot (HostedModule): holds another VST3, feeds it notes from cables and the song position, saves its state. Registered through `extraTypes()` because the engine itself has no JUCE.
- `Source/Sampler.*`: the Sample player (SampleModule). Also registered through `extraTypes()`.
- `Source/PluginProcessor.*`: holds the rack, follows Cubase's tempo and transport, saves with the project.
- `Source/PluginEditor.*`: the screen. StyleBar, TabBar, BoardView (inside `boardHolder`, which is what makes zoom work), AllView (one embedded BoardView per sound), ModuleComp, ParamCell, SeqView, ChordView, HostView, SampleView, ScopeView, HostOverlay, MixerStrip (the desk: Sounds or Inside), AddPanel, SongPanel (the timeline).
- `Source/BigViews.*`: the bigger views (BigPanel, and one BigView per unit that has an Expand label: BigNoteRoll, BigDrumGrid, BigChords, BigSample, BigEq, BigStepMod, BigScope).
- `Source/Look.h`: design tokens and materials (ParaEQ design system, shared with Nebula2 Chord).
- `tests/EngineTest.cpp`: the engine on its own (plain C++).
- `tests/OfflineTest.cpp`: the plugin in a fake Cubase.
- `tests/Snapshot.cpp`: renders the screen to PNGs.

## Adding a module

1. In `Modules.h`, write a struct with `desc()` (name, menu heading, hint, jacks, knobs) and `process()`.
2. Add `entry<YourModule>()` to `registry()`. If it needs a new menu heading, add it to `shelves()`.
3. Add a check to `tests/EngineTest.cpp`.

The screen draws it from `desc()`. No screen code is needed per module.

Signals on cables: audio is -1..1; pitch is octaves from middle C; a gate is "above 0.5"; envelopes are 0..1; LFOs are -1..1.

Rules that tests hold in place:
- Styles: every preset and idea a style names must exist. The same seed makes the same track. Make track, Variation and Random knobs each call `history.beforeAll()` first (one Undo step for the whole project). Random knobs never touches tuning, levels, lengths or switches (`knobCanRandomise`).
- Wide units: the eight under "Notes and rhythm" (`lay::isWide`) use `lay::wideGeo` / `wideFits`: screen left, knobs right, inputs bottom left, outputs on a pad bottom right. Same height as the rest. Every other unit is a tall panel. Their screens read only atomics the unit publishes (e.g. `KeysIn::isHeld`), never audio-thread data.
- Panels: every unit is `lay::panelH` high; `lay::geo (info)` says where its knobs and jacks go and `lay::fits (info)` must hold for every type, the plugin slot and sample player included. A unit with `perRow` keeps its knobs in groups of that many.
- The All view: `proc.showAll`. `selectTab (i)` shows one sound alone; `showAllSounds()` shows them all; `reshow (i)` redraws whichever is up (use it after a change). `selectTab (-1)` / `reshow (-1)` lets go of the boards before the rack changes under them.
- Timeline knob lines: `Board::autos` (`AutoLane`: unit id, knob, up to 64 points of bar and knob position). Points are atomics, so dragging one never takes the lock; adding or removing a line does (`Rack::addAuto` / `removeAuto`). Knobs are set on a fixed grid of song position (1/32 of a step) with chunks cut at its edges, so it never depends on block size. The song is `Rack::songBars()` long and goes round. Saved as `au` lines. A new line is flat at the knob's own position and changes nothing. Each stretch can be bent (`AutoLane::curve`, `shape`, `curveFor`); 0 is straight and must stay exactly the old straight line.
- Bigger views: `BigPanel::has (info)` says which units have one; `ModuleComp::expandArea()` is the label. A view never trusts its unit is still there: `BigView::alive()` looks it up in the rack by address, and nothing is drawn or changed without it. The panel closes in `selectTab (-1)` / `reshow (-1)`, and `check()` (after any redraw of the boards) and `tick()` close it if its unit has gone. Undo and Redo keep it open when the unit survives. Every change goes through `begin()` / `end()` (History, linked units, redraw the board).
- Inside mixer: `proc.mixerInside` (saved after `bigWindow`; older projects load as Sounds). `insideStrips (board)` / `isMixKnob` in `Presets.h` is the one rule for what gets a fader: Level, Make-up, a Mixer unit's inputs, an effect's Mix, a Send's Amount. A fader has no value of its own: it reads the knob and turns it through `ParamCell::setNorm` on the board that is on screen (`MixerStrip::boardFor`), with `BoardView::willChange()` / `settled()` round it. The list is made afresh from the rack each time it is wanted and a unit is only reached by its id, never a kept address. Mixing only: no other knob goes on the desk.
- A note's own length: third row of a note sequencer's data (`kLenRow`), in steps, 0 = the Length knob (exactly as before). `ownNoteSteps` cuts it at the next note. Saved on a `gl` line only when there is one. `notesOf` uses the same functions.
- Drum hits: 0 off, 1 plain (Vel exactly 0.72), 2 accent (exactly 1.0), anything else above 0.5 a strength in between or below (`drumVel`, `drumData`, `drumMidiVel`). Stored to two decimal places.
- A knob at 0% does nothing at all (Drive at 0% is identical to no Drive). Every effect at Mix 0% passes its input through exactly, and gives silence for silence.
- Anything rhythmic is stored as a note length and worked out from the tempo, never milliseconds.
- Saves are text, named by id. Unknown modules and knobs are skipped on load, so never rename an id.
- The sound must not depend on the host's block size.
- A module with no path to the Output is `live = false` and drawn dimmed.
- The chords are one progression for the whole song, like the key. Every chord note is in the key.
- Patterns are up to 8 bars (`kMaxSteps`). A sequencer's data is two rows: hits or note on/off, then which note.
- Projects saved by v0.1.0 (the `d` line, one bar) must keep loading.
- Sends: `Send` units add to `Rack::buses` (A, B); a board with a `Return` (`Board::hasReturn`) is processed after the others. A Send counts as heard with nothing cabled to its outputs.
- Outputs: `Board::out` (0 = main mix, 1.. = Out 2 ...). A sound whose output the host hasn't switched on must stay in the main mix, never vanish.
- Anything "random" (Random, Chance, the arpeggiator's Random pattern) is worked out from the song position by `hashUnit()`, so the same bar always sounds the same.
- Anything the screen changes must call a History "before" first, or Undo will skip it. `BoardView::willChange()` is the usual way; `settled()` drops the step if nothing changed.
- Undo for a plugin slot: `history.beforeUnit (board, uid)` before a slot's plugin is loaded or emptied, `beforeUnit (board, uid, false)` before `assignKnob` (`BoardView::unitWillChange`). Undo calls the unit's `restoreExtra (text, whole)`: whole loads the plugin it held (or empties it), not whole puts back only what the knobs point at and never reloads, so edits in the plugin's own window stay. An emptied slot saves as empty. The master fader: `history.beforeMaster()`.
- Sample files travel: a unit's `saveCopy()` / `loadCopy()` go on an `xd` line after its `x` line, only when there is something (older versions skip it; the `x` line is still just where the file is). The Sample player keeps the file's own bytes when it is up to `SampleModule::kKeepBytes` (10 MB), or writes `big`. On load the file is used if it is still there, else the copy (`usingCopy()`). Undo steps leave the copy out, like other extra state. Saved text may have lines ending in `\r` (a `.n2msound` on disk): `readBoards` strips it.
- Late plugins: a unit says how late it is with `Module::latency()` (the plugin slot passes on what its plugin reports). `Board::latency()` adds them up along the cables into the Output; `Rack::latency` is the latest board, and each board's output is held back by the difference (`Board::process (..., holdBack)`). With nothing late, `holdBack` is 0 and the old path runs untouched: the sound must stay bit-identical. In samples, so it never depends on block size. The processor tells the host from a timer (`syncLatency`), never from the audio thread. `renderTab` plays that much longer and leaves the wait off the front.
- Freeze: `Board::frozen` (saved as a `frozen 1` line, only when it is) and `Board::ice`, the kept audio (`Rack::makeIce`: a private copy of the one sound plays the loop twice and the second pass is kept, taken before level and pan). An iced board's units are not run; `playIce` reads the audio by song position per sample, so it never depends on block size. Frozen with no ice plays its units, so nothing is ever missing. `freezeState` is everything the audio was made from (not the mixer channel): the processor's timer (`refreshFrozen`) drops ice that is out of date at once and makes it again when the state has stopped changing. `cantFreeze` gives the reason for sounds that depend on something outside themselves. The screen: `BoardView`'s cover dims and locks a frozen board; `TabBar::toggleFreeze` is the menu item. `processor.freezeTab` / `unfreezeTab` call `history.before` first.
- Linked knobs and timeline lines: `Rack::pushLine` runs on the audio thread right after `applyAutos`, on the same grid and inside the lock audio already holds (it takes none). A target with a line of its own is skipped; so are a frozen sound's knobs. A project with no linked line never reaches it.
- Song page: `Rack::song` (on, sections, bars) and `Board::plays` (one bit per section). A tab that sits out is processed with `playing = false`; chunks are cut at section edges so it never depends on block size. Screen changes call `history.beforeSong()`.
- Plugin slot knobs: `HostedModule::assignKnob (knob, control)`. A control is only told when its knob or Mod input moves, so edits in the plugin's own window stay. Saved by the plugin's own control ids (third piece of the slot's extra text).
- MIDI in: the processor hands `Rack::process` a `MidiIn` list; `Rack::keys` holds the keys down; the `keys` unit reads them. Chunks are cut at each key so timing is exact. A lost note-off must never happen: events are applied even when the lock is busy.
- MIDI out: `notesOf (rack, module)` in `Presets.h` must give exactly the notes the unit plays (a test plays the song and compares). `Nebula2ModulationProcessor::writeMidi` turns it into a file.
- Sounds: `saveSound` / `loadSound` on the processor; a `.n2msound` file is `N2MSOUND 1` plus one board's text. A loaded sound drops its links, solo and song sections.
- Your plugins: `Nebula2ModulationProcessor::addPluginSound (name)` builds the ready-made tab; `ownPlugins()` is the menu list. Names match without spaces, hyphens or capitals; Nebula2 Drums is installed as plain "Nebula2".
- Stereo cabling: the screen calls `Rack::connectStereo`, which adds the right-hand cable when both jacks have a partner (`rightOf`: ids ending l / r, or left / right).
- Audio out: `renderTab (tab)` renders a private copy of the whole project with that tab soloed; it must leave the real rack untouched.
- Ideas lists: only ever add to the end. Tests and snapshots pick ideas by position.
- Change sound: `soundPresets()` (a starting sound plus knob tweaks; a test checks every tweak lands), `keepPattern (old, fresh)` and `makeCarrier` in `Presets.h`; `changeSound` and `changeTo...` on the processor. The screen must let go of the board (`selectTab (-1)`) before a change and show it again after.
- Links: `Module::patternLink` / `patternShare` and `paramLink`, pushed by `Rack::pushPattern` / `pushParam` when the screen edits something. Knobs link by position, not value. `patternShare` 4 is a follower: it keeps its own pattern and is shifted at play time by `leaderShift()`.

## Build and test (Linux)

```
git clone https://github.com/juce-framework/JUCE.git --branch 8.0.4 --depth 1 ../JUCE   # once
ln -s ../JUCE JUCE                                                                     # once
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target EngineTest OfflineTest Snapshot -j3
./build/EngineTest                                             # "All passed"
./build/OfflineTest_artefacts/Release/OfflineTest               # "All passed"
# with plugin slots tested too: pass a .vst3 that plays with the song, then an instrument
cmake --build build --target Nebula2Modulation_VST3 -j3
./build/OfflineTest_artefacts/Release/OfflineTest "build/Nebula2Modulation_artefacts/Release/VST3/Nebula2 Modulation.vst3" "../nebula2-chord/build/Nebula2Chord_artefacts/Release/VST3/Nebula2 Chord.vst3"
./build/Snapshot_artefacts/Release/Snapshot /tmp/snap           # ui.png, ui_playing.png, ui_add.png ...
cp /tmp/snap/ui_playing.png docs/screenshot.png; cp /tmp/snap/ui_pad.png docs/screenshot-chords.png; cp /tmp/snap/ui_add.png docs/screenshot-add-units.png; cp /tmp/snap/ui_song.png docs/screenshot-song.png
cp /tmp/snap/ui_all_trance.png docs/screenshot-all.png; cp /tmp/snap/ui_timeline.png docs/screenshot-timeline.png
```

The engine test alone compiles in seconds without JUCE:
`g++ -std=c++17 -O2 -ISource tests/EngineTest.cpp -o /tmp/EngineTest -lpthread && /tmp/EngineTest`

Linux packages JUCE needs: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev
libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`.

## Release

Push to main, then start the release through the API (pushing tags doesn't work through the proxy):

```
gh api -X POST repos/Joenebula/Nebula2-Modulation/actions/workflows/build.yml/dispatches -f ref=main -f 'inputs[version]=v0.1.28'
```

The latest release is **v0.1.27**. v0.1.22 was the first with the Modurand rework (styles, All view, rack panels, timeline).
Joe asked for that release on 27 Nov 2026, after it had built on Windows, Mac and Linux and passed every test.

Windows: don't name a variable `near`, `far` or `small`. The Windows headers define them and the build breaks.

## What can't be checked here

Loading Nexus, HALion or any other commercial plugin inside this one can only be tested on Joe's PC in Cubase.
Popup menus can't be opened in the tests either (no screen): test what a menu item does, not the menu.
Here the Plugin slot is tested by hosting this plugin's own Linux build and Nebula2 Chord's. A hosted plugin's window opens
as its own floating window when there is a screen; the tests have none and fall back to showing it over the board (`HostOverlay`). A hosted plugin's
window is a native window, so the Snapshot tool shows it as a black box of the right size: whether it draws
properly inside ours has to be checked on a real machine. Everything else is tested here before each release.
