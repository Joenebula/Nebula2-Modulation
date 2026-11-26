#pragma once
// Nebula2 Chord: the music engine. Plain C++17, no JUCE, so it can be tested on its own.
// A straight port of the Chord Bloom web app's generator and voicing, so the same seed
// gives the same progression in both. Every chord is built only from notes of the scale.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace n2c
{
inline int mod12 (int n) { return ((n % 12) + 12) % 12; }

// ---- Random numbers: mulberry32, bit for bit the same as the web app ----
struct Rng
{
    explicit Rng (uint32_t seed) : a (seed) {}
    double operator()()
    {
        a += 0x6D2B79F5u;
        uint32_t t = (a ^ (a >> 15)) * (1u | a);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return (double) (t ^ (t >> 14)) / 4294967296.0;
    }
    uint32_t a;
};

// Weighted pick, returns an index (-1 if empty)
template <typename R>
inline int pick (R& rng, const std::vector<double>& w)
{
    const int n = (int) w.size();
    if (n == 0) return -1;
    double tot = 0;
    for (double x : w) tot += x;
    if (tot <= 0) return (int) std::floor (rng() * n);
    double r = rng() * tot;
    for (int i = 0; i < n; ++i) { r -= w[(size_t) i]; if (r <= 0) return i; }
    return n - 1;
}

// ---- Music data ----
inline const char* const FLAT[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
inline const char* const SHARP[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

struct Scale { const char* id; const char* name; std::array<int, 7> iv; };
inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s {
        { "aeolian",   "Natural minor",       { 0, 2, 3, 5, 7, 8, 10 } },
        { "dorian",    "Dorian",              { 0, 2, 3, 5, 7, 9, 10 } },
        { "phrygian",  "Phrygian",            { 0, 1, 3, 5, 7, 8, 10 } },
        { "harmonic",  "Harmonic minor",      { 0, 2, 3, 5, 7, 8, 11 } },
        { "phrydom",   "Phrygian dominant",   { 0, 1, 4, 5, 7, 8, 10 } },
        { "hungarian", "Hungarian minor",     { 0, 2, 3, 6, 7, 8, 11 } },
        { "major",     "Major",               { 0, 2, 4, 5, 7, 9, 11 } },
        { "lydian",    "Lydian",              { 0, 2, 4, 6, 7, 9, 11 } } };
    return s;
}
inline int scaleIndex (const std::string& id)
{
    const auto& s = scales();
    for (size_t i = 0; i < s.size(); ++i) if (id == s[i].id) return (int) i;
    return 0;
}
// Short word for the key field: "D minor", "D dorian" ...
inline std::string scaleShort (const std::string& id)
{
    if (id == "aeolian") return "minor";
    if (id == "major") return "major";
    if (id == "harmonic") return "harm. minor";
    if (id == "phrydom") return "phryg. dom";
    if (id == "hungarian") return "hungarian";
    return id;
}

// Chord shapes. A shape is only offered on a degree if every note is in the scale.
struct Tpl { std::string s; char q; std::vector<int> iv; int lv; double w = 1.0; bool rich = false; };
inline const std::vector<Tpl>& shapes()
{
    static const std::vector<Tpl> t {
        { "m", 'm', { 0, 3, 7 }, 0 }, { "m(add9)", 'm', { 0, 3, 7, 14 }, 1 }, { "m7", 'm', { 0, 3, 7, 10 }, 1 }, { "m9", 'm', { 0, 3, 7, 10, 14 }, 2 },
        { "m11", 'm', { 0, 3, 7, 10, 14, 17 }, 2 }, { "m6/9", 'm', { 0, 3, 7, 9, 14 }, 3 }, { "m(maj7)", 'm', { 0, 3, 7, 11 }, 3 }, { "m(maj9)", 'm', { 0, 3, 7, 11, 14 }, 3 },
        { "", 'M', { 0, 4, 7 }, 0 }, { "add9", 'M', { 0, 4, 7, 14 }, 1 }, { "maj7", 'M', { 0, 4, 7, 11 }, 1 }, { "maj9", 'M', { 0, 4, 7, 11, 14 }, 2 },
        { "6/9", 'M', { 0, 4, 7, 9, 14 }, 2 }, { "7", 'M', { 0, 4, 7, 10 }, 1, 0.15 }, { "9", 'M', { 0, 4, 7, 10, 14 }, 2, 0.15 },
        { "maj7(#11)", 'M', { 0, 4, 7, 11, 18 }, 3 }, { "maj9(#11)", 'M', { 0, 4, 7, 11, 14, 18 }, 3 }, { "7(b9)", 'M', { 0, 4, 7, 10, 13 }, 3 },
        { "dim", 'd', { 0, 3, 6 }, 0 }, { "m7b5", 'd', { 0, 3, 6, 10 }, 1 }, { "dim7", 'd', { 0, 3, 6, 9 }, 2 },
        { "aug", 'a', { 0, 4, 8 }, 0, 0.5 }, { "maj7(#5)", 'a', { 0, 4, 8, 11 }, 2, 0.5 },
        { "sus2", 's', { 0, 2, 7 }, 1 }, { "sus4", 's', { 0, 5, 7 }, 1 }, { "7sus4", 's', { 0, 5, 7, 10 }, 2 },
        // Detroit colours: only offered by moods marked rich (UR, Dark techno)
        { "m13", 'm', { 0, 3, 7, 10, 14, 17, 21 }, 3, 1.0, true }, { "m11(no5)", 'm', { 0, 3, 10, 14, 17 }, 3, 1.0, true },
        { "maj13", 'M', { 0, 4, 7, 11, 14, 21 }, 3, 1.0, true }, { "maj9(13)", 'M', { 0, 4, 11, 14, 21 }, 3, 1.0, true },
        { "13", 'M', { 0, 4, 10, 14, 21 }, 3, 0.4, true },
        { "9sus4", 's', { 0, 5, 7, 10, 14 }, 3, 1.0, true }, { "13sus4", 's', { 0, 5, 10, 14, 21 }, 3, 1.0, true },
        { "(quartal)", 's', { 0, 5, 10, 15, 19 }, 3, 1.0, true } };
    return t;
}

// Moods are starting presets: scale(s), key, how lush, which progression shapes to draw on
struct Mood
{
    std::string id, name, scale;
    std::vector<std::string> scaleChoices;
    int tonic;
    double lush;
    std::vector<std::string> templ;
    std::vector<int> fav;
    std::string pattern;             // empty = leave the pattern alone
    bool rich = false;
    std::set<std::string> avoid;
    std::map<std::string, double> prefer;
};
inline const std::vector<Mood>& moods()
{
    static const std::vector<Mood> m {
        { "deep", "Deep emotive", "aeolian", { "aeolian", "dorian" }, 2, 0.85, { "emotive" }, { 0, 5, 2, 6 }, "", false, {}, {} },
        { "sad", "Sad", "aeolian", { "aeolian" }, 2, 0.3, { "sad" }, { 0, 3, 4, 5 }, "", false, {}, {} },
        { "falling", "Falling minor", "aeolian", { "aeolian" }, 3, 0.75, { "falling", "emotive" }, { 0, 6, 5, 3 }, "", false, {}, {} },
        { "glass", "Glass lift", "phrydom", { "phrydom" }, 0, 0.8, { "phrydom" }, { 0, 1, 6, 3 }, "", false, {}, {} },
        { "dark", "Dark minor", "aeolian", { "aeolian", "phrygian" }, 9, 0.6, { "emotive" }, { 0, 5, 6 }, "", false, {}, {} },
        { "eerie", "Eerie", "phrygian", { "phrygian" }, 4, 0.65, { "phrygian", "emotive" }, { 0, 1, 6 }, "", false, {}, {} },
        { "emotional", "Warm minor", "dorian", { "dorian", "aeolian" }, 2, 0.75, { "dorian", "emotive" }, { 0, 3, 6 }, "", false, {}, {} },
        { "dramatic", "Dramatic", "harmonic", { "harmonic" }, 7, 0.55, { "harmonic", "emotive" }, { 0, 4, 5 }, "", false, {}, {} },
        { "strange", "Strange", "hungarian", { "hungarian", "phrydom", "harmonic" }, 9, 0.6, { "emotive", "harmonic", "phrydom" }, { 0, 5, 4 }, "", false, {}, {} },
        { "rave", "Rave stab", "aeolian", { "aeolian" }, 5, 0.2, { "rave", "emotive" }, { 0, 5, 6, 4 }, "rave", false, {}, {} },
        { "techno", "Dark techno", "phrygian", { "phrygian", "aeolian" }, 9, 0.8, { "techno" }, { 0, 3, 6 }, "hold", true,
          { "6/9", "13", "maj9(13)", "maj13", "add9", "7", "9", "m6/9", "maj7", "maj9", "maj7(#11)", "maj9(#11)", "", "13sus4" },
          { { "m7", 1.2 }, { "m9", 1.4 }, { "m11", 1.5 }, { "m11(no5)", 1.5 }, { "m13", 0.6 }, { "m(add9)", 0.5 }, { "(quartal)", 0.7 },
            { "7sus4", 0.3 }, { "9sus4", 0.45 }, { "sus2", 0.25 }, { "sus4", 0.25 }, { "m7b5", 0.5 } } },
        { "ur", "UR (Detroit lush)", "major", { "major", "dorian", "lydian" }, 2, 1.0, { "ur" }, { 0, 2, 4, 1 }, "", true, {}, {} } };
    return m;
}
inline int moodIndex (const std::string& id)
{
    const auto& m = moods();
    for (size_t i = 0; i < m.size(); ++i) if (id == m[i].id) return (int) i;
    return 0;
}

// Tried-and-tested progression shapes, as scale degrees (0 = home chord, 5 = VI, 6 = VII ...)
inline const std::vector<std::vector<int>>& templates (const std::string& k)
{
    static const std::map<std::string, std::vector<std::vector<int>>> t {
        { "emotive", { { 0, 5, 2, 6 }, { 0, 6, 5, 6 }, { 0, 3, 5, 6 }, { 0, 5, 3, 6 }, { 0, 2, 6, 5 }, { 5, 2, 6, 0 }, { 3, 5, 0, 6 }, { 0, 4, 5, 3 }, { 0, 6, 3, 5 }, { 0, 5, 2, 4 },
                       { 0, 2, 3, 5 }, { 0, 3, 6, 2 }, { 0, 6, 5, 4 }, { 5, 6, 0, 2 }, { 0, 5, 6, 3 }, { 0, 2, 5, 6 }, { 3, 0, 5, 6 }, { 0, 5, 6, 0 }, { 2, 6, 0, 5 }, { 0, 3, 5, 2 } } },
        { "sad", { { 0, 3, 5, 4 }, { 0, 4, 5, 3 }, { 5, 3, 0, 4 }, { 0, 3, 4, 0 }, { 0, 5, 3, 4 }, { 3, 0, 4, 5 }, { 0, 4, 3, 5 }, { 0, 3, 0, 4 }, { 5, 4, 3, 0 }, { 0, 2, 3, 4 }, { 0, 5, 4, 3 }, { 3, 4, 5, 0 } } },
        { "falling", { { 0, 6, 5, 3 }, { 0, 6, 5, 4 }, { 0, 6, 5, 6 }, { 0, 5, 6, 3 }, { 0, 6, 5, 2 } } },
        { "phrydom", { { 0, 1, 6, 3 }, { 0, 1, 6, 1 }, { 0, 3, 1, 6 }, { 0, 1, 3, 6 }, { 3, 1, 0, 6 }, { 0, 1, 0, 6 }, { 0, 6, 1, 3 } } },
        { "phrygian", { { 0, 1, 0, 6 }, { 0, 1, 6, 5 }, { 0, 6, 1, 0 }, { 0, 1, 5, 6 }, { 0, 3, 1, 0 }, { 0, 5, 1, 6 } } },
        { "dorian", { { 0, 3, 0, 6 }, { 0, 3, 6, 0 }, { 0, 6, 3, 0 }, { 0, 2, 3, 0 }, { 0, 3, 2, 6 }, { 0, 6, 2, 3 } } },
        { "harmonic", { { 0, 5, 3, 4 }, { 0, 3, 4, 5 }, { 0, 4, 5, 3 }, { 5, 3, 4, 0 }, { 0, 3, 5, 4 }, { 0, 5, 1, 4 } } },
        { "rave", { { 0, 5, 6, 4 }, { 0, 5, 6, 0 }, { 0, 6, 5, 6 }, { 0, 3, 5, 6 }, { 0, 5, 3, 4 }, { 0, 6, 5, 4 } } },
        { "techno", { { 0, 0, 3, 0 }, { 0, 6, 0, 0 }, { 0, 3, 6, 0 }, { 0, 0, 1, 0 }, { 0, 3, 0, 6 }, { 0, 0, 0, 3 }, { 0, 6, 3, 0 }, { 0, 0, 6, 0 }, { 0, 3, 0, 0 }, { 3, 0, 6, 0 }, { 0, 6, 0, 3 }, { 0, 1, 0, 6 } } },
        { "ur", { { 2, 0, 0, 4 }, { 2, 0, 4, 1 }, { 0, 0, 4, 1 }, { 0, 4, 1, 2 }, { 2, 1, 0, 4 }, { 0, 2, 1, 4 }, { 0, 4, 0, 1 }, { 2, 0, 4, 0 }, { 1, 2, 0, 4 }, { 0, 1, 2, 0 } } } };
    static const std::vector<std::vector<int>> none;
    auto it = t.find (k);
    return it == t.end() ? none : it->second;
}

// Patterns: 16 steps a bar, value = gate length in steps. Hold = sustained pad (no steps)
struct Pattern { const char* id; const char* name; std::vector<double> steps; };
inline const std::vector<Pattern>& patterns()
{
    static const std::vector<Pattern> p {
        { "hold", "Hold (pad)", {} },
        { "rave", "Rave stab", { 2, 0, 0, 2, 0, 0, 3, 0, 0, 0, 1, 0, 3, 0, 0, 0 } },
        { "pulse8", "8th pulse", { 1.5, 0, 1.5, 0, 1.5, 0, 1.5, 0, 1.5, 0, 1.5, 0, 1.5, 0, 1.5, 0 } },
        { "pulse16", "16th pulse", { .8, .8, .8, .8, .8, .8, .8, .8, .8, .8, .8, .8, .8, .8, .8, .8 } },
        { "offbeat", "Offbeat", { 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0 } },
        { "sync", "Syncopated", { 2, 0, 0, 2, 0, 0, 2, 0, 2, 0, 0, 2, 0, 0, 2, 0 } },
        { "two", "Two hits a bar", { 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0 } } };
    return p;
}

inline const int WHITE[7] = { 0, 2, 4, 5, 7, 9, 11 };
inline const int BLACK[5] = { 1, 3, 6, 8, 10 };
inline int blackDegree (int pc) { switch (pc) { case 1: return 0; case 3: return 5; case 6: return 6; case 8: return 3; case 10: return 4; default: return -1; } }

// ---- One chord in the progression ----
struct Chord
{
    int deg = 0;            // scale degree 0..6
    std::string t;          // colour: "m7", "maj9" ...
    int bars = 2;
    int tr = 0;             // key-change lift in semitones
    bool locked = false;
    std::string sec;        // section name at the start of a phrase, else empty
    int inv = 0;            // 0 root, 1 and 2 = inversions (bass note)
    bool intro = false;     // the glide intro before chord 1
    bool open = false;      // a key's sus version (keyboard only)
    bool operator== (const Chord& o) const { return deg == o.deg && t == o.t && bars == o.bars && tr == o.tr && inv == o.inv && intro == o.intro; }
};

struct Voicing
{
    int bass = 38;
    std::vector<int> upper;      // 8 voices
    std::vector<int> notes;      // bass, second, then upper: 10 voices
    std::map<int, int> hold;     // how many chords each high note has been held
};

constexpr int kVoices = 10;
inline const int HOME_UP[8] = { 53, 58, 62, 65, 69, 72, 76, 81 };

struct Entry { int deg, off; bool home; };

//==============================================================================
// Everything that depends on the current mood, scale and key
class Theory
{
public:
    std::string moodId = "deep", scaleId = "aeolian";
    int tonic = 2;

    const Mood& mood() const { return moods()[(size_t) moodIndex (moodId)]; }
    const std::array<int, 7>& sc() const { return scales()[(size_t) scaleIndex (scaleId)].iv; }

    bool inScale (int pc) const { for (int x : sc()) if (mod12 (tonic + x) == pc) return true; return false; }

    // Spell with whichever of flats or sharps gives seven different letters (flats if equal)
    const char* const* names() const
    {
        auto score = [&] (const char* const* arr) { std::set<char> s; for (int x : sc()) s.insert (arr[mod12 (tonic + x)][0]); return s.size(); };
        return score (SHARP) > score (FLAT) ? SHARP : FLAT;
    }

    int degRoot (int d) const { return sc()[(size_t) d]; }

    char degQuality (int d) const
    {
        const auto& s = sc();
        const int r = s[(size_t) d], th = mod12 (s[(size_t) ((d + 2) % 7)] - r), fi = mod12 (s[(size_t) ((d + 4) % 7)] - r);
        return th == 3 && fi == 7 ? 'm' : th == 4 && fi == 7 ? 'M' : th == 3 && fi == 6 ? 'd' : th == 4 && fi == 8 ? 'a' : 's';
    }

    std::vector<Tpl> colours (int d) const
    {
        const char q = degQuality (d);
        const int r = tonic + degRoot (d);
        const bool rich = mood().rich;
        auto ok = [&] (const Tpl& t) { if (t.rich && ! rich) return false; for (int x : t.iv) if (! inScale (mod12 (r + x))) return false; return true; };
        std::vector<Tpl> main, sus;
        for (auto& t : shapes()) { if (t.q == q && ok (t)) main.push_back (t); }
        for (auto& t : shapes()) { if (t.q == 's' && ok (t)) sus.push_back (t); }
        if (! main.empty()) { main.insert (main.end(), sus.begin(), sus.end()); return main; }
        if (! sus.empty()) return sus;
        // Odd degree: stack the scale's own notes so it still can't leave the key
        const auto& s = sc();
        const int th = mod12 (s[(size_t) ((d + 2) % 7)] - s[(size_t) d]), fi = mod12 (s[(size_t) ((d + 4) % 7)] - s[(size_t) d]);
        std::string nm = std::string (th == 3 ? "m" : th == 4 ? "" : th == 2 ? "sus2" : th == 5 ? "sus4" : "") + (fi == 6 ? "(b5)" : fi == 8 ? "(#5)" : "");
        Tpl t { nm.empty() ? "(scale)" : nm, 'x', (fi == 7 || fi == 6 || fi == 8) ? std::vector<int> { 0, th, fi } : std::vector<int> { 0, th }, 0 };
        return { t };
    }

    Tpl colourOf (const Chord& c) const
    {
        const auto cols = colours (c.deg);
        for (auto& t : cols) if (t.s == c.t) return t;
        for (auto& t : cols) if (t.lv == 0) return t;
        return cols[0];
    }

    double degWeight (int d, double strange) const
    {
        static const double DEG_W[7] = { 1.3, .5, .8, 1.0, .8, 1.1, 1.1 };
        const char q = degQuality (d);
        double w = DEG_W[d];
        if (q == 'd' || q == 'a' || q == 's') w *= .1 + strange * .45;
        const auto& fav = mood().fav;
        if (std::find (fav.begin(), fav.end(), d) != fav.end()) w *= 1.4;
        if (w < .6) w = w * (.4 + strange * 1.2);
        return w;
    }
    bool isRare (int d) const { return degWeight (d, 0) < .6; }

    std::vector<Entry> palette() const { std::vector<Entry> p; for (int d = 0; d < 7; ++d) p.push_back ({ d, degRoot (d), d == 0 }); return p; }

    int rootPc (const Chord& c) const { return mod12 (tonic + degRoot (c.deg) + c.tr); }
    int bassPc (const Chord& c) const
    {
        const auto col = colourOf (c);
        const int r = rootPc (c);
        return c.inv && c.inv < (int) col.iv.size() ? mod12 (r + col.iv[(size_t) c.inv]) : r;
    }
    std::string chordName (const Chord& c) const
    {
        const int b = bassPc (c), r = rootPc (c);
        const auto sx = colourOf (c).s;
        auto n = names();
        return std::string (n[r]) + sx + (b != r ? (sx.find ('/') != std::string::npos ? " / " : "/") + std::string (n[b]) : "");
    }
    std::string keyName() const { return std::string (names()[mod12 (tonic)]) + " " + scaleShort (scaleId); }

    struct Pcs { int root, bass; std::vector<int> pcs, req; };
    Pcs chordPcs (const Chord& c) const
    {
        const auto col = colourOf (c);
        const int r = rootPc (c);
        std::vector<int> pcs;
        for (int x : col.iv) { const int p = mod12 (r + x); if (std::find (pcs.begin(), pcs.end(), p) == pcs.end()) pcs.push_back (p); }
        std::vector<int> req;
        for (int p : pcs) if (! (pcs.size() > 4 && p == mod12 (r + 7))) req.push_back (p);   // the 5th can drop out of big chords
        return { r, bassPc (c), pcs, req };
    }

    // ---- Generator ----
    static double moveW (const Entry& a, const Entry& b)
    {
        static const double MOVE[12] = { .12, 1.8, 1.3, 1.1, 1.0, 1.5, .45, 1.0, 1.6, 1.3, 2.3, 1.4 };
        const int d = mod12 (b.off - a.off);
        return d == 0 ? 0 : MOVE[d];
    }

    template <typename R>
    std::string pickColour (R& rng, int deg, double lush, double strange) const
    {
        const auto cols = colours (deg);
        const auto& m = mood();
        std::vector<double> w;
        if (m.rich)   // never plain: 9ths, 11ths, 13ths, sus and stacked fourths
        {
            bool any = false;
            for (auto& c : cols)
            {
                double x;
                if (m.avoid.count (c.s)) x = 0;
                else if (! m.prefer.empty() && c.lv >= 1 && m.prefer.count (c.s)) x = m.prefer.at (c.s);
                else if (c.lv < 2) x = 0;
                else x = (m.prefer.empty() ? 1.0 : 0.3) * (c.rich ? 1.4 * c.w : c.q == 's' ? .6 : c.lv == 3 ? 1.0 : .7 * c.w);
                w.push_back (x);
                any = any || x > 0;
            }
            if (any) return cols[(size_t) pick (rng, w)].s;
            w.clear();
        }
        for (auto& c : cols)
        {
            if (c.q == 's') { w.push_back (cols.size() == 1 ? 1.0 : strange * strange * .3); continue; }
            const bool dom = c.s == "7" || c.s == "9";
            const double base = dom ? c.w * (.3 + strange * 2) : c.w;
            const double lv = c.lv == 0 ? (1 - lush) * 1.1 + .05 : c.lv == 1 ? .6 + lush * .5 : c.lv == 2 ? lush * 1.3 : strange * strange * 1.1;
            w.push_back (base * lv);
        }
        return cols[(size_t) pick (rng, w)].s;
    }

    struct Opt { int start = -1; bool avoidHome = false; int endTo = -1; double strange = 0; };

    template <typename R>
    std::vector<int> genWalk (R& rng, int n, const Opt& o) const
    {
        const auto pal = palette();
        const double st = o.strange;
        struct Cand { std::vector<int> seq; double score; };
        std::vector<Cand> cands;
        for (int k = 0; k < 260; ++k)
        {
            std::vector<int> seq;
            if (o.start >= 0) seq.push_back (o.start);
            else
            {
                std::vector<int> pool; std::vector<double> w;
                for (auto& e : pal) if (! (o.avoidHome && e.home)) { pool.push_back (e.deg); w.push_back (degWeight (e.deg, st)); }
                seq.push_back (pool[(size_t) pick (rng, w)]);
            }
            double score = std::log (degWeight (seq[0], st));
            for (int j = 1; j < n; ++j)
            {
                const int cur = seq[(size_t) j - 1];
                std::vector<double> w;
                for (auto& e : pal)
                {
                    double x = degWeight (e.deg, st) * moveW (pal[(size_t) cur], e);
                    const auto c = std::count (seq.begin(), seq.end(), e.deg);
                    if (c) x *= c >= 2 ? .03 : .25;
                    if (j == n - 1 && o.endTo >= 0) x *= moveW (e, pal[(size_t) o.endTo]) * .8 + .2;
                    w.push_back (x);
                }
                const int nx = pal[(size_t) pick (rng, w)].deg;
                seq.push_back (nx);
                score += std::log (std::max (1e-4, moveW (pal[(size_t) cur], pal[(size_t) nx]) * degWeight (nx, st)));
            }
            const int target = o.endTo >= 0 ? o.endTo : seq[0];
            double mw = moveW (pal[(size_t) seq[(size_t) n - 1]], pal[(size_t) target]);
            if (mw == 0) mw = .05;
            score += 1.5 * std::log (std::max (1e-4, mw));
            if (std::find (seq.begin(), seq.end(), 0) != seq.end()) score += .25;
            if (n >= 4 && (int) std::set<int> (seq.begin(), seq.end()).size() < std::min (3, n)) score -= 3;
            cands.push_back ({ seq, score });
        }
        std::stable_sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.score > b.score; });
        const size_t topN = std::min<size_t> (30, cands.size());
        const double temp = .35 + st * 1.2, best = cands[0].score;
        std::vector<double> w;
        for (size_t i = 0; i < topN; ++i) w.push_back (std::exp ((cands[i].score - best) / temp));
        return cands[(size_t) pick (rng, w)].seq;
    }

    std::vector<std::vector<int>> shapesFor (double st) const
    {
        auto ok = [&] (const std::vector<int>& t) { for (int d : t) { const char q = degQuality (d); if (! (q == 'm' || q == 'M' || (st > .6 && q != 'x'))) return false; } return true; };
        std::vector<std::vector<int>> out;
        for (auto& k : mood().templ) for (auto& t : templates (k)) if (ok (t)) out.push_back (t);
        return out;
    }

    template <typename R>
    std::vector<int> genPhrase (R& rng, int n, const Opt& o) const
    {
        const double st = o.strange;
        const auto pal = palette();
        auto pool = shapesFor (st);
        if (pool.empty() || rng() < st * st * .5) return genWalk (rng, n, o);   // strangeness lets it wander (still in scale)
        if (o.avoidHome)
        {
            std::vector<std::vector<int>> rot;
            for (auto& t : pool)
                for (int r = 1; r < 4; ++r)
                {
                    std::vector<int> x (t.begin() + r, t.end());
                    x.insert (x.end(), t.begin(), t.begin() + r);
                    if (x[0] != 0) rot.push_back (x);
                }
            pool = rot;
        }
        else if (o.start >= 0)
        {
            std::vector<std::vector<int>> f;
            for (auto& t : pool) if (t[0] == o.start) f.push_back (t);
            pool = f;
        }
        if (pool.empty()) return genWalk (rng, n, o);
        const auto& fav = mood().fav;
        std::vector<double> w;
        for (auto& t : pool)
        {
            double x = 1;
            for (int d : t) if (std::find (fav.begin(), fav.end(), d) != fav.end()) x *= 1.15;
            if (o.endTo >= 0) x *= moveW (pal[(size_t) t.back()], pal[(size_t) o.endTo]) + .2;
            w.push_back (x);
        }
        const auto t = pool[(size_t) pick (rng, w)];
        std::vector<int> degs;
        if (n == 4) degs = t;
        else if (n == 8)
        {
            std::vector<double> w2;
            for (auto& u : pool)
                w2.push_back (u == t ? .15 : (moveW (pal[(size_t) t[3]], pal[(size_t) u[0]]) + .1) * (moveW (pal[(size_t) u[3]], pal[(size_t) t[0]]) + .1));
            degs = t;
            const auto& u = pool[(size_t) pick (rng, w2)];
            degs.insert (degs.end(), u.begin(), u.end());
        }
        else if (n == 2) degs = { t[0], t[(size_t) (1 + (int) std::floor (rng() * 2))] };
        else for (int i = 0; i < n; ++i) degs.push_back (t[(size_t) (i % 4)]);
        return degs;
    }

    template <typename R>
    std::vector<Chord> dress (R& rng, const std::vector<int>& phrase, double lush, double strange) const
    {
        std::vector<Chord> out;
        for (int d : phrase) { Chord c; c.deg = d; c.t = pickColour (rng, d, lush, strange); out.push_back (c); }
        return out;
    }

    template <typename R>
    std::vector<Chord> vary (R& rng, const std::vector<Chord>& ph, double level, double variation, double strange) const
    {
        const auto pal = palette();
        std::vector<Chord> out;
        for (size_t i = 0; i < ph.size(); ++i)
        {
            const auto& c = ph[i];
            Chord x = c;
            if (i > 0 && rng() < variation * .08 * level)
            {
                const auto& prev = ph[i - 1];
                const auto& next = ph[(i + 1) % ph.size()];
                std::vector<int> alts; std::vector<double> w;
                for (auto& e : pal)
                    if (e.deg != c.deg && e.deg != prev.deg && e.deg != next.deg)
                    {
                        alts.push_back (e.deg);
                        w.push_back (degWeight (e.deg, strange) * moveW (pal[(size_t) prev.deg], e) * moveW (e, pal[(size_t) next.deg]));
                    }
                const int k = pick (rng, w);
                if (k >= 0) { Chord y; y.deg = alts[(size_t) k]; y.t = c.t; x = y; }
            }
            const auto cols = colours (x.deg);
            Tpl cur = cols[0];
            for (auto& t : cols) if (t.s == x.t) { cur = t; break; }
            if (rng() < .25 + variation * .35 * level)
            {
                const int maxLv = std::min (3, cur.lv + 1 + (strange > .6 ? 1 : 0));
                std::vector<const Tpl*> up;
                for (auto& t : cols)
                    if (t.q == cur.q && t.lv > cur.lv && t.lv <= maxLv && (t.lv < 3 || rng() < strange)) up.push_back (&t);
                if (! up.empty()) x.t = up[(size_t) std::floor (rng() * (double) up.size())]->s;
            }
            bool has = false;
            for (auto& t : cols) if (t.s == x.t) has = true;
            if (! has)
            {
                const Tpl* f = &cols[0];
                for (auto& t : cols) if (t.lv == cur.lv) { f = &t; break; }
                x.t = f->s;
            }
            if (mood().avoid.count (x.t)) x.t = pickColour (rng, x.deg, mood().lush, strange);   // keep the mood's banned colours out of variations
            out.push_back (x);
        }
        return out;
    }

    static Chord introOf (const Chord& c) { Chord i; i.deg = c.deg; i.t = c.t; i.bars = 1; i.sec = "Glide"; i.intro = true; return i; }
    int glideFrom (const Chord& c) const { return 48 + mod12 (rootPc (c) - 48); }

    struct GenSettings { int lenBars = 32, chordBars = 2, strange = 35, variation = 50, lift = 0; };

    std::vector<Chord> generate (uint32_t seed, const GenSettings& g) const
    {
        Rng rng (seed);
        const double st = g.strange / 100.0, va = g.variation / 100.0, lush = mood().lush;
        const int per = std::max (1, (int) std::lround (8.0 / g.chordBars));
        const int phrases = std::max (1, g.lenBars / 8);
        Opt oa; oa.start = 0; oa.strange = st;
        const auto A = dress (rng, genPhrase (rng, per, oa), lush, st);
        Opt ob; ob.avoidHome = true; ob.endTo = 0; ob.strange = std::min (1.0, st + .1);
        const auto B = dress (rng, genPhrase (rng, per, ob), lush, st);
        Opt oc; oc.avoidHome = true; oc.endTo = B[0].deg; oc.strange = std::min (1.0, st + .3);
        const auto C = dress (rng, genPhrase (rng, per, oc), std::min (1.0, lush + .15), st);
        std::map<std::string, std::vector<Chord>> lib;
        lib["A"] = A;
        lib["A2"] = vary (rng, A, 1, va, st);
        lib["A3"] = vary (rng, A, 2, va, st);
        lib["A4"] = vary (rng, A, 3, va, st);
        lib["B"] = B;
        lib["B2"] = vary (rng, B, 1.5, va, st);
        lib["C"] = C;
        lib["C2"] = vary (rng, C, 1.5, va, st);
        lib["C3"] = vary (rng, C, 2.5, va, st);

        static const std::map<int, std::vector<std::string>> FORMS {
            { 1, { "A" } }, { 2, { "A", "A2" } }, { 4, { "A", "A2", "B", "A3" } }, { 8, { "A", "A2", "B", "A3", "C", "C2", "B2", "A4" } },
            { 16, { "A", "A2", "B", "A3", "A", "A2", "B2", "A3", "C", "C2", "C3", "B2", "A2", "A3", "B", "A4" } } };
        static const std::map<std::string, std::string> SECNAME {
            { "A", "A" }, { "A2", "A'" }, { "A3", "A''" }, { "A4", "A'''" }, { "B", "B" }, { "B2", "B'" }, { "C", "Break" }, { "C2", "Break'" }, { "C3", "Break''" } };
        auto fit = FORMS.find (phrases);
        const auto& form = fit != FORMS.end() ? fit->second : FORMS.at (4);
        const double liftFrom = g.lift > 0 && phrases >= 2 ? (phrases >= 4 ? phrases - std::max (1, phrases / 4) : phrases - 1)
                                                           : std::numeric_limits<double>::infinity();
        std::vector<Chord> out;
        for (size_t pi = 0; pi < form.size(); ++pi)
        {
            const int tr = (double) pi >= liftFrom ? g.lift : 0;
            const auto& ph = lib[form[pi]];
            for (size_t ci = 0; ci < ph.size(); ++ci)
            {
                Chord c; c.deg = ph[ci].deg; c.t = ph[ci].t; c.bars = g.chordBars; c.tr = tr;
                if (ci == 0) c.sec = SECNAME.at (form[pi]) + (tr ? " \xe2\x86\x91" : "");
                out.push_back (c);
            }
        }
        // Let the bass walk by step where an inversion allows it (C/E, Ab/C ...)
        int prevB = -1;
        for (auto& c : out)
        {
            if (prevB >= 0 && ! (c.deg == 0 && ! c.sec.empty()) && rng() < .18 + va * .35)
            {
                const auto col = colourOf (c);
                const int r = rootPc (c);
                if (col.q == 'm' || col.q == 'M')
                    for (int inv : { 1, 2 })
                    {
                        const int pc = mod12 (r + col.iv[(size_t) inv]);
                        const int d = std::min (mod12 (pc - prevB), mod12 (prevB - pc));
                        if (d > 0 && d <= 2) { c.inv = inv; break; }
                    }
            }
            prevB = bassPc (c);
        }
        if (! out.empty()) out.insert (out.begin(), introOf (out[0]));   // the glide intro is always its own part
        return out;
    }

    // ---- Voicing: 10 voices, each moving the shortest way to the next chord ----
    static std::vector<int> leadUpper (const std::vector<int>& prev, const std::vector<int>& pcs, const std::vector<int>& req,
                                       const std::vector<int>& fixed, const std::map<int, int>& hold)
    {
        const int N = (int) prev.size();
        const int prevTop = *std::max_element (prev.begin(), prev.end());
        auto hasPc = [&] (int q) { return std::find (pcs.begin(), pcs.end(), mod12 (q)) != pcs.end(); };
        std::vector<std::vector<int>> cand;
        for (int p : prev)
        {
            std::vector<int> a;
            const int lo = p >= 70 ? -12 : -7, hi = p >= 70 ? 12 : 7;
            for (int d = lo; d <= hi; ++d) { const int q = p + d; if (q < 50 || q > 88) continue; if (hasPc (q)) a.push_back (q); }
            if (a.empty()) for (int d = -12; d <= 12; ++d) if (hasPc (p + d)) a.push_back (p + d);
            cand.push_back (a);
        }
        auto cost = [&] (const std::vector<int>& v)
        {
            double c = 0;
            for (int i = 0; i < N; ++i) c += std::abs (v[(size_t) i] - prev[(size_t) i]) * .9 + .12 * std::abs (v[(size_t) i] - HOME_UP[i]);
            std::vector<int> all = v;
            all.insert (all.end(), fixed.begin(), fixed.end());
            std::sort (all.begin(), all.end());
            for (size_t i = 1; i < all.size(); ++i)
            {
                const int gap = all[i] - all[i - 1], lo = all[i - 1];
                if (gap == 0) c += lo >= 72 ? 3.5 : 1.2;            // exact doubles thin the sound; up high they ring out
                else if (gap == 1) c += lo < 72 ? 9 : 5;            // semitone rubs sound out of tune
                else if (gap == 2) c += lo < 60 ? 4 : lo < 67 ? 1.2 : .3;
                else if (lo < 55 && gap < 5) c += 2.5;              // keep the low end clear
            }
            for (int pc : req) { bool f = false; for (int q : v) if (mod12 (q) == pc) f = true; if (! f) c += 12; }
            const int top = *std::max_element (v.begin(), v.end()), dt = std::abs (top - prevTop);
            if (dt == 0) c += 5; else if (dt <= 4) c -= .6; else c += (dt - 4) * 1.2;   // the top note moves with the chords
            if (top > 83) c += (top - 83) * 2;
            if (top < 72) c += (72 - top) * 1.5;
            for (int q : v)                                         // high notes shouldn't sit there chord after chord
                if (q >= 72 && std::find (prev.begin(), prev.end(), q) != prev.end())
                {
                    auto it = hold.find (q);
                    c += 3.0 * (1 + (it != hold.end() ? it->second : 0));
                }
            return c;
        };
        std::vector<int> cur;
        for (int i = 0; i < N; ++i)
        {
            const int p = prev[(size_t) i];
            int b = cand[(size_t) i][0];
            for (int q : cand[(size_t) i]) if (std::abs (q - p) < std::abs (b - p)) b = q;
            cur.push_back (b);
        }
        double best = cost (cur);
        for (int pass = 0; pass < 8; ++pass)
        {
            bool improved = false;
            for (int i = 0; i < N; ++i)
                for (int q : cand[(size_t) i])
                {
                    if (q == cur[(size_t) i]) continue;
                    auto t = cur; t[(size_t) i] = q;
                    const double c = cost (t);
                    if (c < best - 1e-9) { best = c; cur = t; improved = true; }
                }
            if (! improved) break;
        }
        std::sort (cur.begin(), cur.end());
        return cur;
    }

    Voicing voiceOne (const Chord& c, const Voicing* prevV) const
    {
        const auto p = chordPcs (c);
        const int pb = prevV ? prevV->bass : 38;
        int bass = 31 + mod12 (p.bass - 31);
        if (std::abs (bass + 12 - pb) < std::abs (bass - pb) && bass + 12 <= 43) bass += 12;
        const int second = p.bass == p.root ? bass + 12 : bass + mod12 (p.root - bass) + (mod12 (p.root - bass) < 5 ? 12 : 0);   // root above an inverted bass
        static const std::map<int, int> noHold;
        const auto& ph = prevV ? prevV->hold : noHold;
        std::vector<int> prevUp = prevV ? prevV->upper : std::vector<int> (HOME_UP, HOME_UP + 8);
        Voicing v;
        v.bass = bass;
        v.upper = leadUpper (prevUp, p.pcs, p.req, { second }, ph);
        for (int q : v.upper)
        {
            const bool held = prevV && std::find (prevV->upper.begin(), prevV->upper.end(), q) != prevV->upper.end();
            auto it = ph.find (q);
            v.hold[q] = held ? (it != ph.end() ? it->second : 0) + 1 : 0;
        }
        v.notes = { bass, second };
        v.notes.insert (v.notes.end(), v.upper.begin(), v.upper.end());
        return v;
    }

    // Chord 1 sounds exactly like the chord the glide lands on
    std::vector<Voicing> voiceAll (const std::vector<Chord>& seq) const
    {
        std::vector<Voicing> out;
        for (size_t i = 0; i < seq.size(); ++i)
        {
            if (i == 1 && seq[0].intro) { Voicing v = out.back(); v.hold.clear(); out.push_back (v); continue; }
            out.push_back (voiceOne (seq[i], out.empty() ? nullptr : &out.back()));
        }
        return out;
    }

    // ---- Keys: the chord each key plays ----
    // keyColour 0 plain, 1 lush, 2 very lush, 3 strange. realLayout: each key plays the chord on its own note.
    bool tileChord (int pc, bool realLayout, int keyColour, Chord& out) const
    {
        auto bestUpTo = [&] (int d)
        {
            const auto all = colours (d);
            std::vector<Tpl> cols;
            bool anyMain = false;
            for (auto& t : all) if (t.q != 's') anyMain = true;
            for (auto& t : all) if (! anyMain || t.q != 's') cols.push_back (t);
            const int cap = (cols[0].q == 'd' || cols[0].q == 'a') ? 0 : keyColour;
            const auto& avoid = mood().avoid;   // a mood's banned colours stay off the keys too (Dark techno)
            const Tpl* best = nullptr;
            for (auto& t : cols) if (t.lv <= cap && ! avoid.count (t.s) && (! best || t.lv > best->lv)) best = &t;
            if (! best) for (auto& t : cols) if (t.lv <= cap && (! best || t.lv > best->lv)) best = &t;
            return best ? best->s : cols[0].s;
        };
        out = Chord();
        if (realLayout)
        {
            int d = -1;
            for (int i = 0; i < 7; ++i) if (sc()[(size_t) i] == mod12 (pc - tonic)) d = i;
            if (d < 0) return false;
            out.deg = d; out.t = bestUpTo (d);
            return true;
        }
        for (int i = 0; i < 7; ++i)
            if (WHITE[i] == pc) { out.deg = i; out.t = bestUpTo (i); return true; }
        const int d = blackDegree (pc);
        if (d < 0) return false;
        const auto cols = colours (d);
        const Tpl* t = nullptr;
        for (auto name : { "sus2", "sus4", "7sus4" })
            if (! t) for (auto& c : cols) if (c.s == name) { t = &c; break; }
        if (! t) { for (auto& c : cols) if (! t || c.lv > t->lv) t = &c; }
        out.deg = d; out.t = t->s; out.open = true;
        return true;
    }

    // Up to three good next chords after the one just played
    std::vector<int> suggestions (int lastDeg, double strange) const
    {
        if (lastDeg < 0) return {};
        const auto pal = palette();
        std::vector<std::pair<double, int>> s;
        for (auto& e : pal) if (e.deg != lastDeg) s.push_back ({ degWeight (e.deg, strange) * moveW (pal[(size_t) lastDeg], e), e.deg });
        std::stable_sort (s.begin(), s.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        std::vector<int> out;
        for (size_t i = 0; i < s.size() && i < 3; ++i) out.push_back (s[i].second);
        return out;
    }

    // ---- Upper octave of the keyboard ----
    // Richer: the next colour up with the same quality (Lush -> Very lush), keeping the mood's banned colours out
    Chord richer (const Chord& c) const
    {
        const auto cols = colours (c.deg);
        const auto cur = colourOf (c);
        const Tpl* best = nullptr;
        for (auto& t : cols)
            if (t.q == cur.q && t.lv > cur.lv && ! mood().avoid.count (t.s) && (! best || t.lv < best->lv)) best = &t;
        Chord r = c;
        if (best) r.t = best->s;
        return r;
    }
    // Other version: the next bass note and a higher top line, so the same chord sounds different
    Chord otherVersion (const Chord& c) const
    {
        const auto col = colourOf (c);
        const int n = col.iv.size() >= 3 && col.q != 'x' ? 3 : 1;
        Chord r = c;
        r.inv = (c.inv + 1) % n;
        return r;
    }
    Voicing otherVoicing (const Chord& c, const Voicing& base) const
    {
        Voicing lifted = base;
        for (auto& q : lifted.upper) q += 5;    // start the voices a fourth higher so the top note changes
        lifted.hold.clear();
        return voiceOne (c, &lifted);
    }

    // Colours for the edit row, plain first, sus last
    std::vector<Tpl> sortedColours (int deg) const
    {
        auto cols = colours (deg);
        std::stable_sort (cols.begin(), cols.end(), [] (const Tpl& a, const Tpl& b)
        {
            const int sa = a.q == 's', sb = b.q == 's';
            if (sa != sb) return sa < sb;
            return a.lv < b.lv;
        });
        return cols;
    }
};

// Plain-word colour level for the Colour knob and edit row
inline const char* colourLevelName (int lv) { switch (lv) { case 0: return "Plain"; case 1: return "Lush"; case 2: return "Very lush"; default: return "Strange"; } }
} // namespace n2c
