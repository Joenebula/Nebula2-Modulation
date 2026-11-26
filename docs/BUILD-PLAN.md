# Nebula2 Modulation: build plan

The design is in [SPEC.md](SPEC.md). This is the order of work. Each stage ends with a release Joe can load in
Cubase. Tick things off here as they land.

## How it's built

- **JUCE 8 VST3**, Windows and Mac, compiled by GitHub Actions, the same way as Nebula2 Chord.
- **The engine has no JUCE in it** (`Source/Engine.h`, `Modules.h`, `Presets.h`). It is tested on its own.
- **Every cable carries the same kind of signal**, like a real Eurorack: audio, pitch (octaves from middle C),
  gates and control signals are all just numbers, so any output can go to any input.
- **A module is one small struct**: a description (jacks, knobs, menu heading) and a `process`. Adding a unit is
  one struct plus one line in the registry. The screen draws it from the description; no screen code per unit.
- **Saves are plain text, named by id**, so old projects keep loading as units are added.

## Stage 1: the rack (done, v0.1.0)

- [x] Engine: boards, modules, cables, running order, feedback loops, "can it be heard"
- [x] Tabs: add a sound, rename, duplicate, move, delete, scroll
- [x] Board: drag modules, pull and re-plug cables, right-click to disconnect or delete
- [x] Add units menu: headings, tick several, arrive unconnected
- [x] Mixing desk: level, mute, solo, meters, master with a safety limiter
- [x] Tidy button
- [x] Follows Cubase's tempo and transport; own Play for when Cubase is stopped
- [x] Saves with the Cubase project
- [x] Shared key and scale

## Stage 2: core sound (part done)

- [x] Oscillator, Noise, Hit envelope, Envelope, Filter, Soft saturation, VCA, Mixer, Attenuator, Output, LFO
- [x] Drum sequencer and Note sequencer with Ideas and Random
- [x] Starting boards: Kick, Snare, Hi-hat, Percussion, Bass, Lead, Stab, Pad, Empty
- [x] Multi-oscillator (four notes at once)
- [x] Sample player (v0.1.9): load or drop an audio file (wav, aiff, flac, ogg). Start, Length, Tune, Forward / Reverse, Once / Loop. A Pitch cable plays it at notes. A "Sample" sound in the + menu. The project remembers where the file is; if it has moved, the unit names the file it wants
- [ ] Sample player: mp3 isn't read (the framework leaves it off for licensing); copy the file into the project so it travels with it
- [x] Clap board, with its own clap envelope (v0.1.7)
- [ ] Stereo cables (today a board is mono until its Output; pan is on the mixer)
- [x] Undo and redo (v0.1.8): buttons and Ctrl+Z / Ctrl+Y. Covers knobs, patterns, cables, adding, moving and deleting units, tabs, the mixer, links, chords and key. Keeps the last 100 steps. A board is put back in place, so undoing a knob never reloads a plugin in a slot
- [ ] Not undoable yet: loading or emptying a plugin slot, changes made inside a hosted plugin's own window, the master fader
- [x] MIDI keyboard unit (v0.1.12): notes from a keyboard or a Cubase MIDI track. Pitch, Gate and Vel for a bass or lead; Note 1 to 4 for chords. Channel (All or 1 to 16), Octave, and Stay in key (on by default) or As played. Keys land on their exact sample. Works with Cubase stopped
- [ ] MIDI keyboard: no pitch bend, mod wheel or sustain pedal yet; the starting sounds still come with a sequencer, so the keyboard unit is cabled in by hand

## Stage 3: rhythm and notes (part done, v0.1.1)

- [x] Arpeggiator (v0.1.7): Up, Down, Up and down, Random, Outside in; 1 to 4 octaves; Rate, Range and Length each have an input for movers
- [x] Arp, Clap and Strings sounds
- [x] Chord generator: the Nebula2 Chord engine (`Source/ChordEngine.h`, copied unchanged). One progression shared by every tab: mood, Generate, song length, bars per chord
- [x] Note sequencer can follow the chords (bass and lead do by default)
- [x] Stab and Pad boards
- [x] Edit single chords (v0.1.10): click a chord tile for Colour, Bass note, Change to, Length and Lock. Locked chords stay through a Generate. The More button has Strange and Variation
- [x] Key change (v0.1.11): the chord unit's More button can raise the last part of the song by 1, 2 or 3 semitones. Sequencers that follow the chords go up with it
- [x] Euclidean rhythm (Hits and Turn have inputs), Clock, Divider, Chance (v0.1.7)
- [x] Ratchet and Swing units (v0.1.10)
- [x] Longer patterns: 1, 2, 4 or 8 bars, with a button per bar. Ideas fill every bar; Random varies the bars and adds fills
- [ ] Pattern chaining
- [ ] Bigger Ideas libraries per sound

## Stage 4: effects and thickeners (most done, v0.1.6)

- [x] Reverb: room, hall, plate (stereo in and out)
- [x] Delay: digital, tape echo, ping-pong. Times are note lengths and follow the tempo
- [x] Distortion: hard clip, bitcrusher (alongside soft saturation)
- [x] Movement: chorus, phaser, flanger. Rates are note lengths, in time with the song
- [x] EQ: 3-band, parametric (4 bands), low cut / high cut
- [x] Pad now runs through chorus and hall reverb in stereo; stab through plate reverb
- [x] Add units menu widened to three columns to hold them
- [x] Dynamics (v0.1.7): compressor with sidechain, transient shaper, ducker. With nothing in its Trig the ducker dips on every beat, so a bass or pad can pump against the kick without a cable between tabs
- [x] Tripler, Harmoniser, stereo width, pan (v0.1.7). The Harmoniser stays in key when the note's Pitch is plugged in
- [x] Cleaner pitch shifter (v0.1.11): it now measures the note's cycle and cuts its grains to fit, so a held note is steady instead of warbling. Sounds with no clear pitch (noise, drums) and full chords are shifted the old way
- [x] Send effects (v0.1.10): a Send unit on any tab feeds bus A or B; a Return unit plays the bus. "Effects (send)" in the + menu is a tab with a Return into a hall reverb. The effects tab always runs after the tabs feeding it, so tab order doesn't matter

Every effect is held to two rules by tests: at 0% it passes the sound through exactly, and silence in gives
silence out.

## Stage 5: movers and utilities (most done, v0.1.7)

- [x] Trance gate, step modulator, random, macro knob, envelope follower (v0.1.7)
- [x] Crossfader, switch, slew, logic (v0.1.7)
- [x] Scope and tuner, in one unit (v0.1.10): the wave, the note name, how many cents off, the frequency
- Not doing: a Splitter. Any output already feeds as many inputs as you cable from it.

## Stage 6: other plugins inside

Joe's own plugins to load this way: Nebula2 Drums, Voice, Chord, Glitch, EQ, Warmer and VenPressorDyn.

- [x] Plugin slot (v0.1.2): one unit for instruments and effects. Load from a list of plugins used before, browse for a .vst3, or drop a .vst3 on the board
- [x] Open shows the plugin's own window over the whole screen, with a Back button
- [x] Notes from cables (four note inputs, gate, velocity) play the plugin; audio in and out on jacks
- [x] The plugin inside follows the song's tempo and position
- [x] Saved with the project, including the plugin's own preset and settings
- [x] Joe tested v0.1.2 in Cubase on Windows (26 Nov 2026): loads, plugin loads into a slot, its window opens and Back returns, sound through cables, saves and reopens. All worked.
- [ ] Still to try: Nexus / HALion in a slot, and dropping a .vst3 on the board inside Cubase
- [x] A slot's Load menu lists every VST3 in the usual plugin folders by name (v0.1.9). The list is made by looking at the folders only; nothing is loaded to build it
- [x] Knobs on the slot (v0.1.11): Knob 1 to 4, each pointed at one of the plugin's own controls from the Knobs button (pick from the list, or "the control I last moved in its window"). Mod 1 to 4 inputs let cables move them. Saved with the project
- [ ] Pointing a knob isn't undoable, and Nexus / HALion controls can only be tried on Joe's PC
- [ ] Notes out of a hosted plugin (so Nebula2 Chord inside a slot could drive other units)
- [ ] Latency of hosted plugins isn't compensated yet
- [ ] Per-tab load meter
- [ ] Freeze / bounce a tab to audio

This is the stage that can only be checked on Joe's PC in Cubase with his own plugins.

## Links (done, v0.1.3)

Joe: "lock a bass to a string and change one, the other changes also", for anything he wants to connect.

- [x] Pattern links: a sequencer's Link label puts it in a group (A to D). Sequencers in a group, on any tab, share rhythm, notes or both. Each keeps its own octave.
- [x] A drum sequencer and a note sequencer can share rhythm
- [x] Knob links: right-click any knob to put it in a group (A to H). Knobs in a group move together, by knob position
- [x] Joining a group takes the group's pattern or position; links save with the project
- [x] Follow link (v0.1.4): a note sequencer keeps its own pattern and moves, in key, to match the group's leader (the first sequencer in the group that isn't following). This is what Joe meant by locking; a sequencer joining a group now follows by default
- [ ] A linked knob moved by a cable (not by hand) doesn't move the others
- [ ] Offsets between linked things (the same line a third up, a knob that moves half as far)

## Separate outputs (done, v0.1.5)

Joe: assign sounds out to channels in the Cubase mixer, so they can be mixed afterwards.

- [x] 16 stereo outputs: Main, and Out 2 to Out 16 (off until switched on in the host)
- [x] Each tab's mixer channel has an output label (MAIN / OUT n); click it to choose
- [x] A sound sent to an output that isn't switched on stays in the main mix, and its label turns orange
- [x] Separate outputs skip the master fader and keep the safety limiter
- [ ] **Joe to test in Cubase**: Activate Outputs, and the channels appearing in the MixConsole
- Looked at (26 Nov 2026) and not done: naming Cubase's channels after the tabs. JUCE 8.0.4 fixes an output's name when the plugin is created, has no way to change it, and never sends the host the "output names changed" notice. Doing it would mean patching JUCE in the build, and Cubase may not rename a channel that already exists anyway. Joe: not a deal breaker; rename in Cubase.

## Stage 7: the song

- [x] Song page (v0.1.11): the Song button opens a grid, sounds down the side and sections along the top. A filled square means the sound plays there. 2 to 16 sections of 1 to 16 bars; All on and Build up; the playing section is lit. A sound that sits out stops its patterns and lets its tail ring. Off by default; saved and undoable
- [ ] Song page: sections all the same length; no names for sections; no copy of a section
- [x] Drag MIDI out (v0.1.12): a DRAG MIDI label on note sequencers, drum sequencers and the chord unit. Drag it into Cubase for a MIDI part of exactly what the unit plays (in key, following the chords and any key change). **Joe to test the drag itself in Cubase**
- [ ] Drag audio out
- [x] Save and load a sound (v0.1.12): right-click a tab, Save this sound. Saved sounds are listed under + (Your saved sounds), from any project. They live in Documents / Nebula2 Modulation / Sounds. A plugin in a slot is saved with its settings
- [ ] A sound's sample file isn't copied with it; no pictures or tags in the list

## Screen polish (Joe: "we can work the UI later")

- [x] Drag the board background to move around; the board always has spare room to the right and below (v0.1.4)
- [x] Zoom: 50% to 125%, with - and + at the top or Ctrl + wheel; saved with the project (v0.1.9)
- [x] Add units menu: hovering a row shows a picture of that unit (v0.1.9)

## Later units

Everything in the SPEC table that isn't bold.

## Known gaps

- Boards are mono until the mixer.
- Fixed window size.
