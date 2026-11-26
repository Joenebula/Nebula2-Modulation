# Nebula2 Modulation: what we're building

Agreed with Joe on 26 November 2026. This is the design. The order of work is in [BUILD-PLAN.md](BUILD-PLAN.md).

## In one paragraph

A VST3 instrument for Cubase. Tabs along the top, one per sound. Each tab opens its own Eurorack-style board that
loads already cabled for its kind of sound. A menu adds more units, which you patch in with cables. Any VST3 you
own can be loaded as a sound source or an effect. Each tab has a pattern area with ready-made ideas, a randomiser
and full editing. A mixing desk along the bottom has mute, solo and level for every tab. One key is shared by the
whole plugin, so everything stays in key.

## The screen

```
┌──────────────────────────────────────────────────────────────┐
│ Play  Tempo  Key                       Add units   Tidy      │  top bar
├──────────────────────────────────────────────────────────────┤
│ [Kick] [Snare] [Hats] [Perc] [Bass] [Lead] [Pad]  ‹ ›  [+]   │  tabs
├──────────────────────────────────────────────────────────────┤
│                                                              │
│        The board for the selected tab only:                  │
│        its modules, jacks and cables                         │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│ Kick   Snare  Hats   Perc   Bass   Lead   Pad    │  Master   │  mixing desk
│ M S ▮  M S ▮  M S ▮  M S ▮  M S ▮  M S ▮  M S ▮  │    ▮      │
└──────────────────────────────────────────────────────────────┘
```

- **One tab per sound.** Only that sound's modules and cables are on screen. Nothing from another sound is mixed in.
- **+ adds a sound.** Pick the kind (kick, snare, hi-hat, percussion, bass, lead, stab, pad, strings, FX, or an
  empty board) and a new tab opens with that board already built and sounding.
- **Tabs scroll** when there are too many to fit. They can be renamed, duplicated, reordered and deleted.
- **The mixing desk** is always visible: mute, solo, level and a meter per tab, plus the master. Click a channel
  to jump to its tab.
- **Each tab has a colour**, used on its tab, its mixer channel and its jacks.
- **Tidy** lines a board's modules up in signal order without changing the cabling.

## Boards and cables

- A board is a small Eurorack. Every module has input and output jacks.
- Pull a cable from an output to an input to connect them. Pull a plug out of an input and drop it somewhere else
  to re-route. Drop it on nothing to remove it.
- One output can feed several inputs.
- Modules can be dragged anywhere; their cables follow.
- A new sound loads **already cabled**. Nothing about that cabling is locked.
- A module that isn't connected through to the Output is dimmed and marked "not connected", so it's obvious why
  it isn't doing anything.

## Adding units

- **Add units** opens a menu of every unit type under headings.
- Tick one or several, then Add. They arrive on the current board **unconnected**, and you cable them in.

## Sources from other plugins

- A **VST instrument slot** is an empty unit you drop any VST3 instrument into: Nexus, HALion, or anything else
  with sounds. Its own window opens for choosing the preset.
- As many slots as you like: five of one plugin, or a mix. Each has its own preset, level, tuning and octave.
- A **VST effect slot** does the same for effects you own.
- Each slot's audio comes out of a jack, so it can go through any other unit, alone or layered with other slots.
- The whole stack (plugins, presets, cabling, knob settings) saves as one sound.
- Joe's own plugins load through the same slots. No special handling.

Limits, agreed:

- VST3 only.
- A hosted plugin's preset is chosen in that plugin's own window. Its preset browser can't be shown inside ours.
- A hosted plugin can be morphed through the knobs it exposes and through anything done to its audio. Its
  insides can't be reached.
- Many heavy plugins on one track is a lot of load. A freeze/bounce button comes soon after the slots do.

## Patterns: an ideas area

- Each tab holds its own pattern, shown on that tab's board.
- **Ideas**: a list of ready-made patterns for that kind of sound.
- **Random**: a new pattern in the style of that sound, so a random kick still behaves like a kick.
- Everything stays editable afterwards.
- Chords come from the Nebula2 Chord engine (moods, progressions, always in key).
- Notes are stored as steps of the key, so changing the key moves every pattern with it.

## The units

Headings are the menu headings. **Bold** = in the first release set.

| Heading | Units |
|---|---|
| Sources | **Oscillator**, **Noise**, **Sample player** (drop one audio file in; start, length, tune, reverse, loop; no slicing), Multi-oscillator, Supersaw, FM pair, Pluck, Wavetable, Kick / Snare / Hat / Clap voices, **VST instrument slot** |
| Shapers | **Hit envelope**, **Envelope (ADSR)**, Transient / click, Looping envelope |
| Filters | **Filter (low, high, band)**, Ladder low-pass, Acid filter, Comb, Formant, Dual |
| EQ | **3-band**, **Parametric**, **Low cut / high cut**, Graphic, Tilt, Vintage |
| Reverb | **Room**, **Hall**, **Plate**, Spring, Shimmer, Gated, Reverse, Freeze |
| Delay and echo | **Digital**, **Tape echo**, **Ping-pong**, Multi-tap, Dub, Reverse, Stutter |
| Distortion | **Soft saturation**, **Hard clip**, **Bitcrusher**, Overdrive, Fuzz, Wavefolder, Rectifier |
| Movement | **Chorus**, **Phaser**, **Flanger**, Ensemble, Tremolo, Vibrato, Auto-pan, Rotary, Ring modulator, Frequency shifter |
| Dynamics | **Compressor** (with sidechain), **Transient shaper**, **Ducker**, Limiter, Noise gate, Multiband |
| Pitch and width | **Tripler** (three detuned, phased copies), **Harmoniser** (in key), **Stereo width**, **Pan**, Harmonics, Pitch shifter, Octaver, Mid/side |
| Movers | **LFO**, **Gate / trance gate**, **Step modulator**, **Random**, **Macro knob**, Envelope follower, Chaos, Probability gate, Ratchet, Swing |
| Notes and rhythm | **Drum sequencer**, **Note sequencer**, **Arpeggiator** (rate, gate, range, pattern all modulatable), **Chord generator**, Euclidean rhythm, Scale quantiser |
| Utilities | **VCA**, **Mixer**, **Attenuator / inverter / offset**, **Output**, **Splitter**, **Crossfader**, **Switch**, **Slew / glide**, **Clock divider**, **Logic**, **Scope and meter**, **Tuner**, VST effect slot |

## What each kind of sound starts with

| Sound | Starting units |
|---|---|
| Kick | Drum sequencer → oscillator + pitch drop + amp envelope → drive |
| Snare | Drum sequencer → oscillator (body) + filtered noise (snap), each with its own envelope → mixer |
| Clap | Drum sequencer → noise → multi-hit envelope → filter → short reverb |
| Hi-hat | Drum sequencer → noise → high-pass filter → short envelope, velocity wired in |
| Percussion | Drum sequencer → oscillator with pitch drop → envelope |
| Bass | Note sequencer → oscillator → filter with pluck envelope → envelope → drive |
| Lead | Note sequencer → two oscillators → mixer → filter with LFO → envelope |
| Stab | Chord generator → multi-oscillator → short envelope → filter → reverb |
| Pad / strings | Chord generator → multi-oscillator → slow envelope → filter with slow LFO → chorus → reverb |
| FX / riser | Noise + oscillator → long pitch and filter sweeps → reverb |

Any board can swap its source for a sample player or a VST instrument slot and keep the rest of the chain.

## Rules the build keeps to

- **Everything stays in key.**
- **Zero means zero.** A knob at 0% does nothing at all.
- **A unit that can't be heard says so** (dimmed, "not connected").
- **Rhythm is stored as note lengths, not milliseconds**, so it follows the tempo.
- **Saves keep loading.** Projects saved by an old version open in a new one.
- **Same look as Nebula2 Chord** (the ParaEQ design system).
