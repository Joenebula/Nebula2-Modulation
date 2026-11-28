#pragma once
// Nebula2 Modulation: what each kind of sound starts with (the pre-cabled boards), the pattern
// ideas for each kind, the in-style randomisers, and the sizes used to lay a board out tidily.
#include "Modules.h"

namespace n2m
{
// ---- Sizes of a module on screen (shared by the screen and by Tidy) ----
// Every module is a rack panel of the same height, like hardware: only the width differs. Knobs sit in a grid
// in the middle, as big as the room allows; jacks sit in a strip along the bottom.
namespace lay
{
constexpr int panelH = 248, header = 22, pad = 8, foot = 12;
constexpr int cell = 48, cellMinH = 52, cellMaxH = 84, jackW = 34, jackH = 36;
constexpr int drumSeqH = 62, noteSeqH = 118, seqW = 16 * 20;
constexpr int chordW = 8 * 60, chordTileH = 22, chordSeqH = 28 + 4 * chordTileH + 6;   // room for 32 chords, 8 to a row
constexpr int hostW = 360, hostSeqH = 58, sampleSeqH = 86, scopeSeqH = 112;
inline int seqHeight (const Info& i) { return i.seq == Seq::Drum ? drumSeqH : i.seq == Seq::Note || i.seq == Seq::Value ? noteSeqH : i.seq == Seq::Chord ? chordSeqH : i.seq == Seq::Host ? hostSeqH : i.seq == Seq::Sample ? sampleSeqH : i.seq == Seq::Scope ? scopeSeqH : 0; }
inline int seqWidth (const Info& i) { return i.seq == Seq::Chord ? chordW : i.seq == Seq::Host ? hostW : i.seq != Seq::None ? seqW : 0; }

// ---- Wide panels: the Notes and rhythm units ----
// Same height as every other panel, but laid out sideways: a screen on the left, the knobs and drop-downs in a row
// to the right of it, and one row of jacks along the bottom (inputs on the left, outputs together on the right).
constexpr int wideSide = pad + 2, wideGap = 10, wideKnobW = 58, wideCellH = 84, wideCellGap = 4, jackSlot = jackW + 4;
constexpr int wideBodyY = header + 4, wideJacksY = panelH - foot - jackH, wideBodyH = wideJacksY - wideBodyY - 6;
constexpr int stepW = 22, wideSeqW = kSteps * stepW;        // a sequencer's step grid
constexpr int chordColX = chordW + 10, chordColW = 250;     // the chord unit's own buttons sit in a column beside its tiles
constexpr int wideTileH = 26;                               // a chord tile: its name over its length
constexpr int midiW = 66, midiH = 18, linkRoom = 150;       // the DRAG MIDI label, and room beside it for a sequencer's LINK label
constexpr int wideScreenH = 124;                            // the small screens (keyboard, arpeggiator, ring, in and out)
constexpr int kMaxCells = 8;
struct Box
{
    int x = 0, y = 0, w = 0, h = 0;
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool empty() const { return w <= 0 || h <= 0; }
    bool overlaps (const Box& o) const { return ! empty() && ! o.empty() && x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom(); }
};
inline bool isWide (const Info& i) { return std::string (i.shelf) == "Notes and rhythm"; }
// How wide a wide unit's screen is
inline int screenWidth (const Info& i)
{
    const std::string t = i.type;
    if (i.seq == Seq::Drum || i.seq == Seq::Note) return wideSeqW;
    if (i.seq == Seq::Chord) return chordW;
    if (t == "keys") return 14 * 20 + 2;
    if (t == "euclid") return wideScreenH;     // a square, for the ring
    if (t == "arp" || t == "ratchet" || t == "swing") return 200;
    return 0;
}
// A drop-down is as wide as its longest choice needs
inline int dropWidth (const Param& p)
{
    size_t longest = 0;
    for (auto* c : p.choices) longest = std::max (longest, std::char_traits<char>::length (c));
    return std::min (116, std::max (72, (int) longest * 7 + 34));
}

// Where everything on a panel goes
struct Geo
{
    int w = 0, h = panelH;
    int cols = 1, rows = 0, cellW = cell, cellH = cellMinH, knobsX = pad, knobsY = header;   // the knob grid
    int jackCols = 1, jackRows = 1, jacksY = 0;                                              // the jack strip
    // A wide panel (Notes and rhythm): the screen, then each knob's own place (an empty box = not shown as a knob:
    // a sequencer's Bars are buttons on its screen)
    bool wide = false;
    Box screen, midi;                  // midi: the DRAG MIDI label, on units that have one
    int extraW = 0;                    // more room to the right of the screen that belongs to the unit's own buttons
    Box cellBox[kMaxCells];
};
inline Geo wideGeo (const Info& i)
{
    Geo g;
    g.wide = true;
    const std::string type = i.type;
    const int nKnobs = (int) i.params.size(), nIn = (int) i.ins.size(), nOut = (int) i.outs.size();
    const bool seq = i.seq == Seq::Drum || i.seq == Seq::Note, chord = i.seq == Seq::Chord;
    g.screen = { wideSide, wideBodyY, screenWidth (i), wideBodyH };
    if (! seq && ! chord) { g.screen.y = wideBodyY + (wideBodyH - wideScreenH) / 2; g.screen.h = wideScreenH; }      // level with the knobs
    g.extraW = chord ? chordColX - chordW + chordColW : 0;
    int x = wideSide + (g.screen.w > 0 ? g.screen.w + g.extraW + wideGap : 0);
    const int cy = wideBodyY + (wideBodyH - wideCellH) / 2;
    int shown = 0;
    for (int k = 0; k < nKnobs && k < kMaxCells; ++k)
    {
        const auto& p = i.params[(size_t) k];
        const std::string id = p.id;
        if (seq && id == "bars") continue;                                                            // the bar buttons
        if (chord && id == "pattern") { g.cellBox[k] = { wideSide + chordColX, wideBodyY + 42, 104, 38 }; continue; }   // Rhythm, under Mood
        const int w = p.choices.empty() ? wideKnobW : dropWidth (p);
        g.cellBox[k] = { x, cy, w, wideCellH };
        x += w + wideCellGap;
        ++shown;
    }
    if (shown > 0) x -= wideCellGap; else if (g.screen.w > 0) x -= wideGap;
    if (seq) g.midi = { wideSide + 2, wideJacksY + 10, midiW, midiH };                                // bottom left, where a sequencer has no inputs
    if (chord) g.midi = { wideSide + chordColX + 176, wideBodyY + 57, 70, midiH };                    // beside More
    const int nameW = (int) std::char_traits<char>::length (i.name) * 8 + 40;
    const int jacksW = (nIn + nOut) * jackSlot + (nIn > 0 && nOut > 0 ? 16 : 0) + (seq ? midiW + 8 + linkRoom : 0);
    g.w = std::max ({ x, wideSide + nameW, wideSide + jacksW }) + wideSide;
    g.cols = std::max (1, shown); g.rows = shown > 0 ? 1 : 0; g.cellW = wideKnobW; g.cellH = wideCellH; g.knobsX = x; g.knobsY = cy;
    g.jackCols = std::max (1, nIn + nOut); g.jackRows = nIn + nOut > 0 ? 1 : 0; g.jacksY = wideJacksY;
    return g;
}
// A wide panel: the screen, every knob and the jacks each have their own room, inside the panel
inline bool wideFits (const Info& i, const Geo& g)
{
    const int nKnobs = (int) i.params.size(), nIn = (int) i.ins.size(), nOut = (int) i.outs.size();
    if (nKnobs > kMaxCells || g.h != panelH) return false;
    const Box inside { pad, header, g.w - 2 * pad, g.jacksY - header };
    auto within = [&inside] (const Box& b) { return b.x >= inside.x && b.y >= inside.y && b.right() <= inside.right() && b.bottom() <= inside.bottom(); };
    Box whole = g.screen; whole.w += g.extraW;
    if (! g.screen.empty() && ! within (whole)) return false;
    if (g.screen.w < screenWidth (i)) return false;
    for (int k = 0; k < nKnobs; ++k)
    {
        const auto& c = g.cellBox[k];
        if (c.empty()) continue;
        const bool knob = i.params[(size_t) k].choices.empty();
        if (! within (c) || c.w < cell - 2 || c.h < (knob ? cellMinH : 36) || c.overlaps (g.screen)) return false;
        for (int j = 0; j < k; ++j) if (c.overlaps (g.cellBox[j])) return false;
        if (c.overlaps (g.midi)) return false;
    }
    // the jacks: one row inside the panel, the inputs (and a sequencer's labels) clear of the outputs
    if (g.jacksY + g.jackRows * jackH > panelH || g.jackRows > 1) return false;
    const bool labelsInRow = ! g.midi.empty() && g.midi.y >= g.jacksY;
    const int insRight = wideSide + nIn * jackSlot + (labelsInRow ? g.midi.w + 8 + linkRoom : 0), outsLeft = g.w - wideSide - nOut * jackSlot;
    if (labelsInRow && nIn > 0) return false;
    if (! g.midi.empty() && (g.midi.x < pad || g.midi.right() > g.w - pad || g.midi.bottom() > panelH - 4)) return false;
    return insRight <= outsLeft;
}
inline Geo geo (const Info& i)
{
    if (isWide (i)) return wideGeo (i);
    const int nKnobs = (int) i.params.size(), nJacks = (int) (i.ins.size() + i.outs.size());
    const int seqH = seqHeight (i);
    // wide enough for its name in the header (sequencers and the like are wider than their names anyway)
    const int nameW = std::min (118, (int) std::char_traits<char>::length (i.name) * 7 + 22);
    Geo best;
    // The narrowest panel everything fits on: more knob columns first, then (for a unit with a great many
    // jacks) wider still so the jacks take fewer rows
    for (int extra = 0; extra <= 16; ++extra)
    for (int cols = 1; cols <= std::max (1, nKnobs); ++cols)
    {
        Geo g;
        const int inner = std::max ({ cols * cell, seqWidth (i), nameW, std::min (3, std::max (2, nJacks)) * jackW }) + extra * jackW;   // up to three jacks always share a row
        g.cols = nKnobs == 0 ? 1 : std::max (cols, std::min (nKnobs, inner / cell));
        if (i.perRow > 0 && g.cols % i.perRow != 0) continue;      // a unit whose knobs come in groups of so many keeps each group together in a row
        g.rows = nKnobs == 0 ? 0 : (nKnobs + g.cols - 1) / g.cols;
        // jacks: one row if they all fit; otherwise the inputs get their own rows above the outputs
        const int nIn = (int) i.ins.size(), nOut = (int) i.outs.size(), perRow = std::max (1, inner / jackW);
        g.jackCols = std::max (1, std::min (std::max (1, nJacks), perRow));
        g.jackRows = nJacks == 0 ? 0 : nJacks <= perRow ? 1 : (nIn + perRow - 1) / perRow + (nOut + perRow - 1) / perRow;
        g.w = inner + 2 * pad;
        g.jacksY = panelH - foot - g.jackRows * jackH;
        const int room = g.jacksY - header - seqH - 4;
        best = g;
        best.knobsY = header + seqH + 2;
        if (g.rows == 0 || g.rows * cellMinH <= room)
        {
            best.cellH = g.rows == 0 ? cellMinH : std::min (cellMaxH, room / g.rows);
            best.cellW = inner / g.cols;
            best.knobsX = pad + (inner - best.cellW * g.cols) / 2;
            best.knobsY = header + seqH + 2 + (room - best.cellH * g.rows) / 2;      // centred in the room there is
            return best;
        }
    }
    best.cellH = cellMinH; best.cellW = cell;       // (never reached by any unit there is: a test checks every one fits)
    return best;
}
// Does everything on the panel have its own room: screen, then knobs, then jacks, inside the panel?
inline bool fits (const Info& i)
{
    const auto g = geo (i);
    if (g.wide) return wideFits (i, g);
    const int nKnobs = (int) i.params.size();
    return g.knobsY >= header + seqHeight (i) && g.knobsY + g.rows * g.cellH <= g.jacksY && g.cellH >= cellMinH && g.cellW >= cell - 2
        && g.jacksY + g.jackRows * jackH <= panelH && g.cols * g.rows >= nKnobs && g.knobsX + g.cols * g.cellW <= g.w - pad + 1 && g.w >= seqWidth (i) + 2 * pad;
}
inline int width (const Info& i) { return geo (i).w; }
inline int height (const Info&) { return panelH; }
}

// ---- A pattern as a list of notes, for a MIDI file to drag into the host ----
// Times are in 16th steps from the start. Works out the same notes the unit plays: in key, following the chords,
// following its leader if it is linked as a follower.
struct NoteOut { double start = 0, len = 1; int midi = 60, vel = 100; };
struct NoteList { std::vector<NoteOut> notes; int steps = 0; };   // steps: how long the whole thing is

inline bool canExportMidi (const Module& m) { return m.info->seq == Seq::Note || m.info->seq == Seq::Chord || std::string (m.info->type) == "drumseq"; }

inline NoteList notesOf (const Rack& rack, const Module& m)
{
    NoteList out;
    const auto& tl = rack.timeline;
    const int* scale = scales()[(size_t) std::min ((int) scales().size() - 1, std::max (0, rack.scale.load()))].iv;
    const int root = rack.root.load();
    if (m.info->seq == Seq::Chord)
    {
        // One held chord per chord of the song: the bass note and the four notes the unit sends
        const int oct = (int) std::lround (m.P (m.paramIndex ("octave")));
        for (auto& s : tl.slots)
        {
            out.notes.push_back ({ (double) s.start, (double) s.len, 60 + (int) std::lround (s.bass * 12.0f), 96 });
            int last = -1;
            for (int k = 0; k < 4; ++k)
            {
                const int note = 60 + (int) std::lround (s.voice[k] * 12.0f) + 12 * oct;
                if (note != last) out.notes.push_back ({ (double) s.start, (double) s.len, note, 88 });
                last = note;
            }
        }
        out.steps = tl.total;
        return out;
    }
    if (std::string (m.info->type) == "drumseq")
    {
        const int len = loopSteps (m.P (m.paramIndex ("length")), m.P (m.paramIndex ("bars")));
        for (int k = 0; k < len; ++k)
        {
            const float d = m.data[(size_t) k].load();
            if (d > 0.5f) out.notes.push_back ({ (double) k, 0.5, 36, d > 1.5f ? 127 : 92 });
        }
        out.steps = ((len + kSteps - 1) / kSteps) * kSteps;
        return out;
    }
    if (m.info->seq != Seq::Note) return out;
    const int len = loopSteps (m.P (m.paramIndex ("length")), m.P (m.paramIndex ("bars")));
    const bool follow = m.P (m.paramIndex ("follow")) > 0.5f;
    const float gl = m.P (m.paramIndex ("gatelen"));
    const int octave = (int) std::lround (m.P (m.paramIndex ("octave")));
    // The sequencer this one follows, if it is linked that way: the first in its group that isn't itself following
    const Module* leader = nullptr;
    if (m.patternShare == 4 && m.patternLink > 0)
        for (auto& b : rack.boards)
            for (auto& o : b->modules)
                if (leader == nullptr && o->patternLink == m.patternLink && o->patternShare != 4 && o->info->seq == Seq::Note) leader = o.get();
    if (leader == &m) leader = nullptr;
    // A line that follows the chords (or a leader that might) is written out for the whole song, so it lines up
    const bool whole = (follow || leader != nullptr) && tl.total > 0;
    const int total = whole ? std::max (tl.total, len) : ((len + kSteps - 1) / kSteps) * kSteps;
    for (int s = 0; s < total; ++s)
    {
        const int k = wrapStep (s, len);
        if (m.data[(size_t) k].load() <= 0.5f) continue;
        int deg = (int) std::lround (m.data[(size_t) (kMaxSteps + k)].load()), lift = 0;
        if (follow) { const int ci = tl.indexAt (s); if (ci >= 0) { deg += tl.slots[(size_t) ci].deg; lift = tl.slots[(size_t) ci].tr; } }
        if (leader != nullptr) deg += leaderShift (*leader, s);
        out.notes.push_back ({ (double) s, gl >= 0.995f ? 1.0 : (double) std::min (0.96f, gl), 60 + root + degreeToSemis (deg, scale) + lift + 12 * octave, 100 });
    }
    out.steps = total;
    return out;
}

// Lines the modules up like a rack: rhythm and movers in the top row, the sound path in the row below,
// left to right in signal order, panels side by side. Cables are not changed.
inline void tidy (Board& b)
{
    const size_t n = b.modules.size();
    auto indexOf = [&b] (int uid) { for (size_t i = 0; i < b.modules.size(); ++i) if (b.modules[i]->uid == uid) return (int) i; return -1; };
    auto isTop = [&b] (size_t i) { const std::string s = b.modules[i]->info->shelf; return s == "Notes and rhythm" || s == "Movers"; };
    std::vector<int> depth (n, 0);
    for (size_t pass = 0; pass < n; ++pass)          // longest path, loops capped by the pass count
        for (auto& c : b.cables)
        {
            const int a = indexOf (c.src), d = indexOf (c.dst);
            if (a < 0 || d < 0 || isTop ((size_t) a) || isTop ((size_t) d)) continue;
            if (depth[(size_t) d] < depth[(size_t) a] + 1 && depth[(size_t) a] + 1 < (int) n) depth[(size_t) d] = depth[(size_t) a] + 1;
        }
    const int gapX = 6, gapY = 18, x0 = 16, y0 = 14;
    int x = x0;
    bool anyTop = false;
    for (size_t i = 0; i < n; ++i)
        if (isTop (i))
        {
            auto& m = *b.modules[i];
            m.x = (float) x; m.y = (float) y0;
            x += lay::width (*m.info) + gapX;
            anyTop = true;
        }
    const int y1 = y0 + (anyTop ? lay::panelH + gapY : 0);
    int maxDepth = 0;
    for (size_t i = 0; i < n; ++i) if (! isTop (i)) maxDepth = std::max (maxDepth, depth[i]);
    x = x0;
    for (int d = 0; d <= maxDepth; ++d)
        for (size_t i = 0; i < n; ++i)
            if (! isTop (i) && depth[i] == d)
            {
                auto& m = *b.modules[i];
                m.x = (float) x; m.y = (float) y1;
                x += lay::width (*m.info) + gapX;
            }
}

// ---- Patterns ----
// One bar of drum pattern as 16 characters: '.' off, 'x' hit, 'X' accent
inline void setDrumBar (Module& m, int bar, const char* pat)
{
    for (int i = 0; i < kSteps && pat[i] != 0; ++i)
        m.data[(size_t) (bar * kSteps + i)].store (pat[i] == 'X' ? 2.0f : pat[i] == 'x' ? 1.0f : 0.0f);
}
inline void setNotesBar (Module& m, int bar, const char* gates, const int* degrees)
{
    for (int i = 0; i < kSteps; ++i)
    {
        m.data[(size_t) (bar * kSteps + i)].store (gates[i] == 'x' ? 1.0f : 0.0f);
        m.data[(size_t) (kMaxSteps + bar * kSteps + i)].store ((float) degrees[i]);
    }
}
// A one-bar idea fills every bar, so making the pattern longer keeps it playing
inline void setDrum (Module& m, const char* pat) { for (int b = 0; b < kMaxBars; ++b) setDrumBar (m, b, pat); }
inline void setNotes (Module& m, const char* gates, const int* degrees) { for (int b = 0; b < kMaxBars; ++b) setNotesBar (m, b, gates, degrees); }

// A drum step's accent: a hit becomes an accent and an accent a plain hit. A step with no hit stays empty.
inline bool toggleAccent (Module& m, int step)
{
    if (step < 0 || step >= kMaxSteps) return false;
    const float d = m.data[(size_t) step].load();
    if (d < 0.5f) return false;
    m.data[(size_t) step].store (d > 1.5f ? 1.0f : 2.0f);
    return true;
}

struct DrumIdea { const char* name; const char* pat; };
struct NoteIdea { const char* name; const char* gates; int deg[16]; };

inline const std::vector<DrumIdea>& drumIdeas (const std::string& boardType)
{
    static const std::vector<DrumIdea> kick {
        { "Four on the floor", "X...x...x...x..." }, { "Push into the bar", "X...x...x...x.x." }, { "Off-beat double", "X...x..xx...x..." },
        { "Broken", "X..x..x...x..x.." }, { "Half time", "X.........x....." }, { "Rolling", "X..xx...x..xx..." },
        { "Two-step", "X.........x..x.." }, { "Driving 8ths", "X.x.x.x.x.x.x.x." },
        { "Skip the three", "X...x.......x..." }, { "Late double", "X...x...x..xx..." }, { "Stumble", "X..x....x.x....." },
        { "Breakbeat", "X.....x...X....." }, { "Garage", "X......x..x....." }, { "Three three two", "X..x..x.X..x..x." },
        { "Heartbeat", "X.x.....X.x....." }, { "Triplet feel", "X..x..x..x..x..." } };
    static const std::vector<DrumIdea> snare {
        { "Backbeat", "....X.......X..." }, { "Backbeat with ghost", "....X..x....X..." }, { "Late push", "....X.......X.x." },
        { "Half time", "........X......." }, { "Break", "....X..x.x..X..." }, { "Roll into the bar", "....X.......Xxxx" },
        { "Ghosts", "..x.X..x..x.X..x" }, { "Early second", "....X......X...." }, { "Drag", "...xX......xX..." },
        { "Double hit", "....X.......XX.." }, { "Garage shuffle", "....X..x....X..x" }, { "Every beat", "X...X...X...X..." } };
    static const std::vector<DrumIdea> hat {
        { "Off-beat", "..x...x...x...x." }, { "Straight 8ths", "x.x.x.x.x.x.x.x." }, { "16ths with accents", "xxXxxxXxxxXxxxXx" },
        { "Shuffle feel", "x.xxx.xxx.xxx.xx" }, { "Off-beat with skip", "..x...x..xx...x." }, { "Gallop", "x.xx.xx.x.xx.xx." },
        { "Accent the off-beat", "x.X.x.X.x.X.x.X." }, { "Roll at the end", "..x...x...x.xxxx" }, { "Sparse 16ths", "x..x.x..x..x.x.." },
        { "Push pull", "xxX.xxX.xxX.xxX." }, { "Open on four", "..x...x...x...X." }, { "Ticking", "xxxxxxxxxxxxxxxx" } };
    static const std::vector<DrumIdea> perc {
        { "Three against four", "x..x..x..x..x..x" }, { "Clave", "x..x..x...x.x..." }, { "Answer", "......x.......xx" },
        { "Tumble", "x.x..x..x.x..x.." }, { "Five in a bar", "x..x..x..x.x...." }, { "Sparse", "...x......x....." },
        { "Rumba", "x..x...x..x.x..." }, { "Shaker", "x.xXx.xXx.xXx.xX" }, { "Late answer", ".........x.x..x." },
        { "Rim clicks", "..x..x....x..x.." }, { "Conga talk", "x..xx.x...xx.x.." }, { "Cowbell", "x...x.x.x...x.x." } };
    // For the trance gate: lit steps let the sound through
    static const std::vector<DrumIdea> gate {
        { "Classic trance", "x.xxx.xxx.xxx.xx" }, { "Off-beat", "..xx..xx..xx..xx" }, { "Gallop", "x.xxx.xxx.xxx.xx" }, { "Dotted", "xxx.xxx.xxx.xx.x" },
        { "Stutter", "x.x.x.x.xxxxxxxx" }, { "Half bar hold", "xxxxxxxxx.x.x.x." }, { "Sparse", "x...x..x..x...x." }, { "Sixteenths", "x.x.x.x.x.x.x.x." } };
    static const std::vector<DrumIdea> any {
        { "Every beat", "x...x...x...x..." }, { "Off-beat", "..x...x...x...x." }, { "8ths", "x.x.x.x.x.x.x.x." }, { "Clave", "x..x..x...x.x..." } };
    if (boardType == "kick") return kick;
    if (boardType == "snare") return snare;
    if (boardType == "hat") return hat;
    if (boardType == "perc") return perc;
    if (boardType == "gate") return gate;
    if (boardType == "clap") return snare;
    return any;
}

inline const std::vector<NoteIdea>& noteIdeas (const std::string& boardType)
{
    static const std::vector<NoteIdea> bass {
        { "Off-beat", "..x...x...x...x.", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Rolling", ".xxx.xxx.xxx.xxx", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Octave jump", "x.x.x.x.x.x.x.x.", { 0, 0, 7, 0, 0, 0, 7, 0, 0, 0, 7, 0, 0, 0, 7, 0 } },
        { "Walk down", "x..x..x.x..x..x.", { 0, 0, 0, 0, 0, 0, -1, 0, -2, 0, 0, -2, 0, 0, -3, 0 } },
        { "Push and answer", "x..x..x...x.x...", { 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 4, 0, 2, 0, 0, 0 } },
        { "Acid line", "xxx.xx.xxx.xx.xx", { 0, 0, 7, 0, 0, 2, 0, 0, 0, 7, 0, 4, 2, 0, 0, 7 } },
        { "On the beat", "x...x...x...x...", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Root and fifth", "x.x...x.x.x...x.", { 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 4, 0 } },
        { "Gallop", "x.xxx.xxx.xxx.xx", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Climb", "x..x..x.x..x..x.", { 0, 0, 0, 1, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 4, 0 } },
        { "Dub", "x.......x.x.....", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -3, 0, 0, 0, 0, 0 } },
        { "Garage skip", "x..x...x..x..x..", { 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 4, 0, 0, 2, 0, 0 } },
        { "Octave bounce", "xxxxxxxxxxxxxxxx", { 0, 0, 7, 0, 0, 7, 0, 0, 7, 0, 0, 7, 0, 7, 0, 7 } },
        { "Long and low", "x.........x.....", { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0, 0, 0, 0 } } };
    static const std::vector<NoteIdea> lead {
        { "Arp up", "x.xx.xx.x.xx.xx.", { 0, 0, 2, 4, 4, 7, 4, 2, 0, 0, 2, 4, 4, 7, 9, 7 } },
        { "Up and down", "xxxxxxxxxxxxxxxx", { 0, 2, 4, 7, 9, 7, 4, 2, 0, 2, 4, 7, 9, 7, 4, 2 } },
        { "Call and answer", "x..x..x.....x.x.", { 4, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 7, 0 } },
        { "Stabs", "x.....x...x.....", { 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 0, 0 } },
        { "Trance gate", "x.xxx.xxx.xxx.xx", { 0, 0, 0, 0, 2, 0, 2, 2, 4, 0, 4, 4, 2, 0, 2, 2 } },
        { "Falling", "x.x.x.x.x.x.x.x.", { 9, 0, 7, 0, 4, 0, 2, 0, 7, 0, 4, 0, 2, 0, 0, 0 } },
        { "Rising thirds", "x.x.x.x.x.x.x.x.", { 0, 0, 2, 0, 1, 0, 3, 0, 2, 0, 4, 0, 3, 0, 5, 0 } },
        { "Hook", "x..x..x.x.....x.", { 4, 0, 0, 4, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1, 0 } },
        { "Off-beat plucks", "..x...x...x...x.", { 0, 0, 4, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 7, 0 } },
        { "Three note loop", "x..x..x..x..x..x", { 0, 0, 0, 2, 0, 0, 4, 0, 0, 0, 0, 0, 2, 0, 0, 4 } },
        { "Top note pedal", "xxxxxxxxxxxxxxxx", { 7, 0, 7, 2, 7, 4, 7, 2, 7, 0, 7, 2, 7, 4, 7, 5 } },
        { "Question and answer", "x.xx....x.xx....", { 0, 0, 2, 4, 0, 0, 0, 0, 4, 0, 2, 0, 0, 0, 0, 0 } },
        { "Octave leap", "x...x.x...x.x...", { 0, 0, 0, 0, 7, 0, 4, 0, 0, 0, 7, 0, 9, 0, 0, 0 } },
        { "Slow line", "x.......x...x...", { 4, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0 } } };
    return boardType == "bass" ? bass : lead;
}

// Which idea a sequencer's first bar is, or -1 if it isn't one of them (after Random, or once it has been edited).
// For the drop-down that shows the idea's name.
inline int drumIdeaIndex (const Module& m, const std::string& kind)
{
    const auto& ideas = drumIdeas (kind);
    for (size_t k = 0; k < ideas.size(); ++k)
    {
        bool same = true;
        for (int i = 0; i < kSteps && same; ++i)
        {
            const float d = m.data[(size_t) i].load();
            const char c = ideas[k].pat[i];
            same = (d > 1.5f ? 'X' : d > 0.5f ? 'x' : '.') == c;
        }
        if (same) return (int) k;
    }
    return -1;
}
inline int noteIdeaIndex (const Module& m, const std::string& boardType)
{
    const auto& ideas = noteIdeas (boardType);
    for (size_t k = 0; k < ideas.size(); ++k)
    {
        bool same = true;
        for (int i = 0; i < kSteps && same; ++i)
        {
            const bool on = m.data[(size_t) i].load() > 0.5f;
            same = on == (ideas[k].gates[i] == 'x') && (! on || (int) std::lround (m.data[(size_t) (kMaxSteps + i)].load()) == ideas[k].deg[i]);
        }
        if (same) return (int) k;
    }
    return -1;
}

// Shapes for the step modulator: 16 values, 0 (bottom) to 21 (top), every step on
struct ValueIdea { const char* name; int v[16]; };
inline const std::vector<ValueIdea>& valueIdeas()
{
    static const std::vector<ValueIdea> v {
        { "Ramp up", { 0, 1, 3, 4, 6, 7, 8, 10, 11, 13, 14, 15, 17, 18, 20, 21 } },
        { "Ramp down", { 21, 20, 18, 17, 15, 14, 13, 11, 10, 8, 7, 6, 4, 3, 1, 0 } },
        { "Up and down", { 0, 3, 6, 9, 12, 15, 18, 21, 21, 18, 15, 12, 9, 6, 3, 0 } },
        { "Two ramps", { 0, 3, 6, 9, 12, 15, 18, 21, 0, 3, 6, 9, 12, 15, 18, 21 } },
        { "High and low", { 21, 0, 21, 0, 21, 0, 21, 0, 21, 0, 21, 0, 21, 0, 21, 0 } },
        { "Steps", { 4, 4, 4, 4, 10, 10, 10, 10, 16, 16, 16, 16, 21, 21, 21, 21 } },
        { "Accent the beat", { 21, 6, 10, 6, 21, 6, 10, 6, 21, 6, 10, 6, 21, 6, 10, 6 } } };
    return v;
}
inline void setValues (Module& m, const int* v)
{
    for (int b = 0; b < kMaxBars; ++b)
        for (int i = 0; i < kSteps; ++i)
        {
            m.data[(size_t) (b * kSteps + i)].store (1.0f);
            m.data[(size_t) (kMaxSteps + b * kSteps + i)].store ((float) (v[i] - 7));     // rows run -7 to 14
        }
}

// ---- Random, but in the style of the sound ----
struct Rand
{
    explicit Rand (uint32_t s) : a (s ? s : 1u) {}
    float next() { a ^= a << 13; a ^= a >> 17; a ^= a << 5; return (float) (a & 0xffffffu) / 16777216.0f; }
    bool chance (float p) { return next() < p; }
    int upTo (int n) { return std::min (n - 1, (int) (next() * (float) n)); }
    uint32_t a;
};

// One bar in the style of the sound
inline void randomDrumBar (const std::string& boardType, Rand& r, char* p)
{
    if (boardType == "kick")
    {
        for (int i = 0; i < 16; i += 4) p[i] = 'x';                        // the four stays: it's a kick
        if (r.chance (0.25f)) p[r.chance (0.5f) ? 4 : 12] = '.';            // sometimes a beat drops out
        for (int i = 0; i < 16; ++i) if (i % 4 != 0 && r.chance (i % 2 ? 0.10f : 0.14f)) p[i] = 'x';
        p[0] = 'X';
    }
    else if (boardType == "snare")
    {
        p[4] = 'X'; p[12] = 'X';
        for (int i = 0; i < 16; ++i) if (p[i] == '.' && r.chance (i > 12 ? 0.25f : 0.08f)) p[i] = 'x';
    }
    else if (boardType == "hat")
    {
        const int style = r.upTo (3);
        for (int i = 0; i < 16; ++i)
        {
            const bool off = i % 4 == 2;
            if (style == 0) p[i] = off ? 'X' : (r.chance (0.12f) ? 'x' : '.');
            else if (style == 1) p[i] = i % 2 == 0 ? (off ? 'X' : 'x') : (r.chance (0.15f) ? 'x' : '.');
            else p[i] = r.chance (0.12f) ? '.' : (off ? 'X' : 'x');
        }
    }
    else
    {
        // Spread a few hits evenly round the bar, then turn the ring to a random start
        const int hits = 3 + r.upTo (5), turn = r.upTo (16);
        for (int i = 0; i < 16; ++i)
            if (((i * hits) % 16) < hits) p[(i + turn) % 16] = r.chance (0.25f) ? 'X' : 'x';
    }
}

// Bar 1 sets the idea, the other bars repeat it with small changes,
// and bars 4 and 8 end with a fill, so a longer pattern goes somewhere.
inline void evolveDrum (const std::string& boardType, Rand& r, const char* base, Module& m)
{
    for (int bar = 0; bar < kMaxBars; ++bar)
    {
        char p[17];
        std::copy (base, base + 17, p);
        if (bar > 0)
        {
            const int changes = bar % 2 == 1 ? 1 + r.upTo (2) : r.upTo (2);
            for (int k = 0; k < changes; ++k)
            {
                const int i = 1 + r.upTo (15);
                if (boardType == "kick" && i % 4 == 0) continue;                       // the four stays
                if (boardType == "snare" && (i == 4 || i == 12)) continue;             // so does the backbeat
                p[i] = p[i] == '.' ? 'x' : '.';
            }
            if (bar % 4 == 3)
            {
                if (boardType == "kick") p[14] = 'x';
                else if (boardType == "snare") { p[13] = 'x'; p[14] = 'x'; p[15] = 'X'; }
                else if (boardType == "hat") { p[12] = '.'; p[13] = '.'; p[14] = 'X'; }
                else p[15] = 'X';
            }
        }
        setDrumBar (m, bar, p);
    }
}

// A whole pattern, made up in the style of the sound
inline void randomDrum (const std::string& boardType, uint32_t seed, Module& m)
{
    Rand r (seed);
    char base[17] = "................";
    randomDrumBar (boardType, r, base);
    evolveDrum (boardType, r, base, m);
}

inline void randomNotesBar (const std::string& boardType, Rand& r, char* g, int* d)
{
    if (boardType == "bass")
    {
        const int style = r.upTo (3);
        static const int pool[6] = { 0, 0, 0, 7, 4, 2 };
        for (int i = 0; i < 16; ++i)
        {
            const bool on = style == 0 ? i % 4 == 2 || r.chance (0.12f)
                          : style == 1 ? i % 4 != 0 && ! r.chance (0.15f)
                          : r.chance (i % 2 == 0 ? 0.7f : 0.3f);
            g[i] = on ? 'x' : '.';
            d[i] = r.chance (0.7f) ? 0 : pool[r.upTo (6)];
        }
    }
    else
    {
        static const int pool[7] = { 0, 2, 4, 7, 9, 4, 11 };   // chord notes of the key, so it sounds like a tune
        int at = r.upTo (4);
        for (int i = 0; i < 16; ++i)
        {
            g[i] = r.chance (i % 2 == 0 ? 0.8f : 0.45f) ? 'x' : '.';
            at = std::min (6, std::max (0, at + r.upTo (3) - 1));
            d[i] = pool[at];
        }
        g[0] = 'x';
    }
}

// Bar 1 sets the tune; later bars keep its rhythm and change a few notes, so it develops instead of looping
inline void evolveNotes (Rand& r, const char* g, const int* d, Module& m)
{
    static const int moves[5] = { -2, -1, 1, 2, 4 };
    for (int bar = 0; bar < kMaxBars; ++bar)
    {
        char gg[17]; int dd[16];
        std::copy (g, g + 17, gg); std::copy (d, d + 16, dd);
        if (bar > 0)
        {
            const int changes = bar % 2 == 1 ? 2 + r.upTo (2) : r.upTo (2);
            for (int k = 0; k < changes; ++k)
            {
                const int i = 8 + r.upTo (8);                 // the second half of the bar is where it varies
                dd[i] = std::min (14, std::max (-7, dd[i] + moves[r.upTo (5)]));
                if (r.chance (0.3f) && i != 0) gg[i] = gg[i] == 'x' ? '.' : 'x';
            }
        }
        setNotesBar (m, bar, gg, dd);
    }
}

inline void randomNotes (const std::string& boardType, uint32_t seed, Module& m)
{
    Rand r (seed);
    char g[17] = "................";
    int d[16] {};
    randomNotesBar (boardType, r, g, d);
    evolveNotes (r, g, d, m);
}

// Random values for the step modulator: each bar its own, some steps held
inline void randomValues (uint32_t seed, Module& m)
{
    Rand r (seed);
    for (int k = 0; k < kMaxSteps; ++k)
    {
        m.data[(size_t) k].store (k % kSteps == 0 || r.chance (0.75f) ? 1.0f : 0.0f);
        m.data[(size_t) (kMaxSteps + k)].store ((float) (r.upTo (22) - 7));
    }
}

// ---- The kinds of sound a new tab can be ----
struct BoardType { const char* id; const char* name; int colour; };
inline const std::vector<BoardType>& boardTypes()
{
    static const std::vector<BoardType> t {
        { "kick", "Kick", 0 }, { "snare", "Snare", 3 }, { "clap", "Clap", 7 }, { "hat", "Hi-hat", 5 }, { "perc", "Percussion", 4 },
        { "bass", "Bass", 2 }, { "lead", "Lead", 1 }, { "arp", "Arp", 5 }, { "stab", "Stab", 7 }, { "pad", "Pad", 6 }, { "strings", "Strings", 4 },
        { "sample", "Sample", 3 }, { "fx", "Effects (send)", 1 }, { "empty", "Empty board", 0 } };
    return t;
}

// Builds a board of the given kind, already cabled and sounding
inline std::unique_ptr<Board> makeBoard (const std::string& type, double sr = 48000)
{
    auto b = std::make_unique<Board>();
    b->type = "empty"; b->name = "Sound"; b->colour = 0;
    for (auto& t : boardTypes()) if (type == t.id) { b->type = t.id; b->name = t.name; b->colour = t.colour; }
    if (b->type == "empty") b->name = "Sound";

    auto add = [&] (const char* t) { return b->add (t, 0, 0, sr); };
    auto cable = [&] (Module* a, const char* out, Module* c, const char* in) { b->connect (a->uid, a->outIndex (out), c->uid, c->inIndex (in)); };
    auto* out = add ("output");

    if (b->type == "kick")
    {
        auto* seq = add ("drumseq"); setDrum (*seq, drumIdeas ("kick")[0].pat);
        auto* amp = add ("hitenv"); amp->set ("decay", 0.32f);
        auto* drop = add ("hitenv"); drop->set ("decay", 0.06f);
        auto* osc = add ("osc"); osc->set ("wave", 0); osc->set ("coarse", -28); osc->set ("modamt", 30);
        auto* vca = add ("vca");
        auto* drive = add ("drive"); drive->set ("drive", 0.35f);
        cable (seq, "trig", amp, "trig"); cable (seq, "vel", amp, "vel"); cable (seq, "trig", drop, "trig");
        cable (drop, "out", osc, "mod"); cable (seq, "trig", osc, "sync"); cable (osc, "out", vca, "in"); cable (amp, "out", vca, "cv");
        cable (vca, "out", drive, "in"); cable (drive, "out", out, "left");
    }
    else if (b->type == "snare")
    {
        auto* seq = add ("drumseq"); setDrum (*seq, drumIdeas ("snare")[0].pat);
        auto* bodyEnv = add ("hitenv"); bodyEnv->set ("decay", 0.14f);
        auto* snapEnv = add ("hitenv"); snapEnv->set ("decay", 0.22f);
        auto* osc = add ("osc"); osc->set ("wave", 1); osc->set ("coarse", -7); osc->set ("modamt", 10);
        auto* noise = add ("noise");
        auto* filt = add ("filter"); filt->set ("type", 1); filt->set ("cutoff", 0.55f);
        auto* body = add ("vca");
        auto* snap = add ("vca");
        auto* mix = add ("mixer"); mix->set ("l1", 0.7f); mix->set ("l2", 0.9f);
        cable (seq, "trig", bodyEnv, "trig"); cable (seq, "trig", snapEnv, "trig"); cable (seq, "vel", snapEnv, "vel");
        cable (bodyEnv, "out", osc, "mod"); cable (seq, "trig", osc, "sync"); cable (osc, "out", body, "in"); cable (bodyEnv, "out", body, "cv");
        cable (noise, "out", filt, "in"); cable (filt, "out", snap, "in"); cable (snapEnv, "out", snap, "cv");
        cable (body, "out", mix, "in1"); cable (snap, "out", mix, "in2"); cable (mix, "out", out, "left");
    }
    else if (b->type == "hat")
    {
        auto* seq = add ("drumseq"); setDrum (*seq, drumIdeas ("hat")[0].pat);
        auto* env = add ("hitenv"); env->set ("decay", 0.07f);
        auto* noise = add ("noise");
        auto* filt = add ("filter"); filt->set ("type", 1); filt->set ("cutoff", 0.86f); filt->set ("res", 0.25f);
        auto* vca = add ("vca"); vca->set ("level", 0.7f);
        cable (seq, "trig", env, "trig"); cable (seq, "vel", env, "vel");
        cable (noise, "out", filt, "in"); cable (filt, "out", vca, "in"); cable (env, "out", vca, "cv");
        cable (vca, "out", out, "left");
    }
    else if (b->type == "perc")
    {
        auto* seq = add ("drumseq"); setDrum (*seq, drumIdeas ("perc")[1].pat);
        auto* amp = add ("hitenv"); amp->set ("decay", 0.16f);
        auto* drop = add ("hitenv"); drop->set ("decay", 0.03f);
        auto* osc = add ("osc"); osc->set ("wave", 0); osc->set ("coarse", 3); osc->set ("modamt", 9);
        auto* vca = add ("vca"); vca->set ("level", 0.7f);
        cable (seq, "trig", amp, "trig"); cable (seq, "vel", amp, "vel"); cable (seq, "trig", drop, "trig");
        cable (drop, "out", osc, "mod"); cable (seq, "trig", osc, "sync"); cable (osc, "out", vca, "in"); cable (amp, "out", vca, "cv");
        cable (vca, "out", out, "left");
    }
    else if (b->type == "bass")
    {
        auto* seq = add ("noteseq"); seq->set ("octave", -2); seq->set ("follow", 1);
        setNotes (*seq, noteIdeas ("bass")[0].gates, noteIdeas ("bass")[0].deg);
        auto* osc = add ("osc"); osc->set ("wave", 2);
        auto* pluck = add ("hitenv"); pluck->set ("decay", 0.18f);
        auto* filt = add ("filter"); filt->set ("cutoff", 0.36f); filt->set ("res", 0.3f); filt->set ("modamt", 3.0f);
        auto* env = add ("adsr"); env->set ("sustain", 0.8f); env->set ("release", 0.08f);
        auto* vca = add ("vca");
        auto* drive = add ("drive"); drive->set ("drive", 0.2f);
        cable (seq, "pitch", osc, "pitch"); cable (seq, "gate", env, "gate"); cable (seq, "gate", pluck, "trig");
        cable (pluck, "out", filt, "cutoff"); cable (osc, "out", filt, "in"); cable (filt, "out", vca, "in");
        cable (env, "out", vca, "cv"); cable (vca, "out", drive, "in"); cable (drive, "out", out, "left");
    }
    else if (b->type == "lead")
    {
        auto* seq = add ("noteseq"); seq->set ("octave", 0); seq->set ("follow", 1);
        setNotes (*seq, noteIdeas ("lead")[0].gates, noteIdeas ("lead")[0].deg);
        auto* lfo = add ("lfo"); lfo->set ("rate", 1);
        auto* saw = add ("osc"); saw->set ("wave", 2); saw->set ("level", 0.6f);
        auto* sq = add ("osc"); sq->set ("wave", 3); sq->set ("fine", 9); sq->set ("level", 0.45f);
        auto* mix = add ("mixer");
        auto* filt = add ("filter"); filt->set ("cutoff", 0.62f); filt->set ("res", 0.35f); filt->set ("modamt", 1.5f);
        auto* env = add ("adsr"); env->set ("sustain", 0.5f); env->set ("release", 0.25f);
        auto* vca = add ("vca"); vca->set ("level", 0.8f);
        cable (seq, "pitch", saw, "pitch"); cable (seq, "pitch", sq, "pitch"); cable (seq, "gate", env, "gate");
        cable (saw, "out", mix, "in1"); cable (sq, "out", mix, "in2"); cable (mix, "out", filt, "in");
        cable (lfo, "out", filt, "cutoff"); cable (filt, "out", vca, "in"); cable (env, "out", vca, "cv");
        cable (vca, "out", out, "left");
    }
    else if (b->type == "pad")
    {
        auto* ch = add ("chords");
        auto* lfo = add ("lfo"); lfo->set ("rate", 0);
        auto* osc = add ("multiosc"); osc->set ("detune", 14);
        auto* filt = add ("filter"); filt->set ("cutoff", 0.52f); filt->set ("res", 0.2f); filt->set ("modamt", 1.0f);
        auto* env = add ("adsr"); env->set ("attack", 0.35f); env->set ("decay", 0.6f); env->set ("sustain", 0.85f); env->set ("release", 1.2f);
        auto* vca = add ("vca"); vca->set ("level", 0.8f);
        cable (ch, "v1", osc, "p1"); cable (ch, "v2", osc, "p2"); cable (ch, "v3", osc, "p3"); cable (ch, "v4", osc, "p4");
        cable (ch, "gate", env, "gate"); cable (osc, "out", filt, "in"); cable (lfo, "out", filt, "cutoff");
        // Chorus for width, then a hall, in stereo all the way to the Output
        auto* chorus = add ("chorus"); chorus->set ("rate", 2); chorus->set ("mix", 0.4f);
        auto* hall = add ("hall"); hall->set ("mix", 0.35f); hall->set ("decay", 0.65f);
        cable (filt, "out", vca, "in"); cable (env, "out", vca, "cv"); cable (vca, "out", chorus, "in");
        cable (chorus, "outl", hall, "inl"); cable (chorus, "outr", hall, "inr");
        cable (hall, "outl", out, "left"); cable (hall, "outr", out, "right");
    }
    else if (b->type == "clap")
    {
        auto* seq = add ("drumseq"); setDrum (*seq, drumIdeas ("snare")[0].pat);
        auto* env = add ("clapenv");
        auto* noise = add ("noise");
        auto* filt = add ("filter"); filt->set ("type", 2); filt->set ("cutoff", 0.62f); filt->set ("res", 0.35f);
        auto* vca = add ("vca"); vca->set ("level", 1.4f);
        auto* room = add ("room"); room->set ("mix", 0.2f);
        cable (seq, "trig", env, "trig"); cable (seq, "vel", env, "vel");
        cable (noise, "out", filt, "in"); cable (filt, "out", vca, "in"); cable (env, "out", vca, "cv");
        cable (vca, "out", room, "inl"); cable (room, "outl", out, "left"); cable (room, "outr", out, "right");
    }
    else if (b->type == "strings")
    {
        auto* ch = add ("chords");
        auto* osc = add ("multiosc"); osc->set ("detune", 20); osc->set ("level", 0.6f);
        auto* cut = add ("cuts"); cut->set ("lowcut", 0.36f);
        auto* filt = add ("filter"); filt->set ("cutoff", 0.66f); filt->set ("res", 0.05f);
        auto* env = add ("adsr"); env->set ("attack", 0.6f); env->set ("decay", 0.8f); env->set ("sustain", 0.9f); env->set ("release", 1.6f);
        auto* vca = add ("vca"); vca->set ("level", 0.8f);
        auto* chorus = add ("chorus"); chorus->set ("rate", 3); chorus->set ("depth", 0.7f); chorus->set ("mix", 0.5f);
        auto* hall = add ("hall"); hall->set ("mix", 0.4f); hall->set ("decay", 0.7f);
        cable (ch, "v1", osc, "p1"); cable (ch, "v2", osc, "p2"); cable (ch, "v3", osc, "p3"); cable (ch, "v4", osc, "p4");
        cable (ch, "gate", env, "gate"); cable (osc, "out", cut, "in"); cable (cut, "out", filt, "in");
        cable (filt, "out", vca, "in"); cable (env, "out", vca, "cv"); cable (vca, "out", chorus, "in");
        cable (chorus, "outl", hall, "inl"); cable (chorus, "outr", hall, "inr");
        cable (hall, "outl", out, "left"); cable (hall, "outr", out, "right");
    }
    else if (b->type == "arp")
    {
        // The song's chords, played one note at a time, with an LFO slowly opening the filter and a bouncing echo
        auto* ch = add ("chords");
        auto* lfo = add ("lfo"); lfo->set ("rate", 1);
        auto* arp = add ("arp"); arp->set ("octave", 0);
        auto* osc = add ("osc"); osc->set ("wave", 2); osc->set ("level", 0.6f);
        auto* pluck = add ("hitenv"); pluck->set ("decay", 0.16f);
        auto* filt = add ("filter"); filt->set ("cutoff", 0.5f); filt->set ("res", 0.3f); filt->set ("modamt", 1.2f);
        auto* vca = add ("vca"); vca->set ("level", 0.8f);
        auto* echo = add ("pingpong"); echo->set ("time", 5); echo->set ("mix", 0.3f); echo->set ("feedback", 0.4f);
        cable (ch, "v1", arp, "p1"); cable (ch, "v2", arp, "p2"); cable (ch, "v3", arp, "p3"); cable (ch, "v4", arp, "p4");
        cable (arp, "pitch", osc, "pitch"); cable (arp, "gate", pluck, "trig"); cable (lfo, "out", filt, "cutoff");
        cable (osc, "out", filt, "in"); cable (filt, "out", vca, "in"); cable (pluck, "out", vca, "cv");
        cable (vca, "out", echo, "in"); cable (echo, "outl", out, "left"); cable (echo, "outr", out, "right");
    }
    else if (b->type == "sample")
    {
        // A sequencer firing a sample player. Silent until a file is loaded into it.
        auto* seq = add ("drumseq"); setDrum (*seq, "X...............");
        if (auto* smp = b->add ("sample", 0, 0, sr))
        {
            cable (seq, "trig", smp, "trig"); cable (seq, "vel", smp, "vel");
            cable (smp, "outl", out, "left"); cable (smp, "outr", out, "right");
        }
    }
    else if (b->type == "fx")
    {
        // A shared reverb: everything the Send units on other tabs feed to bus A, through a hall, all wet
        auto* ret = add ("return");
        auto* hall = add ("hall"); hall->set ("mix", 1.0f); hall->set ("decay", 0.6f);
        cable (ret, "outl", hall, "inl"); cable (ret, "outr", hall, "inr");
        cable (hall, "outl", out, "left"); cable (hall, "outr", out, "right");
    }
    else if (b->type == "stab")
    {
        auto* ch = add ("chords"); ch->set ("pattern", 1);
        auto* osc = add ("multiosc"); osc->set ("detune", 9);
        auto* snap = add ("hitenv"); snap->set ("decay", 0.22f);
        auto* filt = add ("filter"); filt->set ("cutoff", 0.5f); filt->set ("res", 0.3f); filt->set ("modamt", 2.5f);
        auto* vca = add ("vca"); vca->set ("level", 0.9f);
        auto* drive = add ("drive"); drive->set ("drive", 0.15f);
        cable (ch, "v1", osc, "p1"); cable (ch, "v2", osc, "p2"); cable (ch, "v3", osc, "p3"); cable (ch, "v4", osc, "p4");
        cable (ch, "gate", snap, "trig"); cable (osc, "out", filt, "in"); cable (snap, "out", filt, "cutoff");
        auto* plate = add ("plate"); plate->set ("mix", 0.22f); plate->set ("decay", 0.4f);
        cable (filt, "out", vca, "in"); cable (snap, "out", vca, "cv"); cable (vca, "out", drive, "in");
        cable (drive, "out", plate, "inl"); cable (plate, "outl", out, "left"); cable (plate, "outr", out, "right");
    }
    tidy (*b);
    return b;
}

// What a brand new project opens with
inline void loadStarter (Rack& rack)
{
    for (const char* t : { "kick", "snare", "hat", "bass", "pad" }) rack.addBoard (makeBoard (t, rack.sr));
    rack.boards[1]->level.store (0.6f);
    rack.boards[2]->level.store (0.45f);
    rack.boards[3]->level.store (0.6f);
    rack.boards[4]->level.store (0.5f);
}

// ---- Changing a tab's sound while its pattern keeps playing ----
// A preset is one of the starting sounds with some knobs set differently. nth: which unit of that type (0 = first).
struct Tweak { const char* type; int nth; const char* param; float value; };
struct SoundPreset { const char* kind; const char* name; std::vector<Tweak> tweaks; };
inline const std::vector<SoundPreset>& soundPresets()
{
    static const std::vector<SoundPreset> p {
        { "kick", "Standard", {} },
        { "kick", "Deep", { { "osc", 0, "coarse", -31 }, { "hitenv", 0, "decay", 0.5f }, { "drive", 0, "drive", 0.15f } } },
        { "kick", "Punchy", { { "hitenv", 0, "decay", 0.2f }, { "hitenv", 1, "decay", 0.03f }, { "osc", 0, "modamt", 36 }, { "drive", 0, "drive", 0.5f } } },
        { "kick", "Hard", { { "hitenv", 0, "decay", 0.26f }, { "osc", 0, "modamt", 40 }, { "drive", 0, "drive", 0.85f } } },
        { "kick", "Long boom", { { "osc", 0, "coarse", -30 }, { "hitenv", 0, "decay", 0.9f }, { "osc", 0, "modamt", 18 }, { "drive", 0, "drive", 0.25f } } },
        { "kick", "Tight click", { { "hitenv", 0, "decay", 0.12f }, { "hitenv", 1, "decay", 0.02f }, { "osc", 0, "modamt", 44 } } },
        { "snare", "Standard", {} },
        { "snare", "Short", { { "hitenv", 0, "decay", 0.08f }, { "hitenv", 1, "decay", 0.1f } } },
        { "snare", "Long", { { "hitenv", 1, "decay", 0.4f } } },
        { "snare", "Bright", { { "filter", 0, "cutoff", 0.8f } } },
        { "snare", "Dark", { { "filter", 0, "cutoff", 0.38f }, { "osc", 0, "coarse", -10 } } },
        { "snare", "Rim", { { "hitenv", 0, "decay", 0.05f }, { "hitenv", 1, "decay", 0.03f }, { "osc", 0, "coarse", 2 }, { "mixer", 0, "l2", 0.3f } } },
        { "hat", "Closed", {} },
        { "hat", "Open", { { "hitenv", 0, "decay", 0.35f } } },
        { "hat", "Tick", { { "hitenv", 0, "decay", 0.02f }, { "filter", 0, "cutoff", 0.95f } } },
        { "hat", "Shaker", { { "hitenv", 0, "decay", 0.12f }, { "filter", 0, "cutoff", 0.7f }, { "filter", 0, "res", 0.05f } } },
        { "perc", "Standard", {} },
        { "perc", "Tom", { { "osc", 0, "coarse", -8 }, { "hitenv", 0, "decay", 0.3f } } },
        { "perc", "Bongo", { { "osc", 0, "coarse", 8 }, { "hitenv", 0, "decay", 0.1f } } },
        { "perc", "Blip", { { "osc", 0, "coarse", 15 }, { "hitenv", 0, "decay", 0.06f }, { "osc", 0, "modamt", 2 } } },
        { "perc", "Zap", { { "osc", 0, "modamt", 30 }, { "hitenv", 1, "decay", 0.08f } } },
        { "perc", "Woodblock", { { "osc", 0, "coarse", 12 }, { "osc", 0, "wave", 1 }, { "hitenv", 0, "decay", 0.05f } } },
        { "clap", "Standard", {} },
        { "clap", "Tight", { { "room", 0, "mix", 0.0f } } },
        { "clap", "Big room", { { "room", 0, "mix", 0.45f } } },
        { "clap", "Dark", { { "filter", 0, "cutoff", 0.45f } } },
        { "bass", "Standard", {} },
        { "bass", "Sub", { { "osc", 0, "wave", 0 }, { "filter", 0, "cutoff", 0.5f }, { "drive", 0, "drive", 0.0f } } },
        { "bass", "Square", { { "osc", 0, "wave", 3 } } },
        { "bass", "Acid", { { "filter", 0, "res", 0.75f }, { "filter", 0, "modamt", 5.0f }, { "hitenv", 0, "decay", 0.12f }, { "drive", 0, "drive", 0.5f } } },
        { "bass", "Pluck", { { "hitenv", 0, "decay", 0.1f }, { "filter", 0, "cutoff", 0.3f }, { "adsr", 0, "sustain", 0.3f } } },
        { "bass", "Growl", { { "drive", 0, "drive", 0.8f }, { "filter", 0, "cutoff", 0.45f } } },
        { "lead", "Standard", {} },
        { "lead", "Soft", { { "filter", 0, "cutoff", 0.45f }, { "filter", 0, "res", 0.1f }, { "adsr", 0, "attack", 0.05f } } },
        { "lead", "Bright", { { "filter", 0, "cutoff", 0.85f } } },
        { "lead", "Hollow", { { "osc", 0, "wave", 3 } } },
        { "lead", "Wide", { { "osc", 1, "fine", 22 } } },
        { "arp", "Standard", {} },
        { "arp", "Soft", { { "filter", 0, "cutoff", 0.35f } } },
        { "arp", "Bright", { { "filter", 0, "cutoff", 0.7f } } },
        { "arp", "Dry", { { "pingpong", 0, "mix", 0.0f } } },
        { "stab", "Standard", {} },
        { "stab", "Short", { { "hitenv", 0, "decay", 0.1f } } },
        { "stab", "Bright", { { "filter", 0, "cutoff", 0.7f } } },
        { "stab", "Big", { { "plate", 0, "mix", 0.45f }, { "plate", 0, "decay", 0.7f } } },
        { "pad", "Standard", {} },
        { "pad", "Dark", { { "filter", 0, "cutoff", 0.35f } } },
        { "pad", "Bright", { { "filter", 0, "cutoff", 0.75f } } },
        { "pad", "Slow swell", { { "adsr", 0, "attack", 1.2f } } },
        { "pad", "Lush", { { "multiosc", 0, "detune", 24 }, { "chorus", 0, "mix", 0.6f }, { "hall", 0, "mix", 0.5f } } },
        { "strings", "Standard", {} },
        { "strings", "Dark", { { "filter", 0, "cutoff", 0.45f } } },
        { "strings", "Quick", { { "adsr", 0, "attack", 0.08f } } },
        { "strings", "Huge", { { "hall", 0, "mix", 0.6f }, { "hall", 0, "decay", 0.9f } } } };
    return p;
}

// A starting sound with a preset's knobs. Returns nothing if there is no such preset.
// missed (if given) counts tweaks that found no unit or knob to set: it must stay 0.
inline std::unique_ptr<Board> makePreset (const std::string& kind, const std::string& name, double sr = 48000, int* missed = nullptr)
{
    for (auto& p : soundPresets())
    {
        if (kind != p.kind || name != p.name) continue;
        auto b = makeBoard (kind, sr);
        for (auto& t : p.tweaks)
        {
            int seen = 0; bool done = false;
            for (auto& m : b->modules)
                if (std::string (m->info->type) == t.type && seen++ == t.nth)
                {
                    const int k = m->paramIndex (t.param);
                    if (k >= 0) { const auto& pr = m->info->params[(size_t) k]; m->params[(size_t) k].store (std::min (pr.hi, std::max (pr.lo, t.value))); done = true; }
                }
            if (! done && missed != nullptr) ++*missed;
        }
        return b;
    }
    return nullptr;
}

// The units that hold a tab's pattern: its drum sequencers, note sequencers and chord units, in order
inline std::vector<Module*> patternUnits (const Board& b)
{
    std::vector<Module*> v;
    for (auto& m : b.modules) if (std::string (m->info->type) == "drumseq" || m->info->seq == Seq::Note || m->info->seq == Seq::Chord) v.push_back (m.get());
    return v;
}

// Carries a tab's pattern and its mixer channel over to a new sound, so only the sound changes.
// Like goes to like. A drum pattern going to a note sequencer keeps its hits on the key note;
// notes going to a drum sequencer keep their rhythm.
inline void keepPattern (const Board& old, Board& fresh)
{
    fresh.name = old.name; fresh.colour = old.colour;
    fresh.level.store (old.level.load()); fresh.pan.store (old.pan.load());
    fresh.mute.store (old.mute.load()); fresh.solo.store (old.solo.load());
    fresh.out.store (old.out.load()); fresh.plays.store (old.plays.load());
    if (fresh.type == "sample" || fresh.type == "empty") fresh.type = old.type;       // so its pattern Ideas stay the right kind
    const auto from = patternUnits (old), to = patternUnits (fresh);
    for (size_t k = 0; k < to.size() && k < from.size(); ++k)
    {
        const Module& a = *from[k]; Module& b = *to[k];
        const bool aChord = a.info->seq == Seq::Chord, bChord = b.info->seq == Seq::Chord;
        if (aChord || bChord)
        {
            if (aChord && bChord) for (size_t p = 0; p < b.params.size(); ++p) b.params[p].store (a.params[p].load());
            continue;                                   // the chords themselves belong to the song, not the tab
        }
        for (const char* id : { "length", "bars", "octave", "gatelen", "follow" })
        {
            const int ia = a.paramIndex (id), ib = b.paramIndex (id);
            if (ia >= 0 && ib >= 0) b.params[(size_t) ib].store (a.params[(size_t) ia].load());
        }
        const bool aDrum = a.info->seq == Seq::Drum, bDrum = b.info->seq == Seq::Drum;
        for (int s = 0; s < kMaxSteps; ++s)
        {
            const float hit = a.data[(size_t) s].load();
            if (aDrum == bDrum) { b.data[(size_t) s].store (hit); b.data[(size_t) (kMaxSteps + s)].store (a.data[(size_t) (kMaxSteps + s)].load()); }
            else if (bDrum) { b.data[(size_t) s].store (hit > 0.5f ? 1.0f : 0.0f); b.data[(size_t) (kMaxSteps + s)].store (0.0f); }
            else { b.data[(size_t) s].store (hit > 0.5f ? 1.0f : 0.0f); b.data[(size_t) (kMaxSteps + s)].store (0.0f); }
        }
    }
}

// An empty sound with the same kind of pattern unit as the old one, cabled into "source" (a sample player or a
// plugin slot): trig / gate, and the pitch where the source takes one. The source goes to the Output in stereo.
inline std::unique_ptr<Board> makeCarrier (const Board& old, const char* sourceType, double sr)
{
    auto b = makeBoard ("empty", sr);
    auto* out = b->modules[0].get();
    auto* src = b->add (sourceType, 0, 0, sr);
    if (src == nullptr) return nullptr;
    auto cable = [&b] (Module* a, const char* o, Module* c, const char* i) { const int oi = a->outIndex (o), ii = c->inIndex (i); if (oi >= 0 && ii >= 0) b->connect (a->uid, oi, c->uid, ii); };
    const auto had = patternUnits (old);
    const bool plugin = std::string (sourceType) == "plugin";
    const Seq kind = had.empty() ? Seq::Drum : had[0]->info->seq;
    if (kind == Seq::Chord)
    {
        auto* ch = b->add ("chords", 0, 0, sr);
        if (plugin) { cable (ch, "v1", src, "p1"); cable (ch, "v2", src, "p2"); cable (ch, "v3", src, "p3"); cable (ch, "v4", src, "p4"); cable (ch, "gate", src, "gate"); }
        else { cable (ch, "bass", src, "pitch"); cable (ch, "gate", src, "trig"); }
    }
    else if (kind == Seq::Note || plugin)
    {
        // (a plugin needs a note to play, so a drum pattern becomes notes on the key note)
        auto* seq = b->add ("noteseq", 0, 0, sr);
        seq->set ("octave", 0); seq->set ("follow", 0);
        if (plugin) { cable (seq, "pitch", src, "p1"); cable (seq, "gate", src, "gate"); }
        else { cable (seq, "pitch", src, "pitch"); cable (seq, "gate", src, "trig"); }
    }
    else
    {
        auto* seq = b->add ("drumseq", 0, 0, sr);
        cable (seq, "trig", src, "trig"); cable (seq, "vel", src, "vel");
    }
    cable (src, "outl", out, "left"); cable (src, "outr", out, "right");
    tidy (*b);
    return b;
}
} // namespace n2m
