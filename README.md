# Nebula2 Modulation

A modular synth for Cubase. One tab per sound, each with its own rack of modules and cables, a pattern to play
it, and a mixing desk underneath.

![Nebula2 Modulation](docs/screenshot.png)

![The chord generator on the Pad tab](docs/screenshot-chords.png)

![The Add units menu](docs/screenshot-add-units.png)

![The song page](docs/screenshot-song.png)

This is an early build: the rack, the sound modules, effects, chords, plugin slots and the first set of sounds. What's planned next is in
[docs/BUILD-PLAN.md](docs/BUILD-PLAN.md); the full design is in [docs/SPEC.md](docs/SPEC.md).

## Install

**Windows:** unzip and copy `Nebula2 Modulation.vst3` into `C:\Program Files\Common Files\VST3`.

**Mac:** unzip and copy `Nebula2 Modulation.vst3` into `~/Library/Audio/Plug-Ins/VST3`. The plugin isn't signed
by Apple yet, so if Cubase won't load it, open Terminal and run:

```
xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Nebula2 Modulation.vst3"
```

Then rescan plugins in Cubase. Downloads are on the [Releases page](https://github.com/Joenebula/Nebula2-Modulation/releases).

## How to use it

1. Add Nebula2 Modulation as an **instrument track** in Cubase and press play. It follows Cubase's tempo.
   **Play** in the plugin runs it on its own when Cubase is stopped.
2. Each **tab** is one sound. Click a tab to see only that sound's modules.
3. **+** adds a sound: kick, snare, clap, hi-hat, percussion, bass, lead, arp, stab, pad, strings, sample, or an empty board.
   It arrives cabled and playing.
4. **Add units** opens the menu of modules: sound sources, envelopes, filters, EQ, distortion, reverb, delay, chorus,
   phaser, flanger and more. Tick one or several and they land on the board, unconnected.
5. **Cables:** drag from an output jack (coloured ring) to an input jack. Pull a plug out of an input to move it,
   or drop it on nothing to remove it. Right-click a module to disconnect or delete it.
6. A module marked **not connected** has no path to the Output, so it can't be heard yet.
7. **Patterns:** click steps to edit. **Ideas** gives ready-made patterns for that kind of sound; **Random**
   makes a new one in the same style. Set **Bars** to 2, 4 or 8 for a longer pattern, and use the numbered
   buttons to move between bars.
8. **Chords:** the **Chord generator** (on the Pad and Stab tabs) holds the song's chords. Pick a **Mood**, press
   **Generate**, and choose the song length and how long each chord lasts. Click any chord to change its colour,
   bass note or length, swap it for another chord of the key, or lock it so Generate leaves it alone. **More** has a key change that lifts the last part of the song. There is one set of chords for the
   whole song. A Note sequencer set to **Follow** moves its notes with the chords, so the bass and lead keep up.
9. **Key** at the top is shared by every tab. Notes and chords are always in key.
10. **Your own plugins:** add a **Plugin slot** from Add units (under Your plugins), press **Load** and browse to
    a .vst3, pick one from **All installed plugins**, or drop a .vst3 file on the board. Plugins you've loaded before are listed first under Load. **Open** shows
    the plugin's own window; **Back** returns to the board. Cable a sequencer's Pitch and Gate into the slot to
    play an instrument, cable audio into In L / In R to use an effect, and cable Out L / Out R on to the Output.
    **Knobs** points the slot's Knob 1 to 4 at the plugin's own controls; cable an LFO or any mover into Mod 1 to 4
    to move them. **+ > Your plugins** does it all in one go: a new tab with Nebula2 Drums, Voice, Chord, Glitch or
    another of your plugins already loaded and cabled.
11. **Links:** to make two sounds move together, link them. On a sequencer, click **LINK** next to its name and pick
    a group. Do the same on another sequencer, on any tab. The second one **follows** the first: it keeps its own
    pattern and moves up and down, in key, as the first one's notes change. The same menu can instead make them
    share the same rhythm, the same notes or both. To link knobs, right-click a knob and pick a group; every knob
    in that group turns together.
12. **Shared effects:** add **Effects (send)** from the + menu: a tab with a reverb fed by bus A. On any other tab, add
    a **Send** unit in the sound's path and turn up Amount. Several tabs can share the one reverb this way.
13. **Separate outputs:** to mix a sound in Cubase's own mixer, click the **MAIN** label on its channel in the mixing
    desk and pick **Out 2** to **Out 16**. In Cubase, switch that output on from the instrument's Activate Outputs
    list. If the label is orange, the output isn't switched on yet and the sound is still in the main mix.
14. **Song:** the **Song** button opens a grid of your sounds against the sections of the song. Switch it on, then
    click squares to choose which sounds play where. **Build up** brings one more sound in each section.
15. **Play it yourself:** add a **MIDI keyboard** unit (under Notes and rhythm) and cable its Pitch and Gate where a
    sequencer's were. It plays from your keyboard or a Cubase MIDI track, and keeps notes in key unless you switch
    that off.
16. **MIDI into Cubase:** drag the **DRAG MIDI** label on a sequencer or the chord unit into the Cubase project.
    **DRAG AUDIO** on a tab's Output does the same with that sound as a WAV file.
17. **Keep a sound:** right-click a tab and choose **Save this sound**. It then appears under **+**, in **Your saved
    sounds**, in every project.
18. The **mixing desk** at the bottom has level, mute and solo for each tab. Click a channel to jump to its tab.
19. **Tidy** lines the modules up in signal order.

**Undo** and **Redo** are at the top (also Ctrl+Z and Ctrl+Y). **-** and **+** zoom the board (also Ctrl + mouse wheel). Hover a row in Add units to see a picture of that unit. To use your own sound, add a **Sample player** and load or drop an audio file on it. Drag the empty board background to move around a big board. Knobs: drag up and down. Hold Shift for fine moves. Double-click to reset. Double-click a tab to rename it;
right-click it to duplicate, move or delete.

## Build it yourself

Needs CMake 3.22+ and a C++17 compiler (Visual Studio 2022 on Windows, Xcode on Mac).

```
git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build
cmake --build build --config Release --target Nebula2Modulation_VST3
```

Tests (no Cubase needed):

```
cmake --build build --config Release --target EngineTest OfflineTest
build/Release/EngineTest
```

The look is the ParaEQ design system, shared with Nebula2 Chord. The Inter fonts are under the SIL Open Font License.
