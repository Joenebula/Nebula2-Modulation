# Nebula2 Modulation: handover for Claude Code

Read this first. It's everything a new session needs to carry on.

## What it is

A modular synth plugin for Joe (Nebula II): a VST3 instrument for Cubase (Windows and Mac), built in JUCE 8.
Tabs along the top, one per sound. Each tab is its own Eurorack-style board of modules joined by cables. A menu
adds units. A mixing desk along the bottom. Later stages load other VST3 plugins inside it as sound sources.

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
- Keep it tidy: one tab per sound, nothing from another sound on screen.
- The mixing desk is for mixing only (level, pan, mute, solo, output). Nothing that changes a sound goes there.

## Files

- `Source/Engine.h`: the rack. Rack > Board > Module + Cable. Running order, "can it be heard", the shared chords (Harmony, Timeline), save and load. No JUCE.
- `Source/ChordEngine.h`: the chord generator, copied unchanged from Nebula2 Chord. Don't edit it here; change it there and copy it over.
- `Source/Modules.h`: every module type, and `registry()`, the list the Add menu is drawn from. No JUCE.
- `Source/Presets.h`: the starting board for each kind of sound, pattern Ideas, in-style Random, Tidy, module sizes. No JUCE.
- `Source/History.h`: undo and redo. The screen calls `history.before (board)` (or `beforeChords`, `tabAdded`, `tabRemoving`, `tabMoved`) just before a change. No JUCE.
- `Source/PluginHost.*`: the Plugin slot (HostedModule): holds another VST3, feeds it notes from cables and the song position, saves its state. Registered through `extraTypes()` because the engine itself has no JUCE.
- `Source/Sampler.*`: the Sample player (SampleModule). Also registered through `extraTypes()`.
- `Source/PluginProcessor.*`: holds the rack, follows Cubase's tempo and transport, saves with the project.
- `Source/PluginEditor.*`: the screen. TabBar, BoardView (inside `boardHolder`, which is what makes zoom work), ModuleComp, ParamCell, SeqView, ChordView, HostView, SampleView, ScopeView, HostOverlay, MixerStrip, AddPanel, SongPanel.
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
```

The engine test alone compiles in seconds without JUCE:
`g++ -std=c++17 -O2 -ISource tests/EngineTest.cpp -o /tmp/EngineTest -lpthread && /tmp/EngineTest`

Linux packages JUCE needs: `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev
libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`.

## Release

Push to main, then start the release through the API (pushing tags doesn't work through the proxy):

```
gh api -X POST repos/Joenebula/Nebula2-Modulation/actions/workflows/build.yml/dispatches -f ref=main -f 'inputs[version]=v0.1.21'
```

The latest release is **v0.1.20**. Bump by one each time.

## What can't be checked here

Loading Nexus, HALion or any other commercial plugin inside this one can only be tested on Joe's PC in Cubase.
Popup menus can't be opened in the tests either (no screen): test what a menu item does, not the menu.
Here the Plugin slot is tested by hosting this plugin's own Linux build and Nebula2 Chord's. A hosted plugin's
window is a native window, so the Snapshot tool shows it as a black box of the right size: whether it draws
properly inside ours has to be checked on a real machine. Everything else is tested here before each release.
