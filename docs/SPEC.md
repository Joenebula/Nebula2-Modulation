# Nebula2 Modulation: the design

First agreed with Joe on 26 November 2026. Rewritten on 27 November 2026 to match the plugin as it now is, after
the Modurand rework (styles, the All board, rack panels, the timeline). The order of work, and what is done and
open, is in [BUILD-PLAN.md](BUILD-PLAN.md).

Where this says **built**, it is in the code and tested. Where it says **not built**, it is still a plan.
The rework is on a branch and has not been released; the last release is v0.1.21.

## In one paragraph

A VST3 instrument for Cubase. Pick a style and press one button: a whole track appears, every sound built, cabled
and playing, with chords to match. Each sound is its own Eurorack-style rack of modules joined by cables. See every
sound together on one board, or one sound alone on its tab. A menu adds more units, which you patch in with cables.
Any VST3 you own can be loaded as a sound source or an effect. Each sound has a pattern with ready-made ideas, a
randomiser and full editing. A timeline chooses which sounds play where and moves knobs through the song. A mixing
desk along the bottom has level, mute and solo for every sound. One key is shared by the whole plugin, so
everything stays in key.

## The screen

```
┌────────────────────────────────────────────────────────────────────┐
│ Play  Tempo  Key  Undo Redo  - zoom +  Song     Add units   Tidy   │  top bar
├────────────────────────────────────────────────────────────────────┤
│ Techno  Hard techno  Acid  Tech house  House  ...        Ambient   │  styles
│ [Make track] Variation  Random knobs  Parts: Kick Snare ..  Mode   │
├────────────────────────────────────────────────────────────────────┤
│ [All] [Kick] [Clap] [Hi-hat] [Bass] [Lead] [Pad]  [+]  Change sound│  tabs
├────────────────────────────────────────────────────────────────────┤
│                                                                    │
│   All: every sound on one board, each in its own frame             │
│   A tab: that sound alone                                          │
│                                                                    │
├────────────────────────────────────────────────────────────────────┤
│ Kick   Clap   Hi-hat  Bass   Lead   Pad             │   Master     │  mixing desk
│ M S ▮  M S ▮  M S ▮   M S ▮  M S ▮  M S ▮           │     ▮        │
└────────────────────────────────────────────────────────────────────┘
```

The look is ours: dark, text buttons, a fixed window size. Joe chose to keep it rather than copy Modurand's light
theme, icon rail and floating toolbar.

## Styles: a whole track from one button (built)

- **A row of 14 styles**: Techno, Hard techno, Acid, Tech house, House, Deep house, Melodic, Trance, Dub, Electro,
  Drum & bass, Garage, Minimal, Ambient. Clicking one only chooses it.
- **Make track** builds every sound of the chosen style, cabled and playing, and makes chords in that style's
  mood. Press it again for a different track in the same style.
- **Variation** keeps every sound and makes every pattern again.
- **Random knobs** moves every sound-shaping knob a little. Tuning, levels, lengths and switches are left alone,
  so the track stays in key, in time and balanced.
- **Parts**: one chip per kind of sound (Kick, Snare, Clap, Hat, Perc, Bass, Lead, Arp, Stab, Pad, Strings). A
  chip is lit when the track has that sound. Clicking it makes that sound again in its place, keeping its name and
  mixer channel.
- **Mode**: Replace (the default) or Add. In Add, Make track and the part chips add to what is there.
- Each press is one step of Undo. The style is saved with the project.
- A style is a recipe: which sounds, which presets, which pattern ideas, which mood. It is made only from the
  starting sounds, presets and ideas the plugin already has.
- A style does not set the tempo. Cubase owns the tempo.

## The All board and the tabs (built)

- **All**, first in the tab row, shows every sound on one board. Each sound sits in its own frame in its own
  colour, with its name above it. A sound's units never mix with another's.
- The board opens **fitted** to the window. **-** and **+** zoom from 20% to 150%. Clicking the zoom figure fits
  everything in again.
- Everything works on the All board as it does on a tab: knobs, cables, dragging units, Undo.
- Clicking a sound selects it, for Add units, Change sound and Tidy. Double-clicking its name, or clicking its
  tab, shows it alone.
- **One tab per sound.** A tab shows only that sound's units and cables, with room to scroll.
- **+ adds a sound.** Pick the kind and a new tab opens with that board already built and sounding. It also offers
  your saved sounds and your own plugins as ready-made sounds.
- **Tabs scroll** when there are too many to fit. They can be renamed, duplicated, reordered and deleted.
- **Each sound has a colour**, used on its tab, its frame, its mixer channel and its output jacks.
- A new project opens on the All board. A project saved before the All board existed opens on its tab, as saved.

## The units as rack panels (built)

- Every unit is a **rack panel of the same height**, like hardware. Only the width differs.
- Knobs sit in the middle and are **as big as the room allows**: a unit with one knob has one big knob.
- The name is printed across the top, over a line in the **colour of its kind** (sources, shapers, filters,
  effects, movers, rhythm).
- A screw in each corner. A **black screen** behind patterns, chords, waves and samples.
- Jacks sit in a **strip along the bottom**. Inputs are on the left, outputs on the right on a light pad with a
  ring in the sound's colour.
- A unit with a great many jacks gets a wider panel, never a taller one.
- **Tidy** lines a sound's panels up like a rack: rhythm and movers in the top row, the sound path in the row
  below, left to right in signal order. Cabling is not changed.

## Boards and cables (built)

- Pull a cable from an output to an input to connect them. Pull a plug out of an input and drop it somewhere else
  to re-route. Drop it on nothing to remove it.
- While a cable is being dragged, every jack it could go into lights up.
- One output can feed several inputs.
- Dragging a left output to a left input brings the right side along, when both units have one.
- Units can be dragged anywhere; their cables follow.
- A new sound loads **already cabled**. Nothing about that cabling is locked.
- A unit that isn't connected through to the Output is dimmed and marked "not connected".

## Adding units (built)

- **Add units** opens a menu of every unit type under headings, with a search box and a picture of the unit the
  mouse is on.
- Tick one or several, then Add. They arrive on the selected sound's board **unconnected**, and you cable them in.
- A star marks a favourite. A right-click on a board offers the favourites.

## Changing a sound (built)

- **Change sound**, at the end of the tab row, swaps the selected sound for a preset, a sample from a file, a
  plugin, or one of your saved sounds. The pattern, the name and the mixer channel stay.
- Right-click a tab to **save a sound**. Saved sounds can be used in any project.

## Sources from other plugins (built)

- A **Plugin slot** holds any VST3 instrument or effect. Load from a list, browse for a file, or drop a .vst3 on
  the board.
- **Open** shows the plugin's own window, floating beside ours.
- Notes and gates from cables play an instrument. Audio in and out is on jacks, so it can go through any other
  unit.
- Four knobs on the slot can each be pointed at one of the plugin's own controls, and moved by cables.
- The plugin, its preset and its settings save with the project.

Limits, agreed:

- VST3 only.
- A plugin that hands over a list of its presets gets a **Presets** menu on the slot, and **<** **>** arrows to
  step through them. The slot shows the name of the one in use.
- A plugin that keeps presets in its own browser (Nexus, HALion) hands over no list: its sound is chosen in its
  own window, which floats beside ours while the track keeps playing.
- A hosted plugin can be changed through the controls it offers and through anything done to its audio. Its
  insides can't be reached.
- Many heavy plugins on one track is a lot of load. Each mixer channel shows its load. A freeze button is **not built**.

## Patterns (built)

- Each sound holds its own pattern, shown on its sequencer's screen. Patterns are 1, 2, 4 or 8 bars.
- **Ideas**: a list of ready-made patterns for that kind of sound.
- **Random**: a new pattern in the style of that sound, so a random kick still behaves like a kick.
- Everything stays editable afterwards.
- Chords come from the Nebula2 Chord engine: one progression for the whole song, always in key.
- Notes are stored as steps of the key, so changing the key moves every pattern with it.
- **Links** join sequencers or knobs on any sounds, so they move together.

## The timeline (built)

The **Song** button opens it.

- **Bars along the top**, a row for each sound, and a line showing where the song is.
- The song is cut into **sections** of so many bars. A filled block means that sound plays in that section. Click
  or drag to change. **Build up** brings one more sound in each section. Switched off, every sound plays all the time.
- **Moving a knob through the song:** **+ Knob** on a sound's row (or a right-click on any knob) gives that knob a
  line under its sound. Click the line to add a point, drag points, right-click a point to remove it.
- As the song plays the knob follows its line. After the last bar the song, and the line, go round again.
- A new line is flat at where the knob already is, so nothing changes until it is drawn.
- A knob with a line wears a small mark on its panel and turns on screen as the song plays.
- Sections and lines are saved with the project and can be undone.

**Not built:** sections of different lengths, names for sections, copying a section, recording a knob's movement
by turning it.

## The mixing desk (built)

- Always visible: level, mute, solo and a meter per sound, plus the master. Click a channel to go to its sound.
- **More** makes it taller, with longer faders, pan and each sound's load.
- Each sound can go to the main mix or to one of 15 separate outputs, to be mixed in Cubase.
- The desk is for mixing only. Nothing that changes a sound goes there.

## Into Cubase (built)

- Follows Cubase's tempo and transport. Its own **Play** runs it when Cubase is stopped.
- **Drag MIDI** from a sequencer or the chord unit, and **Drag audio** from a sound's Output, into the project.
- A **MIDI keyboard** unit plays a sound from your keyboard or a MIDI track, in key unless switched off.

## The units

Headings are the menu headings. **Bold** = built.

| Heading | Units |
|---|---|
| Sources | **Oscillator**, **Multi-oscillator**, **Noise**, **Sample player**, **Return**, Supersaw, FM pair, Pluck, Wavetable |
| Shapers | **Hit envelope**, **Envelope**, **Clap envelope**, Looping envelope |
| Filters | **Filter (low, high, band)**, Ladder low-pass, Acid filter, Comb, Formant |
| EQ | **3-band**, **Parametric**, **Low cut / high cut**, Graphic, Tilt |
| Dynamics | **Compressor** (with sidechain), **Ducker**, **Transient shaper**, Limiter, Noise gate |
| Distortion | **Soft saturation**, **Hard clip**, **Bitcrusher**, Overdrive, Fuzz, Wavefolder |
| Reverb | **Room**, **Hall**, **Plate**, Spring, Shimmer, Gated, Reverse |
| Delay and echo | **Digital**, **Tape echo**, **Ping-pong**, Multi-tap, Dub, Stutter |
| Movement | **Chorus**, **Phaser**, **Flanger**, Tremolo, Auto-pan, Ring modulator |
| Pitch and width | **Tripler**, **Harmoniser** (in key), **Stereo width**, **Pan**, Octaver |
| Movers | **LFO**, **Trance gate**, **Step modulator**, **Random**, **Macro knob**, **Envelope follower** |
| Notes and rhythm | **Drum sequencer**, **Note sequencer**, **MIDI keyboard**, **Chord generator**, **Arpeggiator**, **Euclidean rhythm**, **Ratchet**, **Swing** |
| Utilities | **VCA**, **Mixer**, **Attenuator**, **Crossfader**, **Switch**, **Slew**, **Clock**, **Divider**, **Logic**, **Chance**, **Send**, **Scope and tuner**, **Output** |
| Your plugins | **Plugin slot** |

## What each kind of sound starts with (built)

| Sound | Starting units |
|---|---|
| Kick | Drum sequencer → oscillator with a pitch drop and an amp envelope → VCA → soft saturation |
| Snare | Drum sequencer → oscillator (body) + filtered noise (snap), each with its own envelope → mixer |
| Clap | Drum sequencer → noise → filter → VCA with the clap envelope → room reverb |
| Hi-hat | Drum sequencer → noise → high-pass filter → VCA with a short envelope |
| Percussion | Drum sequencer → oscillator with a pitch drop → VCA |
| Bass | Note sequencer → oscillator → filter with a pluck envelope → VCA → soft saturation |
| Lead | Note sequencer → two oscillators → mixer → filter with an LFO → VCA |
| Arp | Chord generator → arpeggiator → oscillator → filter with an LFO → VCA → ping-pong delay |
| Stab | Chord generator → multi-oscillator → filter with a short envelope → VCA → soft saturation → plate reverb |
| Pad | Chord generator → multi-oscillator → filter with a slow LFO → VCA → chorus → hall reverb |
| Strings | Chord generator → multi-oscillator → low cut → filter → VCA → chorus → hall reverb |
| Sample | Drum sequencer → sample player |
| Effects (send) | Return → hall reverb, fed by Send units on other sounds |

Each kind has 4 to 6 presets. Styles pick from these.

## Rules the build keeps to

- **Everything stays in key.**
- **Zero means zero.** A knob at 0% does nothing at all.
- **A unit that can't be heard says so** (dimmed, "not connected").
- **Rhythm is stored as note lengths, not milliseconds**, so it follows the tempo.
- **Saves keep loading.** Projects saved by an old version open in a new one.
- **Everything can be undone**, including a whole Make track.
- **The same in any host.** The sound never depends on the block size Cubase uses.
- **Our look**: dark, text buttons, fixed window (the ParaEQ design system, shared with Nebula2 Chord).
- **The mixing desk is for mixing only.**
