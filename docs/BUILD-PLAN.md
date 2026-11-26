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
- [ ] Sample player (drop an audio file on it)
- [ ] Clap board (needs the multi-hit envelope)
- [ ] Stereo cables (today a board is mono until its Output; pan is on the mixer)
- [ ] Undo and redo
- [ ] Play boards from a MIDI keyboard / Cubase MIDI track

## Stage 3: rhythm and notes (part done, v0.1.1)

- [ ] Arpeggiator, with rate, gate, range and pattern as inputs
- [x] Chord generator: the Nebula2 Chord engine (`Source/ChordEngine.h`, copied unchanged). One progression shared by every tab: mood, Generate, song length, bars per chord
- [x] Note sequencer can follow the chords (bass and lead do by default)
- [x] Stab and Pad boards
- [ ] Strings board
- [ ] Edit single chords (colour, bass note, length, lock), and the Strange / Variation / Key change knobs from Nebula2 Chord
- [ ] Euclidean rhythm, clock divider, probability, ratchet, swing
- [x] Longer patterns: 1, 2, 4 or 8 bars, with a button per bar. Ideas fill every bar; Random varies the bars and adds fills
- [ ] Pattern chaining
- [ ] Bigger Ideas libraries per sound

## Stage 4: effects and thickeners

- [ ] Reverb: room, hall, plate
- [ ] Delay: digital, tape, ping-pong (all tempo-synced)
- [ ] Distortion: hard clip, bitcrusher
- [ ] Movement: chorus, phaser, flanger
- [ ] EQ: 3-band, parametric, low cut / high cut
- [ ] Dynamics: compressor with sidechain, transient shaper, ducker
- [ ] Tripler, Harmoniser, stereo width, pan
- [ ] Send effects on the mixing desk

## Stage 5: movers and utilities

- [ ] Gate / trance gate, step modulator, random, macro knob
- [ ] Splitter, crossfader, switch, slew, logic, scope and meter, tuner

## Stage 6: other plugins inside

Joe's own plugins to load this way: Nebula2 Drums, Voice, Chord, Glitch, EQ, Warmer and VenPressorDyn.

- [x] Plugin slot (v0.1.2): one unit for instruments and effects. Load from a list of plugins used before, browse for a .vst3, or drop a .vst3 on the board
- [x] Open shows the plugin's own window over the whole screen, with a Back button
- [x] Notes from cables (four note inputs, gate, velocity) play the plugin; audio in and out on jacks
- [x] The plugin inside follows the song's tempo and position
- [x] Saved with the project, including the plugin's own preset and settings
- [x] Joe tested v0.1.2 in Cubase on Windows (26 Nov 2026): loads, plugin loads into a slot, its window opens and Back returns, sound through cables, saves and reopens. All worked.
- [ ] Still to try: Nexus / HALion in a slot, and dropping a .vst3 on the board inside Cubase
- [ ] List every installed VST3 in the Add units menu (needs a scan; today the list is the plugins loaded before)
- [ ] Knobs on the slot for the plugin's own parameters, so cables can move them
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

- [ ] Song page: which tabs play when
- [ ] Drag MIDI / audio out to Cubase
- [ ] Sound presets: save and load a whole tab; a preset browser

## Screen polish (Joe: "we can work the UI later")

- [x] Drag the board background to move around; the board always has spare room to the right and below (v0.1.4)
- [ ] Zoom out to see a whole board
- [ ] Add units menu: show a picture of each unit, so you can see what you're adding

## Later units

Everything in the SPEC table that isn't bold.

## Known gaps

- No undo yet.
- Boards are mono until the mixer.
- Fixed window size.
