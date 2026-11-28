#pragma once
// Nebula2 Modulation: the module types. Each one is a small struct with a desc() (what it looks
// like: jacks, knobs, which menu heading) and a process() (what it does to the signal).
// To add a module: write the struct here and add one line to registry() at the bottom.
#include "Engine.h"

namespace n2m
{
inline float clamp01 (float v) { return std::min (1.0f, std::max (0.0f, v)); }

// Smooths the corners of saw and square waves so they don't sound harsh and digital
inline float polyBlep (double t, double dt)
{
    if (t < dt) { t /= dt; return (float) (t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float) (t * t + t + t + 1.0); }
    return 0.0f;
}

// ---------------------------------------------------------------- Sources
struct Oscillator : Module
{
    static const Info& desc()
    {
        static const Info i { "osc", "Oscillator", "Sources", "The basic tone: sine, triangle, saw or square",
            { { "pitch", "Pitch" }, { "mod", "Mod" }, { "sync", "Sync" } }, { { "out", "Out" } },
            { { "wave", "Wave", 0, 3, 2, Fmt::Int, 1, { "Sine", "Triangle", "Saw", "Square" } },
              { "coarse", "Tune", -48, 48, 0, Fmt::Semis },
              { "fine", "Fine", -100, 100, 0, Fmt::Cents },
              { "modamt", "Mod amt", 0, 60, 0, Fmt::Semis },
              { "level", "Level", 0, 1, 0.8f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const int wave = (int) std::lround (P (0));
        const double base = (double) P (1) / 12.0 + (double) P (2) / 1200.0;
        const double modAmt = (double) P (3) / 12.0;
        const float level = P (4);
        for (int i = 0; i < n; ++i)
        {
            // Sync: a trigger restarts the wave, so every drum hit starts the same way
            const bool sy = in[2][(size_t) i] > 0.5f;
            if (sy && ! lastSync) phase = 0.0;
            lastSync = sy;
            const double oct = base + (double) in[0][(size_t) i] + (double) in[1][(size_t) i] * modAmt;
            const double f = std::min (c.sr * 0.45, 261.6255653 * std::pow (2.0, oct));
            const double dt = f / c.sr;
            float v = 0;
            switch (wave)
            {
                case 0: v = (float) std::sin (2.0 * kPi * phase); break;
                case 1: v = (float) (4.0 * std::abs (phase - 0.5) - 1.0); break;
                case 2: v = (float) (2.0 * phase - 1.0) - polyBlep (phase, dt); break;
                default:
                {
                    v = phase < 0.5 ? 1.0f : -1.0f;
                    v += polyBlep (phase, dt);
                    double t2 = phase + 0.5; if (t2 >= 1.0) t2 -= 1.0;
                    v -= polyBlep (t2, dt);
                    break;
                }
            }
            out[0][(size_t) i] = v * level;
            phase += dt;
            if (phase >= 1.0) phase -= std::floor (phase);
        }
    }
    double phase = 0;
    bool lastSync = false;
};

struct Noise : Module
{
    static const Info& desc()
    {
        static const Info i { "noise", "Noise", "Sources", "Hiss for snares, hats and claps",
            {}, { { "out", "Out" } },
            { { "colour", "Colour", 0, 1, 1, Fmt::Percent }, { "level", "Level", 0, 1, 0.8f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float level = P (1);
        // Colour 100% is full bright hiss; lower rolls the top off
        const float hz = 200.0f * std::pow (100.0f, clamp01 (P (0)));
        const float a = 1.0f - std::exp (-2.0f * (float) kPi * std::min (hz, (float) c.sr * 0.45f) / (float) c.sr);
        for (int i = 0; i < n; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float white = (float) (int32_t) seed / 2147483648.0f;
            lp += a * (white - lp);
            out[0][(size_t) i] = (P (0) >= 0.999f ? white : lp * 1.6f) * level;
        }
    }
    uint32_t seed = 22222;
    float lp = 0;
};

// ---------------------------------------------------------------- Shapers
// Hit envelope: jumps up on a trigger and falls away. The shape of every drum.
struct HitEnv : Module
{
    static const Info& desc()
    {
        static const Info i { "hitenv", "Hit envelope", "Shapers", "Jumps up on each hit, then falls away",
            { { "trig", "Trig" }, { "vel", "Vel" } }, { { "out", "Out" } },
            { { "attack", "Attack", 0, 0.05f, 0.0f, Fmt::Time, 2 }, { "decay", "Decay", 0.005f, 4.0f, 0.25f, Fmt::Time, 3 } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float att = P (0);
        const float dec = std::max (0.002f, P (1));
        const float coef = std::exp (-6.9078f / (dec * (float) c.sr));   // falls to silence over "decay"
        const float up = att <= 0.0002f ? 1.0f : 1.0f / (att * (float) c.sr);
        for (int i = 0; i < n; ++i)
        {
            const bool g = in[0][(size_t) i] > 0.5f;
            if (g && ! last) { rising = true; amp = inOn[1] ? in[1][(size_t) i] : 1.0f; }
            last = g;
            if (rising) { v += up; if (v >= 1.0f) { v = 1.0f; rising = false; } }
            else v *= coef;
            if (v < 1.0e-5f) v = 0.0f;
            out[0][(size_t) i] = v * amp;
        }
    }
    bool last = false, rising = false;
    float v = 0, amp = 1;
};

// Envelope for held notes: attack, decay, sustain, release
struct Adsr : Module
{
    static const Info& desc()
    {
        static const Info i { "adsr", "Envelope", "Shapers", "Shapes a held note: in, hold, out",
            { { "gate", "Gate" }, { "vel", "Vel" } }, { { "out", "Out" } },
            { { "attack", "Attack", 0.001f, 4, 0.005f, Fmt::Time, 3 }, { "decay", "Decay", 0.005f, 4, 0.2f, Fmt::Time, 3 },
              { "sustain", "Sustain", 0, 1, 0.6f, Fmt::Percent }, { "release", "Release", 0.005f, 6, 0.15f, Fmt::Time, 3 } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float sr = (float) c.sr;
        const float up = 1.0f / (std::max (0.001f, P (0)) * sr);
        const float dCoef = std::exp (-4.6f / (std::max (0.005f, P (1)) * sr));
        const float sus = P (2);
        const float rCoef = std::exp (-4.6f / (std::max (0.005f, P (3)) * sr));
        for (int i = 0; i < n; ++i)
        {
            const bool g = in[0][(size_t) i] > 0.5f;
            if (g && ! last) { stage = 1; amp = inOn[1] ? in[1][(size_t) i] : 1.0f; }
            if (! g && last) stage = 3;
            last = g;
            if (stage == 1) { v += up; if (v >= 1.0f) { v = 1.0f; stage = 2; } }
            else if (stage == 2) v = sus + (v - sus) * dCoef;
            else if (stage == 3) { v *= rCoef; if (v < 1.0e-5f) { v = 0; stage = 0; } }
            out[0][(size_t) i] = v * amp;
        }
    }
    bool last = false;
    int stage = 0;
    float v = 0, amp = 1;
};

struct Filter : Module
{
    static const Info& desc()
    {
        static const Info i { "filter", "Filter", "Filters", "Removes highs, lows or both",
            { { "in", "In" }, { "cutoff", "Cutoff" } }, { { "out", "Out" } },
            { { "type", "Type", 0, 2, 0, Fmt::Int, 1, { "Low-pass", "High-pass", "Band-pass" } },
              { "cutoff", "Cutoff", 0, 1, 0.7f, Fmt::Freq },
              { "res", "Reso", 0, 1, 0.15f, Fmt::Percent },
              { "modamt", "Mod amt", -6, 6, 0, Fmt::Octaves } } };
        return i;
    }
    void setCoefs (float hz, float res, double sr)
    {
        g = (float) std::tan (kPi * std::min ((double) hz, sr * 0.45) / sr);
        k = 2.0f - 1.9f * clamp01 (res);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void process (int n, const Ctx& c) override
    {
        const int type = (int) std::lround (P (0));
        const float cut = P (1), res = P (2), modAmt = P (3);
        const bool mod = inOn[1] && std::abs (modAmt) > 0.0001f;
        if (! mod) setCoefs (cutoffHz (cut), res, c.sr);
        for (int i = 0; i < n; ++i)
        {
            if (mod && (tick++ & 7) == 0)
                setCoefs (std::min (20000.0f, std::max (20.0f, cutoffHz (cut) * std::pow (2.0f, in[1][(size_t) i] * modAmt))), res, c.sr);
            const float v0 = in[0][(size_t) i];
            const float v3 = v0 - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
            if (! std::isfinite (ic1) || ! std::isfinite (ic2)) { ic1 = ic2 = 0; }
            out[0][(size_t) i] = type == 0 ? v2 : type == 1 ? v0 - k * v1 - v2 : v1;
        }
    }
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    unsigned tick = 0;
};

// ---------------------------------------------------------------- Effects
struct Drive : Module
{
    static const Info& desc()
    {
        static const Info i { "drive", "Soft saturation", "Distortion", "Warmth and grit",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "drive", "Drive", 0, 1, 0.3f, Fmt::Percent }, { "level", "Level", 0, 1.5f, 1.0f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        const float d = clamp01 (P (0)), level = P (1);
        const float gain = 1.0f + d * 19.0f, norm = 1.0f / std::tanh (gain), mix = std::min (1.0f, d * 8.0f);
        for (int i = 0; i < n; ++i)
        {
            const float x = in[0][(size_t) i];
            out[0][(size_t) i] = (x + (std::tanh (x * gain) * norm - x) * mix) * level;
        }
    }
};

// ---------------------------------------------------------------- Utilities
struct Vca : Module
{
    static const Info& desc()
    {
        static const Info i { "vca", "VCA", "Utilities", "A volume control a signal can move",
            { { "in", "In" }, { "cv", "Control" } }, { { "out", "Out" } },
            { { "level", "Level", 0, 2, 1, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        const float level = P (0);
        for (int i = 0; i < n; ++i)
            out[0][(size_t) i] = in[0][(size_t) i] * level * (inOn[1] ? std::max (0.0f, in[1][(size_t) i]) : 1.0f);
    }
};

struct Mixer4 : Module
{
    static const Info& desc()
    {
        static const Info i { "mixer", "Mixer", "Utilities", "Blends up to four signals into one",
            { { "in1", "1" }, { "in2", "2" }, { "in3", "3" }, { "in4", "4" } }, { { "out", "Out" } },
            { { "l1", "Level 1", 0, 1.5f, 0.8f, Fmt::Percent }, { "l2", "Level 2", 0, 1.5f, 0.8f, Fmt::Percent },
              { "l3", "Level 3", 0, 1.5f, 0.8f, Fmt::Percent }, { "l4", "Level 4", 0, 1.5f, 0.8f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        const float l[4] = { P (0), P (1), P (2), P (3) };
        for (int i = 0; i < n; ++i)
            out[0][(size_t) i] = in[0][(size_t) i] * l[0] + in[1][(size_t) i] * l[1] + in[2][(size_t) i] * l[2] + in[3][(size_t) i] * l[3];
    }
};

struct Atten : Module
{
    static const Info& desc()
    {
        static const Info i { "atten", "Attenuator", "Utilities", "Turns a signal down, flips it, or shifts it",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "amount", "Amount", -1, 1, 0.5f, Fmt::Bipolar }, { "offset", "Offset", -1, 1, 0, Fmt::Bipolar } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        const float a = P (0), o = P (1);
        for (int i = 0; i < n; ++i) out[0][(size_t) i] = in[0][(size_t) i] * a + o;
    }
};

// ---------------------------------------------------------------- Movers
// Lengths in quarter notes: the LFO always moves in time with the song
inline const double kLfoLen[8] = { 16, 8, 4, 2, 1, 0.5, 0.25, 0.125 };

struct Lfo : Module
{
    static const Info& desc()
    {
        static const Info i { "lfo", "LFO", "Movers", "A slow wave, in time, for moving a knob",
            {}, { { "out", "Out" } },
            { { "rate", "Rate", 0, 7, 2, Fmt::Int, 1, { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32" } },
              { "shape", "Shape", 0, 4, 0, Fmt::Int, 1, { "Sine", "Triangle", "Ramp", "Square", "Random" } },
              { "depth", "Depth", 0, 1, 1, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const double len = kLfoLen[std::min (7, std::max (0, (int) std::lround (P (0))))];
        const int shape = (int) std::lround (P (1));
        const float depth = P (2);
        for (int i = 0; i < n; ++i)
        {
            double ph;
            if (c.playing) { const double p = (c.ppq + i * c.ppqPerSample) / len; ph = p - std::floor (p); freePhase = ph; }
            else { freePhase += c.ppqPerSample / len; freePhase -= std::floor (freePhase); ph = freePhase; }
            if (ph < lastPhase) { seed = seed * 1664525u + 1013904223u; held = (float) (int32_t) seed / 2147483648.0f; }
            lastPhase = ph;
            float v = 0;
            switch (shape)
            {
                case 0: v = (float) std::sin (2.0 * kPi * ph); break;
                case 1: v = (float) (1.0 - 4.0 * std::abs (ph - 0.5)); break;
                case 2: v = (float) (2.0 * ph - 1.0); break;
                case 3: v = ph < 0.5 ? 1.0f : -1.0f; break;
                default: v = held; break;
            }
            out[0][(size_t) i] = v * depth;
        }
    }
    double freePhase = 0, lastPhase = 0;
    uint32_t seed = 777;
    float held = 0;
};

// ---------------------------------------------------------------- Notes and rhythm
inline long stepAt (double ppq) { return (long) std::floor (ppq * 4.0 + 1.0e-9); }
inline int wrapStep (long s, int len) { return (int) (((s % len) + len) % len); }

// How long a pattern is before it loops: whole bars, with the last bar cut to "Steps" (16 = a full bar)
inline int loopSteps (float stepsParam, float barsParam)
{
    const int bars = 1 << std::min (3, std::max (0, (int) std::lround (barsParam)));
    return (bars - 1) * kSteps + std::min (kSteps, std::max (1, (int) std::lround (stepsParam)));
}

// How hard a drum step hits, from the number stored for it. Anything above 0.5 is a hit. 1 is the plain hit
// (0.72) and 2 the accent (1.0), exactly as they always were; in between and below 1 the hit is softer or harder.
inline float drumVel (float d)
{
    if (d >= 2.0f) return 1.0f;
    if (d == 1.0f) return 0.72f;
    if (d > 1.0f) return 0.72f + 0.28f * (d - 1.0f);
    return 0.10f + 0.62f * (std::max (0.5f, d) - 0.5f) * 2.0f;
}
// ...and the number to store for a hit this hard (0.1 to 1)
inline float drumData (float vel)
{
    vel = std::min (1.0f, std::max (0.1f, vel));
    const float d = vel >= 0.72f ? 1.0f + (vel - 0.72f) / 0.28f : 0.5f + (vel - 0.10f) / 0.62f * 0.5f;
    // kept to two decimal places, so a save holds it exactly and the plain hit and the accent are easy to land on
    return std::min (2.0f, std::max (0.51f, std::round (d * 100.0f) / 100.0f));
}
// The same as a MIDI velocity: the plain hit is 92 and the accent 127, as before
inline int drumMidiVel (float d)
{
    const float v = drumVel (d);
    return std::min (127, std::max (1, (int) std::lround (v >= 0.72f ? 92.0f + (v - 0.72f) / 0.28f * 35.0f : v / 0.72f * 92.0f)));
}

// Drum sequencer: up to 8 bars of 16 steps. data[step] = 0 off, 1 hit, 2 accent; other values above 0.5 are
// hits of other strengths (see drumVel).
struct DrumSeq : Module
{
    static const Info& desc()
    {
        static const Info i { "drumseq", "Drum sequencer", "Notes and rhythm", "Up to 8 bars of hits and accents",
            {}, { { "trig", "Trig" }, { "vel", "Vel" } },
            { { "length", "Steps", 1, 16, 16, Fmt::Int }, { "bars", "Bars", 0, 3, 0, Fmt::Int, 1, { "1 bar", "2 bars", "4 bars", "8 bars" } } }, Seq::Drum };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const int len = loopSteps (P (0), P (1));
        const int gateLen = std::max (8, (int) (0.5 * 0.25 / std::max (1.0e-9, c.ppqPerSample)));   // half a step
        for (int i = 0; i < n; ++i)
        {
            if (c.playing)
            {
                const long s = stepAt (c.ppq + i * c.ppqPerSample);
                if (s != last)
                {
                    last = s;
                    const int k = wrapStep (s, len);
                    playStep.store (k);
                    const float d = data[(size_t) k].load (std::memory_order_relaxed);
                    if (d > 0.5f) { left = gateLen; vel = drumVel (d); }
                }
            }
            else if (last != noStep) { last = noStep; left = 0; playStep.store (-1); }
            out[0][(size_t) i] = left > 0 ? 1.0f : 0.0f;
            out[1][(size_t) i] = vel;
            if (left > 0) --left;
        }
    }
    static constexpr long noStep = -1000000000L;
    long last = noStep;
    int left = 0;
    float vel = 0.72f;
};

// For a sequencer that follows another: how far the leader's line has moved from the key note at this step,
// in steps of the key. The leader's most recent note counts, so the follower holds through the leader's rests.
// Folded to within three steps either way, so an octave jump in the leader doesn't throw the follower an octave.
inline int leaderShift (const Module& leader, long step)
{
    const int li = leader.paramIndex ("length"), bi = leader.paramIndex ("bars");
    if (li < 0 || bi < 0) return 0;
    const int len = loopSteps (leader.P (li), leader.P (bi));
    const int at = wrapStep (step, len);
    for (int back = 0; back < len; ++back)
    {
        const int k = wrapStep (at - back, len);
        if (leader.data[(size_t) k].load (std::memory_order_relaxed) > 0.5f)
        {
            int d = (int) std::lround (leader.data[(size_t) (kMaxSteps + k)].load (std::memory_order_relaxed));
            d = ((d % 7) + 7) % 7;
            return d > 3 ? d - 7 : d;
        }
    }
    return 0;
}

// A note's own length, in steps, as it will sound: 0 if it has none (the Length knob sets it). A note with its
// own length is cut where the next note of the pattern starts, so one line never plays two notes at once.
inline int ownNoteSteps (const Module& m, int k, int len)
{
    const int want = std::min (len, (int) std::lround (m.data[(size_t) (kLenRow + k)].load (std::memory_order_relaxed)));
    if (want <= 0) return 0;
    for (int j = 1; j < want; ++j)
        if (m.data[(size_t) wrapStep (k + j, len)].load (std::memory_order_relaxed) > 0.5f) return j;
    return want;
}
// How long such a note's gate is up, in steps: a little short of its length, so the next note starts afresh
inline double ownNoteGate (int steps) { return (double) steps - 0.04; }

// Note sequencer: up to 8 bars. First row of data = note on/off, second row = which note of the key, third row =
// the note's own length in steps (0 = none: the Length knob sets it, as it always did).
// Notes are stored as scale steps, so changing the key moves every pattern with it.
// With Follow on, the steps count from the chord playing now, so the line moves with the chords.
struct NoteSeq : Module
{
    static const Info& desc()
    {
        static const Info i { "noteseq", "Note sequencer", "Notes and rhythm", "Up to 8 bars of notes, always in key",
            {}, { { "pitch", "Pitch" }, { "gate", "Gate" } },
            { { "octave", "Octave", -4, 3, -1, Fmt::Int }, { "gatelen", "Length", 0.1f, 1, 0.55f, Fmt::Percent },
              { "length", "Steps", 1, 16, 16, Fmt::Int }, { "bars", "Bars", 0, 3, 0, Fmt::Int, 1, { "1 bar", "2 bars", "4 bars", "8 bars" } },
              { "follow", "Chords", 0, 1, 0, Fmt::Int, 1, { "Ignore", "Follow" } } }, Seq::Note };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const int len = loopSteps (P (2), P (3));
        const bool follow = P (4) > 0.5f;
        const double stepSamples = 0.25 / std::max (1.0e-9, c.ppqPerSample);
        const float gl = P (1);
        // 100% ties into the next note; anything less leaves a gap so the envelope re-fires
        const int gateLen = gl >= 0.995f ? (int) (stepSamples * 1.05) : std::max (8, (int) (stepSamples * std::min (0.96f, gl)));
        for (int i = 0; i < n; ++i)
        {
            if (c.playing)
            {
                const long s = stepAt (c.ppq + i * c.ppqPerSample);
                if (s != last)
                {
                    last = s;
                    const int k = wrapStep (s, len);
                    playStep.store (k);
                    if (data[(size_t) k].load (std::memory_order_relaxed) > 0.5f)
                    {
                        int deg = (int) std::lround (data[(size_t) (kMaxSteps + k)].load (std::memory_order_relaxed));
                        int lift = 0;
                        if (follow && c.chords != nullptr)
                        {
                            const int ci = c.chords->indexAt (s);
                            if (ci >= 0) { deg += c.chords->slots[(size_t) ci].deg; lift = c.chords->slots[(size_t) ci].tr; }
                        }
                        // Linked as a follower: keep this pattern, and move it to match the leader's line
                        if (patternShare == 4 && patternLink > 0 && c.leaders[patternLink] != nullptr && c.leaders[patternLink] != this)
                            deg += leaderShift (*c.leaders[patternLink], s);
                        const int semis = c.root + degreeToSemis (deg, c.scale) + lift + 12 * (int) std::lround (P (0));
                        pitch = (float) semis / 12.0f;
                        const int own = ownNoteSteps (*this, k, len);
                        left = own > 0 ? std::max (8, (int) (stepSamples * ownNoteGate (own))) : gateLen;
                    }
                }
            }
            else if (last != DrumSeq::noStep) { last = DrumSeq::noStep; left = 0; playStep.store (-1); }
            out[0][(size_t) i] = pitch;
            out[1][(size_t) i] = left > 0 ? 1.0f : 0.0f;
            if (left > 0) --left;
        }
    }
    long last = DrumSeq::noStep;
    int left = 0;
    float pitch = 0;
};

// MIDI keyboard: notes from a keyboard, or from a MIDI track in the host. Pitch is the key pressed last (for a
// bass or lead); Note 1 to 4 are the keys held, lowest first (for chords). The gate dips for a moment on each new
// key so envelopes start again.
struct KeysIn : Module
{
    static const Info& desc()
    {
        static const Info i { "keys", "MIDI keyboard", "Notes and rhythm", "Notes from your keyboard or a MIDI track",
            {}, { { "pitch", "Pitch" }, { "gate", "Gate" }, { "vel", "Vel" }, { "n1", "Note 1" }, { "n2", "Note 2" }, { "n3", "Note 3" }, { "n4", "Note 4" }, { "mod", "Mod wheel" } },
            { { "channel", "Channel", 0, 16, 0, Fmt::Int, 1, { "All", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" } },
              { "octave", "Octave", -3, 3, 0, Fmt::Int },
              { "snap", "Notes", 0, 1, 1, Fmt::Int, 1, { "As played", "Stay in key" } },
              { "bend", "Bend", 0, 12, 2, Fmt::Semis } } };
        return i;
    }
    // The nearest note of the key (the lower one when it falls between two)
    static int inKey (int midi, int root, const int* scale)
    {
        if (scale == nullptr) return midi;
        for (int d = 0; d < 7; ++d)
            for (int sign : { -1, 1 })
            {
                const int pc = (((midi + sign * d - root) % 12) + 12) % 12;
                for (int k = 0; k < 7; ++k) if (scale[k] == pc) return midi + sign * d;
            }
        return midi;
    }
    void process (int n, const Ctx& c) override
    {
        const int channel = (int) std::lround (P (0));
        const float oct = std::round (P (1));
        const bool snap = P (2) > 0.5f;
        int notes[Keys::kMax], count = 0, newest = -1, newestVel = 100; uint32_t newestOrder = 0;
        uint64_t lo = 0, hi = 0;          // the keys held on this unit's channel, for the screen's keyboard
        if (c.keys != nullptr)
            for (int i = 0; i < c.keys->count; ++i)
            {
                const auto& h = c.keys->held[i];
                if (channel != 0 && h.channel != channel) continue;
                const int note = snap ? inKey (h.note, c.root, c.scale) : h.note;
                (h.note < 64 ? lo : hi) |= (uint64_t) 1 << (h.note & 63);
                notes[count++] = note;
                if (h.order >= newestOrder) { newestOrder = h.order; newest = note; newestVel = h.vel; }
            }
        heldLo.store (lo, std::memory_order_relaxed); heldHi.store (hi, std::memory_order_relaxed);
        std::sort (notes, notes + count);
        const bool retrigger = count > 0 && wasHeld && newestOrder != lastOrder;
        if (count > 0)
        {
            pitch = (float) (newest - 60) / 12.0f + oct;
            vel = (float) newestVel / 127.0f;
            for (int k = 0; k < 4; ++k) voice[k] = (float) (notes[std::min (k, count - 1)] - 60) / 12.0f + oct;
            lastOrder = newestOrder;
        }
        wasHeld = count > 0;
        // The pitch wheel bends every note by up to the Bend knob's semitones; smoothed so it doesn't click
        const float bendTo = c.keys != nullptr ? c.keys->bend * std::round (P (3)) / 12.0f : 0.0f;
        const float modTo = c.keys != nullptr ? c.keys->mod : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            bend += 0.002f * (bendTo - bend); if (std::abs (bend - bendTo) < 1.0e-5f) bend = bendTo;
            mod += 0.002f * (modTo - mod); if (std::abs (mod - modTo) < 1.0e-5f) mod = modTo;
            out[0][(size_t) i] = pitch + bend;
            out[1][(size_t) i] = count > 0 && ! (retrigger && i == 0) ? 1.0f : 0.0f;
            out[2][(size_t) i] = vel;
            for (int k = 0; k < 4; ++k) out[(size_t) (3 + k)][(size_t) i] = voice[k] + bend;
            out[7][(size_t) i] = mod;
        }
    }
    // The keys this unit saw held the last time it ran (one bit per MIDI note), as plain numbers the screen can
    // read at any moment without touching the audio thread's own list
    std::atomic<uint64_t> heldLo { 0 }, heldHi { 0 };
    bool isHeld (int note) const { return note >= 0 && note < 128 && (((note < 64 ? heldLo : heldHi).load (std::memory_order_relaxed) >> (note & 63)) & 1u) != 0; }
    float pitch = 0, vel = 0.8f, voice[4] { 0, 0, 0, 0 }, bend = 0, mod = 0;
    uint32_t lastOrder = 0; bool wasHeld = false;
};

// Chord generator: plays the song's chord progression (shared by every tab, like the key).
// Sends the chord out as a bass note and four voices, with a gate in the chosen rhythm.
struct ChordGen : Module
{
    static const Info& desc()
    {
        static const Info i { "chords", "Chord generator", "Notes and rhythm", "Plays the song's chords, always in key",
            {}, { { "bass", "Bass" }, { "v1", "Note 1" }, { "v2", "Note 2" }, { "v3", "Note 3" }, { "v4", "Note 4" }, { "gate", "Gate" } },
            { { "pattern", "Rhythm", 0, 6, 0, Fmt::Int, 1, { "Hold", "Rave stab", "8th pulse", "16th pulse", "Off-beat", "Syncopated", "Two hits" } },
              { "octave", "Octave", -2, 2, 0, Fmt::Int } }, Seq::Chord };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const auto& pats = n2c::patterns();
        const auto& pat = pats[(size_t) std::min ((int) pats.size() - 1, std::max (0, (int) std::lround (P (0))))].steps;
        const float oct = std::round (P (1));
        const double stepSamples = 0.25 / std::max (1.0e-9, c.ppqPerSample);
        for (int i = 0; i < n; ++i)
        {
            if (c.playing && c.chords != nullptr)
            {
                const long s = stepAt (c.ppq + i * c.ppqPerSample);
                if (s != last)
                {
                    last = s;
                    const int ci = c.chords->indexAt (s);
                    if (ci >= 0)
                    {
                        const auto& slot = c.chords->slots[(size_t) ci];
                        bass = slot.bass;
                        for (int k = 0; k < 4; ++k) voice[k] = slot.voice[k] + oct;
                        playStep.store (ci);
                        const int inChord = (int) (((s % c.chords->total) + c.chords->total) % c.chords->total) - slot.start;
                        if (pat.empty())
                        {
                            // Hold: the gate stays up for the whole chord, with a short gap at each change so envelopes restart
                            left = inChord == slot.len - 1 ? (int) (stepSamples * 0.9) : (int) (stepSamples * 1.05);
                        }
                        else
                        {
                            const double g = pat[(size_t) (wrapStep (s, kSteps) % (int) pat.size())];
                            if (g > 0) left = std::max (8, (int) (stepSamples * std::min (g, (double) (slot.len - inChord)) * 0.92));
                        }
                    }
                }
            }
            else if (last != DrumSeq::noStep) { last = DrumSeq::noStep; left = 0; playStep.store (-1); }
            out[0][(size_t) i] = bass;
            for (int k = 0; k < 4; ++k) out[(size_t) (1 + k)][(size_t) i] = voice[k];
            out[5][(size_t) i] = left > 0 ? 1.0f : 0.0f;
            if (left > 0) --left;
        }
    }
    long last = DrumSeq::noStep;
    int left = 0;
    float bass = -2.0f, voice[4] { 0, 0.25f, 0.5833f, 1.0f };
};

// Multi-oscillator: four notes at once, each a pair of slightly detuned waves. For chords, pads and stabs.
struct MultiOsc : Module
{
    static const Info& desc()
    {
        static const Info i { "multiosc", "Multi-oscillator", "Sources", "Four notes at once, for chords and pads",
            { { "p1", "Note 1" }, { "p2", "Note 2" }, { "p3", "Note 3" }, { "p4", "Note 4" } }, { { "out", "Out" } },
            { { "wave", "Wave", 0, 2, 0, Fmt::Int, 1, { "Saw", "Square", "Triangle" } },
              { "detune", "Detune", 0, 50, 12, Fmt::Cents },
              { "coarse", "Tune", -24, 24, 0, Fmt::Semis },
              { "level", "Level", 0, 1, 0.7f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const int wave = (int) std::lround (P (0));
        const double det = (double) P (1) / 1200.0, base = (double) P (2) / 12.0;
        int active = 0;
        for (int v = 0; v < 4; ++v) active += inOn[(size_t) v] ? 1 : 0;
        // Zero means zero: a note input with nothing plugged in makes no sound
        const float level = active > 0 ? P (3) * 0.5f / std::sqrt ((float) active) : 0.0f;
        std::fill (out[0].begin(), out[0].begin() + n, 0.0f);
        for (int v = 0; v < 4; ++v)
        {
            if (! inOn[(size_t) v]) continue;
            for (int half = 0; half < 2; ++half)
            {
                double& ph = phase[v][half];
                const double off = base + (half == 0 ? -det : det);
                for (int i = 0; i < n; ++i)
                {
                    const double f = std::min (c.sr * 0.45, 261.6255653 * std::pow (2.0, (double) in[(size_t) v][(size_t) i] + off));
                    const double dt = f / c.sr;
                    float x;
                    if (wave == 0) x = (float) (2.0 * ph - 1.0) - polyBlep (ph, dt);
                    else if (wave == 1)
                    {
                        x = ph < 0.5 ? 1.0f : -1.0f;
                        x += polyBlep (ph, dt);
                        double t2 = ph + 0.5; if (t2 >= 1.0) t2 -= 1.0;
                        x -= polyBlep (t2, dt);
                    }
                    else x = (float) (4.0 * std::abs (ph - 0.5) - 1.0);
                    out[0][(size_t) i] += x * level;
                    ph += dt;
                    if (ph >= 1.0) ph -= std::floor (ph);
                }
            }
        }
    }
    double phase[4][2] { { 0.0, 0.37 }, { 0.61, 0.13 }, { 0.29, 0.83 }, { 0.47, 0.71 } };
};

// ================================================================ Effects
// Shared pieces. Two rules run through all of these:
//   - a Mix (or amount) at 0% passes the sound through exactly untouched;
//   - silence in gives silence out: no effect makes noise of its own.

// A length of remembered sound, for echoes, reverbs and anything that needs "what came a moment ago"
struct DelayBuf
{
    void size (int n) { b.assign ((size_t) std::max (8, n), 0.0f); w = 0; }
    void push (float x) { if (b.empty()) return; b[(size_t) w] = std::isfinite (x) ? x : 0.0f; if (++w >= (int) b.size()) w = 0; }
    float tapInt (int d) const
    {
        if (b.empty()) return 0.0f;
        d = std::min ((int) b.size() - 1, std::max (1, d));
        int i = w - d; if (i < 0) i += (int) b.size();
        return b[(size_t) i];
    }
    float tap (float d) const       // d samples back, between samples
    {
        if (b.empty()) return 0.0f;
        d = std::min ((float) b.size() - 2.0f, std::max (1.0f, d));
        float p = (float) w - d; if (p < 0) p += (float) b.size();
        int i = (int) p; const float f = p - (float) i;
        if (i >= (int) b.size()) i -= (int) b.size();       // rounding can land exactly on the end: wrap, never read past it
        const int j = i + 1 >= (int) b.size() ? 0 : i + 1;
        return b[(size_t) i] + (b[(size_t) j] - b[(size_t) i]) * f;
    }
    std::vector<float> b; int w = 0;
};

// A slow sweep in time with the song, for chorus, phaser and flanger. Lengths in quarter notes.
inline const double kSweepLen[7] = { 32, 16, 8, 4, 2, 1, 0.5 };
#define N2M_SWEEP_CHOICES { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8" }
struct Sweep
{
    // 0..1 position in the cycle at sample i
    double at (const Ctx& c, int i, float rateParam)
    {
        const double len = kSweepLen[std::min (6, std::max (0, (int) std::lround (rateParam)))];
        if (c.playing) { const double p = (c.ppq + i * c.ppqPerSample) / len; phase = p - std::floor (p); }
        else { phase += c.ppqPerSample / len; phase -= std::floor (phase); }
        return phase;
    }
    double phase = 0;
};

// Echo times as note lengths (in quarter notes), so every echo is in time and follows the tempo
inline const double kEchoLen[10] = { 0.125, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 };
#define N2M_ECHO_CHOICES { "1/32", "1/16", "1/16 dot", "1/8 trip", "1/8", "1/8 dot", "1/4", "1/4 dot", "1/2", "1 bar" }
inline float echoSamples (const Ctx& c, float timeParam, const DelayBuf& buf)
{
    const double q = kEchoLen[std::min (9, std::max (0, (int) std::lround (timeParam)))];
    return (float) std::min ((double) buf.b.size() - 4.0, std::max (2.0, q * 60.0 / std::max (20.0, c.bpm) * c.sr));
}

// The standard EQ building block
struct Biquad
{
    float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; if (! std::isfinite (z1) || ! std::isfinite (z2)) z1 = z2 = 0; return y; }
    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); }
    static double w0 (double hz, double sr) { return 2.0 * kPi * std::min (sr * 0.45, std::max (10.0, hz)) / sr; }
    void peak (double hz, double db, double q, double sr)
    {
        const double A = std::pow (10.0, db / 40.0), w = w0 (hz, sr), al = std::sin (w) / (2.0 * q), cs = std::cos (w);
        set (1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
    }
    void lowShelf (double hz, double db, double sr)
    {
        const double A = std::pow (10.0, db / 40.0), w = w0 (hz, sr), cs = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0), r = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * cs + r), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - r),
             (A + 1) + (A - 1) * cs + r, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - r);
    }
    void highShelf (double hz, double db, double sr)
    {
        const double A = std::pow (10.0, db / 40.0), w = w0 (hz, sr), cs = std::cos (w), al = std::sin (w) / 2.0 * std::sqrt (2.0), r = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * cs + r), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - r),
             (A + 1) - (A - 1) * cs + r, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - r);
    }
    void lowPass (double hz, double sr)
    {
        const double w = w0 (hz, sr), cs = std::cos (w), al = std::sin (w) / (2.0 * 0.7071);
        set ((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
    }
    void highPass (double hz, double sr)
    {
        const double w = w0 (hz, sr), cs = std::cos (w), al = std::sin (w) / (2.0 * 0.7071);
        set ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
    }
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// ---------------------------------------------------------------- Reverb
// One design, three characters. Room: small and tight. Hall: large and smooth, with a moment before it blooms.
// Plate: bright and dense. Stereo in and out; with only In L plugged, both sides are fed from it.
enum { kRoom, kHall, kPlate };
template <int Kind>
struct Reverb : Module
{
    static const Info& desc()
    {
        static const char* const type[3] = { "room", "hall", "plate" };
        static const char* const name[3] = { "Room reverb", "Hall reverb", "Plate reverb" };
        static const char* const hint[3] = { "Small and tight. Good on drums", "Large and smooth, for pads and space", "Bright and dense. For snares and stabs" };
        static const float decay[3] = { 0.35f, 0.6f, 0.5f }, damp[3] = { 0.45f, 0.4f, 0.15f };
        static const Info i { type[Kind], name[Kind], "Reverb", hint[Kind],
            { { "inl", "In L" }, { "inr", "In R" } }, { { "outl", "Out L" }, { "outr", "Out R" } },
            { { "size", "Size", 0, 1, 0.5f, Fmt::Percent }, { "decay", "Decay", 0, 1, decay[Kind], Fmt::Percent },
              { "damp", "Damping", 0, 1, damp[Kind], Fmt::Percent }, { "mix", "Mix", 0, 1, 0.3f, Fmt::Percent } } };
        return i;
    }
    static constexpr int nComb = 8, nAll = 4;
    void prepare (double sampleRate) override
    {
        sr = sampleRate;
        const int big = (int) (0.12 * sr * 2.4) + 64;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& c : comb[ch]) c.size (big);
            for (auto& a : all[ch]) a.size ((int) (0.03 * sr) + 64);
            pre[ch].size ((int) (0.06 * sr) + 64);
            std::fill (std::begin (filt[ch]), std::end (filt[ch]), 0.0f);
        }
    }
    void process (int n, const Ctx&) override
    {
        static const int combLen[nComb] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };   // at 44.1 kHz
        static const int allLen[nAll] = { 556, 441, 341, 225 };
        static const float scale[3] = { 0.55f, 1.35f, 0.8f }, fbLo[3] = { 0.60f, 0.80f, 0.74f }, fbHi[3] = { 0.88f, 0.975f, 0.96f };
        static const int spread[3] = { 17, 31, 23 }, preMs[3] = { 0, 22, 4 };
        const float mix = P (3);
        const bool stereoIn = inOn[1] != 0;
        if (mix <= 0.0f)
        {
            // Mix at 0%: the dry sound, untouched. The tail still has to be kept running so it isn't stale later.
            for (int i = 0; i < n; ++i) { out[0][(size_t) i] = in[0][(size_t) i]; out[1][(size_t) i] = stereoIn ? in[1][(size_t) i] : in[0][(size_t) i]; }
        }
        const float sz = (0.6f + 0.8f * P (0)) * scale[Kind] * (float) (sr / 44100.0);
        const float fb = fbLo[Kind] + (fbHi[Kind] - fbLo[Kind]) * P (1);
        const float damp = std::min (0.95f, P (2) * 0.9f);
        const int preLen = std::max (1, (int) (preMs[Kind] * 0.001 * sr));
        for (int i = 0; i < n; ++i)
        {
            const float dry[2] = { in[0][(size_t) i], stereoIn ? in[1][(size_t) i] : in[0][(size_t) i] };
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = pre[ch].tapInt (preLen);
                pre[ch].push (dry[ch] * 0.03f);
                float sum = 0;
                for (int k = 0; k < nComb; ++k)
                {
                    const int len = (int) ((float) (combLen[k] + ch * spread[Kind]) * sz);
                    const float y = comb[ch][k].tapInt (len);
                    filt[ch][k] = y * (1.0f - damp) + filt[ch][k] * damp;
                    if (std::abs (filt[ch][k]) < 1.0e-15f) filt[ch][k] = 0.0f;
                    comb[ch][k].push (x + filt[ch][k] * fb);
                    sum += y;
                }
                for (int k = 0; k < nAll; ++k)
                {
                    const int len = (int) ((float) (allLen[k] + ch * spread[Kind]) * (Kind == kPlate ? 1.3f : 1.0f) * (float) (sr / 44100.0));
                    const float y = all[ch][k].tapInt (len);
                    all[ch][k].push (sum + y * 0.5f);
                    sum = y - sum;
                }
                if (mix > 0.0f) out[(size_t) ch][(size_t) i] = dry[ch] * (1.0f - mix) + sum * 2.2f * mix;
            }
        }
    }
    double sr = 48000;
    DelayBuf comb[2][nComb], all[2][nAll], pre[2];
    float filt[2][nComb] {};
};

// ---------------------------------------------------------------- Delay and echo
struct DigitalDelay : Module
{
    static const Info& desc()
    {
        static const Info i { "delay", "Digital delay", "Delay and echo", "Clean echoes, in time with the song",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "time", "Time", 0, 9, 5, Fmt::Int, 1, N2M_ECHO_CHOICES }, { "feedback", "Repeats", 0, 0.95f, 0.4f, Fmt::Percent },
              { "mix", "Mix", 0, 1, 0.35f, Fmt::Percent } } };
        return i;
    }
    void prepare (double sr) override { buf.size ((int) (sr * 4.0) + 16); cur = 0; }
    void process (int n, const Ctx& c) override
    {
        const float target = echoSamples (c, P (0), buf), fb = P (1), mix = P (2);
        if (cur <= 0) cur = target;
        for (int i = 0; i < n; ++i)
        {
            cur += (target - cur) * 0.0008f;           // a tempo or time change glides, it doesn't click
            const float wet = buf.tap (cur);
            buf.push (in[0][(size_t) i] + wet * fb);
            out[0][(size_t) i] = mix > 0.0f ? in[0][(size_t) i] + wet * mix : in[0][(size_t) i];
        }
    }
    DelayBuf buf; float cur = 0;
};

// Tape echo: each repeat is duller and a little saturated, and the time wobbles like worn tape
struct TapeEcho : Module
{
    static const Info& desc()
    {
        static const Info i { "tape", "Tape echo", "Delay and echo", "Warm, wobbly echoes that fade duller",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "time", "Time", 0, 9, 5, Fmt::Int, 1, N2M_ECHO_CHOICES }, { "feedback", "Repeats", 0, 0.95f, 0.5f, Fmt::Percent },
              { "tone", "Tone", 0, 1, 0.5f, Fmt::Percent }, { "wobble", "Wobble", 0, 1, 0.3f, Fmt::Percent },
              { "mix", "Mix", 0, 1, 0.35f, Fmt::Percent } } };
        return i;
    }
    void prepare (double sr) override { buf.size ((int) (sr * 4.2) + 16); cur = 0; lp = 0; }
    void process (int n, const Ctx& c) override
    {
        const float target = echoSamples (c, P (0), buf), fb = P (1), mix = P (4);
        const float a = 1.0f - std::exp (-2.0f * (float) kPi * (600.0f * std::pow (20.0f, P (2))) / (float) c.sr);
        const float depth = P (3) * 0.004f;
        if (cur <= 0) cur = target;
        for (int i = 0; i < n; ++i)
        {
            cur += (target - cur) * 0.0004f;
            wow += 0.45 / c.sr; wow -= std::floor (wow);
            const float wet = buf.tap (cur * (1.0f + depth * (float) std::sin (2.0 * kPi * wow)));
            lp += a * (wet - lp);
            if (std::abs (lp) < 1.0e-15f) lp = 0.0f;
            buf.push (in[0][(size_t) i] + std::tanh (lp * fb * 1.2f));
            out[0][(size_t) i] = mix > 0.0f ? in[0][(size_t) i] + lp * mix : in[0][(size_t) i];
        }
    }
    DelayBuf buf; float cur = 0, lp = 0; double wow = 0;
};

// Ping-pong: the echoes bounce left, right, left
struct PingPong : Module
{
    static const Info& desc()
    {
        static const Info i { "pingpong", "Ping-pong delay", "Delay and echo", "Echoes that bounce from left to right",
            { { "in", "In" } }, { { "outl", "Out L" }, { "outr", "Out R" } },
            { { "time", "Time", 0, 9, 4, Fmt::Int, 1, N2M_ECHO_CHOICES }, { "feedback", "Repeats", 0, 0.95f, 0.5f, Fmt::Percent },
              { "mix", "Mix", 0, 1, 0.4f, Fmt::Percent } } };
        return i;
    }
    void prepare (double sr) override { l.size ((int) (sr * 4.0) + 16); r.size ((int) (sr * 4.0) + 16); cur = 0; }
    void process (int n, const Ctx& c) override
    {
        const float target = echoSamples (c, P (0), l), fb = P (1), mix = P (2);
        if (cur <= 0) cur = target;
        for (int i = 0; i < n; ++i)
        {
            cur += (target - cur) * 0.0008f;
            const float dl = l.tap (cur), dr = r.tap (cur);
            l.push (in[0][(size_t) i] + dr * fb);
            r.push (dl);
            out[0][(size_t) i] = mix > 0.0f ? in[0][(size_t) i] + dl * mix : in[0][(size_t) i];
            out[1][(size_t) i] = mix > 0.0f ? in[0][(size_t) i] + dr * mix : in[0][(size_t) i];
        }
    }
    DelayBuf l, r; float cur = 0;
};

// ---------------------------------------------------------------- Distortion
struct HardClip : Module
{
    static const Info& desc()
    {
        static const Info i { "hardclip", "Hard clip", "Distortion", "Aggressive, flat-topped distortion",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "drive", "Drive", 0, 1, 0.4f, Fmt::Percent }, { "level", "Level", 0, 1.5f, 1.0f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        const float d = clamp01 (P (0)), level = P (1), gain = 1.0f + d * 29.0f, mix = std::min (1.0f, d * 8.0f), trim = 1.0f / (1.0f + d * 0.6f);
        for (int i = 0; i < n; ++i)
        {
            const float x = in[0][(size_t) i];
            const float y = std::min (1.0f, std::max (-1.0f, x * gain)) * trim;
            out[0][(size_t) i] = (x + (y - x) * mix) * level;
        }
    }
};

struct Bitcrusher : Module
{
    static const Info& desc()
    {
        static const Info i { "bitcrush", "Bitcrusher", "Distortion", "Lo-fi digital grit",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "crush", "Crush", 0, 1, 0.5f, Fmt::Percent }, { "rate", "Downsample", 0, 1, 0.3f, Fmt::Percent },
              { "level", "Level", 0, 1.5f, 1.0f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        const float crush = clamp01 (P (0)), rate = clamp01 (P (1)), level = P (2);
        const float levels = std::pow (2.0f, 15.0f - crush * 13.0f);
        const int every = 1 + (int) (rate * 39.0f);
        for (int i = 0; i < n; ++i)
        {
            if (++count >= every) { count = 0; held = in[0][(size_t) i]; }
            float y = rate > 0.0f ? held : in[0][(size_t) i];
            if (crush > 0.0f) y = std::round (y * levels) / levels;
            out[0][(size_t) i] = y * level;
        }
    }
    int count = 0; float held = 0;
};

// ---------------------------------------------------------------- Movement
struct Chorus : Module
{
    static const Info& desc()
    {
        static const Info i { "chorus", "Chorus", "Movement", "Thickens a sound and spreads it wide",
            { { "in", "In" } }, { { "outl", "Out L" }, { "outr", "Out R" } },
            { { "rate", "Rate", 0, 6, 3, Fmt::Int, 1, N2M_SWEEP_CHOICES }, { "depth", "Depth", 0, 1, 0.5f, Fmt::Percent },
              { "mix", "Mix", 0, 1, 0.5f, Fmt::Percent } } };
        return i;
    }
    void prepare (double sr) override { buf.size ((int) (sr * 0.05) + 16); }
    void process (int n, const Ctx& c) override
    {
        const float depth = P (1), mix = P (2);
        const float base = 0.012f * (float) c.sr, swing = 0.006f * (float) c.sr * depth;
        for (int i = 0; i < n; ++i)
        {
            const double ph = sweep.at (c, i, P (0));
            const float x = in[0][(size_t) i];
            const float a = buf.tap (base + swing * (float) std::sin (2.0 * kPi * ph));
            const float b = buf.tap (base + swing * (float) std::sin (2.0 * kPi * (ph + 0.25)));
            buf.push (x);
            out[0][(size_t) i] = mix > 0.0f ? x * (1.0f - mix) + a * mix : x;
            out[1][(size_t) i] = mix > 0.0f ? x * (1.0f - mix) + b * mix : x;
        }
    }
    DelayBuf buf; Sweep sweep;
};

struct Flanger : Module
{
    static const Info& desc()
    {
        static const Info i { "flanger", "Flanger", "Movement", "The jet-plane sweep",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "rate", "Rate", 0, 6, 2, Fmt::Int, 1, N2M_SWEEP_CHOICES }, { "depth", "Depth", 0, 1, 0.7f, Fmt::Percent },
              { "feedback", "Feedback", 0, 0.9f, 0.5f, Fmt::Percent }, { "mix", "Mix", 0, 1, 0.5f, Fmt::Percent } } };
        return i;
    }
    void prepare (double sr) override { buf.size ((int) (sr * 0.02) + 16); }
    void process (int n, const Ctx& c) override
    {
        const float depth = P (1), fb = P (2), mix = P (3);
        for (int i = 0; i < n; ++i)
        {
            const double ph = sweep.at (c, i, P (0));
            const float x = in[0][(size_t) i];
            const float d = (0.0004f + 0.0055f * depth * (0.5f + 0.5f * (float) std::sin (2.0 * kPi * ph))) * (float) c.sr;
            const float wet = buf.tap (d);
            buf.push (x + wet * fb);
            out[0][(size_t) i] = mix > 0.0f ? x * (1.0f - mix) + wet * mix : x;
        }
    }
    DelayBuf buf; Sweep sweep;
};

struct Phaser : Module
{
    static const Info& desc()
    {
        static const Info i { "phaser", "Phaser", "Movement", "Slow, swirling notches",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "rate", "Rate", 0, 6, 2, Fmt::Int, 1, N2M_SWEEP_CHOICES }, { "depth", "Depth", 0, 1, 0.8f, Fmt::Percent },
              { "feedback", "Feedback", 0, 0.9f, 0.4f, Fmt::Percent }, { "mix", "Mix", 0, 1, 0.5f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float depth = P (1), fb = P (2), mix = P (3);
        for (int i = 0; i < n; ++i)
        {
            const double ph = sweep.at (c, i, P (0));
            const float x = in[0][(size_t) i];
            if ((tick++ & 15) == 0)
            {
                const float hz = 300.0f * std::pow (12.0f, depth * (0.5f + 0.5f * (float) std::sin (2.0 * kPi * ph)));
                const float g = (float) std::tan (kPi * std::min ((double) hz, c.sr * 0.45) / c.sr);
                a = (g - 1.0f) / (g + 1.0f);
            }
            float y = x + last * fb;
            for (auto& s : st) { const float o = a * y + s; s = y - a * o; if (std::abs (s) < 1.0e-15f) s = 0.0f; y = o; }
            last = std::isfinite (y) ? y : 0.0f;
            out[0][(size_t) i] = mix > 0.0f ? x * (1.0f - mix) + y * mix : x;
        }
    }
    float st[6] {}, a = 0, last = 0; unsigned tick = 0; Sweep sweep;
};

// ---------------------------------------------------------------- EQ
struct Eq3 : Module
{
    static const Info& desc()
    {
        static const Info i { "eq3", "3-band EQ", "EQ", "Quick low, mid and high",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "low", "Low", -15, 15, 0, Fmt::Db }, { "mid", "Mid", -15, 15, 0, Fmt::Db }, { "high", "High", -15, 15, 0, Fmt::Db } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float lo = P (0), mi = P (1), hi = P (2);
        if (lo == 0.0f && mi == 0.0f && hi == 0.0f) { std::copy (in[0].begin(), in[0].begin() + n, out[0].begin()); return; }   // flat: untouched
        b[0].lowShelf (200, lo, c.sr); b[1].peak (1000, mi, 0.7, c.sr); b[2].highShelf (5000, hi, c.sr);
        for (int i = 0; i < n; ++i) out[0][(size_t) i] = b[2].run (b[1].run (b[0].run (in[0][(size_t) i])));
    }
    Biquad b[3];
};

// Four bands; each row of knobs is one band: where, how much, how wide
struct ParamEq : Module
{
    static const Info& desc()
    {
        static const Info i { "parameq", "Parametric EQ", "EQ", "Four bands: where, how much, how wide",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "f1", "Freq 1", 0, 1, 0.233f, Fmt::Freq }, { "g1", "Gain 1", -18, 18, 0, Fmt::Db }, { "q1", "Width 1", 0.3f, 8, 1, Fmt::Plain, 2 },
              { "f2", "Freq 2", 0, 1, 0.466f, Fmt::Freq }, { "g2", "Gain 2", -18, 18, 0, Fmt::Db }, { "q2", "Width 2", 0.3f, 8, 1, Fmt::Plain, 2 },
              { "f3", "Freq 3", 0, 1, 0.667f, Fmt::Freq }, { "g3", "Gain 3", -18, 18, 0, Fmt::Db }, { "q3", "Width 3", 0.3f, 8, 1, Fmt::Plain, 2 },
              { "f4", "Freq 4", 0, 1, 0.867f, Fmt::Freq }, { "g4", "Gain 4", -18, 18, 0, Fmt::Db }, { "q4", "Width 4", 0.3f, 8, 1, Fmt::Plain, 2 } },
            Seq::None, 3 };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        bool flat = true;
        for (int k = 0; k < 4; ++k) flat = flat && P (k * 3 + 1) == 0.0f;
        if (flat) { std::copy (in[0].begin(), in[0].begin() + n, out[0].begin()); return; }
        // A wide band is a low Q, so the Width knob reads the natural way round
        for (int k = 0; k < 4; ++k) b[k].peak (cutoffHz (P (k * 3)), P (k * 3 + 1), 1.0 / std::max (0.1f, P (k * 3 + 2)) * 1.4, c.sr);
        for (int i = 0; i < n; ++i)
        {
            float y = in[0][(size_t) i];
            for (int k = 0; k < 4; ++k) if (P (k * 3 + 1) != 0.0f) y = b[k].run (y);
            out[0][(size_t) i] = y;
        }
    }
    Biquad b[4];
};

struct CutFilter : Module
{
    static const Info& desc()
    {
        static const Info i { "cuts", "Low cut / high cut", "EQ", "Clears rumble below and fizz above",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "lowcut", "Low cut", 0, 1, 0, Fmt::Freq }, { "highcut", "High cut", 0, 1, 1, Fmt::Freq } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const bool lowOn = P (0) > 0.001f, highOn = P (1) < 0.999f;
        if (lowOn) hp.highPass (cutoffHz (P (0)), c.sr);
        if (highOn) lp.lowPass (cutoffHz (P (1)), c.sr);
        for (int i = 0; i < n; ++i)
        {
            float y = in[0][(size_t) i];
            if (lowOn) y = hp.run (y);
            if (highOn) y = lp.run (y);
            out[0][(size_t) i] = y;
        }
    }
    Biquad hp, lp;
};

// ================================================================ Dynamics
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float timeCoef (float seconds, double sr) { return 1.0f - std::exp (-1.0f / (std::max (0.0002f, seconds) * (float) sr)); }

// Compressor: turns the loud parts down. Plug another sound into Side and that sound does the squeezing instead.
struct Compressor : Module
{
    static const Info& desc()
    {
        static const Info i { "comp", "Compressor", "Dynamics", "Evens out the level. Has a sidechain",
            { { "in", "In" }, { "side", "Side" } }, { { "out", "Out" } },
            { { "threshold", "Threshold", -48, 0, -18, Fmt::Db }, { "ratio", "Ratio", 1, 20, 4, Fmt::Plain, 2 },
              { "attack", "Attack", 0.0005f, 0.1f, 0.005f, Fmt::Time, 2 }, { "release", "Release", 0.02f, 1.0f, 0.15f, Fmt::Time, 2 },
              { "makeup", "Make-up", 0, 24, 0, Fmt::Db } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float thr = P (0), slope = 1.0f / std::max (1.0f, P (1)) - 1.0f, makeup = P (4);
        const float att = timeCoef (P (2), c.sr), rel = timeCoef (P (3), c.sr);
        for (int i = 0; i < n; ++i)
        {
            const float key = std::abs (inOn[1] ? in[1][(size_t) i] : in[0][(size_t) i]);
            env += (key > env ? att : rel) * (key - env);
            if (env < 1.0e-9f) env = 0.0f;
            const float over = 20.0f * std::log10 (env + 1.0e-9f) - thr;
            const float db = (over > 0.0f ? over * slope : 0.0f) + makeup;
            out[0][(size_t) i] = db == 0.0f ? in[0][(size_t) i] : in[0][(size_t) i] * dbToGain (db);   // ratio 1 and no make-up: untouched
        }
    }
    float env = 0;
};

// Ducker: dips the sound, then lets it swell back: the pumping effect. With nothing in Trig it dips on every beat,
// so a bass or pad can pump against the kick without a cable between tabs.
inline const double kDuckLen[5] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
struct Ducker : Module
{
    static const Info& desc()
    {
        static const Info i { "ducker", "Ducker", "Dynamics", "Pumps the sound on every beat",
            { { "in", "In" }, { "trig", "Trig" } }, { { "out", "Out" } },
            { { "depth", "Depth", 0, 1, 0.7f, Fmt::Percent }, { "every", "Every", 0, 4, 2, Fmt::Int, 1, { "1/16", "1/8", "1/4", "1/2", "1 bar" } },
              { "recover", "Recover", 0.05f, 1, 0.6f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float depth = P (0);
        const double len = kDuckLen[std::min (4, std::max (0, (int) std::lround (P (1))))];
        // Recover is a share of the gap between dips, so it stays in time at any tempo
        const float fall = std::exp (-5.0f / std::max (8.0f, (float) (len * P (2) / std::max (1.0e-9, c.ppqPerSample))));
        for (int i = 0; i < n; ++i)
        {
            bool hit = false;
            if (inOn[1]) { const bool g = std::abs (in[1][(size_t) i]) > 0.5f; hit = g && ! lastGate; lastGate = g; }
            else if (c.playing)
            {
                const long k = (long) std::floor ((c.ppq + i * c.ppqPerSample) / len + 1.0e-9);
                hit = k != lastBeat; lastBeat = k;
            }
            else lastBeat = -1000000000L;
            if (hit) env = 1.0f;
            out[0][(size_t) i] = depth > 0.0f ? in[0][(size_t) i] * (1.0f - depth * env) : in[0][(size_t) i];
            env *= fall;
            if (env < 1.0e-6f) env = 0.0f;
        }
    }
    float env = 0; bool lastGate = false; long lastBeat = -1000000000L;
};

// Transient shaper: more or less punch at the start of each hit, more or less body after it
struct TransientShaper : Module
{
    static const Info& desc()
    {
        static const Info i { "transient", "Transient shaper", "Dynamics", "More or less punch on each hit",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "attack", "Punch", -1, 1, 0.4f, Fmt::Bipolar }, { "sustain", "Body", -1, 1, 0, Fmt::Bipolar } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float a = P (0), s = P (1);
        const float fa = timeCoef (0.001f, c.sr), fr = timeCoef (0.04f, c.sr), sa = timeCoef (0.025f, c.sr), sr2 = timeCoef (0.15f, c.sr);
        for (int i = 0; i < n; ++i)
        {
            const float x = in[0][(size_t) i], v = std::abs (x);
            fast += (v > fast ? fa : fr) * (v - fast);
            slow += (v > slow ? sa : sr2) * (v - slow);
            if (fast < 1.0e-9f) fast = 0;
            if (slow < 1.0e-9f) slow = 0;
            if (a == 0.0f && s == 0.0f) { out[0][(size_t) i] = x; continue; }
            const float sum = fast + slow + 1.0e-6f;
            const float tr = std::max (0.0f, fast - slow) / sum, su = std::max (0.0f, slow - fast) / sum;
            out[0][(size_t) i] = x * std::max (0.0f, 1.0f + a * 3.0f * tr) * std::max (0.0f, 1.0f + s * 3.0f * su);
        }
    }
    float fast = 0, slow = 0;
};

// ================================================================ Pitch and width
// Shifts the pitch of any sound (an oscillator, a sample, another plugin) without changing its speed
struct Shifter
{
    void size (double sr)
    {
        buf.size ((int) (sr * 0.13) + 16); base = (float) (sr * 0.045); win = base; pos = 0;
        minP = (float) (sr / 2000.0); maxP = (float) (sr * 0.04);
        lp = 0; lastLp = 0; sinceCross = 0; period = 0; lastP = 0;
    }
    float run (float x, float ratio)
    {
        buf.push (x);
        // How long one cycle of the sound is: the time between upward zero crossings of a smoothed copy.
        // Two cycles in a row must agree before it is believed, so noise and drums leave it alone.
        lp += 0.12f * (x - lp);
        sinceCross += 1.0f;
        if (lastLp <= 0.0f && lp > 0.0f)
        {
            const float p = sinceCross - lp / (lp - lastLp);       // to a fraction of a sample
            sinceCross = lp / (lp - lastLp);
            if (p >= minP && p <= maxP) { if (std::abs (p - lastP) < 0.03f * p) period = 0.5f * (p + lastP); lastP = p; }
            else { lastP = 0; if (p > maxP) period = 0; }
        }
        if (sinceCross > maxP) { period = 0; lastP = 0; }
        lastLp = lp;
        pos += 1.0 - (double) ratio;
        if (pos >= win || pos < 0)
        {
            // A grain boundary: the two grains are half a window apart, so a window that is a whole number of
            // double-cycles puts them exactly in step and they join without a wobble.
            pos -= std::floor (pos / win) * win;
            const float frac = (float) (pos / win);
            float next = base;
            if (period > 0) { const float two = 2.0f * period; next = two * std::max (1.0f, std::round (base / two)); }
            if (next != win) { win = next; pos = (double) frac * win; }
        }
        const float d1 = (float) pos; float d2 = d1 + win * 0.5f; if (d2 >= win) d2 -= win;
        const float g1 = std::sin ((float) kPi * d1 / win), g2 = std::sin ((float) kPi * d2 / win);
        return buf.tap (d1 + 2.0f) * g1 * g1 + buf.tap (d2 + 2.0f) * g2 * g2;
    }
    DelayBuf buf; float win = 2000, base = 2000, minP = 24, maxP = 1920; double pos = 0;
    float lp = 0, lastLp = 0, sinceCross = 0, period = 0, lastP = 0;
};

// Tripler: one sound becomes three. A copy tuned slightly down on the left, one slightly up on the right,
// each a few milliseconds late, around the original in the middle.
struct Tripler : Module
{
    static const Info& desc()
    {
        static const Info i { "tripler", "Tripler", "Pitch and width", "Three sounds out of one, spread wide",
            { { "in", "In" } }, { { "outl", "Out L" }, { "outr", "Out R" } },
            { { "detune", "Detune", 0, 50, 14, Fmt::Cents }, { "delay", "Spread", 0, 0.03f, 0.012f, Fmt::Time },
              { "width", "Width", 0, 1, 1, Fmt::Percent }, { "mix", "Mix", 0, 1, 0.7f, Fmt::Percent } } };
        return i;
    }
    void prepare (double sr) override { a.size (sr); b.size (sr); da.size ((int) (sr * 0.04) + 16); db.size ((int) (sr * 0.04) + 16); }
    void process (int n, const Ctx& c) override
    {
        const float cents = P (0), mix = P (3), w = P (2);
        const float down = std::pow (2.0f, -cents / 1200.0f), up = std::pow (2.0f, cents / 1200.0f);
        const float dl = std::max (1.0f, P (1) * 0.6f * (float) c.sr), dr = std::max (1.0f, P (1) * (float) c.sr);
        for (int i = 0; i < n; ++i)
        {
            const float x = in[0][(size_t) i];
            da.push (a.run (x, down)); db.push (b.run (x, up));
            const float lo = da.tap (dl), hi = db.tap (dr);
            out[0][(size_t) i] = mix > 0.0f ? x + (lo * (0.5f + 0.5f * w) + hi * (0.5f - 0.5f * w)) * mix * 0.9f : x;
            out[1][(size_t) i] = mix > 0.0f ? x + (hi * (0.5f + 0.5f * w) + lo * (0.5f - 0.5f * w)) * mix * 0.9f : x;
        }
    }
    Shifter a, b; DelayBuf da, db;
};

// Harmoniser: adds copies at musical intervals. Plug the note's Pitch in and the intervals follow the key
// (a third above D in D minor is F; above E it is G). Without it, intervals are measured from the key note.
struct Harmoniser : Module
{
    static const Info& desc()
    {
        static const Info i { "harmoniser", "Harmoniser", "Pitch and width", "Adds copies at musical intervals, in key",
            { { "in", "In" }, { "pitch", "Pitch" } }, { { "out", "Out" } },
            { { "v1", "Voice 1", 0, 6, 3, Fmt::Int, 1, { "Off", "3rd up", "4th up", "5th up", "6th up", "Octave up", "Octave down" } },
              { "v2", "Voice 2", 0, 6, 0, Fmt::Int, 1, { "Off", "3rd up", "4th up", "5th up", "6th up", "Octave up", "Octave down" } },
              { "mix", "Mix", 0, 1, 0.6f, Fmt::Percent } } };
        return i;
    }
    void prepare (double sr) override { s[0].size (sr); s[1].size (sr); }
    // How many semitones a choice is, from a given note of the key
    static int semisFor (int choice, int degree, const int* scale)
    {
        static const int steps[7] = { 0, 2, 3, 4, 5, 7, -7 };
        if (choice <= 0 || choice > 6) return 0;
        return degreeToSemis (degree + steps[choice], scale) - degreeToSemis (degree, scale);
    }
    void process (int n, const Ctx& c) override
    {
        const int ch[2] = { (int) std::lround (P (0)), (int) std::lround (P (1)) };
        const float mix = P (2);
        for (int i = 0; i < n; ++i)
        {
            const float x = in[0][(size_t) i];
            if ((tick++ & 31) == 0)
            {
                // Which note of the key is playing: the nearest scale note to the pitch signal
                int degree = 0;
                if (inOn[1])
                {
                    const int pc = (((int) std::lround (in[1][(size_t) i] * 12.0f) - c.root) % 12 + 12) % 12;
                    int best = 99;
                    for (int d = 0; d < 7; ++d) { const int gap = std::min ((pc - c.scale[d] + 12) % 12, (c.scale[d] - pc + 12) % 12); if (gap < best) { best = gap; degree = d; } }
                }
                for (int v = 0; v < 2; ++v) ratio[v] = std::pow (2.0f, (float) semisFor (ch[v], degree, c.scale) / 12.0f);
            }
            float wet = 0;
            for (int v = 0; v < 2; ++v) { const float y = s[v].run (x, ratio[v]); if (ch[v] > 0) wet += y; }
            out[0][(size_t) i] = mix > 0.0f && (ch[0] > 0 || ch[1] > 0) ? x + wet * mix : x;
        }
    }
    Shifter s[2]; float ratio[2] { 1, 1 }; unsigned tick = 0;
};

struct StereoWidth : Module
{
    static const Info& desc()
    {
        static const Info i { "width", "Stereo width", "Pitch and width", "Narrower or wider. 0% is mono",
            { { "inl", "In L" }, { "inr", "In R" } }, { { "outl", "Out L" }, { "outr", "Out R" } },
            { { "width", "Width", 0, 2, 1, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        const float w = P (0);
        for (int i = 0; i < n; ++i)
        {
            const float l = in[0][(size_t) i], r = inOn[1] ? in[1][(size_t) i] : l;
            if (w == 1.0f) { out[0][(size_t) i] = l; out[1][(size_t) i] = r; continue; }
            const float mid = (l + r) * 0.5f, side = (l - r) * 0.5f * w;
            out[0][(size_t) i] = mid + side; out[1][(size_t) i] = mid - side;
        }
    }
};

struct Pan : Module
{
    static const Info& desc()
    {
        static const Info i { "pan", "Pan", "Pitch and width", "Places a sound left or right",
            { { "in", "In" }, { "cv", "Move" } }, { { "outl", "Out L" }, { "outr", "Out R" } },
            { { "pan", "Position", -1, 1, 0, Fmt::Bipolar } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float p = std::min (1.0f, std::max (-1.0f, P (0) + in[1][(size_t) i]));
            out[0][(size_t) i] = in[0][(size_t) i] * std::min (1.0f, 1.0f - p);
            out[1][(size_t) i] = in[0][(size_t) i] * std::min (1.0f, 1.0f + p);
        }
    }
};

// ================================================================ More movers
// A repeatable "random" number from a position in the song, so the same bar always sounds the same
inline float hashUnit (long a, int b)
{
    uint32_t h = (uint32_t) a * 2654435761u ^ ((uint32_t) b * 40503u + 0x9E3779B9u);
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return (float) (h & 0xffffffu) / 16777216.0f;
}

// Trance gate: chops a held sound in a rhythm. The steps are the pattern; a lit step lets the sound through.
struct TranceGate : Module
{
    static const Info& desc()
    {
        static const Info i { "gate", "Trance gate", "Movers", "Chops a held sound into a rhythm",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "length", "Steps", 1, 16, 16, Fmt::Int }, { "bars", "Bars", 0, 3, 0, Fmt::Int, 1, { "1 bar", "2 bars", "4 bars", "8 bars" } },
              { "depth", "Depth", 0, 1, 1, Fmt::Percent }, { "smooth", "Smooth", 0, 1, 0.2f, Fmt::Percent } }, Seq::Drum };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const int len = loopSteps (P (0), P (1));
        const float depth = P (2), coef = timeCoef (0.0005f + P (3) * 0.03f, c.sr);
        for (int i = 0; i < n; ++i)
        {
            float target = 1.0f;
            if (c.playing)
            {
                const int k = wrapStep (stepAt (c.ppq + i * c.ppqPerSample), len);
                if (k != shown) { shown = k; playStep.store (k); }
                target = data[(size_t) k].load (std::memory_order_relaxed) > 0.5f ? 1.0f : 1.0f - depth;
            }
            else if (shown != -1) { shown = -1; playStep.store (-1); }
            gain += coef * (target - gain);
            if (std::abs (gain - target) < 1.0e-5f) gain = target;
            out[0][(size_t) i] = gain == 1.0f ? in[0][(size_t) i] : in[0][(size_t) i] * gain;
        }
    }
    float gain = 1; int shown = -1;
};

// Step modulator: a row of 16 values that plays in time, for moving any knob in steps
struct StepMod : Module
{
    static const Info& desc()
    {
        static const Info i { "stepmod", "Step modulator", "Movers", "Moves a knob in steps, in time",
            {}, { { "out", "Out" } },
            { { "length", "Steps", 1, 16, 16, Fmt::Int }, { "bars", "Bars", 0, 3, 0, Fmt::Int, 1, { "1 bar", "2 bars", "4 bars", "8 bars" } },
              { "depth", "Depth", -1, 1, 1, Fmt::Bipolar }, { "glide", "Glide", 0, 1, 0, Fmt::Percent } }, Seq::Value };
        return i;
    }
    static constexpr int lo = -7, hi = 14;      // the grid's rows, shared with the note sequencer's screen
    void process (int n, const Ctx& c) override
    {
        const int len = loopSteps (P (0), P (1));
        const float depth = P (2), coef = timeCoef (0.0002f + P (3) * 0.12f, c.sr);
        for (int i = 0; i < n; ++i)
        {
            if (c.playing)
            {
                const int k = wrapStep (stepAt (c.ppq + i * c.ppqPerSample), len);
                if (k != shown)
                {
                    shown = k; playStep.store (k);
                    // A step that's switched off holds the value before it
                    if (data[(size_t) k].load (std::memory_order_relaxed) > 0.5f)
                        target = (data[(size_t) (kMaxSteps + k)].load (std::memory_order_relaxed) - (float) lo) / (float) (hi - lo);
                }
            }
            else if (shown != -1) { shown = -1; playStep.store (-1); }
            value += coef * (target - value);
            if (std::abs (value - target) < 1.0e-5f) value = target;
            out[0][(size_t) i] = value * depth;
        }
    }
    float value = 0, target = 0; int shown = -1;
};

// Random: a new random value at each step of its rate (or at each trigger). The same bar always gives the same values.
struct RandomMod : Module
{
    static const Info& desc()
    {
        static const Info i { "random", "Random", "Movers", "A new random value each step",
            { { "trig", "Trig" } }, { { "out", "Out" } },
            { { "rate", "Rate", 0, 5, 3, Fmt::Int, 1, { "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32" } },
              { "smooth", "Smooth", 0, 1, 0, Fmt::Percent }, { "depth", "Depth", 0, 1, 1, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        static const double len[6] = { 4, 2, 1, 0.5, 0.25, 0.125 };
        const double l = len[std::min (5, std::max (0, (int) std::lround (P (0))))];
        const float coef = timeCoef (0.0002f + P (1) * 0.2f, c.sr), depth = P (2);
        for (int i = 0; i < n; ++i)
        {
            if (inOn[0])
            {
                const bool g = in[0][(size_t) i] > 0.5f;
                if (g && ! last) target = hashUnit (++count, uid) * 2.0f - 1.0f;
                last = g;
            }
            else if (c.playing)
            {
                const long k = (long) std::floor ((c.ppq + i * c.ppqPerSample) / l + 1.0e-9);
                if (k != step) { step = k; target = hashUnit (k, uid) * 2.0f - 1.0f; }
            }
            value += coef * (target - value);
            if (std::abs (value - target) < 1.0e-5f) value = target;
            out[0][(size_t) i] = value * depth;
        }
    }
    float value = 0, target = 0; bool last = false; long step = -1000000000L, count = 0;
};

// Macro: one knob that sends its position down a cable. Plug it into several Mod inputs to move them all at once.
struct Macro : Module
{
    static const Info& desc()
    {
        static const Info i { "macro", "Macro knob", "Movers", "One knob that moves several things",
            {}, { { "out", "Out" } }, { { "value", "Amount", 0, 1, 0, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float coef = timeCoef (0.01f, c.sr), t = P (0);
        for (int i = 0; i < n; ++i) { v += coef * (t - v); if (std::abs (v - t) < 1.0e-5f) v = t; out[0][(size_t) i] = v; }
    }
    float v = 0;
};

struct EnvFollower : Module
{
    static const Info& desc()
    {
        static const Info i { "follow", "Envelope follower", "Movers", "Loudness of a sound, as a mover",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "attack", "Attack", 0.0005f, 0.2f, 0.005f, Fmt::Time, 2 }, { "release", "Release", 0.01f, 2.0f, 0.2f, Fmt::Time, 2 }, { "gain", "Amount", 0, 4, 1, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float a = timeCoef (P (0), c.sr), r = timeCoef (P (1), c.sr), g = P (2);
        for (int i = 0; i < n; ++i)
        {
            const float v = std::abs (in[0][(size_t) i]);
            env += (v > env ? a : r) * (v - env);
            if (env < 1.0e-9f) env = 0;
            out[0][(size_t) i] = std::min (1.0f, env * g);
        }
    }
    float env = 0;
};

// ================================================================ More notes and rhythm
inline const double kArpLen[5] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 0.125 };

// Arpeggiator: plays the notes plugged into it one at a time. Every setting has an input, so movers can change it as it plays.
struct Arpeggiator : Module
{
    static const Info& desc()
    {
        static const Info i { "arp", "Arpeggiator", "Notes and rhythm", "Plays the plugged-in notes one at a time",
            { { "p1", "Note 1" }, { "p2", "Note 2" }, { "p3", "Note 3" }, { "p4", "Note 4" }, { "ratecv", "Rate" }, { "rangecv", "Range" }, { "lencv", "Length" } },
            { { "pitch", "Pitch" }, { "gate", "Gate" } },
            { { "rate", "Rate", 0, 4, 3, Fmt::Int, 1, { "1/4", "1/8", "1/8 trip", "1/16", "1/32" } },
              { "pattern", "Pattern", 0, 4, 0, Fmt::Int, 1, { "Up", "Down", "Up and down", "Random", "Outside in" } },
              { "range", "Octaves", 1, 4, 2, Fmt::Int }, { "gatelen", "Length", 0.1f, 1, 0.6f, Fmt::Percent }, { "octave", "Octave", -3, 3, 0, Fmt::Int } } };
        return i;
    }
    // Which note of the spread-out chord plays at this step of the pattern
    static int pick (int pattern, long step, int count, int uid)
    {
        if (count <= 1) return 0;
        const int s = (int) (((step % 100000) + 100000) % 100000);
        switch (pattern)
        {
            case 1: return count - 1 - s % count;
            case 2: { const int period = 2 * count - 2, k = s % period; return k < count ? k : period - k; }
            case 3: return std::min (count - 1, (int) (hashUnit (step, uid) * (float) count));
            case 4: { const int k = s % count; return k % 2 == 0 ? k / 2 : count - 1 - k / 2; }
            default: return s % count;
        }
    }
    void process (int n, const Ctx& c) override
    {
        for (int i = 0; i < n; ++i)
        {
            if (c.playing)
            {
                const int rate = std::min (4, std::max (0, (int) std::lround (P (0) + in[4][(size_t) i] * 2.0f)));
                const double len = kArpLen[rate];
                const double pos = (c.ppq + i * c.ppqPerSample) / len;
                const long k = (long) std::floor (pos + 1.0e-9);
                if (k != last)
                {
                    last = k;
                    float notes[4]; int nn = 0;
                    for (int v = 0; v < 4; ++v) if (inOn[(size_t) v]) notes[nn++] = in[(size_t) v][(size_t) i];
                    if (nn > 0)
                    {
                        for (int a = 1; a < nn; ++a)                      // low to high (at most four)
                            for (int b2 = a; b2 > 0 && notes[b2 - 1] > notes[b2]; --b2) std::swap (notes[b2 - 1], notes[b2]);
                        const int range = std::min (4, std::max (1, (int) std::lround (P (2) + in[5][(size_t) i] * 2.0f)));
                        const int idx = pick ((int) std::lround (P (1)), k, nn * range, uid);
                        pitch = notes[idx % nn] + (float) (idx / nn) + std::round (P (4));
                        const float gl = std::min (1.0f, std::max (0.05f, P (3) + in[6][(size_t) i] * 0.5f));
                        const double stepSamples = len / std::max (1.0e-9, c.ppqPerSample);
                        left = gl >= 0.995f ? (int) (stepSamples * 1.05) : std::max (8, (int) (stepSamples * std::min (0.96f, gl)));
                        playStep.store (idx);
                    }
                }
            }
            else if (last != DrumSeq::noStep) { last = DrumSeq::noStep; left = 0; playStep.store (-1); }
            out[0][(size_t) i] = pitch;
            out[1][(size_t) i] = left > 0 ? 1.0f : 0.0f;
            if (left > 0) --left;
        }
    }
    long last = DrumSeq::noStep; int left = 0; float pitch = 0;
};

// Euclidean rhythm: spreads a number of hits as evenly as possible round a number of steps
struct Euclid : Module
{
    static const Info& desc()
    {
        static const Info i { "euclid", "Euclidean rhythm", "Notes and rhythm", "Spreads hits evenly round the bar",
            { { "hitscv", "Hits" }, { "turncv", "Turn" } }, { { "trig", "Trig" }, { "vel", "Vel" } },
            { { "steps", "Steps", 1, 16, 16, Fmt::Int }, { "hits", "Hits", 0, 16, 5, Fmt::Int }, { "turn", "Turn", 0, 15, 0, Fmt::Int },
              { "accent", "Accent", 0, 8, 4, Fmt::Int } } };
        return i;
    }
    static bool hitAt (int step, int steps, int hits, int turn)
    {
        if (hits <= 0) return false;
        if (hits >= steps) return true;
        const int k = ((step - turn) % steps + steps) % steps;
        return (k * hits) % steps < hits;
    }
    // Accent: every so-many steps is a loud one (0 = none). A hit that lands on one comes out at full velocity.
    static bool accentAt (int step, int accent) { return accent > 0 && step % accent == 0; }
    void process (int n, const Ctx& c) override
    {
        const int steps = std::min (16, std::max (1, (int) std::lround (P (0))));
        const int gateLen = std::max (8, (int) (0.5 * 0.25 / std::max (1.0e-9, c.ppqPerSample)));
        for (int i = 0; i < n; ++i)
        {
            if (c.playing)
            {
                const long s = stepAt (c.ppq + i * c.ppqPerSample);
                if (s != last)
                {
                    last = s;
                    const int k = wrapStep (s, steps);
                    playStep.store (k);
                    const int hits = std::min (steps, std::max (0, (int) std::lround (P (1) + in[0][(size_t) i] * 8.0f)));
                    const int turn = (int) std::lround (P (2) + in[1][(size_t) i] * 8.0f);
                    if (hitAt (k, steps, hits, turn))
                    {
                        left = gateLen;
                        vel = accentAt (k, (int) std::lround (P (3))) ? 1.0f : 0.72f;
                    }
                }
            }
            else if (last != DrumSeq::noStep) { last = DrumSeq::noStep; left = 0; playStep.store (-1); }
            out[0][(size_t) i] = left > 0 ? 1.0f : 0.0f;
            out[1][(size_t) i] = vel;
            if (left > 0) --left;
        }
    }
    long last = DrumSeq::noStep; int left = 0; float vel = 0.72f;
};

// Clap envelope: a few fast bursts and then a tail, the shape that turns noise into a hand clap
struct ClapEnv : Module
{
    static const Info& desc()
    {
        static const Info i { "clapenv", "Clap envelope", "Shapers", "The shape of a hand clap",
            { { "trig", "Trig" }, { "vel", "Vel" } }, { { "out", "Out" } },
            { { "spread", "Spread", 0.003f, 0.03f, 0.011f, Fmt::Time }, { "decay", "Decay", 0.02f, 1.5f, 0.22f, Fmt::Time, 2 } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const int gap = std::max (4, (int) (P (0) * c.sr));
        const float burst = std::exp (-6.9f / (float) gap * 1.6f), tail = std::exp (-6.9078f / (std::max (0.02f, P (1)) * (float) c.sr));
        for (int i = 0; i < n; ++i)
        {
            const bool g = in[0][(size_t) i] > 0.5f;
            if (g && ! last) { since = 0; v = 1.0f; amp = inOn[1] ? in[1][(size_t) i] : 1.0f; }
            last = g;
            if (since >= 0)
            {
                ++since;
                if (since == gap || since == 2 * gap || since == 3 * gap) v = 1.0f;     // the bursts restart the shape
                v *= since < 3 * gap ? burst : tail;
                if (v < 1.0e-5f) { v = 0; since = -1; }
            }
            out[0][(size_t) i] = v * amp;
        }
    }
    bool last = false; int since = -1; float v = 0, amp = 1;
};

// ================================================================ More utilities
// Clock: steady triggers at note lengths, for firing envelopes, the ducker, Random or the Switch
struct Clock : Module
{
    static const Info& desc()
    {
        static const Info i { "clock", "Clock", "Utilities", "Steady triggers: bar, beat, 1/8, 1/16",
            {}, { { "bar", "Bar" }, { "q", "1/4" }, { "e", "1/8" }, { "s", "1/16" } }, {} };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        static const double len[4] = { 4, 1, 0.5, 0.25 };
        for (int i = 0; i < n; ++i)
        {
            const double p = c.ppq + i * c.ppqPerSample;
            for (int k = 0; k < 4; ++k)
            {
                const double x = p / len[k] + 1.0e-9;
                out[(size_t) k][(size_t) i] = c.playing && x - std::floor (x) < 0.5 ? 1.0f : 0.0f;
            }
        }
    }
};

struct Divider : Module
{
    static const Info& desc()
    {
        static const Info i { "divider", "Divider", "Utilities", "Lets through every 2nd, 3rd or 4th trigger",
            { { "in", "In" } }, { { "d2", "/2" }, { "d3", "/3" }, { "d4", "/4" } }, {} };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        for (int i = 0; i < n; ++i)
        {
            const bool g = in[0][(size_t) i] > 0.5f;
            if (g && ! last) ++count;
            if (! c.playing) count = -1;                 // starts again from the first trigger each time the song plays
            last = g;
            out[0][(size_t) i] = g && count >= 0 && count % 2 == 0 ? 1.0f : 0.0f;
            out[1][(size_t) i] = g && count >= 0 && count % 3 == 0 ? 1.0f : 0.0f;
            out[2][(size_t) i] = g && count >= 0 && count % 4 == 0 ? 1.0f : 0.0f;
        }
    }
    bool last = false; long count = -1;
};

struct Logic : Module
{
    static const Info& desc()
    {
        static const Info i { "logic", "Logic", "Utilities", "Combines two triggers",
            { { "a", "A" }, { "b", "B" } }, { { "both", "Both" }, { "either", "Either" }, { "one", "Only one" } }, {} };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        for (int i = 0; i < n; ++i)
        {
            const bool a = in[0][(size_t) i] > 0.5f, b = in[1][(size_t) i] > 0.5f;
            out[0][(size_t) i] = a && b ? 1.0f : 0.0f; out[1][(size_t) i] = a || b ? 1.0f : 0.0f; out[2][(size_t) i] = a != b ? 1.0f : 0.0f;
        }
    }
};

// Chance: lets each trigger through or not. The same bar always makes the same choices.
struct Chance : Module
{
    static const Info& desc()
    {
        static const Info i { "chance", "Chance", "Utilities", "Lets some triggers through, drops others",
            { { "in", "In" } }, { { "out", "Out" } }, { { "chance", "Chance", 0, 1, 0.5f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float p = P (0);
        for (int i = 0; i < n; ++i)
        {
            const bool g = in[0][(size_t) i] > 0.5f;
            if (g && ! last) open = p >= 1.0f || hashUnit (stepAt (c.ppq + i * c.ppqPerSample), uid) < p;
            last = g;
            out[0][(size_t) i] = g && open ? 1.0f : 0.0f;
        }
    }
    bool last = false, open = false;
};

struct Slew : Module
{
    static const Info& desc()
    {
        static const Info i { "slew", "Slew", "Utilities", "Turns jumps into slides. Glides notes",
            { { "in", "In" } }, { { "out", "Out" } },
            { { "rise", "Rise", 0, 2, 0.05f, Fmt::Time, 3 }, { "fall", "Fall", 0, 2, 0.05f, Fmt::Time, 3 } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float up = P (0) <= 0.0f ? 1.0e9f : 1.0f / (P (0) * (float) c.sr), down = P (1) <= 0.0f ? 1.0e9f : 1.0f / (P (1) * (float) c.sr);
        for (int i = 0; i < n; ++i)
        {
            const float t = in[0][(size_t) i];
            v = t > v ? std::min (t, v + up) : std::max (t, v - down);     // at 0 it follows exactly
            out[0][(size_t) i] = v;
        }
    }
    float v = 0;
};

struct Crossfader : Module
{
    static const Info& desc()
    {
        static const Info i { "xfade", "Crossfader", "Utilities", "Blends or morphs between two inputs",
            { { "a", "A" }, { "b", "B" }, { "cv", "Move" } }, { { "out", "Out" } }, { { "mix", "A to B", 0, 1, 0.5f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx&) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float m = clamp01 (P (0) + in[2][(size_t) i]);
            out[0][(size_t) i] = m <= 0.0f ? in[0][(size_t) i] : m >= 1.0f ? in[1][(size_t) i] : in[0][(size_t) i] * (1.0f - m) + in[1][(size_t) i] * m;
        }
    }
};

// Switch: picks one of four inputs. Each trigger into Step moves on to the next one that has something plugged in.
struct Switch : Module
{
    static const Info& desc()
    {
        static const Info i { "switch", "Switch", "Utilities", "Picks one of four inputs, by hand or in steps",
            { { "in1", "1" }, { "in2", "2" }, { "in3", "3" }, { "in4", "4" }, { "step", "Step" } }, { { "out", "Out" } },
            { { "pick", "Input", 1, 4, 1, Fmt::Int } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        for (int i = 0; i < n; ++i)
        {
            const bool g = in[4][(size_t) i] > 0.5f;
            if (g && ! last) for (int k = 0; k < 4; ++k) { offset = (offset + 1) % 4; if (inOn[(size_t) (((int) std::lround (P (0)) - 1 + offset) % 4)]) break; }
            if (! c.playing && ! inOn[4]) offset = 0;
            last = g;
            out[0][(size_t) i] = in[(size_t) ((std::min (4, std::max (1, (int) std::lround (P (0)))) - 1 + offset) % 4)][(size_t) i];
        }
    }
    bool last = false; int offset = 0;
};

// ================================================================ Sends, ratchet, swing, scope
// Send: passes its sound through untouched and also feeds a copy to a shared bus, A or B.
// A Return on any tab (usually an effects tab) plays everything sent to that bus.
struct Send : Module
{
    static const Info& desc()
    {
        static const Info i { "send", "Send", "Utilities", "Feeds a copy of this sound to a shared bus, for an effect on another tab",
            { { "inl", "In L" }, { "inr", "In R" } }, { { "outl", "Out L" }, { "outr", "Out R" } },
            { { "amount", "Amount", 0, 1, 0.3f, Fmt::Percent }, { "bus", "Bus", 0, 1, 0, Fmt::Int, 1, { "A", "B" } } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float amount = P (0);
        const int bus = P (1) > 0.5f ? 1 : 0;
        for (int i = 0; i < n; ++i)
        {
            const float l = in[0][(size_t) i], r = inOn[1] ? in[1][(size_t) i] : l;
            out[0][(size_t) i] = l; out[1][(size_t) i] = r;
            if (c.buses != nullptr && amount > 0.0f) { c.buses->l[bus][(size_t) i] += l * amount; c.buses->r[bus][(size_t) i] += r * amount; }
        }
    }
};

struct Return : Module
{
    static const Info& desc()
    {
        static const Info i { "return", "Return", "Sources", "Plays everything the Send units on other tabs feed to a bus",
            {}, { { "outl", "Out L" }, { "outr", "Out R" } },
            { { "bus", "Bus", 0, 1, 0, Fmt::Int, 1, { "A", "B" } }, { "level", "Level", 0, 1.5f, 1, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const int bus = P (0) > 0.5f ? 1 : 0;
        const float level = P (1);
        for (int i = 0; i < n; ++i)
        {
            out[0][(size_t) i] = c.buses != nullptr ? c.buses->l[bus][(size_t) i] * level : 0.0f;
            out[1][(size_t) i] = c.buses != nullptr ? c.buses->r[bus][(size_t) i] * level : 0.0f;
        }
    }
};

// Ratchet: turns each hit into a fast burst of two, three or four within one 1/16 step
struct Ratchet : Module
{
    static const Info& desc()
    {
        static const Info i { "ratchet", "Ratchet", "Notes and rhythm", "Turns each hit into a fast burst",
            { { "trig", "Trig" }, { "vel", "Vel" } }, { { "trig", "Trig" }, { "vel", "Vel" } },
            { { "count", "Hits", 0, 3, 1, Fmt::Int, 1, { "1", "2", "3", "4" } }, { "fade", "Fade", 0, 1, 0.3f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const int count = 1 + std::min (3, std::max (0, (int) std::lround (P (0))));
        const float fade = P (1);
        const int stepLen = std::max (16, (int) (0.25 / std::max (1.0e-9, c.ppqPerSample)));
        const int gap = stepLen / count;
        for (int i = 0; i < n; ++i)
        {
            const bool g = in[0][(size_t) i] > 0.5f;
            if (g && ! last) { since = 0; baseVel = inOn[1] ? in[1][(size_t) i] : 0.72f; }
            last = g;
            float gate = 0.0f;
            if (since >= 0)
            {
                const int k = since / gap;                       // which hit of the burst
                if (k < count)
                {
                    gate = since - k * gap < gap / 2 ? 1.0f : 0.0f;
                    vel = baseVel * (1.0f - fade * (float) k / (float) std::max (1, count));
                    ++since;
                }
                else since = -1;
            }
            out[0][(size_t) i] = count == 1 ? in[0][(size_t) i] : gate;      // one hit: straight through
            out[1][(size_t) i] = count == 1 ? (inOn[1] ? in[1][(size_t) i] : 0.72f) : vel;
        }
    }
    bool last = false; int since = -1; float baseVel = 0.72f, vel = 0.72f;
};

// Swing: holds back every other 1/16 hit, for a shuffled feel
struct Swing : Module
{
    static const Info& desc()
    {
        static const Info i { "swing", "Swing", "Notes and rhythm", "Holds back every other 1/16 hit, for a shuffle",
            { { "trig", "Trig" } }, { { "trig", "Trig" } }, { { "amount", "Swing", 0, 0.75f, 0.3f, Fmt::Percent } } };
        return i;
    }
    void process (int n, const Ctx& c) override
    {
        const float amount = P (0);
        const int stepLen = std::max (16, (int) (0.25 / std::max (1.0e-9, c.ppqPerSample)));
        for (int i = 0; i < n; ++i)
        {
            const bool g = in[0][(size_t) i] > 0.5f;
            if (amount <= 0.0f) { out[0][(size_t) i] = in[0][(size_t) i]; last = g; wait = -1; left = 0; continue; }   // no swing: untouched
            if (g && ! last)
            {
                const bool offBeat = (stepAt (c.ppq + i * c.ppqPerSample) & 1) != 0;
                wait = offBeat ? (int) (amount * (float) stepLen * 0.5f) : 0;
            }
            last = g;
            if (wait == 0) { left = stepLen / 4; wait = -1; }
            else if (wait > 0) --wait;
            out[0][(size_t) i] = left > 0 ? 1.0f : 0.0f;
            if (left > 0) --left;
        }
    }
    bool last = false; int wait = -1, left = 0;
};

// Scope and tuner: passes the sound through and shows it. The screen reads the picture, the note and the level.
struct Scope : Module
{
    static const Info& desc()
    {
        static const Info i { "scope", "Scope and tuner", "Utilities", "Shows the sound passing through, and the note it is playing",
            { { "in", "In" } }, { { "out", "Out" } }, {}, Seq::Scope };
        return i;
    }
    static constexpr int size = 1024;
    void process (int n, const Ctx& c) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float x = in[0][(size_t) i];
            out[0][(size_t) i] = x;
            if ((count++ & 1) == 0) { ring[(size_t) write] = x; write = (write + 1) % size; }    // about 43 ms across the picture
            peak = std::max (peak * 0.9995f, std::abs (x));
            // The note: time between upward crossings, with a dead band so hiss doesn't count as a crossing
            const float band = std::max (0.01f, peak * 0.2f);
            if (x < -band) armed = true;
            if (armed && x > band)
            {
                armed = false;
                if (sinceCross > 0)
                {
                    const float hzNow = (float) c.sr / (float) sinceCross;
                    if (hzNow > 20.0f && hzNow < 5000.0f) smooth = smooth <= 0.0f || std::abs (hzNow - smooth) > smooth * 0.2f ? hzNow : smooth + 0.15f * (hzNow - smooth);
                }
                sinceCross = 0;
            }
            ++sinceCross;
            if (sinceCross > (int) c.sr / 10) smooth = 0.0f;       // nothing crossing: no note
        }
        hz.store (peak > 0.003f ? smooth : 0.0f);
        level.store (peak);
        head.store (write);
    }
    std::array<float, size> ring {};
    std::atomic<int> head { 0 };
    std::atomic<float> hz { 0.0f }, level { 0.0f };
    int write = 0, sinceCross = 0; unsigned count = 0;
    float peak = 0, smooth = 0; bool armed = false;
};

// A frequency as a note name and how far off it is, for the tuner
inline std::string noteText (float hz, int* centsOut = nullptr)
{
    if (hz <= 0.0f) return "";
    const double midi = 69.0 + 12.0 * std::log2 ((double) hz / 440.0);
    const int nearest = (int) std::lround (midi);
    if (centsOut) *centsOut = (int) std::lround ((midi - nearest) * 100.0);
    return std::string (kNoteNames[((nearest % 12) + 12) % 12]) + std::to_string (nearest / 12 - 1);
}

// Turns a gate and up to four pitch signals into note-on and note-off events, for playing another plugin.
// A note starts when the gate goes up, ends when it drops, and re-plays if its pitch changes while held.
struct NoteTracker
{
    int sounding[4] { -1, -1, -1, -1 };
    bool gate = false;
    static int noteOf (float pitch) { return std::min (127, std::max (0, (int) std::lround (pitch * 12.0f) + 60)); }
    // emit (note, velocity): velocity 0 means note off
    template <typename Emit>
    void step (bool gateNow, const float* pitch, const char* plugged, int velocity, Emit&& emit)
    {
        // What should sound now. Two cables carrying the same note are one note, not two.
        int want[4];
        for (int v = 0; v < 4; ++v)
        {
            want[v] = gateNow && plugged[v] ? noteOf (pitch[v]) : -1;
            for (int u = 0; u < v; ++u) if (want[u] == want[v]) want[v] = -1;
        }
        auto in = [] (const int* set, int note) { return set[0] == note || set[1] == note || set[2] == note || set[3] == note; };
        // Notes that stop first, then notes that start: a note that only moved to another cable carries on untouched
        for (int v = 0; v < 4; ++v) if (sounding[v] >= 0 && ! in (want, sounding[v])) emit (sounding[v], 0);
        for (int v = 0; v < 4; ++v) if (want[v] >= 0 && ! in (sounding, want[v])) emit (want[v], std::min (127, std::max (1, velocity)));
        for (int v = 0; v < 4; ++v) sounding[v] = want[v];
        gate = gateNow;
    }
};

// ---------------------------------------------------------------- The list of every module type
// The order here is the order inside each heading of the Add menu.
template <typename T> TypeEntry entry() { return { &T::desc(), [] { return std::unique_ptr<Module> (new T()); } }; }

inline const std::vector<TypeEntry>& registry()
{
    static const std::vector<TypeEntry> r {
        entry<Oscillator>(), entry<MultiOsc>(), entry<Noise>(), entry<Return>(),
        entry<HitEnv>(), entry<Adsr>(), entry<ClapEnv>(),
        entry<Filter>(),
        entry<Drive>(), entry<HardClip>(), entry<Bitcrusher>(),
        entry<Eq3>(), entry<ParamEq>(), entry<CutFilter>(),
        entry<Reverb<kRoom>>(), entry<Reverb<kHall>>(), entry<Reverb<kPlate>>(),
        entry<DigitalDelay>(), entry<TapeEcho>(), entry<PingPong>(),
        entry<Chorus>(), entry<Phaser>(), entry<Flanger>(),
        entry<Compressor>(), entry<Ducker>(), entry<TransientShaper>(),
        entry<Tripler>(), entry<Harmoniser>(), entry<StereoWidth>(), entry<Pan>(),
        entry<Lfo>(), entry<TranceGate>(), entry<StepMod>(), entry<RandomMod>(), entry<Macro>(), entry<EnvFollower>(),
        entry<DrumSeq>(), entry<NoteSeq>(), entry<KeysIn>(), entry<ChordGen>(), entry<Arpeggiator>(), entry<Euclid>(), entry<Ratchet>(), entry<Swing>(),
        entry<Vca>(), entry<Mixer4>(), entry<Atten>(), entry<Crossfader>(), entry<Switch>(), entry<Slew>(),
        entry<Clock>(), entry<Divider>(), entry<Logic>(), entry<Chance>(), entry<Send>(), entry<Scope>(), entry<OutputModule>() };
    return r;
}

// Menu headings, in the order they are shown
inline const std::vector<const char*>& shelves()
{
    static const std::vector<const char*> s { "Sources", "Shapers", "Filters", "EQ", "Dynamics", "Distortion", "Reverb", "Delay and echo", "Movement", "Pitch and width", "Movers", "Notes and rhythm", "Utilities", "Your plugins" };
    return s;
}
} // namespace n2m
