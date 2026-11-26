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
        static const Info i { "adsr", "Envelope", "Shapers", "Shapes a held note: fade in, hold, fade out",
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
        static const Info i { "drive", "Soft saturation", "Distortion", "Warmth and grit. At 0% the sound passes untouched",
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
        static const Info i { "vca", "VCA", "Utilities", "A volume control that another signal opens and closes",
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
        static const Info i { "lfo", "LFO", "Movers", "A slow wave, in time with the song, for moving any knob",
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

// Drum sequencer: up to 8 bars of 16 steps. data[step] = 0 off, 1 hit, 2 accent.
struct DrumSeq : Module
{
    static const Info& desc()
    {
        static const Info i { "drumseq", "Drum sequencer", "Notes and rhythm", "Up to 8 bars. Click a step for a hit, again for an accent",
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
                    if (d > 0.5f) { left = gateLen; vel = d > 1.5f ? 1.0f : 0.72f; }
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

// Note sequencer: up to 8 bars. First row of data = note on/off, second row = which note of the key.
// Notes are stored as scale steps, so changing the key moves every pattern with it.
// With Follow on, the steps count from the chord playing now, so the line moves with the chords.
struct NoteSeq : Module
{
    static const Info& desc()
    {
        static const Info i { "noteseq", "Note sequencer", "Notes and rhythm", "Up to 8 bars of notes, always in the key. Can follow the chords",
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
                        left = gateLen;
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

// Chord generator: plays the song's chord progression (shared by every tab, like the key).
// Sends the chord out as a bass note and four voices, with a gate in the chosen rhythm.
struct ChordGen : Module
{
    static const Info& desc()
    {
        static const Info i { "chords", "Chord generator", "Notes and rhythm", "Plays the song's chord progression, always in key",
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
        static const Info i { "multiosc", "Multi-oscillator", "Sources", "Four notes at once, each thickened. For chords, pads and stabs",
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
        const int i = (int) p; const float f = p - (float) i;
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
        static const char* const hint[3] = { "Small and tight. Good on drums", "Large and smooth, for pads and space", "Bright and dense. Classic on snares and stabs" };
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
        static const Info i { "tape", "Tape echo", "Delay and echo", "Warm, wobbly echoes that get duller as they repeat",
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
        static const Info i { "hardclip", "Hard clip", "Distortion", "Aggressive, flat-topped distortion. At 0% the sound passes untouched",
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
        static const Info i { "bitcrush", "Bitcrusher", "Distortion", "Lo-fi digital grit: fewer steps of loudness, fewer samples",
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
        static const Info i { "chorus", "Chorus", "Movement", "Thickens a sound and spreads it left and right",
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
        static const Info i { "parameq", "Parametric EQ", "EQ", "Four bands. Each row: where, how much, how wide",
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
        static const Info i { "cuts", "Low cut / high cut", "EQ", "Clears out rumble below and fizz above. Fully open, it does nothing",
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
        for (int v = 0; v < 4; ++v)
        {
            const int want = gateNow && plugged[v] ? noteOf (pitch[v]) : -1;
            if (want == sounding[v]) continue;
            if (sounding[v] >= 0) emit (sounding[v], 0);
            if (want >= 0) emit (want, std::min (127, std::max (1, velocity)));
            sounding[v] = want;
        }
        gate = gateNow;
    }
};

// ---------------------------------------------------------------- The list of every module type
// The order here is the order inside each heading of the Add menu.
template <typename T> TypeEntry entry() { return { &T::desc(), [] { return std::unique_ptr<Module> (new T()); } }; }

inline const std::vector<TypeEntry>& registry()
{
    static const std::vector<TypeEntry> r {
        entry<Oscillator>(), entry<MultiOsc>(), entry<Noise>(),
        entry<HitEnv>(), entry<Adsr>(),
        entry<Filter>(),
        entry<Drive>(), entry<HardClip>(), entry<Bitcrusher>(),
        entry<Eq3>(), entry<ParamEq>(), entry<CutFilter>(),
        entry<Reverb<kRoom>>(), entry<Reverb<kHall>>(), entry<Reverb<kPlate>>(),
        entry<DigitalDelay>(), entry<TapeEcho>(), entry<PingPong>(),
        entry<Chorus>(), entry<Phaser>(), entry<Flanger>(),
        entry<Lfo>(),
        entry<DrumSeq>(), entry<NoteSeq>(), entry<ChordGen>(),
        entry<Vca>(), entry<Mixer4>(), entry<Atten>(), entry<OutputModule>() };
    return r;
}

// Menu headings, in the order they are shown
inline const std::vector<const char*>& shelves()
{
    static const std::vector<const char*> s { "Sources", "Shapers", "Filters", "EQ", "Distortion", "Reverb", "Delay and echo", "Movement", "Movers", "Notes and rhythm", "Utilities", "Your plugins" };
    return s;
}
} // namespace n2m
