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
- [x] Starting boards: Kick, Snare, Hi-hat, Percussion, Bass, Lead, Empty
- [ ] Sample player (drop an audio file on it)
- [ ] Clap board (needs the multi-hit envelope)
- [ ] Stereo cables (today a board is mono until its Output; pan is on the mixer)
- [ ] Undo and redo
- [ ] Play boards from a MIDI keyboard / Cubase MIDI track

## Stage 3: rhythm and notes

- [ ] Arpeggiator, with rate, gate, range and pattern as inputs
- [ ] Chord generator: bring in the Nebula2 Chord engine (`ChordEngine.h` is plain C++, so it drops in)
- [ ] Stab and Pad / strings boards (need chords and the multi-oscillator)
- [ ] Euclidean rhythm, clock divider, probability, ratchet, swing
- [ ] Longer patterns (more than one bar) and pattern chaining
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

- [ ] VST instrument slot: scan, load by drag and drop, open the plugin's window, save its state in the sound
- [ ] VST effect slot
- [ ] Per-tab load meter
- [ ] Freeze / bounce a tab to audio

This is the stage that can only be checked on Joe's PC in Cubase with his own plugins.

## Stage 7: the song

- [ ] Song page: which tabs play when
- [ ] Drag MIDI / audio out to Cubase
- [ ] Sound presets: save and load a whole tab; a preset browser

## Screen polish (Joe: "we can work the UI later")

- [ ] Add units menu: show a picture of each unit, so you can see what you're adding

## Later units

Everything in the SPEC table that isn't bold.

## Known gaps in v0.1.0

- No undo yet.
- One bar of 16 steps per pattern.
- Boards are mono until the mixer.
- Fixed window size.
