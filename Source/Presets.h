#pragma once
// Nebula2 Modulation: what each kind of sound starts with (the pre-cabled boards), the pattern
// ideas for each kind, the in-style randomisers, and the sizes used to lay a board out tidily.
#include "Modules.h"

namespace n2m
{
// ---- Sizes of a module on screen (shared by the screen and by Tidy) ----
namespace lay
{
constexpr int cell = 54, cellH = 62, header = 26, jackW = 40, jackH = 44, pad = 8;
constexpr int drumSeqH = 62, noteSeqH = 118, seqW = 16 * 20;
constexpr int chordW = 8 * 60, chordTileH = 28, chordSeqH = 28 + 4 * chordTileH + 6;   // room for 32 chords, 8 to a row
constexpr int hostW = 360, hostSeqH = 58, sampleSeqH = 86;
inline int seqHeight (const Info& i) { return i.seq == Seq::Drum ? drumSeqH : i.seq == Seq::Note || i.seq == Seq::Value ? noteSeqH : i.seq == Seq::Chord ? chordSeqH : i.seq == Seq::Host ? hostSeqH : i.seq == Seq::Sample ? sampleSeqH : 0; }
inline int seqWidth (const Info& i) { return i.seq == Seq::Chord ? chordW : i.seq == Seq::Host ? hostW : i.seq == Seq::Sample ? seqW : i.seq != Seq::None ? seqW : 0; }
constexpr int perRow = 5;
inline int rowLen (const Info& i) { return i.perRow > 0 ? i.perRow : perRow; }
inline int cols (const Info& i) { return (int) std::min<size_t> ((size_t) rowLen (i), i.params.size()); }
inline int rows (const Info& i) { return (int) ((i.params.size() + (size_t) rowLen (i) - 1) / (size_t) rowLen (i)); }
inline int width (const Info& i)
{
    const int jacks = (int) (i.ins.size() + i.outs.size());
    return std::max ({ cols (i) * cell, jacks * jackW, seqWidth (i), 96 }) + 2 * pad;
}
inline int height (const Info& i) { return header + seqHeight (i) + rows (i) * cellH + jackH + pad; }
}

// Lines the modules up in signal order: rhythm and movers along the top, the sound path below,
// left to right. Cables are not changed.
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
    int x = 24, topH = 0;
    for (size_t i = 0; i < n; ++i)
        if (isTop (i))
        {
            auto& m = *b.modules[i];
            m.x = (float) x; m.y = 24.0f;
            x += lay::width (*m.info) + 24;
            topH = std::max (topH, lay::height (*m.info));
        }
    const int y0 = 24 + (topH > 0 ? topH + 36 : 0);
    int maxDepth = 0;
    for (size_t i = 0; i < n; ++i) if (! isTop (i)) maxDepth = std::max (maxDepth, depth[i]);
    x = 24;
    for (int d = 0; d <= maxDepth; ++d)
    {
        int y = y0, colW = 0;
        for (size_t i = 0; i < n; ++i)
            if (! isTop (i) && depth[i] == d)
            {
                auto& m = *b.modules[i];
                m.x = (float) x; m.y = (float) y;
                y += lay::height (*m.info) + 20;
                colW = std::max (colW, lay::width (*m.info));
            }
        if (colW > 0) x += colW + 36;
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

struct DrumIdea { const char* name; const char* pat; };
struct NoteIdea { const char* name; const char* gates; int deg[16]; };

inline const std::vector<DrumIdea>& drumIdeas (const std::string& boardType)
{
    static const std::vector<DrumIdea> kick {
        { "Four on the floor", "X...x...x...x..." }, { "Push into the bar", "X...x...x...x.x." }, { "Off-beat double", "X...x..xx...x..." },
        { "Broken", "X..x..x...x..x.." }, { "Half time", "X.........x....." }, { "Rolling", "X..xx...x..xx..." },
        { "Two-step", "X.........x..x.." }, { "Driving 8ths", "X.x.x.x.x.x.x.x." } };
    static const std::vector<DrumIdea> snare {
        { "Backbeat", "....X.......X..." }, { "Backbeat with ghost", "....X..x....X..." }, { "Late push", "....X.......X.x." },
        { "Half time", "........X......." }, { "Break", "....X..x.x..X..." }, { "Roll into the bar", "....X.......Xxxx" } };
    static const std::vector<DrumIdea> hat {
        { "Off-beat", "..x...x...x...x." }, { "Straight 8ths", "x.x.x.x.x.x.x.x." }, { "16ths with accents", "xxXxxxXxxxXxxxXx" },
        { "Shuffle feel", "x.xxx.xxx.xxx.xx" }, { "Off-beat with skip", "..x...x..xx...x." }, { "Gallop", "x.xx.xx.x.xx.xx." } };
    static const std::vector<DrumIdea> perc {
        { "Three against four", "x..x..x..x..x..x" }, { "Clave", "x..x..x...x.x..." }, { "Answer", "......x.......xx" },
        { "Tumble", "x.x..x..x.x..x.." }, { "Five in a bar", "x..x..x..x.x...." }, { "Sparse", "...x......x....." } };
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
        { "Acid line", "xxx.xx.xxx.xx.xx", { 0, 0, 7, 0, 0, 2, 0, 0, 0, 7, 0, 4, 2, 0, 0, 7 } } };
    static const std::vector<NoteIdea> lead {
        { "Arp up", "x.xx.xx.x.xx.xx.", { 0, 0, 2, 4, 4, 7, 4, 2, 0, 0, 2, 4, 4, 7, 9, 7 } },
        { "Up and down", "xxxxxxxxxxxxxxxx", { 0, 2, 4, 7, 9, 7, 4, 2, 0, 2, 4, 7, 9, 7, 4, 2 } },
        { "Call and answer", "x..x..x.....x.x.", { 4, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 7, 0 } },
        { "Stabs", "x.....x...x.....", { 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 0, 0 } },
        { "Trance gate", "x.xxx.xxx.xxx.xx", { 0, 0, 0, 0, 2, 0, 2, 2, 4, 0, 4, 4, 2, 0, 2, 2 } },
        { "Falling", "x.x.x.x.x.x.x.x.", { 9, 0, 7, 0, 4, 0, 2, 0, 7, 0, 4, 0, 2, 0, 0, 0 } } };
    return boardType == "bass" ? bass : lead;
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

// A whole pattern: bar 1 sets the idea, the other bars repeat it with small changes,
// and bars 4 and 8 end with a fill, so a longer pattern goes somewhere.
inline void randomDrum (const std::string& boardType, uint32_t seed, Module& m)
{
    Rand r (seed);
    char base[17] = "................";
    randomDrumBar (boardType, r, base);
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
inline void randomNotes (const std::string& boardType, uint32_t seed, Module& m)
{
    Rand r (seed);
    char g[17] = "................";
    int d[16] {};
    randomNotesBar (boardType, r, g, d);
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
        { "sample", "Sample", 3 }, { "empty", "Empty board", 0 } };
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
} // namespace n2m
