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

    if (failures == 0) std::printf ("All passed\n");
    return failures == 0 ? 0 : 1;
}
