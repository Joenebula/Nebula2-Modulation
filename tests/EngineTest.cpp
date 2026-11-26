// Checks the rack engine on its own (no Cubase, no JUCE). Prints "All passed" or what failed.
#include "Presets.h"
#include <cstdio>
#include <cstring>

using namespace n2m;

static int failures = 0;
#define CHECK(cond, what) do { if (! (cond)) { ++failures; std::printf ("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

struct Render { std::vector<float> l, r; };

// Plays the rack for some bars at 124 BPM, in blocks of the given size
static Render render (Rack& rack, double bars, bool playing = true, int block = 256, double bpm = 124.0)
{
    const int total = (int) (bars * 4.0 * 60.0 / bpm * rack.sr);
    Render out; out.l.resize ((size_t) total); out.r.resize ((size_t) total);
    double ppq = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        rack.process (out.l.data() + pos, out.r.data() + pos, n, bpm, ppq, playing);
        ppq += n * bpm / 60.0 / rack.sr;
    }
    return out;
}
static float peakOf (const std::vector<float>& v, size_t from = 0, size_t to = (size_t) -1)
{
    float p = 0; to = std::min (to, v.size());
    for (size_t i = from; i < to; ++i) p = std::max (p, std::abs (v[i]));
    return p;
}
static bool allFinite (const std::vector<float>& v) { for (float x : v) if (! std::isfinite (x)) return false; return true; }
static Rack* one (const char* type) { auto* r = new Rack(); r->prepare (48000); r->addBoard (makeBoard (type, 48000)); return r; }
static Module* firstOf (Board& b, const char* type) { for (auto& m : b.modules) if (std::strcmp (m->info->type, type) == 0) return m.get(); return nullptr; }
// Rough pitch: count upward zero crossings
static double roughHz (const std::vector<float>& v, size_t from, size_t to, double sr)
{
    int n = 0;
    for (size_t i = from + 1; i < to; ++i) if (v[i - 1] <= 0 && v[i] > 0) ++n;
    return n / ((double) (to - from) / sr);
}

int main()
{
    // 1. Every kind of board makes sound when the song plays, stays clean, and never clips
    for (auto& t : boardTypes())
    {
        if (std::string (t.id) == "empty") continue;
        std::unique_ptr<Rack> rack (one (t.id));
        auto a = render (*rack, 2);
        CHECK (peakOf (a.l) > 0.02f, t.name);
        CHECK (peakOf (a.l) <= 1.0f && peakOf (a.r) <= 1.0f, "never above full level");
        CHECK (allFinite (a.l) && allFinite (a.r), "no broken numbers");
        // every module on a preset board is in the signal path
        for (auto& m : rack->boards[0]->modules) CHECK (m->live, "every preset module is heard");
        // stopped: sequencers go quiet and the sound dies away
        auto s = render (*rack, 4, false);
        CHECK (peakOf (s.l, s.l.size() - 4800) < 0.001f, "silent when stopped");
    }

    // 2. An empty board is silent
    { std::unique_ptr<Rack> rack (one ("empty")); CHECK (peakOf (render (*rack, 1).l) == 0.0f, "empty board is silent"); }

    // 3. The kick lands on the beat: loud right after each beat, quiet just before the next
    {
        std::unique_ptr<Rack> rack (one ("kick"));
        auto a = render (*rack, 1);
        const size_t beat = (size_t) (60.0 / 124.0 * 48000);
        for (size_t k = 0; k < 4; ++k)
        {
            CHECK (peakOf (a.l, k * beat, k * beat + 2400) > 0.2f, "kick sounds on the beat");
            CHECK (peakOf (a.l, (k + 1) * beat - 1200, (k + 1) * beat - 10) < 0.05f, "kick has died away before the next beat");
        }
    }

    // 4. The same song gives the same sound whatever block size Cubase uses
    {
        std::unique_ptr<Rack> a (one ("bass")), b (one ("bass"));
        auto x = render (*a, 2, true, 64), y = render (*b, 2, true, 441);
        float diff = 0;
        for (size_t i = 0; i < x.l.size(); ++i) diff = std::max (diff, std::abs (x.l[i] - y.l[i]));
        CHECK (diff < 1.0e-4f, "block size does not change the sound");
    }

    // 5. Save and load: the text comes back the same and sounds the same
    {
        Rack a; a.prepare (48000); loadStarter (a);
        a.root.store (9); a.scale.store (2); a.boards[1]->mute.store (true); a.boards[0]->name = "Big | kick";
        const std::string saved = a.save();
        Rack b; b.prepare (48000);
        CHECK (b.load (saved), "loads its own save");
        CHECK (b.save() == saved, "save, load, save gives the same text");
        CHECK (b.boards.size() == 5 && b.root.load() == 9 && b.scale.load() == 2, "boards and key restored");
        CHECK (b.boards[0]->name == "Big   kick", "a name can't break the save format");
        Rack c; c.prepare (48000); c.load (saved);
        auto x = render (b, 2), y = render (c, 2);
        CHECK (x.l == y.l, "two loads of one save sound identical");
        CHECK (! b.load ("something else"), "rejects text that isn't a save");
        // A save from a newer version with a module this version doesn't know still loads
        Rack d; d.prepare (48000);
        CHECK (d.load ("N2M 1\nboard kick|Kick|0|0.8|0|0|0\nmod 1 output 0 0\nmod 2 fromthefuture 0 0\nmod 3 osc 0 0\np 3 nosuchknob 1\ncable 3 out 1 left\ncable 2 out 1 left\nend\n"), "loads with unknown parts");
        CHECK (d.boards.size() == 1 && d.boards[0]->modules.size() == 2 && d.boards[0]->cables.size() == 1, "unknown parts skipped");
    }

    // 6. Pulling the cable out of the Output silences the board, and the screen is told why
    {
        std::unique_ptr<Rack> rack (one ("kick"));
        auto& b = *rack->boards[0];
        auto* out = firstOf (b, "output"); auto* drive = firstOf (b, "drive");
        rack->disconnect (b, drive->uid, 0, out->uid, 0);
        CHECK (peakOf (render (*rack, 1).l) == 0.0f, "no cable to the Output, no sound");
        CHECK (! drive->live && out->live, "unconnected modules are marked as not heard");
        CHECK (rack->connect (b, drive->uid, 0, out->uid, 0), "plug it back in");
        CHECK (peakOf (render (*rack, 1).l) > 0.2f && drive->live, "sound returns");
        CHECK (! rack->connect (b, drive->uid, 0, out->uid, 0), "the same cable can't be added twice");
        CHECK (! rack->connect (b, out->uid, 0, out->uid, 0), "a module can't be plugged into itself");
        // adding a unit leaves it unconnected until the user cables it
        auto* extra = rack->addModule (b, "filter", 0, 0);
        CHECK (extra != nullptr && ! extra->live, "a newly added unit starts unconnected");
        rack->removeModule (b, drive->uid);
        CHECK (b.cables.size() == 7 && peakOf (render (*rack, 1).l) == 0.0f, "deleting a module takes its cables with it");
    }

    // 7. Zero means zero: Drive at 0% passes the sound untouched, a level at 0 is silent
    {
        std::unique_ptr<Rack> a (one ("kick")), b (one ("kick"));
        firstOf (*a->boards[0], "drive")->set ("drive", 0.0f);
        auto& bb = *b->boards[0];
        auto* drive = firstOf (bb, "drive"); auto* vca = firstOf (bb, "vca"); auto* out = firstOf (bb, "output");
        b->removeModule (bb, drive->uid);
        b->connect (bb, vca->uid, 0, out->uid, 0);
        CHECK (render (*a, 1).l == render (*b, 1).l, "Drive at 0% is the same as no Drive");
        firstOf (*a->boards[0], "vca")->set ("level", 0.0f);
        CHECK (peakOf (render (*a, 1).l) == 0.0f, "VCA at 0% is silent");
    }

    // 8. Mixer: mute, solo, level, pan
    {
        Rack rack; rack.prepare (48000); loadStarter (rack);
        for (auto& b : rack.boards) b->mute.store (true);
        CHECK (peakOf (render (rack, 1).l) == 0.0f, "all muted is silent");
        rack.boards[0]->solo.store (true);
        CHECK (peakOf (render (rack, 1).l) > 0.1f, "solo beats mute");
        rack.boards[0]->pan.store (1.0f);
        auto p = render (rack, 1);
        CHECK (peakOf (p.l) == 0.0f && peakOf (p.r) > 0.1f, "pan hard right");
        rack.master.store (0.0f);
        CHECK (peakOf (render (rack, 1).r) == 0.0f, "master at zero is silent");
    }

    // 9. Everything follows the key: the bass plays the key note, and moves when the key moves
    {
        std::unique_ptr<Rack> rack (one ("bass"));
        auto& b = *rack->boards[0];
        auto* seq = firstOf (b, "noteseq");
        firstOf (b, "filter")->set ("cutoff", 1.0f); firstOf (b, "filter")->set ("modamt", 0.0f);
        firstOf (b, "drive")->set ("drive", 0.0f);
        firstOf (b, "osc")->set ("wave", 0);
        seq->set ("gatelen", 1.0f); seq->set ("follow", 0);
        for (int i = 0; i < 16; ++i) { seq->data[(size_t) i].store (1.0f); seq->data[(size_t) (kMaxSteps + i)].store (0.0f); }
        rack->root.store (9);                      // A, two octaves down = A2 = 110 Hz
        auto a = render (*rack, 1);
        CHECK (std::abs (roughHz (a.l, 24000, 72000, 48000) - 110.0) < 3.0, "bass plays the key note");
        rack->root.store (2);                      // D2 = 73.4 Hz
        a = render (*rack, 1);
        CHECK (std::abs (roughHz (a.l, 24000, 72000, 48000) - 73.4) < 3.0, "changing the key moves the bass");
        for (int i = 0; i < 16; ++i) seq->data[(size_t) (kMaxSteps + i)].store (7.0f);   // an octave up within the key
        a = render (*rack, 1);
        CHECK (std::abs (roughHz (a.l, 24000, 72000, 48000) - 146.8) < 4.0, "seven scale steps is an octave");
        CHECK (degreeToSemis (2, scales()[0].iv) == 3 && degreeToSemis (-1, scales()[0].iv) == -2 && degreeToSemis (9, scales()[6].iv) == 16, "scale steps");
    }

    // 10. Pattern ideas and random patterns
    {
        std::unique_ptr<Rack> rack (one ("kick"));
        auto* seq = firstOf (*rack->boards[0], "drumseq");
        for (uint32_t s = 1; s < 200; ++s)
        {
            randomDrum ("kick", s * 7919u, *seq);
            CHECK (seq->data[0].load() == 2.0f, "a random kick always hits beat 1");
            int hits = 0; for (int i = 0; i < 16; ++i) hits += seq->data[(size_t) i].load() > 0 ? 1 : 0;
            CHECK (hits >= 3 && hits <= 10, "a random kick is still a kick");
            randomDrum ("snare", s * 104729u, *seq);
            CHECK (seq->data[4].load() == 2.0f && seq->data[12].load() == 2.0f, "a random snare keeps the backbeat");
            randomDrum ("perc", s * 31u, *seq);
            hits = 0; for (int i = 0; i < 16; ++i) hits += seq->data[(size_t) i].load() > 0 ? 1 : 0;
            CHECK (hits >= 3 && hits <= 8, "random percussion has a few hits");
        }
        std::unique_ptr<Rack> lead (one ("lead"));
        auto* ns = firstOf (*lead->boards[0], "noteseq");
        randomNotes ("lead", 5, *ns); const float d5 = ns->data[(size_t) (kMaxSteps + 4)].load();
        randomNotes ("lead", 5, *ns);
        CHECK (ns->data[(size_t) (kMaxSteps + 4)].load() == d5, "the same seed gives the same pattern");
        for (auto t : { "kick", "snare", "hat", "perc", "empty" }) CHECK (drumIdeas (t).size() >= 4, "drum ideas for every kind");
        CHECK (noteIdeas ("bass").size() >= 4 && noteIdeas ("lead").size() >= 4, "note ideas");
    }

    // 11. Copying a board gives an independent twin
    {
        Rack rack; rack.prepare (48000); rack.addBoard (makeBoard ("hat", 48000));
        auto copy = rack.copyBoard (*rack.boards[0]);
        CHECK (copy != nullptr && copy->modules.size() == rack.boards[0]->modules.size() && copy->cables.size() == rack.boards[0]->cables.size(), "copy has the same modules and cables");
        rack.addBoard (std::move (copy));
        firstOf (*rack.boards[1], "hitenv")->set ("decay", 1.0f);
        CHECK (firstOf (*rack.boards[0], "hitenv")->P (1) < 0.1f, "changing the copy leaves the original alone");
        rack.moveBoard (1, 0); rack.removeBoard (1);
        CHECK (rack.boards.size() == 1 && firstOf (*rack.boards[0], "hitenv")->P (1) == 1.0f, "reorder and delete tabs");
    }

    // 12. Tidy never stacks two modules on top of each other
    for (auto& t : boardTypes())
    {
        auto b = makeBoard (t.id);
        for (size_t i = 0; i < b->modules.size(); ++i)
            for (size_t j = i + 1; j < b->modules.size(); ++j)
            {
                auto& p = *b->modules[i]; auto& q = *b->modules[j];
                const bool apart = p.x + lay::width (*p.info) <= q.x || q.x + lay::width (*q.info) <= p.x
                                || p.y + lay::height (*p.info) <= q.y || q.y + lay::height (*q.info) <= p.y;
                CHECK (apart, "tidy leaves no overlaps");
            }
    }

    // 13. A loop of cables (feedback) must not hang or blow up
    {
        std::unique_ptr<Rack> rack (one ("lead"));
        auto& b = *rack->boards[0];
        auto* filt = firstOf (b, "filter"); auto* mix = firstOf (b, "mixer");
        rack->connect (b, filt->uid, 0, mix->uid, 2);
        auto a = render (*rack, 2);
        CHECK (allFinite (a.l) && peakOf (a.l) <= 1.0f, "feedback loop stays under control");
    }

    // 14. A project saved by v0.1.0 (one-bar patterns, no chords) still loads and plays the same pattern
    {
        Rack rack; rack.prepare (48000);
        const char* old = "N2M 1\nkey 9 0\nmaster 0.8\nboard bass|Bass|2|0.8|0|0|0\nmod 1 output 0 0\nmod 2 noteseq 0 0\np 2 octave -2\n"
                          "d 2 1 0 0 0 1 0 0 0 1 0 0 0 1 0 0 0 0 0 0 0 2 0 0 0 4 0 0 0 7 0 0 0\nmod 3 osc 0 0\ncable 2 pitch 3 pitch\ncable 3 out 1 left\nend\n";
        CHECK (rack.load (old), "a v0.1.0 project loads");
        auto* seq = firstOf (*rack.boards[0], "noteseq");
        CHECK (seq->data[0].load() == 1.0f && seq->data[4].load() == 1.0f && seq->data[1].load() == 0.0f, "old pattern: which steps play");
        CHECK (seq->data[(size_t) (kMaxSteps + 4)].load() == 2.0f && seq->data[(size_t) (kMaxSteps + 12)].load() == 7.0f, "old pattern: which notes");
        CHECK (seq->P (seq->paramIndex ("follow")) == 0.0f && seq->P (seq->paramIndex ("bars")) == 0.0f, "old projects keep ignoring chords and stay one bar");
        CHECK (! rack.harmony.seq.empty() && rack.timeline.total > 0, "and get a chord progression to use");
        CHECK (peakOf (render (rack, 1).l) > 0.05f, "and still sounds");
    }

    // 15. The scale list is the same as Nebula2 Chord's, so the two stay in step
    {
        CHECK (scales().size() == n2c::scales().size(), "same number of scales as Nebula2 Chord");
        for (size_t i = 0; i < scales().size() && i < n2c::scales().size(); ++i)
        {
            CHECK (std::string (scales()[i].id) == n2c::scales()[i].id, "same scale order");
            for (int k = 0; k < 7; ++k) CHECK (scales()[i].iv[k] == n2c::scales()[i].iv[(size_t) k], "same scale notes");
        }
    }

    // 16. Chords: every mood, in several keys, gives a progression of the right length with every note in key
    {
        Rack rack; rack.prepare (48000);
        for (auto& mood : n2c::moods())
            for (int key : { 0, 2, 7, 9 })
                for (uint32_t seed : { 1u, 77u, 362668u })
                {
                    rack.root.store (key);
                    rack.harmony.lenBars = seed == 77u ? 32 : 16; rack.harmony.chordBars = seed == 1u ? 1 : 2;
                    rack.harmony.seed = seed;
                    rack.setMood (mood.id);
                    const auto t = rack.theory();
                    CHECK (rack.timeline.total == rack.harmony.lenBars * 16, "the progression is as long as asked");
                    CHECK (rack.harmony.names.size() == rack.harmony.seq.size() && ! rack.harmony.names[0].empty(), "every chord has a name");
                    CHECK (rack.harmony.seq.size() <= 32, "no more chords than the screen can show");
                    bool inKey = true, ordered = true;
                    for (auto& sl : rack.timeline.slots)
                    {
                        for (int k = 0; k < 4; ++k)
                        {
                            const int midi = (int) std::lround (sl.voice[k] * 12.0f) + 60;
                            inKey = inKey && t.inScale (n2c::mod12 (midi - sl.tr));
                            if (k > 0) ordered = ordered && sl.voice[k] >= sl.voice[k - 1];
                        }
                        inKey = inKey && t.inScale (n2c::mod12 ((int) std::lround (sl.bass * 12.0f) + 60 - sl.tr));
                    }
                    CHECK (inKey, "every chord note is in the key");
                    CHECK (ordered, "the four notes run low to high");
                }
        // the same seed gives the same chords; a new seed gives different ones
        rack.setMood ("deep"); rack.generateChords (5); const auto a = rack.harmony.names;
        rack.generateChords (6); const auto b = rack.harmony.names;
        rack.generateChords (5);
        CHECK (rack.harmony.names == a && a != b, "Generate is repeatable by seed");
        // changing the key keeps the progression and moves its notes
        const float before = rack.timeline.slots[0].voice[0];
        const size_t count = rack.harmony.seq.size();
        rack.setKey (rack.root.load() + 3, rack.scale.load());
        CHECK (rack.harmony.seq.size() == count && rack.timeline.slots[0].voice[0] != before, "a key change moves the chords with it");
    }

    // 17. Pad and stab boards play the chords, and the notes change as the song moves on
    for (const char* type : { "pad", "stab" })
    {
        std::unique_ptr<Rack> rack (one (type));
        auto a = render (*rack, 8);
        CHECK (peakOf (a.l) > 0.02f && peakOf (a.l) <= 1.0f && allFinite (a.l), type);
        auto* ch = firstOf (*rack->boards[0], "chords");
        CHECK (ch->playStep.load() == rack->timeline.indexAt (8 * 16 - 1), "the chord unit knows which chord is playing");
        CHECK (rack->timeline.slots.size() >= 4, "several chords in the song");
        for (auto& m : rack->boards[0]->modules) CHECK (m->live, "every preset module is heard");
    }

    // 18. A multi-oscillator with nothing plugged into its note inputs is silent
    {
        std::unique_ptr<Rack> rack (one ("empty"));
        auto& b = *rack->boards[0];
        auto* osc = rack->addModule (b, "multiosc", 0, 0);
        rack->connect (b, osc->uid, 0, firstOf (b, "output")->uid, 0);
        CHECK (peakOf (render (*rack, 1).l) == 0.0f, "no notes in, no sound out");
    }

    // 19. Follow chords: the bass plays each chord's own note, not just the key note
    {
        std::unique_ptr<Rack> rack (one ("bass"));
        auto& b = *rack->boards[0];
        auto* seq = firstOf (b, "noteseq");
        firstOf (b, "filter")->set ("cutoff", 1.0f); firstOf (b, "filter")->set ("modamt", 0.0f);
        firstOf (b, "drive")->set ("drive", 0.0f); firstOf (b, "osc")->set ("wave", 0);
        seq->set ("gatelen", 1.0f);
        for (int i = 0; i < kMaxSteps; ++i) { seq->data[(size_t) i].store (1.0f); seq->data[(size_t) (kMaxSteps + i)].store (0.0f); }
        CHECK (seq->P (seq->paramIndex ("follow")) == 1.0f, "a new bass follows the chords");
        // find a chord that isn't the home chord and listen to the bass there
        int ci = -1;
        for (size_t i = 0; i < rack->timeline.slots.size(); ++i) if (rack->timeline.slots[i].deg != 0 && rack->timeline.slots[i].tr == 0) { ci = (int) i; break; }
        CHECK (ci >= 0, "the progression leaves the home chord");
        if (ci >= 0)
        {
            const auto& sl = rack->timeline.slots[(size_t) ci];
            const int semis = rack->root.load() + degreeToSemis (sl.deg, scales()[(size_t) rack->scale.load()].iv) - 24;
            const double want = 261.6255653 * std::pow (2.0, semis / 12.0);
            auto a = render (*rack, (sl.start + sl.len) / 16.0);
            const double stepLen = 60.0 / 124.0 / 4.0 * 48000;
            const size_t from = (size_t) ((sl.start + 1) * stepLen), to = (size_t) ((sl.start + sl.len - 1) * stepLen);
            CHECK (std::abs (roughHz (a.l, from, to, 48000) - want) < want * 0.04, "the bass plays the chord's note");
            seq->set ("follow", 0);
            a = render (*rack, (sl.start + sl.len) / 16.0);
            const double home = 261.6255653 * std::pow (2.0, (rack->root.load() - 24) / 12.0);
            CHECK (std::abs (roughHz (a.l, from, to, 48000) - home) < home * 0.04, "with Follow off it stays on the key note");
        }
    }

    // 20. Longer patterns: the second bar plays its own steps, and the loop is as long as Bars says
    {
        std::unique_ptr<Rack> rack (one ("kick"));
        auto* seq = firstOf (*rack->boards[0], "drumseq");
        for (int i = 0; i < kMaxSteps; ++i) seq->data[(size_t) i].store (0.0f);
        seq->data[16].store (2.0f);                       // one hit, at the start of bar 2
        const size_t bar = (size_t) (4 * 60.0 / 124.0 * 48000);
        auto a = render (*rack, 4);
        CHECK (peakOf (a.l) == 0.0f, "one bar long: bar 2's step never plays");
        seq->set ("bars", 1);                             // 2 bars
        a = render (*rack, 4);
        CHECK (peakOf (a.l, 0, bar - 100) == 0.0f && peakOf (a.l, bar, bar + 4800) > 0.2f, "two bars long: it plays in bar 2");
        CHECK (peakOf (a.l, 2 * bar, 3 * bar - 100) == 0.0f && peakOf (a.l, 3 * bar, 3 * bar + 4800) > 0.2f, "and loops every two bars");
        CHECK (loopSteps (16, 0) == 16 && loopSteps (16, 3) == 128 && loopSteps (12, 0) == 12 && loopSteps (12, 1) == 28, "pattern lengths");
        // an Idea fills every bar; Random keeps the kick's four in every bar but varies the bars
        setDrum (*seq, drumIdeas ("kick")[0].pat);
        CHECK (seq->data[0].load() == 2.0f && seq->data[112].load() == 2.0f && seq->data[116].load() == 1.0f, "an idea fills all eight bars");
        bool varied = false;
        for (uint32_t sd = 1; sd < 40; ++sd)
        {
            randomDrum ("kick", sd * 7919u, *seq);
            for (int b2 = 1; b2 < kMaxBars; ++b2)
            {
                for (int i = 0; i < 16; ++i) varied = varied || (seq->data[(size_t) (b2 * 16 + i)].load() > 0) != (seq->data[(size_t) i].load() > 0);
                // beats that bar 1 has stay in every bar
                for (int i = 0; i < 16; i += 4) if (seq->data[(size_t) i].load() > 0) CHECK (seq->data[(size_t) (b2 * 16 + i)].load() > 0, "the kick's beats stay in every bar");
            }
        }
        CHECK (varied, "later bars of a random pattern differ from the first");
        // patterns longer than a bar survive saving
        seq->set ("bars", 3); seq->data[127].store (2.0f);
        Rack other; other.prepare (48000);
        CHECK (other.load (rack->save()) && firstOf (*other.boards[0], "drumseq")->data[127].load() == 2.0f, "an eight-bar pattern saves and loads");
        CHECK (other.save() == rack->save(), "and saves back identically");
    }

    // 21. Notes for a hosted plugin: on when the gate rises, off when it falls, re-played when the pitch moves
    {
        NoteTracker t;
        std::vector<std::pair<int, int>> ev;
        auto emit = [&ev] (int note, int vel) { ev.push_back ({ note, vel }); };
        float pitch[4] = { 0.0f, 4.0f / 12.0f, 7.0f / 12.0f, 1.0f };
        const char two[4] = { 1, 1, 0, 0 };
        t.step (false, pitch, two, 100, emit);
        CHECK (ev.empty(), "no gate, no notes");
        t.step (true, pitch, two, 90, emit);
        CHECK (ev.size() == 2 && ev[0] == std::make_pair (60, 90) && ev[1] == std::make_pair (64, 90), "gate up starts the plugged-in notes only");
        t.step (true, pitch, two, 90, emit);
        CHECK (ev.size() == 2, "a held note isn't sent again");
        pitch[0] = 2.0f / 12.0f;
        t.step (true, pitch, two, 90, emit);
        CHECK (ev.size() == 4 && ev[2] == std::make_pair (60, 0) && ev[3] == std::make_pair (62, 90), "a pitch change ends the old note and starts the new one");
        t.step (false, pitch, two, 90, emit);
        CHECK (ev.size() == 6 && ev[4].second == 0 && ev[5].second == 0, "gate down ends every note");
        int on = 0, off = 0;
        for (auto& e : ev) (e.second > 0 ? on : off)++;
        CHECK (on == off, "every note that starts is ended: nothing left hanging");
        CHECK (NoteTracker::noteOf (-20.0f) == 0 && NoteTracker::noteOf (20.0f) == 127, "notes stay in the playable range");
    }

    // 22. Links: sequencers in the same group, on different tabs, change together
    {
        Rack rack; rack.prepare (48000);
        rack.addBoard (makeBoard ("bass", 48000)); rack.addBoard (makeBoard ("lead", 48000)); rack.addBoard (makeBoard ("kick", 48000));
        auto* bass = firstOf (*rack.boards[0], "noteseq"); auto* lead = firstOf (*rack.boards[1], "noteseq"); auto* kick = firstOf (*rack.boards[2], "drumseq");
        const float bassOct = bass->P (bass->paramIndex ("octave")), leadOct = lead->P (lead->paramIndex ("octave"));
        CHECK (bassOct != leadOct, "bass and lead sit in different octaves");
        rack.linkPattern (*lead, 1, 3);
        const float leadNote5 = lead->data[(size_t) (kMaxSteps + 5)].load();
        rack.linkPattern (*bass, 1, 3);                      // joining takes the group's pattern
        CHECK (bass->data[(size_t) (kMaxSteps + 5)].load() == leadNote5, "a sequencer joining a group takes the group's pattern");
        // edit the bass: the lead follows, notes and rhythm
        bass->data[3].store (1.0f); bass->data[(size_t) (kMaxSteps + 3)].store (9.0f); bass->data[0].store (0.0f);
        rack.pushPattern (*bass);
        CHECK (lead->data[3].load() == 1.0f && lead->data[(size_t) (kMaxSteps + 3)].load() == 9.0f && lead->data[0].load() == 0.0f, "an edit on one shows up on the other");
        CHECK (bass->P (bass->paramIndex ("octave")) == bassOct && lead->P (lead->paramIndex ("octave")) == leadOct, "each keeps its own octave");
        // and the other way round
        lead->data[(size_t) (kMaxSteps + 3)].store (4.0f);
        rack.pushPattern (*lead);
        CHECK (bass->data[(size_t) (kMaxSteps + 3)].load() == 4.0f, "it works in both directions");
        // longer pattern lengths travel with the rhythm
        bass->set ("bars", 2); rack.pushPattern (*bass);
        CHECK (lead->P (lead->paramIndex ("bars")) == 2.0f, "pattern length travels with the rhythm");

        // rhythm only: notes stay separate
        rack.linkPattern (*lead, 1, 1);
        bass->data[7].store (1.0f); bass->data[(size_t) (kMaxSteps + 7)].store (11.0f); lead->data[(size_t) (kMaxSteps + 7)].store (2.0f);
        rack.pushPattern (*bass);
        CHECK (lead->data[7].load() == 1.0f && lead->data[(size_t) (kMaxSteps + 7)].load() == 2.0f, "rhythm only: the hits follow, the notes don't");

        // a drum and a note sequencer can share rhythm; accents don't become notes
        rack.linkPattern (*kick, 1, 3);
        kick->data[2].store (2.0f); kick->data[7].store (0.0f);
        const float noteBefore = bass->data[(size_t) (kMaxSteps + 2)].load();
        rack.pushPattern (*kick);
        CHECK (bass->data[2].load() == 1.0f && bass->data[7].load() == 0.0f, "a drum pattern drives when the bass plays");
        CHECK (noteBefore == bass->data[(size_t) (kMaxSteps + 2)].load(), "and leaves its notes alone");
        CHECK (rack.membersOf (1, true) == 3 && rack.membersOf (2, true) == 0, "group membership is counted");

        // unlinked: no longer follows
        rack.linkPattern (*lead, 0, 3);
        bass->data[9].store (1.0f); lead->data[9].store (0.0f);
        rack.pushPattern (*bass);
        CHECK (lead->data[9].load() == 0.0f, "an unlinked sequencer stops following");

        // links are saved with the project
        Rack other; other.prepare (48000);
        CHECK (other.load (rack.save()) && other.save() == rack.save(), "links save and load");
        CHECK (firstOf (*other.boards[0], "noteseq")->patternLink == 1 && firstOf (*other.boards[2], "drumseq")->patternLink == 1 && firstOf (*other.boards[1], "noteseq")->patternLink == 0, "each sequencer comes back in its group");
    }

    // 22b. Follow link: the follower keeps its own pattern and moves, in key, to match the leader's line
    {
        Rack rack; rack.prepare (48000);
        rack.addBoard (makeBoard ("bass", 48000)); rack.addBoard (makeBoard ("lead", 48000));
        auto& lb = *rack.boards[1];
        auto* leader = firstOf (*rack.boards[0], "noteseq"); auto* follower = firstOf (lb, "noteseq");
        rack.boards[0]->mute.store (true);                       // listen to the follower alone
        for (auto* q : { leader, follower })
        {
            q->set ("follow", 0); q->set ("gatelen", 1.0f);
            for (int i = 0; i < kMaxSteps; ++i) { q->data[(size_t) i].store (1.0f); q->data[(size_t) (kMaxSteps + i)].store (0.0f); }
        }
        // a plain sine lead so pitch is easy to measure
        firstOf (lb, "filter")->set ("cutoff", 1.0f); firstOf (lb, "filter")->set ("modamt", 0.0f); firstOf (lb, "filter")->set ("res", 0.0f);
        for (auto& m : lb.modules) if (std::string (m->info->type) == "osc") { m->set ("wave", 0); m->set ("fine", 0); }
        follower->set ("octave", -1);
        follower->data[(size_t) (kMaxSteps + 3)].store (2.0f);   // the follower's own shape: step 4 is a third up
        const std::string followerBefore = [&] { std::string t; for (int i = 0; i < 32; ++i) t += std::to_string ((int) follower->data[(size_t) (kMaxSteps + i)].load()) + ","; return t; }();

        rack.linkPattern (*leader, 3, 3);
        rack.linkPattern (*follower, 3, 4);
        rack.root.store (9); rack.scale.store (0);               // A minor
        const double a3 = 220.0;
        auto hz = [&] { auto a = render (rack, 1); return roughHz (a.l, 30000, 80000, 48000); };
        CHECK (std::abs (hz() - a3) < 6.0, "leader on the key note: the follower plays where it was written");
        for (int i = 0; i < kMaxSteps; ++i) leader->data[(size_t) (kMaxSteps + i)].store (2.0f);     // leader moves up a third (A to C)
        rack.pushPattern (*leader);
        const std::string followerAfter = [&] { std::string t; for (int i = 0; i < 32; ++i) t += std::to_string ((int) follower->data[(size_t) (kMaxSteps + i)].load()) + ","; return t; }();
        CHECK (followerAfter == followerBefore, "the follower's own pattern is left exactly as it was");
        CHECK (std::abs (hz() - 261.6) < 7.0, "the leader moves up a third and the follower moves with it, in key");
        for (int i = 0; i < kMaxSteps; ++i) leader->data[(size_t) (kMaxSteps + i)].store (9.0f);     // an octave and a third: still just a third for the follower
        CHECK (std::abs (hz() - 261.6) < 7.0, "an octave jump in the leader doesn't throw the follower an octave");
        for (int i = 0; i < kMaxSteps; ++i) leader->data[(size_t) (kMaxSteps + i)].store (i < 16 ? -1.0f : 0.0f);   // one step below the key note
        CHECK (std::abs (hz() - 196.0) < 6.0, "the leader steps down and the follower steps down");
        // the leader resting doesn't drop the follower back: it holds the leader's last note
        for (int i = 0; i < kMaxSteps; ++i) { leader->data[(size_t) (kMaxSteps + i)].store (2.0f); leader->data[(size_t) i].store (i % 16 == 0 ? 1.0f : 0.0f); }
        CHECK (std::abs (hz() - 261.6) < 7.0, "while the leader rests, the follower stays with its last note");
        CHECK (leaderShift (*leader, 5) == 2 && leaderShift (*leader, 16) == 2, "the shift is read from the leader's pattern");
        // unlinked: back to its own key
        rack.linkPattern (*follower, 0, 4);
        CHECK (std::abs (hz() - a3) < 6.0, "unlinked, the follower goes back to where it was written");
        // a follower with no leader in its group just plays as written
        rack.linkPattern (*leader, 0, 3); rack.linkPattern (*follower, 5, 4);
        CHECK (std::abs (hz() - a3) < 6.0, "a follower with nothing to follow plays as written");
        Rack other; other.prepare (48000);
        CHECK (other.load (rack.save()) && firstOf (*other.boards[1], "noteseq")->patternShare == 4, "the follow setting saves with the project");
    }

    // 22c. Separate outputs: a sound sent to Out 2 leaves the main mix and arrives on Out 2
    {
        Rack rack; rack.prepare (48000); loadStarter (rack);
        for (size_t i = 1; i < rack.boards.size(); ++i) rack.boards[i]->mute.store (true);     // the kick alone
        const int n = 48000;
        std::vector<float> l ((size_t) n), r ((size_t) n), l2 ((size_t) n, 9.0f), r2 ((size_t) n, 9.0f);
        Outs outs; outs.l[1] = l2.data(); outs.r[1] = r2.data();
        rack.process (l.data(), r.data(), n, 124, 0, true, &outs);
        CHECK (peakOf (l) > 0.2f && peakOf (l2) == 0.0f, "by default everything is in the main mix, and unused outputs are silent");
        rack.boards[0]->out.store (1);
        rack.process (l.data(), r.data(), n, 124, 0, true, &outs);
        CHECK (peakOf (l) == 0.0f && peakOf (l2) > 0.2f && peakOf (r2) > 0.2f, "sent to Out 2: gone from the main mix, present on Out 2");
        CHECK (peakOf (l2) <= 1.0f, "separate outputs are limited too");
        rack.boards[0]->mute.store (true);
        rack.process (l.data(), r.data(), n, 124, 0, true, &outs);
        CHECK (peakOf (l2) == 0.0f, "mute still works on a separate output");
        rack.boards[0]->mute.store (false);
        // Out 3 isn't switched on in the host: the sound stays in the main mix instead of vanishing
        rack.boards[0]->out.store (2);
        rack.process (l.data(), r.data(), n, 124, 0, true, &outs);
        CHECK (peakOf (l) > 0.2f && peakOf (l2) == 0.0f, "an output that isn't switched on falls back to the main mix");
        CHECK ((rack.outsOn.load() & 2) != 0 && (rack.outsOn.load() & 4) == 0, "the rack knows which outputs are on");
        rack.process (l.data(), r.data(), n, 124, 0, true);
        CHECK (peakOf (l) > 0.2f, "with no separate outputs at all, everything is in the main mix");
        Rack other; other.prepare (48000);
        CHECK (other.load (rack.save()) && other.boards[0]->out.load() == 2 && other.boards[1]->out.load() == 0, "output choices save with the project");
    }

    // 23. Links: knobs in the same group move together, by knob position
    {
        Rack rack; rack.prepare (48000);
        rack.addBoard (makeBoard ("bass", 48000)); rack.addBoard (makeBoard ("pad", 48000));
        auto* f1 = firstOf (*rack.boards[0], "filter"); auto* f2 = firstOf (*rack.boards[1], "filter"); auto* drive = firstOf (*rack.boards[0], "drive");
        const int cut = f1->paramIndex ("cutoff"), drv = drive->paramIndex ("drive"), res = f1->paramIndex ("res");
        rack.linkParam (*f1, cut, 2);
        const float was = f1->P (cut);
        rack.linkParam (*f2, cut, 2);
        CHECK (f2->P (cut) == was, "a knob joining a group takes the group's position");
        f2->params[(size_t) cut].store (0.25f); rack.pushParam (*f2, cut);
        CHECK (f1->P (cut) == 0.25f, "turn one, the other turns");
        CHECK (f1->P (res) != 0.25f || f1->paramLink[(size_t) res] == 0, "knobs not in the group stay put");
        // different kinds of knob share a group by position: half way on one is half way on the other
        rack.linkParam (*drive, drv, 2);
        f1->params[(size_t) cut].store (0.5f); rack.pushParam (*f1, cut);
        CHECK (std::abs (drive->P (drv) - 0.5f) < 1.0e-5f && f2->P (cut) == 0.5f, "every knob in the group goes to the same position");
        rack.linkParam (*f2, cut, 0);
        f1->params[(size_t) cut].store (0.9f); rack.pushParam (*f1, cut);
        CHECK (f2->P (cut) == 0.5f && std::abs (drive->P (drv) - 0.9f) < 1.0e-5f, "an unlinked knob stays where it is");
        Rack other; other.prepare (48000);
        CHECK (other.load (rack.save()) && other.save() == rack.save(), "knob links save and load");
        CHECK (firstOf (*other.boards[0], "filter")->paramLink[(size_t) cut] == 2 && firstOf (*other.boards[1], "filter")->paramLink[(size_t) cut] == 0, "each knob comes back in its group");
    }

    if (failures == 0) std::printf ("All passed\n");
    return failures == 0 ? 0 : 1;
}
