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
- [x] Sample player reads mp3 (v0.1.14)
- [ ] Sample player: copy the file into the project so it travels with it
- [x] Clap board, with its own clap envelope (v0.1.7)
- [x] Stereo cabling (v0.1.14): dragging a left output to a left input brings the right side along, when both units have one and the right input is free. Units themselves are as before: stereo ones have L and R jacks, mono ones have one
- [x] Undo and redo (v0.1.8): buttons and Ctrl+Z / Ctrl+Y. Covers knobs, patterns, cables, adding, moving and deleting units, tabs, the mixer, links, chords and key. Keeps the last 100 steps. A board is put back in place, so undoing a knob never reloads a plugin in a slot
- [ ] Not undoable yet: loading or emptying a plugin slot, changes made inside a hosted plugin's own window, the master fader
- [x] MIDI keyboard unit (v0.1.12): notes from a keyboard or a Cubase MIDI track. Pitch, Gate and Vel for a bass or lead; Note 1 to 4 for chords. Channel (All or 1 to 16), Octave, and Stay in key (on by default) or As played. Keys land on their exact sample. Works with Cubase stopped
- [x] MIDI keyboard (v0.1.14): pitch wheel (Bend knob sets how far, 2 semitones to start), mod wheel on its own jack, sustain pedal
- [ ] MIDI keyboard: the starting sounds still come with a sequencer, so the keyboard unit is cabled in by hand

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
- [x] Bigger Ideas libraries (v0.1.14): 16 kick, 12 snare, 12 hat, 12 percussion, 14 bass and 14 lead ideas

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
- [x] Your plugins as ready-made sounds (v0.1.13): + > Your plugins lists Nebula2 Drums, Voice, Chord, Glitch, EQ, Warmer, VenPressorDyn and any other Nebula2 plugin installed. Picking one makes a tab with it loaded in a slot and cabled: an instrument straight to the Output, an effect fed by send bus A. A MIDI keyboard unit (As played) is cabled into it. Not installed: the tab is still made and the slot says to press Load. Tested here with Nebula2 Chord and Nebula2 Glitch built for Linux
- [x] The same note on several cables into a slot is now one note, and a held note isn't cut when another key is added (v0.1.13)
- [ ] Still to try: Nexus / HALion in a slot, and dropping a .vst3 on the board inside Cubase
- [x] A slot's Load menu lists every VST3 in the usual plugin folders by name (v0.1.9). The list is made by looking at the folders only; nothing is loaded to build it
- [x] Knobs on the slot (v0.1.11): Knob 1 to 4, each pointed at one of the plugin's own controls from the Knobs button (pick from the list, or "the control I last moved in its window"). Mod 1 to 4 inputs let cables move them. Saved with the project
- [ ] Pointing a knob isn't undoable, and Nexus / HALion controls can only be tried on Joe's PC
- [x] Notes out of a hosted plugin (v0.1.14): a slot has Note 1 to 4 and Gate outputs carrying whatever notes the plugin sends out, so Nebula2 Chord in a slot can play an oscillator or another slot. Tested with Nebula2 Chord
- [ ] Latency of hosted plugins isn't compensated yet
- [x] Per-tab load figure (v0.1.15): each mixer channel shows LOAD n% when the desk is opened with More; orange from 40%. A Total load figure by the master adds them up (v0.1.20); orange from 70%

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
- [x] Drag audio out (v0.1.14): a DRAG AUDIO label on each tab's Output. Drag it into Cubase for a WAV of that sound alone, the chords once through from bar 1, at the song's tempo. **Joe to test the drag itself in Cubase**
- [ ] Freeze a tab (swap it for its audio inside the plugin)
- [x] Save and load a sound (v0.1.12): right-click a tab, Save this sound. Saved sounds are listed under + (Your saved sounds), from any project. They live in Documents / Nebula2 Modulation / Sounds. A plugin in a slot is saved with its settings
- [ ] A sound's sample file isn't copied with it; no pictures or tags in the list

## Change sound (done, v0.1.15)

Joe: "say for a kick, I need to be able to select presets or load in samples, and that's the same for every tab. I can
change it with a preset or another VST or sample. I need a menu I can select from for each."

- [x] A **Change sound** button at the top right of every board, and Change sound in the tab's right-click menu
- [x] Presets: 4 to 6 for every kind of sound (kick, snare, hi-hat, percussion, clap, bass, lead, arp, stab, pad, strings), 55 in all. Any tab can take any kind
- [x] A sample from a file: the same pattern plays it; a note pattern plays it at pitch
- [x] A plugin (instrument): yours, or any installed. The same pattern plays it; a drum pattern becomes notes on the key note
- [x] One of your saved sounds
- [x] The pattern, the tab's name, level, pan, mute, solo, output and song sections all stay. Undo brings the old sound back, plugin or sample included
- [x] A plugin's own presets (v0.1.16): a slot shows a Presets button when the plugin offers a list (Nebula2 Glitch's 8 show up). Tested with Nebula2 Glitch
- [ ] Plugins that keep their presets in their own browser (Nexus, HALion) won't show a list here: use Open
- [ ] An effect plugin can't be a tab's sound (it makes none of its own): the menu refuses it quietly. It should say why

## Joe's Cubase test of v0.1.20 (27 Nov 2026) and what v0.1.21 changed

Worked for him: loads and follows Cubase, saves and reopens, presets under Change sound, the desk's More / Less and pan,
the Song page, DRAG MIDI from a sequencer, Save this sound, the sound so far.

- [x] A plugin's window replaced the whole board, and closing it left him lost. It now opens as its own window on top, beside the board, so Knobs and cables stay usable. **Joe to confirm on Windows**: this path can't run in the tests (no screen there), which still use the old in-window view
- [x] Change sound did nothing when he picked some plugins. It now says why: not found on this computer, or it is an effect
- [x] Change sound button was hard to see: now drawn lit
- [x] The output menu listed all 16 outputs. Now only the ones switched on in Cubase, with a line saying where to switch more on
- [x] "What are the groups?" The Link menu now says to pick the same letter on each sequencer, and names the tabs already on each letter
- [x] Stereo cabling didn't seem to work, and he asked for the possible jacks to light up while dragging: they now do. Right into right also brings the left along
- [x] "Playing" with a dead button read as a bug: it now says "With Cubase" while Cubase is running the transport
- [x] Add units: text was too small to read; now bigger and brighter, with a search box, a star per unit for Favourites (kept between sessions), and a right-click on the board that offers the favourites and Add units
- [ ] Still open from his list: the sample change "kept playing" (not clear yet what he saw), whether Drums / Voice are found by name on his PC, the whole window being small on his screen (a size setting for the whole plugin), DRAG AUDIO is easy to miss on the Output unit
- Not a fault: DRAG MIDI is on our Chord generator unit; a hosted Nebula2 Chord plugin in a slot has no DRAG MIDI because its notes are its own

## The Modurand rework (27 Nov 2026, released as v0.1.22)

Joe sent a video of Modurand: "This is where we need to be." Of the five differences, he chose: keep our look
(dark, text buttons, fixed window) and rework the other four.

- [x] **One button makes a whole track.** A style bar under the top bar: 14 styles (Techno, Hard techno, Acid, Tech house, House, Deep house, Melodic, Trance, Dub, Electro, Drum & bass, Garage, Minimal, Ambient). Make track builds every sound of the style, cabled and playing, with chords in the style's mood. Variation (new patterns, same sounds), Random knobs (shaping knobs move a little; tuning, levels and switches stay), Parts chips (one sound made again in place, keeping its mixer channel), Mode Replace / Add. Each is one Undo step. The style is saved with the project. `Source/Styles.h`
- [x] **Everything on one board.** An All tab, first in the tab row, shows every sound in its own frame on one board, fitted to the window. Zoom 20% to 150%; click the zoom figure to fit again. Each frame is an ordinary board, so cables, knobs and Undo work as on a tab. A new project opens on it; an older project opens on its tab as it was saved
- [x] **Modules look like hardware.** Every unit is a rack panel of the same height: knobs as big as the room allows, a screw in each corner, the name printed on the plate over a line in the colour of its kind, a black screen behind patterns and chords, jacks in a strip along the bottom with outputs on a light pad. Tidy lines panels up side by side in two rows. A test checks every unit fits its panel
- [x] **A timeline with knob movement.** The Song page is now a timeline: bars along the top, a row per sound, the section blocks as before, a playhead. + Knob (or right-click a knob) gives a knob a line; click to add points, drag them, right-click to remove. Knobs follow their lines as the song plays and go round with it. Saved, undoable, and the same at any block size
- [x] **Curves on knob lines** (v0.1.23; Joe: "can the arranger automation have curves?", and he chose bending each stretch over one Smooth switch). A small diamond sits half way along each stretch: drag it up or down and the middle of the stretch follows the mouse, so the knob moves early and eases in, or holds back and moves late. Double-click the diamond to go straight again. Saved on an `ac` line, written only when there is a bend, so older projects and older versions are untouched
- [ ] **Joe to test in Cubase**: all four. None of it can be heard or clicked here beyond the tests and snapshots
- [ ] A style doesn't set the tempo (Cubase owns it). Drum & bass at 124 isn't drum & bass until Cubase is at 174
- [ ] Old projects keep their unit positions, which were laid out for the bigger panels: press Tidy
- [ ] A knob with a line can still be turned by hand, but the line takes it back while the song plays
- [ ] Change sound drops that sound's knob lines (the units they pointed at are gone)
- [ ] Not done: their light theme, icon side rail and floating toolbar (Joe: keep ours)

## More room for the board (done, v0.1.24)

Joe: "can we make an option to view full plugin screen so I can see more of the workspace".

- [x] **Full screen**: a button at the right of the tab row grows the plugin's window to fill the monitor it is on (leaving room for Cubase's own frame). It then reads Normal size, which puts it back
- [x] **Focus**: hides the style bar and the mixing desk so the board takes their room. Press again to bring them back
- [x] Both together leave only the top bar and the tabs above the board. Both are saved with the project
- [ ] **Joe to test in Cubase**: that the window really grows there, and to the right monitor. Here there is no screen, so the tests use a stand-in size

## The Notes and rhythm units, from Joe's design (done, v0.1.25)

Joe sent a picture of the Notes and rhythm units redrawn and asked for it to be built, for those units only (every
other unit stays a tall panel).

- [x] **Wide panels** for the eight units (drum sequencer, note sequencer, MIDI keyboard, chord generator, arpeggiator, Euclidean rhythm, ratchet, swing): name centred over a line, screws, screen on the left, knobs and drop-downs to its right, inputs at the bottom left, outputs together on a raised pad at the bottom right. Still the same height as every other unit, so Tidy lines everything up
- [x] **Drum sequencer**: bar buttons (1 bar, 2, 4, 8), the pattern idea as a drop-down (it reads "Your own" after an edit), Random; a mark per bar to move between bars (or click the chosen length again); velocity bars under the steps, tall for an accent, click to toggle; DRAG MIDI at the bottom left
- [x] **Note sequencer**: the same top line; notes drawn piano-roll style with the playing step shaded; click or drag to set a note, click a note to remove it
- [x] **MIDI keyboard**: a two-octave keyboard that lights the keys being held
- [x] **Arpeggiator**: a screen drawing the order the notes play in, with the pattern and rate on it; Rate and Pattern as drop-downs
- [x] **Chord generator**: tiles with the chord's name over its length, above a keyboard that lights the notes of the chord playing; Mood and Rhythm drop-downs, Generate, More, DRAG MIDI
- [x] **Euclidean rhythm**: a ring with the hits lit, the loud ones ringed, the playing step marked, and "5 / 16" in the middle
- [x] **Ratchet** and **Swing**: a row of dots going in and the row coming out
- Different from the picture, for Joe to decide: the panels are taller in proportion (they keep the common height); no rack rails; the line under the name is the sound's colour, not always green; kept because they do a job today: the note names under the note grid, the chord unit's song length row and Octave knob, and LINK beside DRAG MIDI
- Not a new knob: the picture's **Accent** on the Euclidean rhythm was already there (every Nth step is loud; default 4). Its sound is unchanged. If Joe wants "N of the hits, spread evenly" instead, that is a new knob with a new id, so old projects stay as they are
- [ ] Boards are wider now: a bass's sequencer is about 640 across and a chord unit about 830. Older projects open with units overlapping until Tidy is pressed
- [ ] **Joe to test in Cubase**: clicking the bar buttons, velocity bars and drop-downs with a real mouse; the keyboard lighting from a real MIDI keyboard
- Fixed on the way (v0.1.25): a delay line could read one sample past its end when the position rounded exactly onto it

## Bigger views (done, v0.1.26: for Joe to try)

Joe: "do any of the modules need to expand? Open a bigger view? So you can do more". Each of these units has an Expand label in its top line. It opens a panel over the board (Close, Escape or a click outside closes it; Undo, Redo and Play are on it). The song keeps playing and the view follows it. It closes itself if its unit goes.

- [x] **Note sequencer**: a piano roll. Every bar of the pattern at once (up to 8), three octaves of the key. Click to add or remove a note, drag up and down for pitch, drag a note's right-hand end for its own length (new: a note without one still takes the Length knob). Copy / paste / clear a bar
- [x] **Drum sequencer**: every bar at once, one row each. A bar under every hit to drag for how hard it hits (new: any strength, not just hit and accent). Copy / paste / clear a bar
- [x] **Chord generator**: every chord of the song with its name, length and notes on a keyboard; the picked chord's colour, bass note, change to, length and lock as buttons
- [x] **Sample player**: a large wave; drag the start and the end; zoom in and out (buttons or the mouse wheel); the playhead
- [x] **Parametric EQ**: the curve drawn large; drag a band's point for where and how much; mouse wheel or right-button drag for how wide
- [x] **Step modulator**: every bar in one row; drag across the steps to draw, right-drag to switch steps off. Copy / paste / clear a bar
- [x] **Scope and tuner**: a large scope, with the note, cents and frequency in large letters, and Freeze
- Different from the list, for Joe to decide: the piano roll shows the same three octaves as the unit (a wider range would not show properly on the unit); a long note is cut where the next note starts (one line, one note at a time); notes move up and down only, not sideways; the sample view has no Load button of its own
- [ ] **Joe to test in Cubase**: all of it with a real mouse, at the normal size and Full screen
- Already has one: the plugin slot (Open). No need: single-knob units (VCA, attenuator, pan and the like)

## Inside mixer (built, not released yet)

Joe (28 Nov 2026): mix the units inside one sound on the desk, e.g. the layers inside a kick.

- [x] **Sounds / Inside** on the desk's top edge, left of More / Less. Sounds is the desk as it was. Inside shows the selected sound: its name, then a fader for each of its levels and mixes. Saved with the project; older projects open on Sounds
- [x] **What gets a fader** (one rule, `isMixKnob` in `Presets.h`): every knob called Level, the compressor's Make-up, every input of a Mixer unit, every effect's Mix, and a Send's Amount. Nothing else
- [x] A fader is the knob: moving one turns the other at once. Undo, linked knobs and the timeline work as they do on the board
- [x] Each strip says the unit and the knob, and what it is set to. Click the name and the board shows that unit, outlined for a moment
- [x] Units that can't be heard are dimmed. More strips than fit: the mouse wheel moves along them. More / Less gives longer faders. The master stays where it is. No pan, mute, solo or output in Inside
- [x] On the All view it shows the sound last selected. A sound with nothing to mix says so
- For Joe to decide: the strips go left to right as the units sit on the board; a Mixer unit shows all four inputs, used or not; the Crossfader, Attenuator and EQ gains are left out; the Output unit's Level is in (it is the last strip)
- [ ] **Joe to test in Cubase** with a real mouse

## Screen polish (Joe: "we can work the UI later")

- [x] The mixing desk opens up (v0.1.17). Joe: "the mix at the bottom needs to be able to be made taller showing the other hidden options, and reduced hiding them again". A More / Less tab sits on the desk's top edge above the master. Opened, the desk is about twice as tall: every fader, the master included, grows to fill it, and each channel shows Pan and its load figure (v0.1.19; Joe: "make it taller, the master level needs to grow also", and the More / Less was "badly placed" beside the master fader). Saved with the project. Change sound was there in v0.1.17 and came out in v0.1.18 (Joe: "this is mixing elements"): the desk is for mixing only

- [x] Drag the board background to move around; the board always has spare room to the right and below (v0.1.4)
- [x] Zoom: 50% to 125%, with - and + at the top or Ctrl + wheel; saved with the project (v0.1.9)
- [x] Add units menu: hovering a row shows a picture of that unit (v0.1.9)

## Later units

Everything in the SPEC table that isn't bold.

## Known gaps

- Boards are mono until the mixer.
- Fixed window size (Joe, 27 Nov 2026: keep it).
