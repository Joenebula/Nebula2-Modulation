#pragma once
// Nebula2 Modulation: the rack engine. Plain C++17, no JUCE, so it can be tested on its own.
//
// A Rack holds Boards (one per sound, one tab each). A Board holds Modules joined by Cables.
// Every cable carries the same kind of signal, a stream of numbers, like a real Eurorack:
//   audio   -1 .. 1
//   pitch   octaves from middle C (0 = C4, 1 = C5, -2 = C2)
//   gate    above 0.5 is "on"; a trigger is the moment it goes on
//   control 0 .. 1 for envelopes, -1 .. 1 for LFOs
// Any output can be patched to any input.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "ChordEngine.h"   // the chord generator from Nebula2 Chord, unchanged

namespace n2m
{
constexpr int kBlock = 64;    // the engine works in chunks of up to 64 samples
constexpr int kSteps = 16;    // steps in a bar
constexpr int kMaxBars = 8;   // the longest pattern
constexpr int kMaxSteps = kSteps * kMaxBars;
constexpr int kData = 2 * kMaxSteps;   // a sequencer carries two rows: hits (or note on/off), then which note
constexpr int kLinkGroups = 8;         // link groups A to H
constexpr int kOuts = 16;              // stereo outputs to the host: 1 is the main mix, 2 to 16 are separate
constexpr double kPi = 3.14159265358979323846;

// ---- Keys: the same scales as Nebula2 Chord, so the two stay in step ----
struct Scale { const char* id; const char* name; int iv[7]; };
inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s {
        { "aeolian",   "Minor",             { 0, 2, 3, 5, 7, 8, 10 } },
        { "dorian",    "Dorian",            { 0, 2, 3, 5, 7, 9, 10 } },
        { "phrygian",  "Phrygian",          { 0, 1, 3, 5, 7, 8, 10 } },
        { "harmonic",  "Harmonic minor",    { 0, 2, 3, 5, 7, 8, 11 } },
        { "phrydom",   "Phrygian dominant", { 0, 1, 4, 5, 7, 8, 10 } },
        { "hungarian", "Hungarian minor",   { 0, 2, 3, 6, 7, 8, 11 } },
        { "major",     "Major",             { 0, 2, 4, 5, 7, 9, 11 } },
        { "lydian",    "Lydian",            { 0, 2, 4, 6, 7, 9, 11 } } };
    return s;
}
inline const char* const kNoteNames[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

// A scale degree (0 = home note, 7 = an octave up, negatives go down) as semitones above the key note
inline int degreeToSemis (int degree, const int* scale)
{
    const int oct = (int) std::floor (degree / 7.0);
    return oct * 12 + scale[degree - oct * 7];
}

// ---- What a module type looks like from outside ----
enum class Fmt { Plain, Percent, Bipolar, Semis, Cents, Time, Freq, Int, Octaves, Db };

struct Port { const char* id; const char* name; };
struct Param
{
    const char* id; const char* name;
    float lo, hi, def;
    Fmt fmt = Fmt::Plain;
    float skew = 1.0f;                       // above 1 gives the low end of the knob more room
    std::vector<const char*> choices {};     // not empty: a switch, not a knob
};
enum class Seq { None, Drum, Note, Chord, Host, Value, Sample, Scope };   // Host: a slot holding another plugin. Value: a row of amounts, not notes

struct Info
{
    const char* type; const char* name; const char* shelf; const char* hint;
    std::vector<Port> ins, outs;
    std::vector<Param> params;
    Seq seq = Seq::None;
    int perRow = 0;          // knobs per row on screen; 0 = the usual
};

// Knob position (0..1) to value and back
inline float paramFromNorm (const Param& p, float n)
{
    n = std::min (1.0f, std::max (0.0f, n));
    if (! p.choices.empty() || p.fmt == Fmt::Int) return std::round (p.lo + (p.hi - p.lo) * n);
    return p.lo + (p.hi - p.lo) * std::pow (n, p.skew);
}
inline float paramToNorm (const Param& p, float v)
{
    if (p.hi <= p.lo) return 0.0f;
    const float n = std::min (1.0f, std::max (0.0f, (v - p.lo) / (p.hi - p.lo)));
    return (! p.choices.empty() || p.fmt == Fmt::Int) ? n : std::pow (n, 1.0f / p.skew);
}
// Filter cutoff knob (0..1) to Hz: 20 Hz to 20 kHz
inline float cutoffHz (float n) { return 20.0f * std::pow (1000.0f, std::min (1.0f, std::max (0.0f, n))); }

inline std::string paramText (const Param& p, float v)
{
    char b[48];
    if (! p.choices.empty()) return p.choices[(size_t) std::min ((int) p.choices.size() - 1, std::max (0, (int) std::lround (v)))];
    switch (p.fmt)
    {
        case Fmt::Percent: std::snprintf (b, sizeof b, "%d%%", (int) std::lround (v * 100)); break;
        case Fmt::Bipolar: std::snprintf (b, sizeof b, "%+d%%", (int) std::lround (v * 100)); break;
        case Fmt::Semis:   std::snprintf (b, sizeof b, "%+d st", (int) std::lround (v)); break;
        case Fmt::Cents:   std::snprintf (b, sizeof b, "%+d ct", (int) std::lround (v)); break;
        case Fmt::Octaves: std::snprintf (b, sizeof b, "%+.1f oct", (double) v); break;
        case Fmt::Db:      std::snprintf (b, sizeof b, "%+.1f dB", (double) v); break;
        case Fmt::Int:     std::snprintf (b, sizeof b, "%d", (int) std::lround (v)); break;
        case Fmt::Time:
            if (v < 1.0f) std::snprintf (b, sizeof b, "%d ms", (int) std::lround (v * 1000));
            else std::snprintf (b, sizeof b, "%.2f s", (double) v);
            break;
        case Fmt::Freq:
        {
            const float hz = cutoffHz (v);
            if (hz < 1000.0f) std::snprintf (b, sizeof b, "%d Hz", (int) std::lround (hz));
            else std::snprintf (b, sizeof b, "%.1f kHz", (double) hz / 1000.0);
            break;
        }
        case Fmt::Plain: default: std::snprintf (b, sizeof b, "%.2f", (double) v); break;
    }
    return b;
}

// ---- The song's chords, as the audio thread sees them ----
// One slot per chord: where it starts, how long it lasts, and the notes to play (as pitch signals).
struct ChordSlot
{
    int start = 0, len = 32;          // in 16th steps
    int deg = 0, tr = 0;              // the chord's step of the key, and any key-change lift in semitones
    float bass = -2.0f;               // pitch signals (octaves from middle C)
    float voice[4] { 0, 0, 0, 0 };
};
struct Timeline
{
    std::vector<ChordSlot> slots;
    int total = 0;                    // steps before the progression loops
    int indexAt (long step) const
    {
        if (slots.empty() || total <= 0) return -1;
        const int s = (int) (((step % total) + total) % total);
        for (int i = (int) slots.size(); --i >= 0;) if (s >= slots[(size_t) i].start) return i;
        return 0;
    }
};

// Notes from a MIDI keyboard, or from a MIDI track in the host. The MIDI keyboard unit reads them.
struct KeyEvent { int sample = 0; unsigned char note = 60, vel = 100, channel = 1; bool on = true; };   // channel 1 to 16
struct MidiIn { const KeyEvent* events = nullptr; int count = 0; };      // in time order
struct Keys
{
    struct Held { unsigned char note, vel, channel; uint32_t order; };
    static constexpr int kMax = 32;
    Held held[kMax] {};
    int count = 0;
    uint32_t serial = 0;                  // goes up with every key pressed
    void off (int note, int channel)
    {
        int w = 0;
        for (int i = 0; i < count; ++i) if (! (held[i].note == note && held[i].channel == channel)) held[w++] = held[i];
        count = w;
    }
    void on (int note, int vel, int channel)
    {
        off (note, channel);
        if (count == kMax) { for (int i = 1; i < count; ++i) held[i - 1] = held[i]; --count; }      // the oldest gives way
        held[count++] = { (unsigned char) note, (unsigned char) std::max (1, vel), (unsigned char) channel, ++serial };
    }
    void apply (const KeyEvent& e) { if (e.on && e.vel > 0) on (e.note, e.vel, e.channel); else if (e.note == 255) count = 0; else off (e.note, e.channel); }
};

// Two shared buses, A and B: Send units on any tab add to them, Return units play them
struct Buses { std::array<float, kBlock> l[2] {}, r[2] {}; };

// What every module is told about the moment it is playing
struct Ctx
{
    double sr = 48000, bpm = 124;
    double ppq = 0;              // song position in quarter notes at the first sample of this chunk
    double ppqPerSample = 0;
    bool playing = false;
    int root = 2;                // key note, 0 = C
    const int* scale = nullptr;  // 7 intervals
    const Timeline* chords = nullptr;
    Buses* buses = nullptr;
    const Keys* keys = nullptr;  // the keys held down right now
    // Per link group: the note sequencer the others follow (nullptr if the group has none)
    const struct Module* leaders[kLinkGroups + 1] {};
};

using Buf = std::array<float, kBlock>;

// ---- A module on a board ----
struct Module
{
    virtual ~Module() = default;
    virtual void prepare (double) {}
    virtual void process (int n, const Ctx&) = 0;
    // Anything more than knobs and patterns that a module needs saved (one line of text, no line breaks)
    virtual std::string saveExtra() const { return {}; }
    virtual void loadExtra (const std::string&) {}

    void init (const Info& i, int id)
    {
        info = &i; uid = id;
        params = std::vector<std::atomic<float>> (i.params.size());
        for (size_t k = 0; k < i.params.size(); ++k) params[k].store (i.params[k].def);
        paramLink.assign (i.params.size(), 0);
        in.assign (i.ins.size(), Buf {});
        out.assign (i.outs.size(), Buf {});
        inOn.assign (i.ins.size(), 0);
        for (auto& d : data) d.store (0.0f);
    }
    float P (int i) const { return params[(size_t) i].load (std::memory_order_relaxed); }
    int paramIndex (const std::string& id) const
    {
        for (size_t k = 0; k < info->params.size(); ++k) if (id == info->params[k].id) return (int) k;
        return -1;
    }
    int inIndex (const std::string& id) const
    {
        for (size_t k = 0; k < info->ins.size(); ++k) if (id == info->ins[k].id) return (int) k;
        return -1;
    }
    int outIndex (const std::string& id) const
    {
        for (size_t k = 0; k < info->outs.size(); ++k) if (id == info->outs[k].id) return (int) k;
        return -1;
    }
    void set (const std::string& id, float v) { const int k = paramIndex (id); if (k >= 0) params[(size_t) k].store (v); }

    const Info* info = nullptr;
    int uid = 0;
    float x = 0, y = 0;                                // position on the board
    std::vector<std::atomic<float>> params;
    std::array<std::atomic<float>, kData> data;        // sequencer pattern
    std::vector<Buf> in, out;
    std::vector<char> inOn;                            // is anything plugged into this input
    std::atomic<int> playStep { -1 };                  // the step sounding now, for the screen
    // Links (set from the screen): things in the same group, on any tab, change together
    std::vector<int> paramLink;                        // per knob: 0 = not linked, 1.. = group A, B, C ...
    int patternLink = 0;                               // a sequencer's group, 0 = not linked
    int patternShare = 3;                              // what it shares: 1 rhythm, 2 notes, 3 both, 4 = keeps its own pattern and moves to match the group's leader
    bool live = true;                                  // can its signal reach the board's output
};

struct Cable { int src, srcPort, dst, dstPort; };

// The Output module: whatever is plugged into it is what the board sends to the mixer
struct OutputModule : Module
{
    static const Info& desc()
    {
        static const Info i { "output", "Output", "Utilities", "Sends this sound to the mixer",
            { { "left", "In / L" }, { "right", "R" } }, {}, { { "level", "Level", 0, 1.5f, 1.0f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        const float g = P (0);
        for (int i = 0; i < n; ++i)
        {
            l[(size_t) i] = in[0][(size_t) i] * g;
            r[(size_t) i] = (inOn[1] ? in[1][(size_t) i] : in[0][(size_t) i]) * g;
        }
    }
    Buf l {}, r {};
};

// ---- Module types: filled in by Modules.h ----
struct TypeEntry { const Info* info; std::function<std::unique_ptr<Module>()> make; };
inline const std::vector<TypeEntry>& registry();
// Module types that need the plugin framework (the plugin slot) add themselves here when the plugin starts
inline std::vector<TypeEntry>& extraTypes() { static std::vector<TypeEntry> v; return v; }
inline const TypeEntry* findType (const std::string& type)
{
    for (auto& e : registry()) if (type == e.info->type) return &e;
    for (auto& e : extraTypes()) if (type == e.info->type) return &e;
    return nullptr;
}
// Every type the Add menu can offer
inline std::vector<const Info*> allTypes()
{
    std::vector<const Info*> v;
    for (auto& e : registry()) v.push_back (e.info);
    for (auto& e : extraTypes()) v.push_back (e.info);
    return v;
}

// ---- A board: one sound ----
struct Board
{
    std::string type = "empty", name = "Sound";
    int colour = 0;
    std::atomic<float> level { 0.8f }, pan { 0.0f }, peak { 0.0f };
    std::atomic<bool> mute { false }, solo { false };
    std::atomic<int> out { 0 };       // which output this sound goes to: 0 = the main mix, 1.. = Out 2, Out 3 ...
    std::atomic<uint32_t> plays { 0xffffffffu };   // the song page: one bit per section, set = this sound plays in it
    std::vector<std::unique_ptr<Module>> modules;
    std::vector<Cable> cables;
    int nextUid = 1;

    Module* find (int uid) const
    {
        for (auto& m : modules) if (m->uid == uid) return m.get();
        return nullptr;
    }

    Module* add (const std::string& moduleType, float x, float y, double sr)
    {
        const auto* e = findType (moduleType);
        if (e == nullptr) return nullptr;
        auto m = e->make();
        m->init (*e->info, nextUid++);
        m->x = x; m->y = y;
        m->prepare (sr);
        modules.push_back (std::move (m));
        rebuild();
        return modules.back().get();
    }

    void remove (int uid)
    {
        cables.erase (std::remove_if (cables.begin(), cables.end(), [uid] (const Cable& c) { return c.src == uid || c.dst == uid; }), cables.end());
        modules.erase (std::remove_if (modules.begin(), modules.end(), [uid] (const std::unique_ptr<Module>& m) { return m->uid == uid; }), modules.end());
        rebuild();
    }

    bool connect (int src, int srcPort, int dst, int dstPort)
    {
        auto* a = find (src); auto* b = find (dst);
        if (a == nullptr || b == nullptr || a == b) return false;
        if (srcPort < 0 || srcPort >= (int) a->out.size() || dstPort < 0 || dstPort >= (int) b->in.size()) return false;
        for (auto& c : cables) if (c.src == src && c.srcPort == srcPort && c.dst == dst && c.dstPort == dstPort) return false;
        cables.push_back ({ src, srcPort, dst, dstPort });
        rebuild();
        return true;
    }

    void disconnect (int src, int srcPort, int dst, int dstPort)
    {
        cables.erase (std::remove_if (cables.begin(), cables.end(), [&] (const Cable& c)
                      { return c.src == src && c.srcPort == srcPort && c.dst == dst && c.dstPort == dstPort; }), cables.end());
        rebuild();
    }

    void disconnectAll (int uid)
    {
        cables.erase (std::remove_if (cables.begin(), cables.end(), [uid] (const Cable& c) { return c.src == uid || c.dst == uid; }), cables.end());
        rebuild();
    }

    // Work out the running order, what feeds what, and which modules can be heard.
    void rebuild()
    {
        const size_t n = modules.size();
        routes.assign (n, {});
        auto indexOf = [this] (int uid) { for (size_t i = 0; i < modules.size(); ++i) if (modules[i]->uid == uid) return (int) i; return -1; };
        std::vector<int> indeg (n, 0);
        std::vector<std::vector<int>> next (n), prev (n);
        for (auto& m : modules) std::fill (m->inOn.begin(), m->inOn.end(), (char) 0);
        for (auto& c : cables)
        {
            const int a = indexOf (c.src), b = indexOf (c.dst);
            if (a < 0 || b < 0) continue;
            routes[(size_t) b].push_back ({ modules[(size_t) a].get(), c.srcPort, c.dstPort });
            modules[(size_t) b]->inOn[(size_t) c.dstPort] = 1;
            next[(size_t) a].push_back (b); prev[(size_t) b].push_back (a);
            ++indeg[(size_t) b];
        }
        // Sources first. A loop of cables still runs: the looped part just hears the chunk before.
        order.clear();
        std::vector<char> done (n, 0);
        std::vector<int> queue;
        for (size_t i = 0; i < n; ++i) if (indeg[i] == 0) queue.push_back ((int) i);
        while (order.size() < n)
        {
            if (queue.empty())
            {
                for (size_t i = 0; i < n; ++i) if (! done[i]) { queue.push_back ((int) i); break; }
            }
            const int i = queue.front(); queue.erase (queue.begin());
            if (done[(size_t) i]) continue;
            done[(size_t) i] = 1;
            order.push_back (i);
            for (int j : next[(size_t) i]) if (! done[(size_t) j] && --indeg[(size_t) j] <= 0) queue.push_back (j);
        }
        // Heard = a path of cables leads from it to an Output
        std::vector<char> heard (n, 0);
        std::vector<int> stack;
        outputs.clear();
        hasReturn = false;
        for (size_t i = 0; i < n; ++i)
        {
            const std::string t = modules[i]->info->type;
            hasReturn = hasReturn || t == "return";
            // A Send is heard through its bus even with nothing cabled to its outputs
            if (t == "send") { heard[i] = 1; stack.push_back ((int) i); }
            if (auto* o = dynamic_cast<OutputModule*> (modules[i].get())) { outputs.push_back (o); heard[i] = 1; stack.push_back ((int) i); }
        }
        while (! stack.empty())
        {
            const int i = stack.back(); stack.pop_back();
            for (int j : prev[(size_t) i]) if (! heard[(size_t) j]) { heard[(size_t) j] = 1; stack.push_back (j); }
        }
        for (size_t i = 0; i < n; ++i) modules[i]->live = heard[i] != 0;
    }

    // Adds this board's sound into l and r
    void process (int n, const Ctx& c, float* l, float* r, bool audible)
    {
        for (int idx : order)
        {
            auto* m = modules[(size_t) idx].get();
            for (auto& b : m->in) std::fill (b.begin(), b.begin() + n, 0.0f);
            for (auto& rt : routes[(size_t) idx])
            {
                const auto& s = rt.src->out[(size_t) rt.srcPort];
                auto& d = m->in[(size_t) rt.dstPort];
                for (int i = 0; i < n; ++i) d[(size_t) i] += s[(size_t) i];
            }
            m->process (n, c);
        }
        const float lv = audible ? level.load() : 0.0f, p = pan.load();
        const float gl = lv * std::min (1.0f, 1.0f - p), gr = lv * std::min (1.0f, 1.0f + p);
        float pk = 0.0f;
        for (auto* o : outputs)
            for (int i = 0; i < n; ++i)
            {
                const float a = o->l[(size_t) i] * gl, b = o->r[(size_t) i] * gr;
                l[i] += a; r[i] += b;
                pk = std::max (pk, std::max (std::abs (a), std::abs (b)));
            }
        if (pk > peak.load()) peak.store (pk);
    }

    struct Route { Module* src; int srcPort, dstPort; };
    std::vector<std::vector<Route>> routes;
    std::vector<int> order;
    std::vector<OutputModule*> outputs;
    bool hasReturn = false;      // this board plays a send bus, so it must run after the boards that feed it
};

// A lock the audio thread never waits on: if the screen is changing the rack, audio skips one chunk
struct TryLock
{
    void lock() { while (flag.test_and_set (std::memory_order_acquire)) std::this_thread::yield(); }
    bool tryLock() { return ! flag.test_and_set (std::memory_order_acquire); }
    void unlock() { flag.clear (std::memory_order_release); }
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
};
struct Hold
{
    explicit Hold (TryLock& l) : lock (l) { lock.lock(); }
    ~Hold() { lock.unlock(); }
    TryLock& lock;
};

// Gentle safety limiter on the master: clean below half level, never above 1
inline float softLimit (float x)
{
    const float a = std::abs (x);
    if (a <= 0.5f) return x;
    return (x < 0 ? -1.0f : 1.0f) * (0.5f + 0.5f * std::tanh ((a - 0.5f) * 2.0f));
}

// The song's chord progression, shared by every tab the way the key is. Made by the Nebula2 Chord engine.
struct Harmony
{
    std::string mood = "deep";
    uint32_t seed = 362668;
    int lenBars = 16, chordBars = 2, strange = 35, variation = 50;
    int lift = 0;                     // key change: the last section goes up this many semitones (0 = none)
    std::vector<n2c::Chord> seq;
    std::vector<std::string> names;   // "Dm7", "Bbmaj9" ... one per chord, for the screen
};

// The song page: the song is cut into sections of so many bars, and each tab plays in the sections ticked for it.
// After the last section it goes round again. Off = every tab plays all the time.
constexpr int kSongSections = 16;
struct Song
{
    std::atomic<bool> on { false };
    std::atomic<int> sections { 8 }, bars { 4 };
    std::atomic<int> now { -1 };      // the section playing, for the screen (-1 = none)
};

// The separate outputs, for one call to process. A null pair means the host hasn't switched that output on.
struct Outs { float* l[kOuts] {}; float* r[kOuts] {}; };

// ---- The rack: every board, the key, the chords, the master ----
struct Rack
{
    std::vector<std::unique_ptr<Board>> boards;
    std::atomic<int> root { 2 }, scale { 0 };
    std::atomic<float> master { 0.8f }, peakL { 0.0f }, peakR { 0.0f };
    double sr = 48000;
    std::atomic<int> outsOn { 1 };      // which outputs the host has switched on, one bit each (bit 0 = main)
    TryLock lock;
    Harmony harmony;
    Song song;
    Timeline timeline;            // swapped in whole, under the lock
    Buses buses;                  // audio thread only
    Keys keys;                    // audio thread only

    Rack() { generateChords (harmony.seed); }

    n2c::Theory theory() const
    {
        n2c::Theory t;
        t.moodId = harmony.mood;
        t.scaleId = scales()[(size_t) std::min ((int) scales().size() - 1, std::max (0, scale.load()))].id;
        t.tonic = root.load();
        return t;
    }

    // Works the chords out again as notes: after a new progression, or a change of key
    void rebuildChords()
    {
        const auto t = theory();
        const auto voiced = t.voiceAll (harmony.seq);
        Timeline tl;
        harmony.names.clear();
        int at = 0;
        for (size_t i = 0; i < harmony.seq.size(); ++i)
        {
            const auto& c = harmony.seq[i];
            ChordSlot s;
            s.start = at; s.len = std::max (1, c.bars) * kSteps; s.deg = c.deg; s.tr = c.tr;
            at += s.len;
            s.bass = (float) (voiced[i].bass + 12 - 60) / 12.0f;
            // Four notes from the full voicing: different notes of the chord first, then the highest left over
            std::vector<int> pick, rest;
            for (int q : voiced[i].upper)
            {
                bool seen = false;
                for (int p : pick) seen = seen || n2c::mod12 (p) == n2c::mod12 (q);
                (seen ? rest : pick).push_back (q);
            }
            while (pick.size() > 4) pick.erase (pick.begin() + (long) (pick.size() / 2));
            while (pick.size() < 4 && ! rest.empty()) { pick.push_back (rest.back()); rest.pop_back(); }
            while (pick.size() < 4) pick.push_back (pick.empty() ? 60 : pick.back());
            std::sort (pick.begin(), pick.end());
            for (int k = 0; k < 4; ++k) s.voice[k] = (float) (pick[(size_t) k] - 60) / 12.0f;
            tl.slots.push_back (s);
            harmony.names.push_back (t.chordName (c));
        }
        tl.total = at;
        { Hold h (lock); timeline.slots.swap (tl.slots); timeline.total = tl.total; }
    }

    // A new progression from the mood, in the current key
    void generateChords (uint32_t newSeed)
    {
        harmony.seed = newSeed;
        n2c::Theory::GenSettings g;
        g.lenBars = harmony.lenBars; g.chordBars = harmony.chordBars; g.strange = harmony.strange; g.variation = harmony.variation; g.lift = harmony.lift;
        auto seq = theory().generate (newSeed, g);
        if (! seq.empty() && seq[0].intro) seq.erase (seq.begin());   // no glide intro here: the song starts on chord 1
        // Locked chords stay where they are through a Generate
        for (size_t i = 0; i < seq.size() && i < harmony.seq.size(); ++i)
            if (harmony.seq[i].locked) seq[i] = harmony.seq[i];
        harmony.seq = seq;
        rebuildChords();
    }

    // ---- Editing one chord (i counts from 0) ----
    bool validChord (int i) const { return i >= 0 && i < (int) harmony.seq.size(); }
    void setChordColour (int i, const std::string& t) { if (validChord (i)) { harmony.seq[(size_t) i].t = t; rebuildChords(); } }
    void setChordBass (int i, int inv) { if (validChord (i)) { harmony.seq[(size_t) i].inv = std::min (2, std::max (0, inv)); rebuildChords(); } }
    void setChordBars (int i, int bars) { if (validChord (i)) { harmony.seq[(size_t) i].bars = std::min (8, std::max (1, bars)); rebuildChords(); } }
    void setChordDegree (int i, int deg) { if (validChord (i)) { auto& c = harmony.seq[(size_t) i]; c.deg = ((deg % 7) + 7) % 7; c.t = theory().colourOf (c).s; c.inv = 0; rebuildChords(); } }
    void toggleChordLock (int i) { if (validChord (i)) harmony.seq[(size_t) i].locked = ! harmony.seq[(size_t) i].locked; }

    // Choosing a mood also moves to that mood's scale, as in Nebula2 Chord. The key note stays.
    void setMood (const std::string& id)
    {
        harmony.mood = n2c::moods()[(size_t) n2c::moodIndex (id)].id;
        const auto& m = n2c::moods()[(size_t) n2c::moodIndex (id)];
        for (size_t i = 0; i < scales().size(); ++i) if (m.scale == scales()[i].id) scale.store ((int) i);
        generateChords (harmony.seed);
    }

    // ---- Links ----
    // After a linked knob moves: every other knob in its group, on any tab, goes to the same position.
    // Position, not value, so a 0-100% knob and a 20 Hz-20 kHz knob can share a group.
    void pushParam (const Module& src, int index)
    {
        const int group = src.paramLink[(size_t) index];
        if (group <= 0) return;
        const float n = paramToNorm (src.info->params[(size_t) index], src.P (index));
        for (auto& b : boards)
            for (auto& m : b->modules)
                for (size_t k = 0; k < m->info->params.size(); ++k)
                    if (m->paramLink[k] == group && ! (m.get() == &src && (int) k == index))
                        m->params[k].store (paramFromNorm (m->info->params[k], n));
    }

    // Copies what two linked sequencers share from one to the other
    static void copyPattern (const Module& from, Module& to)
    {
        const int share = from.patternShare & to.patternShare;
        const bool fromDrum = from.info->seq == Seq::Drum, toDrum = to.info->seq == Seq::Drum;
        if (share & 1)
        {
            for (int k = 0; k < kMaxSteps; ++k)
            {
                const float v = from.data[(size_t) k].load();
                // Between a drum and a note sequencer only "plays here or not" carries across; accents stay with drums
                to.data[(size_t) k].store (fromDrum == toDrum ? v : v > 0.5f ? 1.0f : 0.0f);
            }
            for (const char* id : { "length", "bars" })
            {
                const int a = from.paramIndex (id), c = to.paramIndex (id);
                if (a >= 0 && c >= 0) to.params[(size_t) c].store (from.P (a));
            }
        }
        if ((share & 2) && ! fromDrum && ! toDrum)
            for (int k = 0; k < kMaxSteps; ++k) to.data[(size_t) (kMaxSteps + k)].store (from.data[(size_t) (kMaxSteps + k)].load());
    }

    // After a linked pattern is edited: the others in its group follow
    void pushPattern (const Module& src)
    {
        if (src.patternLink <= 0) return;
        for (auto& b : boards)
            for (auto& m : b->modules)
                if (m.get() != &src && m->patternLink == src.patternLink && (m->info->seq == Seq::Drum || m->info->seq == Seq::Note))
                    copyPattern (src, *m);
    }

    // Putting a knob into a group: it takes the group's position if the group already has members
    void linkParam (Module& mod, int index, int group)
    {
        mod.paramLink[(size_t) index] = 0;
        if (group > 0)
            for (auto& b : boards)
                for (auto& m : b->modules)
                    for (size_t k = 0; k < m->info->params.size(); ++k)
                        if (m->paramLink[k] == group)
                        {
                            mod.params[(size_t) index].store (paramFromNorm (mod.info->params[(size_t) index], paramToNorm (m->info->params[k], m->P ((int) k))));
                            mod.paramLink[(size_t) index] = group;
                            return;
                        }
        mod.paramLink[(size_t) index] = std::max (0, group);
    }

    // Putting a sequencer into a group: it takes the group's pattern if the group already has members
    void linkPattern (Module& mod, int group, int share)
    {
        mod.patternShare = share >= 1 && share <= 4 ? share : 3;
        mod.patternLink = 0;
        if (group > 0)
            for (auto& b : boards)
                for (auto& m : b->modules)
                    if (m->patternLink == group && m.get() != &mod) { mod.patternLink = group; copyPattern (*m, mod); return; }
        mod.patternLink = std::max (0, group);
    }

    int membersOf (int group, bool patterns) const
    {
        int n = 0;
        for (auto& b : boards)
            for (auto& m : b->modules)
            {
                if (patterns) n += m->patternLink == group ? 1 : 0;
                else for (int g : m->paramLink) n += g == group ? 1 : 0;
            }
        return n;
    }

    void setKey (int newRoot, int newScale)
    {
        root.store (((newRoot % 12) + 12) % 12);
        scale.store (std::min ((int) scales().size() - 1, std::max (0, newScale)));
        rebuildChords();
    }

    void prepare (double sampleRate)
    {
        Hold h (lock);
        sr = sampleRate;
        for (auto& b : boards) for (auto& m : b->modules) m->prepare (sr);
    }

    // Fills l and r (n samples). ppq is the song position at the first sample.
    // Sounds sent to a separate output go there instead of the main mix. If that output isn't switched on
    // in the host, the sound stays in the main mix, so nothing ever goes missing.
    void process (float* l, float* r, int n, double bpm, double ppq, bool playing, const Outs* outs = nullptr, const MidiIn* midi = nullptr)
    {
        std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f);
        int on = 1;
        if (outs != nullptr)
            for (int k = 1; k < kOuts; ++k)
                if (outs->l[k] != nullptr && outs->r[k] != nullptr)
                {
                    std::fill (outs->l[k], outs->l[k] + n, 0.0f); std::fill (outs->r[k], outs->r[k] + n, 0.0f);
                    on |= 1 << k;
                }
        outsOn.store (on);
        if (! lock.tryLock())
        {
            if (midi != nullptr) for (int k = 0; k < midi->count; ++k) keys.apply (midi->events[k]);     // never lose a key coming up
            return;
        }
        Ctx c;
        c.sr = sr; c.bpm = bpm; c.playing = playing;
        c.ppqPerSample = bpm / 60.0 / sr;
        c.root = root.load();
        c.scale = scales()[(size_t) std::min ((int) scales().size() - 1, std::max (0, scale.load()))].iv;
        c.chords = &timeline;
        c.buses = &buses;
        c.keys = &keys;
        int ev = 0;
        // A group's leader is its first note sequencer that isn't itself following
        for (auto& b : boards)
            for (auto& m : b->modules)
                if (m->patternLink > 0 && m->patternShare != 4 && m->info->seq == Seq::Note && c.leaders[m->patternLink] == nullptr)
                    c.leaders[m->patternLink] = m.get();
        bool anySolo = false;
        for (auto& b : boards) anySolo = anySolo || b->solo.load();
        // The song page. A tab that sits out a section is run as if the song were stopped: its sequencers go quiet
        // and whatever was sounding rings out. Chunks are cut at section edges so the sample it happens on never
        // depends on the host's block size.
        const bool songOn = playing && song.on.load();
        const int nSec = std::min (kSongSections, std::max (1, song.sections.load())), secBars = std::max (1, song.bars.load());
        auto sectionAt = [nSec, secBars] (double q)
        {
            const long bar = std::max (0L, (long) std::floor (q * 4.0 + 1.0e-9)) / 16;
            return (int) ((bar / secBars) % nSec);
        };
        Ctx resting = c; resting.playing = false;
        int sec = -1;
        for (int pos = 0, len = 0; pos < n; pos += len)
        {
            len = std::min (kBlock, n - pos);
            // Keys land on the exact sample: a chunk ends where the next key starts
            if (midi != nullptr)
            {
                while (ev < midi->count && midi->events[ev].sample <= pos) keys.apply (midi->events[ev++]);
                if (ev < midi->count) len = std::min (len, std::max (1, midi->events[ev].sample - pos));
            }
            c.ppq = ppq + pos * c.ppqPerSample;
            resting.ppq = c.ppq;
            if (songOn)
            {
                sec = sectionAt (c.ppq);
                int k = 1;
                while (k < len && sectionAt (c.ppq + k * c.ppqPerSample) == sec) ++k;
                len = k;
            }
            for (int k = 0; k < 2; ++k) { std::fill (buses.l[k].begin(), buses.l[k].begin() + len, 0.0f); std::fill (buses.r[k].begin(), buses.r[k].begin() + len, 0.0f); }
            // Tabs that play a send bus run after the tabs that feed it, wherever they sit in the tab row
            for (int pass = 0; pass < 2; ++pass)
                for (auto& b : boards)
                {
                    if (b->hasReturn != (pass == 1)) continue;
                    const int k = b->out.load();
                    const bool separate = k > 0 && k < kOuts && (on & (1 << k)) != 0;
                    const bool sitsOut = songOn && ((b->plays.load() >> sec) & 1u) == 0;
                    b->process (len, sitsOut ? resting : c, (separate ? outs->l[k] : l) + pos, (separate ? outs->r[k] : r) + pos, anySolo ? b->solo.load() : ! b->mute.load());
                }
        }
        if (midi != nullptr) while (ev < midi->count) keys.apply (midi->events[ev++]);
        lock.unlock();
        song.now.store (sec);
        // Separate outputs skip the master fader (the host's own channel fader does that job) but keep the safety limiter
        for (int k = 1; k < kOuts; ++k)
            if ((on & (1 << k)) != 0)
                for (int i = 0; i < n; ++i) { outs->l[k][i] = softLimit (outs->l[k][i]); outs->r[k][i] = softLimit (outs->r[k][i]); }
        const float g = master.load();
        float pl = 0, pr = 0;
        for (int i = 0; i < n; ++i)
        {
            l[i] = softLimit (l[i] * g); r[i] = softLimit (r[i] * g);
            pl = std::max (pl, std::abs (l[i])); pr = std::max (pr, std::abs (r[i]));
        }
        if (pl > peakL.load()) peakL.store (pl);
        if (pr > peakR.load()) peakR.store (pr);
    }

    // ---- Changes from the screen. Each takes the lock so audio never sees a half-made board ----
    Board* addBoard (std::unique_ptr<Board> b)
    {
        for (auto& m : b->modules) m->prepare (sr);
        Hold h (lock);
        boards.push_back (std::move (b));
        return boards.back().get();
    }
    void removeBoard (int i)
    {
        if (i < 0 || i >= (int) boards.size()) return;
        std::unique_ptr<Board> gone;
        { Hold h (lock); gone = std::move (boards[(size_t) i]); boards.erase (boards.begin() + i); }
    }
    void moveBoard (int from, int to)
    {
        if (from < 0 || to < 0 || from >= (int) boards.size() || to >= (int) boards.size() || from == to) return;
        Hold h (lock);
        auto b = std::move (boards[(size_t) from]);
        boards.erase (boards.begin() + from);
        boards.insert (boards.begin() + to, std::move (b));
    }
    Module* addModule (Board& b, const std::string& type, float x, float y) { Hold h (lock); return b.add (type, x, y, sr); }
    void removeModule (Board& b, int uid) { Hold h (lock); b.remove (uid); }
    bool connect (Board& b, int s, int sp, int d, int dp) { Hold h (lock); return b.connect (s, sp, d, dp); }
    void disconnect (Board& b, int s, int sp, int d, int dp) { Hold h (lock); b.disconnect (s, sp, d, dp); }
    void disconnectAll (Board& b, int uid) { Hold h (lock); b.disconnectAll (uid); }

    // ---- Saving: plain text, one thing per line, named by id so old saves keep loading ----
    static std::string clean (std::string s)
    {
        for (auto& ch : s) if (ch == '|' || ch == '\n' || ch == '\r') ch = ' ';
        return s;
    }
    // wantExtra: which modules also write their "extra" state (a plugin slot's plugin and its settings, which is
    // slow to gather). Null means all of them, as a project save needs. Undo passes a filter so it stays quick.
    static void writeBoard (std::ostringstream& o, const Board& b, const std::function<bool (int uid)>* wantExtra = nullptr)
    {
        o << "board " << b.type << '|' << clean (b.name) << '|' << b.colour << '|' << b.level.load() << '|' << b.pan.load()
          << '|' << (b.mute.load() ? 1 : 0) << '|' << (b.solo.load() ? 1 : 0) << '|' << b.out.load() << '|' << b.plays.load() << '\n';
        for (auto& m : b.modules)
        {
            o << "mod " << m->uid << ' ' << m->info->type << ' ' << m->x << ' ' << m->y << '\n';
            for (size_t k = 0; k < m->info->params.size(); ++k)
                o << "p " << m->uid << ' ' << m->info->params[k].id << ' ' << m->params[k].load() << '\n';
            for (size_t k = 0; k < m->info->params.size(); ++k)
                if (m->paramLink[k] > 0) o << "l " << m->uid << ' ' << m->info->params[k].id << ' ' << m->paramLink[k] << '\n';
            if (m->patternLink > 0) o << "k " << m->uid << ' ' << m->patternLink << ' ' << m->patternShare << '\n';
            if (wantExtra == nullptr || (*wantExtra) (m->uid))
            {
                const auto extra = m->saveExtra();
                if (! extra.empty()) o << "x " << m->uid << ' ' << clean (extra) << '\n';
            }
            if (m->info->seq == Seq::Drum || m->info->seq == Seq::Note || m->info->seq == Seq::Value)
                for (int row = 0; row < 2; ++row)
                {
                    // Each row is written up to its last used bar, so short patterns make short saves
                    int used = kSteps;
                    for (int k = 0; k < kMaxSteps; ++k) if (m->data[(size_t) (row * kMaxSteps + k)].load() != 0.0f) used = ((k / kSteps) + 1) * kSteps;
                    o << (row == 0 ? "g " : "n ") << m->uid;
                    for (int k = 0; k < used; ++k) o << ' ' << m->data[(size_t) (row * kMaxSteps + k)].load();
                    o << '\n';
                }
        }
        for (auto& c : b.cables)
        {
            auto* s = b.find (c.src); auto* d = b.find (c.dst);
            if (s && d) o << "cable " << c.src << ' ' << s->info->outs[(size_t) c.srcPort].id << ' ' << c.dst << ' ' << d->info->ins[(size_t) c.dstPort].id << '\n';
        }
    }
    std::string save() const
    {
        std::ostringstream o;
        o << "N2M 1\n" << "key " << root.load() << ' ' << scale.load() << '\n' << "master " << master.load() << '\n';
        o << "song " << (song.on.load() ? 1 : 0) << ' ' << song.sections.load() << ' ' << song.bars.load() << '\n';
        o << "harmony " << harmony.mood << ' ' << harmony.seed << ' ' << harmony.lenBars << ' ' << harmony.chordBars << ' ' << harmony.strange << ' ' << harmony.variation << ' ' << harmony.lift << '\n';
        for (auto& c : harmony.seq)
            o << "chord " << c.deg << ' ' << c.bars << ' ' << c.tr << ' ' << c.inv << ' ' << (c.t.empty() ? "-" : c.t) << ' ' << (c.locked ? 1 : 0) << '\n';
        for (auto& b : boards) writeBoard (o, *b);
        o << "end\n";
        return o.str();
    }

    // Reads boards from saved text. Unknown module types and lines are skipped, not fatal.
    static std::vector<std::unique_ptr<Board>> readBoards (const std::string& text, double sampleRate, int* rootOut = nullptr, int* scaleOut = nullptr, float* masterOut = nullptr, Harmony* harmonyOut = nullptr, int* songOut = nullptr)
    {
        bool sawHarmony = false;
        std::vector<std::unique_ptr<Board>> made;
        std::istringstream in (text);
        std::string line;
        Board* b = nullptr;
        while (std::getline (in, line))
        {
            std::istringstream ls (line);
            std::string tag; ls >> tag;
            if (tag == "key") { int r = 2, s = 0; ls >> r >> s; if (rootOut) *rootOut = ((r % 12) + 12) % 12; if (scaleOut) *scaleOut = std::min ((int) scales().size() - 1, std::max (0, s)); }
            else if (tag == "master") { float m = 0.8f; ls >> m; if (masterOut) *masterOut = m; }
            else if (tag == "song") { if (songOut) ls >> songOut[0] >> songOut[1] >> songOut[2]; }
            else if (tag == "harmony")
            {
                if (harmonyOut == nullptr) continue;
                Harmony hm; std::string mood;
                ls >> mood >> hm.seed >> hm.lenBars >> hm.chordBars >> hm.strange >> hm.variation;
                int lift = 0; if (ls >> lift) hm.lift = std::min (3, std::max (0, lift));   // saves from before v0.1.11 have none
                hm.mood = n2c::moods()[(size_t) n2c::moodIndex (mood)].id;
                *harmonyOut = hm; sawHarmony = true;
            }
            else if (tag == "chord")
            {
                if (harmonyOut == nullptr || ! sawHarmony) continue;
                n2c::Chord c; std::string t;
                ls >> c.deg >> c.bars >> c.tr >> c.inv >> t;
                int locked = 0; if (ls >> locked) c.locked = locked != 0;       // saves from before v0.1.10 have no lock
                c.deg = ((c.deg % 7) + 7) % 7; c.bars = std::min (8, std::max (1, c.bars)); c.inv = std::min (2, std::max (0, c.inv));
                c.t = t == "-" ? "" : t;
                if (harmonyOut->seq.size() < 64) harmonyOut->seq.push_back (c);
            }
            else if (tag == "board")
            {
                std::string rest; std::getline (ls, rest);
                if (! rest.empty() && rest[0] == ' ') rest.erase (0, 1);
                std::vector<std::string> f; std::string cur; std::istringstream rs (rest);
                while (std::getline (rs, cur, '|')) f.push_back (cur);
                made.push_back (std::make_unique<Board>());
                b = made.back().get();
                if (f.size() > 0) b->type = f[0];
                if (f.size() > 1) b->name = f[1];
                if (f.size() > 2) b->colour = std::atoi (f[2].c_str());
                if (f.size() > 3) b->level.store ((float) std::atof (f[3].c_str()));
                if (f.size() > 4) b->pan.store ((float) std::atof (f[4].c_str()));
                if (f.size() > 5) b->mute.store (f[5] == "1");
                if (f.size() > 6) b->solo.store (f[6] == "1");
                if (f.size() > 7) b->out.store (std::min (kOuts - 1, std::max (0, std::atoi (f[7].c_str()))));
                if (f.size() > 8) b->plays.store ((uint32_t) std::strtoul (f[8].c_str(), nullptr, 10));
            }
            else if (b == nullptr) continue;
            else if (tag == "mod")
            {
                int uid = 0; std::string type; float x = 0, y = 0; ls >> uid >> type >> x >> y;
                const auto* e = findType (type);
                if (e == nullptr || uid <= 0) continue;
                auto m = e->make();
                m->init (*e->info, uid);
                m->x = x; m->y = y;
                m->prepare (sampleRate);
                b->nextUid = std::max (b->nextUid, uid + 1);
                b->modules.push_back (std::move (m));
            }
            else if (tag == "p")
            {
                int uid = 0; std::string id; float v = 0; ls >> uid >> id >> v;
                if (auto* m = b->find (uid))
                {
                    const int k = m->paramIndex (id);
                    if (k >= 0) { const auto& p = m->info->params[(size_t) k]; m->params[(size_t) k].store (std::min (p.hi, std::max (p.lo, v))); }
                }
            }
            else if (tag == "l")
            {
                int uid = 0, group = 0; std::string id; ls >> uid >> id >> group;
                if (auto* m = b->find (uid)) { const int k = m->paramIndex (id); if (k >= 0) m->paramLink[(size_t) k] = std::min (kLinkGroups, std::max (0, group)); }
            }
            else if (tag == "k")
            {
                int uid = 0, group = 0, share = 3; ls >> uid >> group >> share;
                if (auto* m = b->find (uid)) { m->patternLink = std::min (kLinkGroups, std::max (0, group)); m->patternShare = share >= 1 && share <= 4 ? share : 3; }
            }
            else if (tag == "x")
            {
                int uid = 0; ls >> uid;
                std::string rest; std::getline (ls, rest);
                if (! rest.empty() && rest[0] == ' ') rest.erase (0, 1);
                if (auto* m = b->find (uid)) m->loadExtra (rest);
            }
            else if (tag == "g" || tag == "n")
            {
                int uid = 0; ls >> uid;
                const int base = tag == "g" ? 0 : kMaxSteps;
                if (auto* m = b->find (uid)) for (int k = 0; k < kMaxSteps; ++k) { float v = 0; if (ls >> v) m->data[(size_t) (base + k)].store (v); else break; }
            }
            else if (tag == "d")
            {
                // v0.1.0 saves: one bar. Hits in the first 16 numbers; a note sequencer's notes in the next 16.
                int uid = 0; ls >> uid;
                if (auto* m = b->find (uid))
                {
                    std::vector<float> v; float x = 0;
                    while (ls >> x) v.push_back (x);
                    for (int k = 0; k < kSteps && k < (int) v.size(); ++k)
                        for (int bar = 0; bar < kMaxBars; ++bar) m->data[(size_t) (bar * kSteps + k)].store (v[(size_t) k]);
                    for (int k = 0; k < kSteps && kSteps + k < (int) v.size(); ++k)
                        for (int bar = 0; bar < kMaxBars; ++bar) m->data[(size_t) (kMaxSteps + bar * kSteps + k)].store (v[(size_t) (kSteps + k)]);
                }
            }
            else if (tag == "cable")
            {
                int s = 0, d = 0; std::string sp, dp; ls >> s >> sp >> d >> dp;
                auto* a = b->find (s); auto* c = b->find (d);
                if (a && c)
                {
                    const int si = a->outIndex (sp), di = c->inIndex (dp);
                    if (si >= 0 && di >= 0 && a != c) b->cables.push_back ({ s, si, d, di });
                }
            }
        }
        for (auto& mb : made) mb->rebuild();
        return made;
    }

    bool load (const std::string& text)
    {
        if (text.compare (0, 4, "N2M ") != 0) return false;
        int r = root.load(), s = scale.load(); float m = master.load();
        Harmony hm;
        int sg[3] = { 0, 8, 4 };                                  // a save from before the song page: off
        auto made = readBoards (text, sr, &r, &s, &m, &hm, sg);
        std::vector<std::unique_ptr<Board>> old;
        { Hold h (lock); old.swap (boards); boards.swap (made); }
        root.store (r); scale.store (s); master.store (m);
        song.on.store (sg[0] != 0); song.sections.store (std::min (kSongSections, std::max (1, sg[1]))); song.bars.store (std::min (16, std::max (1, sg[2])));
        harmony = hm;
        if (harmony.seq.empty()) generateChords (harmony.seed);   // a save from before chords existed
        else rebuildChords();
        return true;
    }

    static std::string boardText (const Board& b, const std::function<bool (int uid)>* wantExtra = nullptr)
    {
        std::ostringstream o;
        writeBoard (o, b, wantExtra);
        return o.str();
    }

    // The module ids named in a piece of saved board text
    static std::vector<int> uidsIn (const std::string& text)
    {
        std::vector<int> ids;
        std::istringstream in (text);
        std::string line;
        while (std::getline (in, line))
            if (line.compare (0, 4, "mod ") == 0) ids.push_back (std::atoi (line.c_str() + 4));
        return ids;
    }

    // Puts a board back to a saved state without rebuilding what hasn't changed: a module that is in both keeps
    // running (so a plugin in a slot isn't reloaded) and just takes the saved knobs, pattern and position.
    // Modules only in the saved state are created; modules not in it are removed.
    void applyBoard (int index, const std::string& text)
    {
        if (index < 0 || index >= (int) boards.size()) return;
        auto made = readBoards (text, sr);
        if (made.empty()) return;
        auto& src = *made[0];
        auto& dst = *boards[(size_t) index];
        std::vector<std::unique_ptr<Module>> gone;        // destroyed after the lock is released
        {
            Hold h (lock);
            std::vector<std::unique_ptr<Module>> next;
            for (auto& sm : src.modules)
            {
                std::unique_ptr<Module>* live = nullptr;
                for (auto& lm : dst.modules) if (lm != nullptr && lm->uid == sm->uid && lm->info == sm->info) live = &lm;
                if (live == nullptr) { next.push_back (std::move (sm)); continue; }
                auto& m = **live;
                for (size_t k = 0; k < m.params.size(); ++k) m.params[k].store (sm->params[k].load());
                for (size_t k = 0; k < m.data.size(); ++k) m.data[k].store (sm->data[k].load());
                m.paramLink = sm->paramLink; m.patternLink = sm->patternLink; m.patternShare = sm->patternShare;
                m.x = sm->x; m.y = sm->y;
                next.push_back (std::move (*live));
                gone.push_back (std::move (sm));
            }
            for (auto& lm : dst.modules) if (lm != nullptr) gone.push_back (std::move (lm));
            dst.modules = std::move (next);
            dst.cables = src.cables;
            dst.nextUid = std::max (dst.nextUid, src.nextUid);
            dst.type = src.type; dst.name = src.name; dst.colour = src.colour;
            dst.level.store (src.level.load()); dst.pan.store (src.pan.load());
            dst.mute.store (src.mute.load()); dst.solo.store (src.solo.load()); dst.out.store (src.out.load()); dst.plays.store (src.plays.load());
            dst.rebuild();
        }
    }

    // A copy of one board, as a new board
    std::unique_ptr<Board> copyBoard (const Board& b) const
    {
        std::ostringstream o;
        writeBoard (o, b);
        auto made = readBoards (o.str(), sr);
        return made.empty() ? nullptr : std::move (made[0]);
    }
};
} // namespace n2m
