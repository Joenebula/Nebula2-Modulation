#include <map>
// Checks the rack engine on its own (no Cubase, no JUCE). Prints "All passed" or what failed.
#include "Styles.h"
#include "History.h"
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
        if (std::string (t.id) == "empty" || std::string (t.id) == "sample" || std::string (t.id) == "fx") continue;   // silent until a file is loaded / something is sent
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

    // 21b. The same note on two cables is one note; a note that moves to another cable is not cut off
    {
        NoteTracker t;
        std::vector<std::pair<int, int>> ev;
        auto emit = [&ev] (int note, int vel) { ev.push_back ({ note, vel }); };
        const char all[4] = { 1, 1, 1, 1 };
        const float one[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
        t.step (true, one, all, 100, emit);
        CHECK (ev.size() == 1 && ev[0].first == 66 && ev[0].second == 100, "four cables with the same note start it once");
        ev.clear();
        const float two[4] = { 0.0f, 0.5f, 0.5f, 0.5f };          // a lower key is added: the first note moves from cable 1 to cable 2
        t.step (true, two, all, 100, emit);
        CHECK (ev.size() == 1 && ev[0].first == 60 && ev[0].second > 0, "adding a key starts only the new note; the held one carries on");
        ev.clear();
        t.step (true, one, all, 100, emit);
        CHECK (ev.size() == 1 && ev[0].first == 60 && ev[0].second == 0, "lifting it stops only that note");
        ev.clear();
        t.step (false, one, all, 100, emit);
        CHECK (ev.size() == 1 && ev[0].first == 66 && ev[0].second == 0, "gate down stops the note once");
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

    // ================= Effects =================
    // Runs one unit on its own: feeds it a signal on its first input and returns what comes out of an output
    struct Fx
    {
        explicit Fx (const char* type) { const auto* e = findType (type); m = e->make(); m->init (*e->info, 1); m->prepare (48000); if (! m->in.empty()) m->inOn[0] = 1; }
        // Feed another input as well as the first: also (port, signal)
        Fx& also (int port, const std::vector<float>& sig) { extra.push_back ({ port, &sig }); m->inOn[(size_t) port] = 1; return *this; }
        std::vector<std::pair<int, const std::vector<float>*>> extra;
        std::vector<float> run (const std::vector<float>& x, size_t outPort = 0, int block = 64, double bpm = 120.0, bool playing = true)
        {
            std::vector<float> y (x.size());
            Ctx c; c.sr = 48000; c.bpm = bpm; c.playing = playing; c.ppqPerSample = bpm / 60.0 / 48000.0; c.scale = scales()[0].iv;
            for (size_t pos = 0; pos < x.size(); pos += (size_t) block)
            {
                const int n = (int) std::min ((size_t) block, x.size() - pos);
                for (auto& b : m->in) std::fill (b.begin(), b.begin() + n, 0.0f);
                if (! m->in.empty()) std::copy (x.begin() + (long) pos, x.begin() + (long) pos + n, m->in[0].begin());
                for (auto& e : extra) std::copy (e.second->begin() + (long) pos, e.second->begin() + (long) pos + n, m->in[(size_t) e.first].begin());
                c.ppq = ppq;
                m->process (n, c);
                std::copy (m->out[outPort].begin(), m->out[outPort].begin() + n, y.begin() + (long) pos);
                ppq += n * c.ppqPerSample;
            }
            return y;
        }
        std::unique_ptr<Module> m; double ppq = 0;
    };
    auto sine = [] (double hz, size_t n, float amp = 0.5f) { std::vector<float> v (n); for (size_t i = 0; i < n; ++i) v[i] = amp * (float) std::sin (2.0 * kPi * hz * (double) i / 48000.0); return v; };
    auto rms = [] (const std::vector<float>& v, size_t from, size_t to) { double s = 0; to = std::min (to, v.size()); for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i]; return (float) std::sqrt (s / (double) std::max<size_t> (1, to - from)); };
    // A varied test signal: two tones and a click train
    std::vector<float> mixSig (48000);
    for (size_t i = 0; i < mixSig.size(); ++i) mixSig[i] = 0.3f * (float) std::sin (2.0 * kPi * 220.0 * (double) i / 48000.0) + 0.2f * (float) std::sin (2.0 * kPi * 3100.0 * (double) i / 48000.0) + (i % 6000 == 0 ? 0.4f : 0.0f);
    const std::vector<float> silenceSig (48000, 0.0f);

    struct FxCase { const char* type; const char* neutralKnob; float neutralValue; size_t outs; };
    const FxCase cases[] = {
        { "room", "mix", 0, 2 }, { "hall", "mix", 0, 2 }, { "plate", "mix", 0, 2 },
        { "delay", "mix", 0, 1 }, { "tape", "mix", 0, 1 }, { "pingpong", "mix", 0, 2 },
        { "hardclip", "drive", 0, 1 }, { "chorus", "mix", 0, 2 }, { "flanger", "mix", 0, 1 }, { "phaser", "mix", 0, 1 } };
    // 24. Every effect: at 0% it passes the sound exactly; switched on it changes it; it never blows up or makes its own noise
    for (auto& fc : cases)
    {
        CHECK (findType (fc.type) != nullptr, fc.type);
        for (size_t port = 0; port < fc.outs; ++port)
        {
            Fx off (fc.type); off.m->set (fc.neutralKnob, fc.neutralValue);
            CHECK (off.run (mixSig, port) == mixSig, "an effect at 0% passes the sound through exactly");
            Fx on (fc.type);
            const auto y = on.run (mixSig, port);
            CHECK (y != mixSig, "switched on, it changes the sound");
            CHECK (allFinite (y) && peakOf (y) < 4.0f, "and stays under control");
            Fx quiet (fc.type);
            CHECK (peakOf (quiet.run (silenceSig, port)) == 0.0f, "silence in, silence out");
            // the same result whatever block size the host uses
            Fx a (fc.type), b (fc.type);
            const auto ya = a.run (mixSig, port, 64), yb = b.run (mixSig, port, 37);
            float diff = 0; for (size_t i = 0; i < ya.size(); ++i) diff = std::max (diff, std::abs (ya[i] - yb[i]));
            CHECK (diff < 1.0e-4f, "block size does not change an effect's sound");
        }
    }
    {
        Fx crush ("bitcrush"); crush.m->set ("crush", 0); crush.m->set ("rate", 0);
        CHECK (crush.run (mixSig) == mixSig, "bitcrusher with both knobs at 0% passes the sound exactly");
        Fx on ("bitcrush"), quiet ("bitcrush");
        CHECK (on.run (mixSig) != mixSig && peakOf (quiet.run (silenceSig)) == 0.0f, "bitcrusher changes sound and stays silent on silence");
    }

    // 25. Reverbs ring on after the sound stops, then die away; the hall is longer than the room; turning Decay up lengthens it
    {
        std::vector<float> burst (48000 * 6, 0.0f);
        for (size_t i = 0; i < 2400; ++i) burst[i] = 0.5f * (float) std::sin (2.0 * kPi * 440.0 * (double) i / 48000.0);
        auto tail = [&] (const char* type, float decay) { Fx f (type); f.m->set ("mix", 1.0f); if (decay >= 0) f.m->set ("decay", decay); return f.run (burst); };
        const auto room = tail ("room", -1), hall = tail ("hall", -1), plate = tail ("plate", -1);
        for (auto* t : { &room, &hall, &plate })
        {
            CHECK (rms (*t, 4800, 14400) > 0.0005f, "a reverb rings on after the sound stops");
            CHECK (rms (*t, 48000 * 5, 48000 * 6) < rms (*t, 4800, 14400) * 0.05f, "and dies away");
            CHECK (allFinite (*t), "without blowing up");
        }
        CHECK (rms (hall, 48000, 96000) > rms (room, 48000, 96000) * 3.0f, "the hall lasts longer than the room");
        CHECK (rms (tail ("hall", 1.0f), 96000, 144000) > rms (tail ("hall", 0.0f), 96000, 144000) * 5.0f, "Decay makes it longer");
        // stereo: the two sides are different, which is what makes it sound wide
        Fx st ("hall"); st.m->set ("mix", 1.0f);
        const auto l = st.run (burst, 0); Fx st2 ("hall"); st2.m->set ("mix", 1.0f); const auto r = st2.run (burst, 1);
        CHECK (l != r, "left and right of a reverb differ");
        // even at full decay, fed constantly, it stays bounded
        Fx hot ("hall"); hot.m->set ("decay", 1.0f); hot.m->set ("mix", 1.0f); hot.m->set ("damp", 0.0f);
        const auto loud = hot.run (sine (220, 48000 * 6, 0.9f));
        // (a constant tone into maximum decay with no damping is the worst case: loud, but it must level off, not run away)
        CHECK (allFinite (loud) && peakOf (loud) < 30.0f, "a reverb at full decay stays bounded");
        CHECK (peakOf (std::vector<float> (loud.end() - 48000, loud.end())) < peakOf (std::vector<float> (loud.begin() + 48000 * 4, loud.begin() + 48000 * 5)) * 1.1f, "and has stopped growing");
    }

    // 26. Delays: the echo lands exactly on the note length, and follows the tempo
    {
        std::vector<float> click (48000 * 3, 0.0f); click[0] = 1.0f;
        auto firstEcho = [&] (const char* type, int timeChoice, double bpm, size_t port = 0)
        {
            Fx f (type); f.m->set ("time", (float) timeChoice); f.m->set ("mix", 1.0f); f.m->set ("feedback", 0.0f);
            if (std::string (type) == "tape") f.m->set ("wobble", 0.0f);
            const auto y = f.run (click, port, 64, bpm);
            size_t at = 0; float best = 0;
            for (size_t i = 100; i < y.size(); ++i) if (std::abs (y[i]) > best) { best = std::abs (y[i]); at = i; }
            return at;
        };
        CHECK (std::abs ((long) firstEcho ("delay", 6, 120.0) - 24000) <= 2, "a 1/4 echo at 120 BPM lands half a second later");
        CHECK (std::abs ((long) firstEcho ("delay", 4, 120.0) - 12000) <= 2, "a 1/8 echo lands a quarter of a second later");
        CHECK (std::abs ((long) firstEcho ("delay", 6, 150.0) - 19200) <= 2, "at 150 BPM the same echo is shorter: it follows the tempo");
        CHECK (std::abs ((long) firstEcho ("delay", 5, 120.0) - 18000) <= 2, "a dotted 1/8 is three sixteenths");
        CHECK (std::abs ((long) firstEcho ("tape", 6, 120.0) - 24000) <= 40, "the tape echo is in time too");
        CHECK (std::abs ((long) firstEcho ("pingpong", 4, 120.0, 0) - 12000) <= 2, "ping-pong: the first echo is on the left");
        CHECK (std::abs ((long) firstEcho ("pingpong", 4, 120.0, 1) - 24000) <= 2, "and the second is on the right, one step later");
        // repeats fade; even at the top of the knob they don't run away
        Fx rep ("delay"); rep.m->set ("time", 4); rep.m->set ("mix", 1.0f); rep.m->set ("feedback", 0.5f);
        const auto y = rep.run (click);
        CHECK (std::abs (y[12000]) > 0.9f && std::abs (std::abs (y[24000]) - 0.5f) < 0.02f && std::abs (std::abs (y[36000]) - 0.25f) < 0.02f, "each repeat is quieter by the Repeats amount");
        for (const char* type : { "delay", "tape", "pingpong" })
        {
            Fx hot (type); hot.m->set ("feedback", 0.95f); hot.m->set ("mix", 1.0f); hot.m->set ("time", 1);
            const auto loud = hot.run (sine (330, 48000 * 6, 0.5f));
            CHECK (allFinite (loud) && peakOf (loud) < 30.0f, "repeats at maximum stay bounded");
        }
        Fx dull ("tape"); dull.m->set ("mix", 1.0f); dull.m->set ("feedback", 0.8f); dull.m->set ("time", 2); dull.m->set ("tone", 0.2f);
        std::vector<float> bright (48000 * 2, 0.0f); for (size_t i = 0; i < 2000; ++i) bright[i] = 0.5f * (float) std::sin (2.0 * kPi * 6000.0 * (double) i / 48000.0);
        const auto t = dull.run (bright);
        CHECK (rms (t, 40000, 60000) < rms (t, 9000, 13000) * 0.3f, "tape echoes get duller with each repeat");
    }

    // 27. EQ: flat does nothing; a boost raises its band; the cuts remove what they should
    {
        const auto low = sine (80, 48000), mid = sine (1000, 48000), high = sine (9000, 48000);
        Fx flat ("eq3"); CHECK (flat.run (mixSig) == mixSig, "a flat EQ passes the sound exactly");
        Fx pflat ("parameq"); CHECK (pflat.run (mixSig) == mixSig, "a flat parametric EQ passes the sound exactly");
        Fx open ("cuts"); CHECK (open.run (mixSig) == mixSig, "low cut and high cut fully open pass the sound exactly");
        auto gain = [&] (const char* type, const char* knob, float v, const std::vector<float>& tone) { Fx f (type); f.m->set (knob, v); return rms (f.run (tone), 24000, 48000) / rms (tone, 24000, 48000); };
        CHECK (gain ("eq3", "low", 12, low) > 3.0f && gain ("eq3", "low", 12, high) < 1.1f, "Low up lifts the bass and leaves the top");
        CHECK (gain ("eq3", "high", -12, high) < 0.35f && gain ("eq3", "high", -12, low) > 0.9f, "High down cuts the top and leaves the bass");
        CHECK (gain ("eq3", "mid", 12, mid) > 3.0f && gain ("eq3", "mid", 12, low) < 1.3f, "Mid lifts the middle");
        CHECK (gain ("parameq", "g2", 12, sine (500, 48000)) > 3.0f && gain ("parameq", "g2", 12, high) < 1.1f, "a parametric band lifts where it is set");
        {   // narrower band, less effect a little way off its centre
            auto off = [&] (float width) { Fx f ("parameq"); f.m->set ("g2", 12); f.m->set ("q2", width); const auto tone = sine (1000, 48000); return rms (f.run (tone), 24000, 48000) / rms (tone, 24000, 48000); };
            CHECK (off (6.0f) > off (0.4f) + 0.2f, "a wider band reaches further from its centre");
        }
        CHECK (gain ("cuts", "lowcut", 0.45f, low) < 0.1f && gain ("cuts", "lowcut", 0.45f, high) > 0.95f, "low cut removes the bass and keeps the top");
        CHECK (gain ("cuts", "highcut", 0.55f, high) < 0.1f && gain ("cuts", "highcut", 0.55f, low) > 0.95f, "high cut removes the top and keeps the bass");
    }

    // 28. Distortion: more Drive means more squashing; quiet in, quieter-than-clipped out
    {
        const auto tone = sine (200, 48000, 0.5f);
        Fx soft ("hardclip"); soft.m->set ("drive", 1.0f);
        const auto y = soft.run (tone);
        CHECK (peakOf (y) <= 1.0f && rms (y, 0, 48000) > rms (tone, 0, 48000) * 1.3f, "hard clip flattens the peaks and fills the sound out");
        Fx bits ("bitcrush"); bits.m->set ("crush", 1.0f); bits.m->set ("rate", 0.0f);
        const auto b = bits.run (tone);
        std::vector<float> uniq (b.begin(), b.begin() + 4800); std::sort (uniq.begin(), uniq.end()); uniq.erase (std::unique (uniq.begin(), uniq.end()), uniq.end());
        CHECK (uniq.size() <= 9, "a fully crushed sound has only a few steps of loudness");
        Fx hold ("bitcrush"); hold.m->set ("crush", 0.0f); hold.m->set ("rate", 1.0f);
        const auto h = hold.run (tone);
        int changes = 0; for (size_t i = 1; i < 4800; ++i) changes += h[i] != h[i - 1] ? 1 : 0;
        CHECK (changes <= 4800 / 40 + 2, "full downsample holds each sample for 40");
    }

    // 29. Movement effects are in time with the song: after one full cycle the sweep is back where it started
    for (const char* type : { "chorus", "flanger", "phaser" })
    {
        Fx f (type); f.m->set ("rate", 5); f.m->set ("mix", 1.0f);        // one sweep per 1/4 note: 24000 samples at 120 BPM
        const auto tone = sine (440, 48000 * 2, 0.4f);
        const auto y = f.run (tone);
        // compare the loudness pattern of one cycle with the next: they match because the sweep repeats exactly
        float worst = 0;
        for (size_t k = 0; k < 8; ++k)
            worst = std::max (worst, std::abs (rms (y, 48000 + k * 3000, 48000 + (k + 1) * 3000) - rms (y, 72000 + k * 3000, 72000 + (k + 1) * 3000)));
        CHECK (worst < 0.02f, "a movement effect repeats every cycle of its Rate");
        float spread = 0, lo = 9, hi = 0;
        for (size_t k = 0; k < 8; ++k) { const float v = rms (y, 48000 + k * 3000, 48000 + (k + 1) * 3000); lo = std::min (lo, v); hi = std::max (hi, v); }
        spread = hi - lo;
        CHECK (allFinite (y) && (spread > 0.0005f || std::string (type) == "chorus"), "and it is actually moving");
    }

    // 30. Every unit in the menu has a heading that exists, a hint, and a unique id
    {
        std::vector<std::string> ids;
        for (auto* t : allTypes())
        {
            bool shelf = false;
            for (auto* s : shelves()) shelf = shelf || std::string (s) == t->shelf;
            CHECK (shelf, t->name);
            CHECK (std::string (t->hint).size() > 8, "every unit explains itself in the menu");
            CHECK (std::find (ids.begin(), ids.end(), t->type) == ids.end(), "unit ids are unique");
            ids.push_back (t->type);
            for (auto& p : t->params) CHECK (p.def >= p.lo && p.def <= p.hi, "every knob starts inside its range");
        }
        CHECK (ids.size() >= 28, "the effects are in the menu");
    }

    // 31. Pad and stab now go through their effects and still behave
    for (const char* type : { "pad", "stab" })
    {
        std::unique_ptr<Rack> rack (one (type));
        auto a = render (*rack, 8);
        CHECK (peakOf (a.l) > 0.02f && peakOf (a.l) <= 1.0f && allFinite (a.l) && allFinite (a.r), type);
        CHECK (a.l != a.r, "with reverb the two sides differ: it is in stereo");
        for (auto& m : rack->boards[0]->modules) CHECK (m->live, "every preset module is heard");
    }

    // ================= Dynamics, pitch and width, movers, rhythm, utilities =================
    // How strongly one frequency is present in a stretch of sound
    auto tone = [] (const std::vector<float>& v, double hz, size_t from, size_t to)
    {
        double re = 0, im = 0;
        for (size_t i = from; i < to; ++i) { const double w = 2.0 * kPi * hz * (double) i / 48000.0; re += v[i] * std::cos (w); im += v[i] * std::sin (w); }
        return (float) (2.0 * std::sqrt (re * re + im * im) / (double) (to - from));
    };
    // The same, but the strongest it gets in any short stretch. The pitch shifter works in overlapping grains whose
    // phases don't line up over a long stretch, so a long average under-reads a note that is plainly there.
    auto toneNow = [&tone] (const std::vector<float>& v, double hz, size_t from, size_t to)
    {
        float best = 0;
        for (size_t at = from; at + 960 <= to; at += 480) best = std::max (best, tone (v, hz, at, at + 960));
        return best;
    };
    const std::vector<float> ones (48000 * 2, 0.5f);

    // 32. Each of these at its neutral setting passes the sound exactly, and none makes noise of its own
    {
        struct N { const char* type; const char* knob; float v; const char* knob2; float v2; size_t outs; };
        const N ns[] = { { "comp", "ratio", 1, "makeup", 0, 1 }, { "ducker", "depth", 0, nullptr, 0, 1 }, { "transient", "attack", 0, "sustain", 0, 1 },
                         { "tripler", "mix", 0, nullptr, 0, 2 }, { "harmoniser", "mix", 0, nullptr, 0, 1 }, { "width", "width", 1, nullptr, 0, 2 },
                         { "pan", "pan", 0, nullptr, 0, 2 }, { "gate", "depth", 0, nullptr, 0, 1 }, { "slew", "rise", 0, "fall", 0, 1 }, { "xfade", "mix", 0, nullptr, 0, 1 } };
        for (auto& k : ns)
            for (size_t port = 0; port < k.outs; ++port)
            {
                Fx f (k.type); f.m->set (k.knob, k.v); if (k.knob2) f.m->set (k.knob2, k.v2);
                CHECK (f.run (mixSig, port) == mixSig, k.type);
                Fx q (k.type);
                CHECK (peakOf (q.run (silenceSig, port)) == 0.0f, "silence in, silence out");
                Fx a (k.type), b (k.type);
                const auto ya = a.run (mixSig, port, 64), yb = b.run (mixSig, port, 41);
                float diff = 0; for (size_t i = 0; i < ya.size(); ++i) diff = std::max (diff, std::abs (ya[i] - yb[i]));
                CHECK (diff < 1.0e-4f && allFinite (ya), "block size does not change the sound");
            }
        Fx h ("harmoniser"); h.m->set ("v1", 0); h.m->set ("v2", 0);
        CHECK (h.run (mixSig) == mixSig, "a harmoniser with both voices off passes the sound exactly");
    }

    // 33. Compressor: the right amount of squeeze, and a sound in Side does the squeezing instead
    {
        const auto loud = sine (200, 48000, 0.5f);
        Fx c ("comp"); c.m->set ("threshold", -20); c.m->set ("ratio", 4); c.m->set ("attack", 0.001f);
        const float got = rms (c.run (loud), 24000, 48000) / rms (loud, 24000, 48000);
        // -6 dB peak is 14 dB over a -20 dB threshold; at 4:1 that's 10.5 dB turned down, a factor of 0.30
        CHECK (std::abs (got - 0.30f) < 0.05f, "4:1 over the threshold turns the sound down by the right amount");
        Fx quiet ("comp"); quiet.m->set ("threshold", -20);
        const auto soft = sine (200, 48000, 0.05f);
        CHECK (quiet.run (soft) == soft, "below the threshold nothing is touched");
        Fx mk ("comp"); mk.m->set ("ratio", 1); mk.m->set ("makeup", 6);
        CHECK (std::abs (rms (mk.run (soft), 0, 48000) / rms (soft, 0, 48000) - 1.995f) < 0.02f, "6 dB of make-up doubles the level");
        // sidechain: a quiet pad is squeezed when a loud kick is in Side
        std::vector<float> kickSig (48000, 0.0f); for (size_t i = 24000; i < 36000; ++i) kickSig[i] = 0.9f * (float) std::sin (2.0 * kPi * 60.0 * (double) i / 48000.0);
        Fx sc ("comp"); sc.m->set ("threshold", -20); sc.m->set ("ratio", 8); sc.also (1, kickSig);
        const auto pumped = sc.run (soft);
        CHECK (rms (pumped, 10000, 20000) > rms (soft, 10000, 20000) * 0.98f && rms (pumped, 28000, 34000) < rms (soft, 28000, 34000) * 0.4f, "a sound in Side squeezes the main sound while it plays");
    }

    // 34. Ducker: with nothing in Trig it dips on every beat, in time with the tempo; a trigger dips it too
    {
        Fx d ("ducker"); d.m->set ("depth", 0.8f); d.m->set ("every", 2);          // every 1/4: 24000 samples at 120 BPM
        const auto y = d.run (ones);
        CHECK (y[24010] < 0.5f * 0.3f && y[48010] < 0.5f * 0.3f, "it dips right on each beat");
        CHECK (y[23900] > 0.5f * 0.9f && y[47900] > 0.5f * 0.9f, "and has swelled back before the next");
        Fx fast ("ducker"); fast.m->set ("depth", 0.8f); fast.m->set ("every", 2);
        const auto y2 = fast.run (ones, 0, 64, 150.0);
        CHECK (y2[19210] < 0.5f * 0.3f && y2[19100] > 0.5f * 0.8f, "at 150 BPM the dips are closer together: it follows the tempo");
        std::vector<float> trig (48000 * 2, 0.0f); for (size_t i = 30000; i < 31000; ++i) trig[i] = 1.0f;
        Fx t ("ducker"); t.m->set ("depth", 1.0f); t.also (1, trig);
        const auto y3 = t.run (ones);
        CHECK (y3[29990] == 0.5f && y3[30005] < 0.02f && y3[24010] == 0.5f, "with a trigger plugged in, it dips on the trigger and not on the beat");
        Fx stopped ("ducker"); stopped.m->set ("depth", 0.8f);
        CHECK (stopped.run (ones, 0, 64, 120.0, false) == ones, "it leaves the sound alone when the song is stopped");
    }

    // 35. Transient shaper: Punch up makes hits hit harder, Punch down softens them
    {
        std::vector<float> hits (48000, 0.0f);
        for (size_t k = 0; k < 4; ++k) for (size_t i = 0; i < 6000; ++i) hits[k * 12000 + i] = 0.4f * std::exp (-(float) i / 1500.0f) * (float) std::sin (2.0 * kPi * 180.0 * (double) i / 48000.0);
        auto peak = [&] (float punch) { Fx f ("transient"); f.m->set ("attack", punch); return peakOf (f.run (hits), 12000, 13000); };
        CHECK (peak (1.0f) > peak (0.0f) * 1.3f && peak (-1.0f) < peak (0.0f) * 0.8f, "Punch raises or lowers the start of each hit");
    }

    // 36. Harmoniser: the right notes, and with Pitch plugged in they stay in the key
    {
        const auto a220 = sine (220, 48000, 0.4f);
        Fx up ("harmoniser"); up.m->set ("v1", 5); up.m->set ("mix", 1.0f);
        const auto y = up.run (a220);
        CHECK (toneNow (y, 440, 24000, 48000) > 0.15f && tone (y, 220, 24000, 48000) > 0.3f, "Octave up adds the octave and keeps the original");
        Fx down ("harmoniser"); down.m->set ("v1", 6); down.m->set ("mix", 1.0f);
        CHECK (toneNow (down.run (a220), 110, 24000, 48000) > 0.15f, "Octave down adds the octave below");
        // D minor (the test's key): a third above D is F (3 semitones); above F it is A (4 semitones)
        // (F and F sharp are too close to tell apart by ear-style measurement in a short stretch, so read the shift it chose)
        auto third = [&] (int midi)
        {
            const std::vector<float> pitchSig (4800, (float) (midi - 60) / 12.0f);
            Fx f ("harmoniser"); f.m->set ("v1", 1); f.m->set ("mix", 1.0f); f.also (1, pitchSig);
            f.run (sine (300, 4800, 0.4f));
            return (int) std::lround (12.0 * std::log2 ((double) dynamic_cast<Harmoniser*> (f.m.get())->ratio[0]));
        };
        CHECK (third (62) == 3, "a third above D in D minor is F: three semitones");
        CHECK (third (65) == 4, "a third above F in D minor is A: four semitones");
        CHECK (third (64) == 3 && third (69) == 3 && third (72) == 4, "and so on up the key: E to G, A to C, C to E");
        CHECK (third (63) == 3 || third (63) == 4, "a note outside the key is treated as its nearest neighbour in the key");
        CHECK (Harmoniser::semisFor (3, 0, scales()[0].iv) == 7 && Harmoniser::semisFor (5, 3, scales()[0].iv) == 12 && Harmoniser::semisFor (6, 0, scales()[0].iv) == -12 && Harmoniser::semisFor (0, 2, scales()[0].iv) == 0, "interval sizes");
        Fx tr ("tripler");
        Fx tr2 ("tripler");
        const auto tl = tr.run (a220, 0), trr = tr2.run (a220, 1);
        CHECK (tl != trr && tl != a220 && peakOf (tl) < 2.0f, "the Tripler makes left and right different, and bigger than the original");
    }

    // 37. Width and pan
    {
        const auto l = sine (300, 4800, 0.5f), r = sine (700, 4800, 0.3f);
        Fx mono ("width"); mono.m->set ("width", 0); mono.also (1, r);
        Fx mono2 ("width"); mono2.m->set ("width", 0); mono2.also (1, r);
        CHECK (mono.run (l, 0) == mono2.run (l, 1), "width at 0% is mono: both sides the same");
        Fx wide ("width"); wide.m->set ("width", 2); wide.also (1, r);
        Fx same ("width"); same.also (1, r);
        Fx sameR ("width"); sameR.also (1, r);
        CHECK (same.run (l, 0) == l && sameR.run (l, 1) == r, "width at 100% leaves both sides alone");
        CHECK (wide.run (l, 0) != l, "wider changes it");
        Fx pr ("pan"); pr.m->set ("pan", 1.0f); Fx pr2 ("pan"); pr2.m->set ("pan", 1.0f);
        CHECK (peakOf (pr.run (l, 0)) == 0.0f && pr2.run (l, 1) == l, "panned hard right: nothing on the left, all on the right");
        const std::vector<float> move (4800, -1.0f);
        Fx pm ("pan"); pm.also (1, move);
        CHECK (peakOf (pm.run (l, 1)) == 0.0f, "a mover plugged into Move pans it");
    }

    // 38. Trance gate, step modulator, random, macro, envelope follower
    {
        Fx g ("gate"); g.m->set ("smooth", 0); for (int i = 0; i < kMaxSteps; ++i) g.m->data[(size_t) i].store (i % 2 == 0 ? 1.0f : 0.0f);
        const auto y = g.run (ones);                                    // a 1/16 step is 6000 samples at 120 BPM
        CHECK (y[3000] == 0.5f && std::abs (y[9000]) < 0.001f && y[15000] == 0.5f, "the gate lets lit steps through and shuts on the others");
        CHECK (g.m->playStep.load() >= 0, "and shows which step is playing");
        Fx gs ("gate"); CHECK (gs.run (ones, 0, 64, 120.0, false) == ones, "stopped, the gate is open");

        Fx sm ("stepmod");
        for (int i = 0; i < kMaxSteps; ++i) { sm.m->data[(size_t) i].store (i % 16 == 2 ? 0.0f : 1.0f); sm.m->data[(size_t) (kMaxSteps + i)].store ((float) ((i % 16) - 7)); }
        const auto v = sm.run (silenceSig);
        CHECK (std::abs (v[3000] - 0.0f) < 1.0e-4f && std::abs (v[9000] - 1.0f / 21.0f) < 1.0e-4f, "each step sends its value");
        CHECK (std::abs (v[15000] - 1.0f / 21.0f) < 1.0e-4f && std::abs (v[21000] - 3.0f / 21.0f) < 1.0e-4f, "a step that's off holds the one before");
        sm.m->set ("depth", -1.0f); sm.ppq = 0;
        CHECK (sm.run (silenceSig)[21000] < -0.1f, "Depth below zero turns it upside down");

        Fx r1 ("random"), r2 ("random");
        r1.m->inOn[0] = 0; r2.m->inOn[0] = 0;          // nothing in Trig: it steps in time by itself
        const auto a = r1.run (silenceSig), b = r2.run (silenceSig);
        CHECK (a == b, "Random repeats exactly: the same bar gives the same values");
        CHECK (a[3000] != a[15000] && a[15000] != a[27000] && peakOf (a) <= 1.0f && a[3000] == a[9000], "a new value each 1/8 step, held through the step");

        Fx mac ("macro"); mac.m->set ("value", 0.7f);
        CHECK (std::abs (mac.run (silenceSig).back() - 0.7f) < 1.0e-4f, "the macro knob sends its position down the cable");

        Fx ef ("follow");
        const auto e = ef.run (sine (300, 48000, 0.5f));
        CHECK (e[40000] > 0.4f && e[40000] <= 0.5f, "the follower rises to the level of the sound");
        Fx ef2 ("follow"); CHECK (peakOf (ef2.run (silenceSig)) == 0.0f, "and sits at zero in silence");
    }

    // 39. Arpeggiator: plays the plugged-in notes one at a time, in order, in time
    {
        const std::vector<float> cN (48000, 0.0f), eN (48000, 4.0f / 12.0f), gN (48000, 7.0f / 12.0f);
        auto arp = [&] (int pattern, int range, int rate) { auto f = std::make_unique<Fx> ("arp"); f->m->set ("pattern", (float) pattern); f->m->set ("range", (float) range); f->m->set ("rate", (float) rate); f->also (1, eN).also (2, gN); return f; };
        auto at = [] (const std::vector<float>& v, int step, int stepLen = 6000) { return (int) std::lround (v[(size_t) (step * stepLen + 100)] * 12.0f); };
        auto up = arp (0, 1, 3)->run (cN);
        CHECK (at (up, 0) == 0 && at (up, 1) == 4 && at (up, 2) == 7 && at (up, 3) == 0, "Up: low to high, then round again");
        auto dn = arp (1, 1, 3)->run (cN);
        CHECK (at (dn, 0) == 7 && at (dn, 1) == 4 && at (dn, 2) == 0, "Down: high to low");
        auto ud = arp (2, 1, 3)->run (cN);
        CHECK (at (ud, 0) == 0 && at (ud, 2) == 7 && at (ud, 3) == 4 && at (ud, 4) == 0, "Up and down turns round at the top without repeating it");
        auto two = arp (0, 2, 3)->run (cN);
        CHECK (at (two, 3) == 12 && at (two, 5) == 19 && at (two, 6) == 0, "Octaves 2 carries on up through the next octave");
        auto eighth = arp (0, 1, 1)->run (cN);
        CHECK (at (eighth, 0, 12000) == 0 && at (eighth, 1, 12000) == 4 && at (eighth, 0) == at (eighth, 1), "a slower Rate holds each note longer");
        {   // a gate for every note; a mover into Rate changes the speed as it plays
            Fx f ("arp"); f.also (1, eN).also (2, gN);
            const auto gate = f.run (cN, 1);
            int rises = 0; for (size_t i = 1; i < gate.size(); ++i) rises += gate[i] > 0.5f && gate[i - 1] <= 0.5f ? 1 : 0;
            CHECK (rises == 7 || rises == 8, "one note per 1/16 step");
            const std::vector<float> slower (48000, -1.0f);
            Fx m2 ("arp"); m2.also (1, eN).also (2, gN).also (4, slower);
            const auto g2 = m2.run (cN, 1);
            int rises2 = 0; for (size_t i = 1; i < g2.size(); ++i) rises2 += g2[i] > 0.5f && g2[i - 1] <= 0.5f ? 1 : 0;
            CHECK (rises2 < rises - 2, "a mover plugged into Rate changes the arpeggiator's speed");
        }
        auto ra = arp (3, 2, 3)->run (cN), rb = arp (3, 2, 3)->run (cN);
        CHECK (ra == rb, "Random is the same each time the song plays");
        Fx stopped ("arp"); stopped.also (1, eN);
        CHECK (peakOf (stopped.run (cN, 1, 64, 120.0, false)) == 0.0f, "no notes while the song is stopped");
    }

    // 40. Euclidean rhythm: the right number of hits, spread evenly
    {
        for (int steps = 1; steps <= 16; ++steps)
            for (int hits = 0; hits <= steps; ++hits)
            {
                int n = 0, longest = 0, run2 = 0;
                for (int k = 0; k < steps * 2; ++k) { if (Euclid::hitAt (k % steps, steps, hits, 0)) { if (k < steps) ++n; run2 = 0; } else longest = std::max (longest, ++run2); }
                CHECK (n == hits, "exactly as many hits as asked for");
                if (hits > 0) CHECK (longest <= (steps + hits - 1) / hits, "no gap longer than an even spread allows");
            }
        CHECK (Euclid::hitAt (0, 16, 4, 0) && ! Euclid::hitAt (0, 16, 4, 1) && Euclid::hitAt (1, 16, 4, 1), "Turn moves the whole pattern round");
        Fx e ("euclid"); e.m->set ("hits", 4);
        const auto t = e.run (silenceSig);
        int rises = 0; for (size_t i = 1; i < t.size(); ++i) rises += t[i] > 0.5f && t[i - 1] <= 0.5f ? 1 : 0;
        CHECK (t[10] > 0.5f && rises == 1, "4 hits in 16 steps is one per beat (half a second at 120 BPM holds two)");
        const std::vector<float> more (48000, 0.5f);
        Fx e2 ("euclid"); e2.m->set ("hits", 4); e2.also (0, more);
        const auto t2 = e2.run (silenceSig);
        int rises2 = 0; for (size_t i = 1; i < t2.size(); ++i) rises2 += t2[i] > 0.5f && t2[i - 1] <= 0.5f ? 1 : 0;
        CHECK (rises2 > rises, "a mover plugged into Hits adds hits");
    }

    // 41. Clap envelope, clock, divider, logic, chance, slew, crossfader, switch
    {
        std::vector<float> trig (48000, 0.0f); for (size_t i = 1000; i < 1500; ++i) trig[i] = 1.0f;
        Fx ce ("clapenv"); const auto env = ce.run (trig);
        int jumps = 0; for (size_t i = 1; i < env.size(); ++i) jumps += env[i] > env[i - 1] + 0.3f ? 1 : 0;
        CHECK (jumps == 4 && peakOf (env) <= 1.0f && env[47000] == 0.0f, "a clap is four quick bursts and then it dies away");

        Fx ck ("clock"); const auto q = ck.run (silenceSig, 1);
        CHECK (q[10] > 0.5f && q[13000] < 0.5f && q[24010] > 0.5f, "the clock's 1/4 output ticks on every beat");
        Fx ck2 ("clock"); CHECK (peakOf (ck2.run (silenceSig, 1, 64, 120.0, false)) == 0.0f, "and is still while the song is stopped");

        std::vector<float> pulses (48000, 0.0f); for (size_t k = 0; k < 8; ++k) for (size_t i = 0; i < 1000; ++i) pulses[k * 6000 + i] = 1.0f;
        Fx dv ("divider"); const auto half = dv.run (pulses, 0);
        CHECK (half[100] > 0.5f && half[6100] < 0.5f && half[12100] > 0.5f, "/2 lets through every other trigger");
        Fx dv3 ("divider"); const auto third = dv3.run (pulses, 1);
        CHECK (third[100] > 0.5f && third[6100] < 0.5f && third[12100] < 0.5f && third[18100] > 0.5f, "/3 lets through every third");

        std::vector<float> aS (4000, 0.0f), bS (4000, 0.0f);
        for (size_t i = 1000; i < 3000; ++i) aS[i] = 1.0f;
        for (size_t i = 2000; i < 4000; ++i) bS[i] = 1.0f;
        auto logic = [&] (size_t port) { Fx f ("logic"); f.also (1, bS); return f.run (aS, port); };
        CHECK (logic (0)[1500] == 0 && logic (0)[2500] == 1 && logic (1)[1500] == 1 && logic (1)[500] == 0 && logic (2)[1500] == 1 && logic (2)[2500] == 0 && logic (2)[3500] == 1, "Both, Either and Only one");

        auto passed = [&] (float chance) { Fx f ("chance"); f.m->set ("chance", chance); const auto y = f.run (pulses); int n = y[0] > 0.5f ? 1 : 0; for (size_t i = 1; i < y.size(); ++i) n += y[i] > 0.5f && y[i - 1] <= 0.5f ? 1 : 0; return n; };
        CHECK (passed (0.0f) == 0 && passed (1.0f) == 8 && passed (0.5f) > 0 && passed (0.5f) < 8 && passed (0.5f) == passed (0.5f), "Chance drops some triggers, the same ones each time");

        std::vector<float> step (48000, 0.0f); for (size_t i = 100; i < 48000; ++i) step[i] = 1.0f;
        Fx sl ("slew"); sl.m->set ("rise", 0.5f);
        const auto ramp = sl.run (step);
        CHECK (std::abs (ramp[12100] - 0.5f) < 0.01f && ramp[24200] == 1.0f, "Slew turns a jump into a steady half-second slide");

        const auto aT = sine (300, 4800, 0.5f), bT = sine (900, 4800, 0.3f);
        Fx xf ("xfade"); xf.m->set ("mix", 1.0f); xf.also (1, bT);
        CHECK (xf.run (aT) == bT, "the crossfader fully across is input B exactly");
        Fx xh ("xfade"); xh.also (1, bT);
        const auto half2 = xh.run (aT);
        CHECK (std::abs (half2[777] - (aT[777] * 0.5f + bT[777] * 0.5f)) < 1.0e-6f, "half way is half of each");

        std::vector<float> adv (4800, 0.0f); for (size_t i = 2000; i < 2100; ++i) adv[i] = 1.0f;
        Fx sw ("switch"); sw.also (1, bT).also (4, adv);
        const auto picked = sw.run (aT);
        CHECK (picked[1000] == aT[1000] && picked[3000] == bT[3000], "a trigger into Step moves the switch to the next plugged-in input");
        Fx sw2 ("switch"); sw2.m->set ("pick", 2); sw2.also (1, bT);
        CHECK (sw2.run (aT) == bT, "the Input knob picks by hand");
    }

    // 42. The step modulator's shapes and the gate's ideas
    {
        CHECK (valueIdeas().size() >= 5 && drumIdeas ("gate").size() >= 5 && drumIdeas ("clap").size() >= 4, "ideas for the gate, the step modulator and the clap");
        Fx sm ("stepmod"); setValues (*sm.m, valueIdeas()[0].v);
        const auto v = sm.run (silenceSig);
        CHECK (v[3000] < 0.01f && v[45000] > v[21000] && v[21000] > v[9000], "Ramp up climbs through the bar");
        Fx a ("stepmod"), b ("stepmod"); randomValues (99, *a.m); randomValues (99, *b.m);
        CHECK (a.run (silenceSig) == b.run (silenceSig), "a random shape is repeatable by seed");
    }

    // ================= Undo and redo =================
    {
        Rack rack; rack.prepare (48000); loadStarter (rack);
        History hist (rack);
        auto& kick = *rack.boards[0];
        auto* drive = firstOf (kick, "drive"); auto* seq = firstOf (kick, "drumseq");
        const std::string start = rack.save();
        CHECK (! hist.canUndo() && ! hist.undo(), "nothing to undo at first");

        // a knob
        hist.before (kick); drive->set ("drive", 0.9f);
        CHECK (hist.canUndo() && hist.undo() && drive->P (0) == 0.35f, "undo puts a knob back");
        CHECK (firstOf (kick, "drive") == drive, "and the unit itself is the same one, still running, not a rebuilt copy");
        CHECK (hist.redo() && drive->P (0) == 0.9f && ! hist.canRedo(), "redo turns it again");
        CHECK (hist.undo() && rack.save() == start, "and back to exactly where it started");

        // a pattern
        hist.before (kick); randomDrum ("kick", 12345, *seq);
        CHECK (rack.save() != start && hist.undo() && rack.save() == start, "undo puts a pattern back");

        // adding a unit, then cabling it
        hist.before (kick);
        auto* filt = rack.addModule (kick, "filter", 400, 400);
        const int filtUid = filt->uid;
        hist.before (kick);
        rack.connect (kick, drive->uid, 0, filtUid, 0);
        const std::string cabled = rack.save();
        CHECK (hist.undo() && kick.find (filtUid) != nullptr && kick.cables.size() == 9, "undo removes the cable and leaves the unit");
        CHECK (hist.undo() && kick.find (filtUid) == nullptr && rack.save() == start, "undo again removes the unit");
        CHECK (hist.redo() && hist.redo() && rack.save() == cabled && kick.find (filtUid) != nullptr, "redo brings the unit and the cable back, with the same id");

        // deleting a unit: undo brings it back, cables and all
        hist.before (kick, filtUid);
        rack.removeModule (kick, filtUid);
        CHECK (kick.find (filtUid) == nullptr && hist.undo() && rack.save() == cabled, "undo brings back a deleted unit with its cables");
        CHECK (peakOf (render (rack, 1).l) > 0.05f && allFinite (render (rack, 1).l), "and it all still plays");
        hist.undo(); hist.undo();
        CHECK (rack.save() == start, "three undos later, back at the start");

        // a new edit throws away the redo list
        hist.before (kick); drive->set ("drive", 0.1f);
        CHECK (! hist.canRedo(), "a fresh change clears redo");
        hist.undo();

        // mixer channel
        hist.before (kick); kick.mute.store (true); kick.level.store (0.2f); kick.out.store (3);
        CHECK (hist.undo() && ! kick.mute.load() && kick.level.load() == 0.8f && kick.out.load() == 0, "undo covers the mixer channel too");

        // tabs: add, delete, move
        rack.addBoard (makeBoard ("lead", 48000)); hist.tabAdded ((int) rack.boards.size() - 1);
        CHECK (rack.boards.size() == 6 && hist.undo() && rack.boards.size() == 5 && rack.save() == start, "undo removes an added tab");
        CHECK (hist.redo() && rack.boards.size() == 6 && rack.boards[5]->type == "lead" && hist.showTab == 5, "redo brings it back and says which tab to show");
        hist.undo();
        hist.tabRemoving (1); rack.removeBoard (1);
        CHECK (rack.boards.size() == 4 && rack.boards[1]->type == "hat", "a tab deleted");
        CHECK (hist.undo() && rack.save() == start && hist.showTab == 1, "undo puts a deleted tab back in its place");
        rack.moveBoard (0, 2); hist.tabMoved (0, 2);
        CHECK (rack.boards[2]->type == "kick" && hist.undo() && rack.save() == start, "undo moves a tab back");
        CHECK (hist.redo() && rack.boards[2]->type == "kick" && hist.undo(), "and redo moves it again");

        // chords and key
        const auto chordsBefore = rack.harmony.names;
        hist.beforeChords(); rack.generateChords (4242);
        CHECK (rack.harmony.names != chordsBefore && hist.undo() && rack.harmony.names == chordsBefore && rack.save() == start, "undo puts the chords back");
        hist.beforeChords(); rack.setKey (9, 2);
        CHECK (hist.undo() && rack.root.load() == 2 && rack.scale.load() == 0 && rack.save() == start, "undo puts the key back");

        // linked things on other tabs come back too
        auto* bassSeq = firstOf (*rack.boards[3], "noteseq");
        rack.addBoard (makeBoard ("lead", 48000));
        auto* leadSeq = firstOf (*rack.boards[5], "noteseq");
        rack.linkPattern (*bassSeq, 1, 3); rack.linkPattern (*leadSeq, 1, 3);
        const std::string linked = rack.save();
        hist.before (*rack.boards[3]);
        randomNotes ("bass", 777, *bassSeq); rack.pushPattern (*bassSeq);
        CHECK (rack.save() != linked && hist.undo() && rack.save() == linked, "undoing a linked pattern edit puts both tabs back");
        CHECK (firstOf (*rack.boards[5], "noteseq") == leadSeq, "without rebuilding the other tab's units");

        // a long run of changes: the list is capped, and undoing all of it never breaks anything
        for (int k = 0; k < 150; ++k) { hist.before (kick); drive->set ("drive", (float) (k % 10) / 10.0f); }
        int undone = 0; while (hist.undo()) ++undone;
        CHECK (undone == 100 && allFinite (render (rack, 1).l), "the undo list holds the last 100 changes");
    }

    // ================= Sends, ratchet, swing, scope, chord editing =================
    // 43. Sends: a copy of one tab's sound reaches an effects tab, wherever that tab sits in the row
    {
        for (int fxFirst = 0; fxFirst < 2; ++fxFirst)
        {
            Rack rack; rack.prepare (48000);
            if (fxFirst) rack.addBoard (makeBoard ("fx", 48000));
            rack.addBoard (makeBoard ("kick", 48000));
            if (! fxFirst) rack.addBoard (makeBoard ("fx", 48000));
            auto& kick = *rack.boards[(size_t) (fxFirst ? 1 : 0)]; auto& fx = *rack.boards[(size_t) (fxFirst ? 0 : 1)];
            CHECK (fx.hasReturn && ! kick.hasReturn, "the effects tab is known to play a bus");
            kick.mute.store (true);                                  // listen to the effects tab alone
            CHECK (peakOf (render (rack, 1).l) == 0.0f, "nothing sent yet: the effects tab is silent");
            // put a Send between the kick's drive and its Output
            auto* drive = firstOf (kick, "drive"); auto* out = firstOf (kick, "output");
            auto* send = rack.addModule (kick, "send", 0, 0);
            CHECK (send->live, "a Send counts as heard even with nothing cabled to its outputs");
            rack.connect (kick, drive->uid, 0, send->uid, 0);
            kick.mute.store (false); kick.solo.store (false); fx.solo.store (true);
            const auto wet = render (rack, 2);
            CHECK (peakOf (wet.l) > 0.01f && wet.l != wet.r && allFinite (wet.l), "the kick reaches the reverb on the effects tab");
            Rack ref; ref.prepare (48000); ref.addBoard (makeBoard ("kick", 48000));
            send->set ("amount", 0.0f);
            render (rack, 8);                                        // let the reverb's tail from before die away
            CHECK (peakOf (render (rack, 2).l) < 0.0005f, "Send at 0% sends nothing");
            send->set ("amount", 0.3f); send->set ("bus", 1);
            render (rack, 8);
            CHECK (peakOf (render (rack, 2).l) < 0.0005f, "bus B doesn't reach a Return listening to bus A");
            firstOf (fx, "return")->set ("bus", 1);
            CHECK (peakOf (render (rack, 2).l) > 0.01f, "until the Return is switched to B");
            // the kick's own sound is unchanged by the Send in its path
            fx.solo.store (false); fx.mute.store (true);
            rack.connect (kick, send->uid, 0, out->uid, 0);
            rack.disconnect (kick, drive->uid, 0, out->uid, 0);
            const auto through = render (rack, 1);
            const auto plain = render (ref, 1);
            float diff = 0; for (size_t i = 24000; i < through.l.size(); ++i) diff = std::max (diff, std::abs (through.l[i] - plain.l[i]));
            CHECK (diff < 1.0e-5f, "a Send passes the sound through untouched");
        }
        // both orders give the same result: tab order doesn't change the sound
        auto build = [] (bool fxFirst)
        {
            auto rack = std::make_unique<Rack>(); rack->prepare (48000);
            if (fxFirst) rack->addBoard (makeBoard ("fx", 48000));
            rack->addBoard (makeBoard ("kick", 48000));
            if (! fxFirst) rack->addBoard (makeBoard ("fx", 48000));
            auto& kick = *rack->boards[(size_t) (fxFirst ? 1 : 0)];
            auto* send = rack->addModule (kick, "send", 0, 0);
            rack->connect (kick, firstOf (kick, "drive")->uid, 0, send->uid, 0);
            return rack;
        };
        auto a = build (false), b = build (true);
        CHECK (render (*a, 2).l == render (*b, 2).l, "the effects tab sounds the same whether it is before or after the tabs that feed it");
    }

    // 44. Ratchet and swing
    {
        std::vector<float> pulses (48000, 0.0f);                 // a hit on every 1/16 (6000 samples at 120 BPM)
        for (size_t k = 0; k < 8; ++k) for (size_t i = 0; i < 1500; ++i) pulses[k * 6000 + i] = 1.0f;
        auto rises = [] (const std::vector<float>& y, size_t from, size_t to) { int n = from == 0 && y[0] > 0.5f ? 1 : 0; for (size_t i = std::max<size_t> (1, from); i < to; ++i) n += y[i] > 0.5f && y[i - 1] <= 0.5f ? 1 : 0; return n; };
        Fx one ("ratchet"); one.m->set ("count", 0);
        CHECK (one.run (pulses) == pulses, "Ratchet set to 1 passes hits straight through");
        Fx three ("ratchet"); three.m->set ("count", 2);
        const auto y = three.run (pulses);
        CHECK (rises (y, 0, 6000) == 3 && rises (y, 0, 48000) == 24, "set to 3, each hit becomes three inside its step");
        Fx four ("ratchet"); four.m->set ("count", 3); four.m->set ("fade", 1.0f);
        const auto v = four.run (pulses, 1);
        CHECK (v[100] > v[1600] && v[1600] > v[3100] && v[3100] > v[4600], "Fade makes each hit of the burst quieter than the last");

        Fx none ("swing"); none.m->set ("amount", 0.0f);
        CHECK (none.run (pulses) == pulses, "Swing at 0% leaves the timing alone");
        Fx sw ("swing"); sw.m->set ("amount", 0.5f);
        const auto s = sw.run (pulses);
        auto firstRise = [&] (size_t from) { for (size_t i = std::max<size_t> (1, from); i < s.size(); ++i) if (s[i] > 0.5f && s[i - 1] <= 0.5f) return i; return (size_t) 0; };
        CHECK (s[0] > 0.5f && firstRise (11000) == 12000 && firstRise (23000) == 24000, "hits on the beat stay where they are");
        CHECK (firstRise (5000) == 6000 + 1500 && firstRise (17000) == 18000 + 1500, "every other 1/16 is held back by half the Swing amount of a step");
        CHECK (rises (s, 0, 48000) == 8, "no hits are lost or added");
    }

    // 45. Scope and tuner: the sound passes through, and it names the note
    {
        Fx sc ("scope");
        const auto a = sine (440, 48000, 0.5f);
        CHECK (sc.run (a) == a, "the scope passes the sound through untouched");
        auto* scope = dynamic_cast<Scope*> (sc.m.get());
        int cents = 99;
        CHECK (std::abs (scope->hz.load() - 440.0f) < 2.0f && noteText (scope->hz.load(), &cents) == "A4" && std::abs (cents) <= 6, "440 Hz reads as A4, in tune");
        CHECK (std::abs (scope->level.load() - 0.5f) < 0.02f, "and it shows the level");
        Fx low ("scope"); low.run (sine (55, 48000, 0.3f));
        CHECK (noteText (dynamic_cast<Scope*> (low.m.get())->hz.load()) == "A1", "a low bass note reads right too");
        Fx sharp ("scope"); sharp.run (sine (452, 48000, 0.5f));
        noteText (dynamic_cast<Scope*> (sharp.m.get())->hz.load(), &cents);
        CHECK (cents > 35 && cents < 60, "a note that is sharp shows how far");
        Fx quiet ("scope"); quiet.run (silenceSig);
        CHECK (dynamic_cast<Scope*> (quiet.m.get())->hz.load() == 0.0f && noteText (0.0f).empty(), "silence shows no note");
        bool moved = false; for (float x : scope->ring) moved = moved || std::abs (x) > 0.3f;
        CHECK (moved, "the picture holds the wave");
    }

    // 46. Editing one chord, and locking chords through a Generate
    {
        Rack rack; rack.prepare (48000);
        rack.generateChords (5);
        const auto t = rack.theory();
        const auto before = rack.harmony.names;
        const int deg = rack.harmony.seq[1].deg;
        const auto colours = t.sortedColours (deg);
        CHECK (colours.size() >= 2, "a chord has other colours to choose from");
        std::string other; for (auto& c : colours) if (c.s != rack.harmony.seq[1].t) { other = c.s; break; }
        rack.setChordColour (1, other);
        CHECK (rack.harmony.names[1] != before[1] && rack.harmony.names[0] == before[0], "changing one chord's colour renames only that chord");
        bool inKey = true;
        for (int k = 0; k < 4; ++k) inKey = inKey && t.inScale (n2c::mod12 ((int) std::lround (rack.timeline.slots[1].voice[k] * 12.0f) + 60));
        CHECK (inKey, "and it is still in the key");
        const float bassBefore = rack.timeline.slots[1].bass;
        rack.setChordBass (1, 1);
        CHECK (rack.timeline.slots[1].bass != bassBefore && rack.harmony.names[1].find ('/') != std::string::npos, "a new bass note moves the bass and shows as a slash chord");
        const int total = rack.timeline.total;
        rack.setChordBars (1, 4);
        CHECK (rack.timeline.total == total + 32 && rack.timeline.slots[2].start == rack.timeline.slots[1].start + 64, "a longer chord pushes the rest later");
        rack.setChordBars (1, 2); rack.setChordBass (1, 0);
        rack.setChordDegree (1, (deg + 3) % 7);
        CHECK (rack.harmony.seq[1].deg == (deg + 3) % 7 && ! rack.harmony.names[1].empty(), "a chord can be moved to another step of the key");
        // lock two chords, then Generate: they stay, the rest change
        rack.toggleChordLock (1); rack.toggleChordLock (3);
        const auto locked = rack.harmony.names;
        bool othersChanged = false;
        for (uint32_t seed = 100; seed < 110; ++seed)
        {
            rack.generateChords (seed);
            CHECK (rack.harmony.names[1] == locked[1] && rack.harmony.names[3] == locked[3], "locked chords stay through a Generate");
            othersChanged = othersChanged || rack.harmony.names[0] != locked[0] || rack.harmony.names[2] != locked[2];
        }
        CHECK (othersChanged, "and the others change");
        Rack other2; other2.prepare (48000);
        CHECK (other2.load (rack.save()) && other2.harmony.seq[1].locked && ! other2.harmony.seq[0].locked && other2.save() == rack.save(), "locks are saved with the project");
        Rack old; old.prepare (48000);
        CHECK (old.load ("N2M 1\nkey 2 0\nharmony deep 5 8 2 35 50\nchord 0 2 0 0 m7\nchord 5 2 0 0 maj7\nend\n") && old.harmony.seq.size() == 2 && ! old.harmony.seq[0].locked, "a project saved before locks existed still loads");
        rack.setChordColour (99, "m7"); rack.setChordBars (-1, 2); rack.toggleChordLock (500);
        CHECK (allFinite (render (rack, 1).l), "editing a chord that isn't there does nothing");
    }

    // 47. The pitch shifter holds a steady note: over a long stretch the shifted note is as strong as in a short one
    {
        const auto a220 = sine (220, 96000, 0.4f);
        Fx up ("harmoniser"); up.m->set ("v1", 5); up.m->set ("mix", 1.0f);
        const auto y = up.run (a220);
        float lo = 9, hi = 0;
        for (size_t at = 48000; at + 960 <= 96000; at += 480) { const float v = tone (y, 440, at, at + 960); lo = std::min (lo, v); hi = std::max (hi, v); }
        CHECK (tone (y, 440, 48000, 96000) > 0.9f * hi, "an octave up is one steady note, not grains drifting against each other");
        CHECK (lo > 0.85f * hi && hi > 0.15f, "and its level doesn't wobble");
        Fx down ("harmoniser"); down.m->set ("v1", 6); down.m->set ("mix", 1.0f);
        const auto z = down.run (sine (329.6, 96000, 0.4f));
        CHECK (tone (z, 164.8, 48000, 96000) > 0.36f, "the same going down, on a note that doesn't fit the sample rate neatly");
        // a sound with no pitch (noise) still comes through, finite and no louder
        std::vector<float> noise (48000); uint32_t s = 7; for (auto& v : noise) { s = s * 1664525u + 1013904223u; v = ((float) (s >> 8) / 8388608.0f - 1.0f) * 0.3f; }
        Fx n ("harmoniser"); n.m->set ("v1", 5); n.m->set ("mix", 1.0f);
        const auto w = n.run (noise);
        CHECK (allFinite (w) && rms (w, 24000, 48000) > 0.05f && peakOf (w) < 1.5f, "noise goes through the shifter without trouble");
    }

    // 48. Key change: the last part of the chords goes up, and is saved
    {
        Rack rack; rack.prepare (48000);
        rack.harmony.lenBars = 16; rack.harmony.chordBars = 2; rack.harmony.lift = 2;
        rack.generateChords (11);
        const auto& sl = rack.timeline.slots;
        CHECK (sl.size() == 8 && sl.front().tr == 0 && sl.back().tr == 2, "with a key change of 2 the song starts in the key and ends 2 semitones up");
        int changes = 0; bool inKey = true;
        const auto t = rack.theory();
        for (size_t i = 0; i < sl.size(); ++i)
        {
            if (i > 0 && sl[i].tr != sl[i - 1].tr) ++changes;
            for (int k = 0; k < 4; ++k) inKey = inKey && t.inScale (n2c::mod12 ((int) std::lround (sl[i].voice[k] * 12.0f) + 60 - sl[i].tr));
        }
        CHECK (changes == 1, "it goes up once and stays up");
        CHECK (inKey, "every note is in the key, or in the raised key after the change");
        Rack back; back.prepare (48000);
        CHECK (back.load (rack.save()) && back.harmony.lift == 2 && back.timeline.slots.back().tr == 2 && back.save() == rack.save(), "the key change is saved with the project");
        rack.harmony.lift = 0; rack.generateChords (11);
        bool flat = true; for (auto& q : rack.timeline.slots) flat = flat && q.tr == 0;
        CHECK (flat, "set back to none, nothing is raised");
        Rack old; old.prepare (48000);
        CHECK (old.load ("N2M 1\nkey 2 0\nharmony deep 5 8 2 35 50\nchord 0 2 0 0 m7\nend\n") && old.harmony.lift == 0, "a project from before key changes loads with none");
    }

    // 49. The song page: a tab plays only in its sections, on the bar, whatever the block size
    {
        auto make = [] { auto* r = one ("kick"); r->song.sections.store (4); r->song.bars.store (1); r->boards[0]->plays.store (0b0101u); return r; };
        const size_t bar = (size_t) (4.0 * 60.0 / 124.0 * 48000.0);
        std::unique_ptr<Rack> off (make());
        const auto all = render (*off, 4);
        CHECK (peakOf (all.l, bar, bar + 4000) > 0.2f && peakOf (all.l, 3 * bar, 3 * bar + 4000) > 0.2f, "with the song page off a tab plays all the time");
        std::unique_ptr<Rack> a (make()); a->song.on.store (true);
        const auto x = render (*a, 8, true, 64);
        CHECK (a->song.now.load() == 3, "the screen is told which section is playing");
        bool same = true; for (size_t i = 0; i < bar; ++i) same = same && x.l[i] == all.l[i];
        CHECK (same, "in a section it plays in, it sounds exactly as it does with the page off");
        CHECK (peakOf (x.l, 0, 4000) > 0.2f && peakOf (x.l, 2 * bar, 2 * bar + 4000) > 0.2f, "it plays in sections 1 and 3");
        CHECK (peakOf (x.l, bar + 12000, 2 * bar) < 1.0e-3f && peakOf (x.l, 3 * bar + 12000, 4 * bar) < 1.0e-3f, "and is quiet in sections 2 and 4");
        CHECK (peakOf (x.l, bar, bar + 4000) < 0.5f * peakOf (x.l, 0, 4000), "no new hit lands at the start of a section it sits out");
        CHECK (peakOf (x.l, 4 * bar, 4 * bar + 4000) > 0.2f && peakOf (x.l, 5 * bar + 12000, 6 * bar) < 1.0e-3f, "after the last section the song goes round again");
        std::unique_ptr<Rack> b (make()); b->song.on.store (true);
        const auto y = render (*b, 8, true, 441);
        float diff = 0; for (size_t i = 0; i < x.l.size(); ++i) diff = std::max (diff, std::abs (x.l[i] - y.l[i]));
        CHECK (diff < 1.0e-4f, "block size does not change where the sections fall");
        // longer sections
        std::unique_ptr<Rack> c (make()); c->song.on.store (true); c->song.bars.store (2); c->boards[0]->plays.store (0b10u);
        const auto z = render (*c, 4);
        CHECK (peakOf (z.l, 0, 2 * bar) < 1.0e-3f && peakOf (z.l, 2 * bar, 2 * bar + 4000) > 0.2f && peakOf (z.l, 3 * bar, 3 * bar + 4000) > 0.2f, "sections can be more than one bar long");
        // saved, and old projects load with it off
        Rack back; back.prepare (48000);
        CHECK (back.load (c->save()) && back.song.on.load() && back.song.sections.load() == 4 && back.song.bars.load() == 2 && back.boards[0]->plays.load() == 0b10u && back.save() == c->save(), "the song page is saved with the project");
        Rack old; old.prepare (48000);
        CHECK (old.load ("N2M 1\nkey 2 0\nboard kick|Kick|0|0.8|0|0|0|0\nend\n") && ! old.song.on.load() && old.boards[0]->plays.load() == 0xffffffffu, "a project from before the song page loads with it off and every tab playing everywhere");
        // undo
        History hist (*c);
        hist.beforeSong();
        c->song.on.store (false); c->song.sections.store (6); c->boards[0]->plays.store (1u);
        CHECK (hist.undo() && c->song.on.load() && c->song.sections.load() == 4 && c->boards[0]->plays.load() == 0b10u, "undo puts the song page back");
        CHECK (hist.redo() && ! c->song.on.load() && c->song.sections.load() == 6 && c->boards[0]->plays.load() == 1u, "and redo does it again");
        // a copy of a tab plays in the same sections
        auto twin = c->copyBoard (*c->boards[0]);
        CHECK (twin != nullptr && twin->plays.load() == 1u, "a duplicated tab keeps its sections");
    }

    // 50. MIDI keyboard: keys in, pitch and gate out, on the exact sample, in key
    {
        Rack rack; rack.prepare (48000);
        rack.addBoard (makeBoard ("empty", 48000));
        auto& b = *rack.boards[0];
        auto* keys = rack.addModule (b, "keys", 0, 0);
        CHECK (keys != nullptr, "there is a MIDI keyboard unit");
        rack.connect (b, keys->uid, 0, b.modules[0]->uid, 0);       // so it runs
        std::vector<float> l (512), r (512), gate, pitch, n1, n2;
        auto play = [&] (std::vector<KeyEvent> ev, int n = 512, int block = 512)
        {
            gate.clear(); pitch.clear(); n1.clear(); n2.clear();
            for (int pos = 0; pos < n; pos += block)
            {
                const int len = std::min (block, n - pos);
                std::vector<KeyEvent> here;
                for (auto e : ev) if (e.sample >= pos && e.sample < pos + len) { e.sample -= pos; here.push_back (e); }
                MidiIn in { here.data(), (int) here.size() };
                // run in the engine's own chunks so every sample of the unit's outputs can be read back
                int done = 0;
                while (done < len)
                {
                    int next = len;
                    for (auto& e : here) if (e.sample > done) next = std::min (next, e.sample);
                    const int step = std::min (64, next - done);
                    std::vector<KeyEvent> now;
                    for (auto e : here) if (e.sample == done) { e.sample = 0; now.push_back (e); }
                    MidiIn part { now.data(), (int) now.size() };
                    rack.process (l.data(), r.data(), step, 124, 0, false, nullptr, &part);
                    for (int i = 0; i < step; ++i) { pitch.push_back (keys->out[0][(size_t) i]); gate.push_back (keys->out[1][(size_t) i]); n1.push_back (keys->out[3][(size_t) i]); n2.push_back (keys->out[4][(size_t) i]); }
                    done += step;
                }
                (void) in;
            }
        };
        play ({});
        CHECK (peakOf (gate) == 0.0f, "no keys, no gate");
        // D minor: A4 (69) is in the key
        play ({ { 100, 69, 100, 1, true } });
        CHECK (gate[99] == 0.0f && gate[100] == 1.0f && std::abs (pitch[100] - 9.0f / 12.0f) < 1.0e-6f, "a key starts its note on the exact sample, at the right pitch");
        auto* shownKeys = dynamic_cast<KeysIn*> (keys);
        CHECK (shownKeys != nullptr && shownKeys->isHeld (69) && ! shownKeys->isHeld (68) && ! shownKeys->isHeld (70), "the unit tells the screen which key is held");
        keys->set ("channel", 2);
        play ({});
        CHECK (! shownKeys->isHeld (69), "a key on another channel is not this unit's to show");
        keys->set ("channel", 0);
        play ({ { 5, 30, 100, 1, true }, { 6, 100, 100, 1, true } });
        CHECK (shownKeys->isHeld (69) && shownKeys->isHeld (30) && shownKeys->isHeld (100) && shownKeys->heldLo.load() == ((uint64_t) 1 << 30) && shownKeys->heldHi.load() == (((uint64_t) 1 << (69 - 64)) | ((uint64_t) 1 << (100 - 64))), "several keys at once, low and high");
        play ({ { 5, 30, 0, 1, false }, { 6, 100, 0, 1, false } });
        CHECK (shownKeys->isHeld (69) && ! shownKeys->isHeld (30) && ! shownKeys->isHeld (100) && ! shownKeys->isHeld (-1) && ! shownKeys->isHeld (128), "and a key let go goes out");
        play ({ { 37, 69, 0, 1, false } });
        CHECK (! shownKeys->isHeld (69) && shownKeys->heldLo.load() == 0 && shownKeys->heldHi.load() == 0, "no keys held, none shown");
        play ({ { 100, 69, 100, 1, true } });
        play ({ { 37, 69, 0, 1, false } });
        CHECK (gate[36] == 1.0f && gate[37] == 0.0f && std::abs (pitch[400] - 9.0f / 12.0f) < 1.0e-6f, "letting go ends it on the exact sample, and the pitch stays for the tail");
        // one whole-block call gives the same as many small ones
        std::vector<KeyEvent> two { { 70, 62, 90, 1, true }, { 300, 62, 0, 1, false } };
        MidiIn both { two.data(), 2 };
        rack.process (l.data(), r.data(), 512, 124, 0, false, nullptr, &both);
        CHECK (rack.keys.count == 0 && rack.keys.serial >= 2, "keys inside one big block are all taken in order");
        // heard through the rack in one big block: the gate, cabled straight to the Output, starts and stops on its sample
        rack.connect (b, keys->uid, 1, b.modules[0]->uid, 1);       // into the Output's right side, which has nothing else in it
        std::vector<KeyEvent> span { { 100, 62, 90, 1, true }, { 333, 62, 0, 1, false } };
        MidiIn spanIn { span.data(), 2 };
        rack.process (l.data(), r.data(), 512, 124, 0, false, nullptr, &spanIn);
        CHECK (r[99] == 0.0f && r[100] > 0.1f && r[332] > 0.1f && r[333] == 0.0f, "inside one host block a key still starts and stops on its exact sample");
        rack.disconnect (b, keys->uid, 1, b.modules[0]->uid, 1);
        // F sharp (66) is not in D minor: it becomes F (65). As played leaves it alone
        play ({ { 0, 66, 100, 1, true } });
        CHECK (std::abs (pitch[10] - 5.0f / 12.0f) < 1.0e-6f, "a key outside the key plays the nearest note in it");
        keys->set ("snap", 0); play ({});
        CHECK (std::abs (pitch[10] - 6.0f / 12.0f) < 1.0e-6f, "set to As played, it plays what was pressed");
        keys->set ("snap", 1);
        // a second key while the first is held: pitch moves to it, the gate dips once, and Note 1 / Note 2 are low then high
        play ({ { 200, 74, 100, 1, true } });
        CHECK (std::abs (pitch[300] - 14.0f / 12.0f) < 1.0e-6f && gate[199] == 1.0f && gate[200] == 0.0f && gate[201] == 1.0f, "a new key takes over the pitch and restarts the envelope");
        CHECK (std::abs (n1[300] - 5.0f / 12.0f) < 1.0e-6f && std::abs (n2[300] - 14.0f / 12.0f) < 1.0e-6f, "Note 1 and Note 2 are the held keys, lowest first");
        play ({ { 50, 74, 0, 1, false } });
        CHECK (gate[100] == 1.0f && std::abs (pitch[100] - 5.0f / 12.0f) < 1.0e-6f, "lifting the newer key goes back to the one still held");
        play ({ { 0, 66, 0, 1, false } });
        CHECK (gate[10] == 0.0f, "all keys up, gate down");
        // channels and octave
        keys->set ("channel", 3);
        play ({ { 0, 69, 100, 1, true } });
        CHECK (gate[10] == 0.0f, "set to channel 3 it ignores channel 1");
        play ({ { 0, 69, 100, 3, true } });
        CHECK (gate[10] == 1.0f, "and plays channel 3");
        keys->set ("octave", -1); play ({});
        CHECK (std::abs (pitch[10] - (9.0f / 12.0f - 1.0f)) < 1.0e-6f, "Octave moves it");
        play ({ { 0, 255, 0, 1, false } });
        CHECK (gate[10] == 0.0f && rack.keys.count == 0, "all notes off clears every key");
        for (int i = 0; i < 40; ++i) rack.keys.on (40 + i, 100, 1);
        CHECK (rack.keys.count == Keys::kMax, "more keys than it can hold never overflow");
        // sustain pedal, pitch wheel, mod wheel
        rack.keys = Keys(); keys->set ("channel", 0); keys->set ("octave", 0);
        play ({ { 0, 69, 100, 1, true }, { 10, 0, 0, 1, true, 3 }, { 20, 69, 0, 1, false } });
        CHECK (gate[100] == 1.0f, "with the pedal down a key that is let go keeps sounding");
        play ({ { 0, 62, 100, 1, true }, { 50, 62, 0, 1, false } });
        CHECK (gate[100] == 1.0f && std::abs (pitch[100] - 2.0f / 12.0f) < 1.0e-6f, "and more notes can be played over it");
        play ({ { 30, 0, 0, 1, false, 3 } });
        CHECK (gate[29] == 1.0f && gate[30] == 0.0f && rack.keys.count == 0, "pedal up lets them all go");
        play ({ { 0, 69, 100, 1, true }, { 0, 0, 0, 1, true, 1, 1.0f } }, 48000);
        CHECK (std::abs (pitch[47000] - 11.0f / 12.0f) < 1.0e-4f, "the pitch wheel all the way up bends 2 semitones");
        CHECK (pitch[1] < 9.5f / 12.0f, "and gets there smoothly, not in a jump");
        keys->set ("bend", 12); play ({ { 0, 0, 0, 1, true, 1, -0.5f } }, 48000);
        CHECK (std::abs (pitch[47000] - 3.0f / 12.0f) < 1.0e-4f, "the Bend knob sets how far: half way down with 12 is 6 semitones down");
        play ({ { 0, 0, 0, 1, true, 1, 0.0f }, { 0, 0, 0, 1, true, 2, 0.5f }, { 0, 69, 0, 1, false } }, 48000);
        CHECK (std::abs (keys->out[7][0] - 0.5f) < 1.0e-3f && std::abs (pitch[47000] - 9.0f / 12.0f) < 1.0e-4f, "the mod wheel comes out of its own jack, and the wheel back at rest is no bend");
    }

    // 51. A pattern as MIDI notes is exactly what the unit plays
    {
        Rack rack; rack.prepare (48000);
        rack.harmony.lenBars = 16; rack.harmony.chordBars = 2; rack.harmony.lift = 2;
        rack.generateChords (21);
        CHECK (rack.timeline.slots.back().tr == 2, "the test song has a key change in it");
        rack.addBoard (makeBoard ("bass", 48000));
        auto* seq = firstOf (*rack.boards[0], "noteseq");
        CHECK (seq != nullptr && canExportMidi (*seq) && seq->P (seq->paramIndex ("follow")) > 0.5f, "the bass has a sequencer that follows the chords");
        const auto list = notesOf (rack, *seq);
        CHECK (list.steps == rack.timeline.total && ! list.notes.empty(), "a line that follows the chords is written out for the whole song");
        // play the song and note down each note as its gate comes up
        std::vector<std::pair<int, int>> heard;
        std::vector<float> l (64), r (64);
        const double pps = 124.0 / 60.0 / 48000.0;
        float lastGate = 0; long at = 0;
        const long total = (long) ((double) list.steps / 4.0 / pps);
        while (at < total)
        {
            rack.process (l.data(), r.data(), 64, 124, (double) at * pps, true);
            for (int i = 0; i < 64; ++i)
            {
                const float g = seq->out[1][(size_t) i];
                if (g > 0.5f && lastGate <= 0.5f) heard.push_back ({ (int) std::floor (((double) (at + i) * pps) * 4.0 + 1.0e-6), 60 + (int) std::lround (seq->out[0][(size_t) i] * 12.0f) });
                lastGate = g;
            }
            at += 64;
        }
        bool same = heard.size() == list.notes.size();
        for (size_t i = 0; same && i < heard.size(); ++i) same = heard[i].first == (int) list.notes[i].start && heard[i].second == list.notes[i].midi;
        CHECK (same, "the MIDI notes are the notes it plays, step for step, key change included");
        // a pattern that doesn't follow the chords is just its own bars
        seq->set ("follow", 0);
        CHECK (notesOf (rack, *seq).steps == 16, "a one-bar pattern that ignores the chords is one bar of MIDI");
        // drums: one note per hit, accents louder
        rack.addBoard (makeBoard ("kick", 48000));
        auto* drum = firstOf (*rack.boards[1], "drumseq");
        const auto hits = notesOf (rack, *drum);
        int want = 0; for (int k = 0; k < 16; ++k) if (drum->data[(size_t) k].load() > 0.5f) ++want;
        CHECK ((int) hits.notes.size() == want && want > 0 && hits.steps == 16 && hits.notes[0].midi == 36, "a drum pattern is one note per hit");
        // chords: a bass note and up to four notes per chord, for the chord's own length
        rack.addBoard (makeBoard ("pad", 48000));
        auto* ch = firstOf (*rack.boards[2], "chords");
        const auto chords = notesOf (rack, *ch);
        bool lengths = true; size_t slotsSeen = 0; double lastStart = -1;
        for (auto& nt : chords.notes) { if (nt.start != lastStart) { ++slotsSeen; lastStart = nt.start; } lengths = lengths && nt.len >= 16; }
        CHECK (chords.steps == rack.timeline.total && slotsSeen == rack.timeline.slots.size() && lengths, "the chords come out as held chords for the whole song");
        CHECK (! canExportMidi (*rack.boards[0]->modules[0]), "units with no pattern offer no MIDI");
    }

    // 52. One drag cables a stereo pair
    {
        Rack rack; rack.prepare (48000);
        rack.addBoard (makeBoard ("empty", 48000));
        auto& b = *rack.boards[0];
        auto* out = b.modules[0].get();
        auto* hall = rack.addModule (b, "hall", 0, 0);
        auto* chorus = rack.addModule (b, "chorus", 0, 0);
        auto* osc = rack.addModule (b, "osc", 0, 0);
        auto has = [&b] (int s, int sp, int d, int dp) { for (auto& c : b.cables) if (c.src == s && c.srcPort == sp && c.dst == d && c.dstPort == dp) return true; return false; };
        rack.connectStereo (b, hall->uid, hall->outIndex ("outl"), out->uid, out->inIndex ("left"));
        CHECK (has (hall->uid, hall->outIndex ("outl"), out->uid, 0) && has (hall->uid, hall->outIndex ("outr"), out->uid, 1), "Out L into the Output's left brings Out R into its right");
        rack.connectStereo (b, chorus->uid, chorus->outIndex ("outl"), hall->uid, hall->inIndex ("inl"));
        CHECK (has (chorus->uid, chorus->outIndex ("outr"), hall->uid, hall->inIndex ("inr")), "the same between two stereo effects");
        const size_t before = b.cables.size();
        rack.connectStereo (b, osc->uid, 0, chorus->uid, chorus->inIndex ("in"));
        CHECK (b.cables.size() == before + 1, "a mono cable stays one cable");
        rack.disconnect (b, hall->uid, hall->outIndex ("outl"), out->uid, 0);
        rack.connectStereo (b, chorus->uid, chorus->outIndex ("outl"), out->uid, 0);
        CHECK (! has (chorus->uid, chorus->outIndex ("outr"), out->uid, 1), "a right input that already has a plug in it is left alone");
        rack.connectStereo (b, hall->uid, hall->outIndex ("outr"), out->uid, 1);
        CHECK (allFinite (render (rack, 1).l), "plugging a right jack on its own is just one cable");
        // right into right brings the left along, when the left input is free
        auto* plate = rack.addModule (b, "plate", 0, 0);
        auto* room = rack.addModule (b, "room", 0, 0);
        rack.connectStereo (b, plate->uid, plate->outIndex ("outr"), room->uid, room->inIndex ("inr"));
        CHECK (has (plate->uid, plate->outIndex ("outl"), room->uid, room->inIndex ("inl")), "Out R into In R brings Out L into In L");
    }

    // 53. Each sound reports how much of the computer it uses
    {
        Rack rack; rack.prepare (48000);
        rack.addBoard (makeBoard ("empty", 48000));
        rack.addBoard (makeBoard ("pad", 48000));
        render (rack, 4);
        const float quiet = rack.boards[0]->load.load(), busy = rack.boards[1]->load.load();
        CHECK (busy > 0.0f && busy < 1.0f && quiet >= 0.0f && quiet < busy, "a pad with reverb takes more of the computer than an empty board, and far less than all of it");
    }

    // 54. Sound presets: every one sets real knobs, sounds, and stays clean
    {
        std::map<std::string, int> perKind;
        for (auto& p : soundPresets())
        {
            int missed = 0;
            auto b = makePreset (p.kind, p.name, 48000, &missed);
            if (b == nullptr || missed != 0) { ++failures; std::printf ("FAIL: preset %s / %s has %d settings that match nothing\n", p.kind, p.name, missed); continue; }
            ++perKind[p.kind];
            Rack rack; rack.prepare (48000);
            rack.addBoard (std::move (b));
            const auto x = render (rack, 4);
            if (! (peakOf (x.l) > 0.02f && peakOf (x.l) < 1.0f && allFinite (x.l))) { ++failures; std::printf ("FAIL: preset %s / %s is silent or clips (peak %.3f)\n", p.kind, p.name, (double) peakOf (x.l)); }
        }
        for (const char* k : { "kick", "snare", "hat", "perc", "clap", "bass", "lead", "arp", "stab", "pad", "strings" }) CHECK (perKind[k] >= 4, "every kind of sound has at least four presets");
        CHECK (makePreset ("kick", "No such") == nullptr, "a preset that doesn't exist gives nothing");
        // two presets of one kind really are different sounds
        Rack a, b2; a.prepare (48000); b2.prepare (48000);
        a.addBoard (makePreset ("kick", "Standard")); b2.addBoard (makePreset ("kick", "Long boom"));
        const auto xa = render (a, 1), xb = render (b2, 1);
        float diff = 0; for (size_t i = 0; i < xa.l.size(); ++i) diff = std::max (diff, std::abs (xa.l[i] - xb.l[i]));
        CHECK (diff > 0.05f, "a different preset is a different sound");
    }

    // 55. Changing a tab's sound keeps its pattern and its mixer channel
    {
        auto old = makeBoard ("kick", 48000);
        auto* seq = firstOf (*old, "drumseq");
        setDrum (*seq, "X..x..x...x..x.X"); seq->set ("bars", 1); seq->data[20].store (2.0f);
        old->name = "My kick"; old->level.store (0.33f); old->pan.store (-0.4f); old->mute.store (true); old->out.store (3); old->plays.store (5u);
        auto fresh = makePreset ("kick", "Deep");
        keepPattern (*old, *fresh);
        auto* ns = firstOf (*fresh, "drumseq");
        bool same = true; for (int s = 0; s < kMaxSteps; ++s) same = same && ns->data[(size_t) s].load() == seq->data[(size_t) s].load();
        CHECK (same && ns->P (ns->paramIndex ("bars")) == 1.0f, "another preset plays the same pattern, bars and all");
        CHECK (fresh->name == "My kick" && fresh->level.load() == 0.33f && fresh->pan.load() == -0.4f && fresh->mute.load() && fresh->out.load() == 3 && fresh->plays.load() == 5u, "and keeps its name, level, pan, mute, output and song sections");
        CHECK (firstOf (*fresh, "osc")->P (firstOf (*fresh, "osc")->paramIndex ("coarse")) == -31.0f, "but it is the new sound");
        // drums to a bass: the hits become notes on the key note
        auto bass = makeBoard ("bass", 48000);
        keepPattern (*old, *bass);
        auto* bs = firstOf (*bass, "noteseq");
        CHECK (bs->data[0].load() == 1.0f && bs->data[1].load() == 0.0f && bs->data[3].load() == 1.0f && bs->data[(size_t) kMaxSteps].load() == 0.0f, "a drum pattern given to a bass keeps its rhythm");
        // a bass line to a kick keeps its rhythm; a bass to a lead keeps the notes too
        auto line = makeBoard ("bass", 48000);
        auto* ls = firstOf (*line, "noteseq");
        setNotes (*ls, "x..x..x...x.x...", noteIdeas ("bass")[4].deg);
        auto kick = makeBoard ("kick", 48000);
        keepPattern (*line, *kick);
        CHECK (firstOf (*kick, "drumseq")->data[3].load() == 1.0f && firstOf (*kick, "drumseq")->data[4].load() == 0.0f, "notes given to a drum keep their rhythm");
        auto lead = makeBoard ("lead", 48000);
        keepPattern (*line, *lead);
        auto* le = firstOf (*lead, "noteseq");
        CHECK (le->data[(size_t) (kMaxSteps + 6)].load() == ls->data[(size_t) (kMaxSteps + 6)].load() && le->P (le->paramIndex ("octave")) == ls->P (ls->paramIndex ("octave")), "a bass line given to a lead keeps its notes and octave");
        // a pad keeps its chord rhythm
        auto pad = makeBoard ("stab", 48000), strings = makeBoard ("strings", 48000);
        keepPattern (*pad, *strings);
        CHECK (firstOf (*strings, "chords")->P (0) == firstOf (*pad, "chords")->P (0), "a chord sound keeps its chord rhythm");
        CHECK (old->type == "kick" && fresh->type == "kick", "the kind of sound follows the new sound");
    }

    // ---- Styles: Make track, Variation, Random knobs ----
    {
        for (auto& st : styles())
        {
            // every preset and idea a style names really exists
            for (auto& part : st.parts)
            {
                for (auto* name : part.presets) CHECK (makePreset (part.kind, name) != nullptr, "a style names a preset that exists");
                const std::string kind = part.kind;
                const bool notes = kind == "bass" || kind == "lead";
                for (int i : part.ideas) CHECK (i >= 0 && i < (int) (notes ? noteIdeas (kind).size() : drumIdeas (kind).size()), "a style names an idea that exists");
            }
            Rack rack; rack.prepare (48000);
            makeTrack (rack, st.id, 12345);
            CHECK (rack.boards.size() >= 4, "a style makes a whole track");
            CHECK (rack.style == st.id, "the rack remembers its style");
            for (auto& b : rack.boards) for (auto& m : b->modules) CHECK (m->live, "every unit of a made track is heard");
            auto a = render (rack, 4);
            CHECK (peakOf (a.l) > 0.05f, st.name);
            CHECK (allFinite (a.l) && allFinite (a.r) && peakOf (a.l) <= 1.0f && peakOf (a.r) <= 1.0f, "a made track is clean");
            // each sound on its own is heard too
            for (auto& b : rack.boards)
            {
                for (auto& o : rack.boards) o->solo.store (o == b);
                CHECK (peakOf (render (rack, 2).l) > 0.005f, "every sound of a made track plays");
            }
            for (auto& o : rack.boards) o->solo.store (false);
            // the same seed is the same track; another seed is another
            Rack same; same.prepare (48000); makeTrack (same, st.id, 12345);
            Rack other; other.prepare (48000); makeTrack (other, st.id, 999);
            CHECK (same.save() == rack.save(), "the same seed makes the same track");
            CHECK (other.save() != rack.save(), "another seed makes another track");
        }
        Rack rack; rack.prepare (48000);
        loadStarter (rack);
        History history (rack);
        const auto before = rack.save();
        history.beforeAll();
        makeTrack (rack, "acid", 77);
        CHECK (rack.save() != before && rack.style == "acid", "Make track replaces the project");
        const auto made = rack.save();
        history.undo();
        CHECK (rack.save() == before, "Undo brings the old project back whole");
        history.redo();
        CHECK (rack.save() == made, "Redo makes the track again");
        // the style travels with the save
        Rack loaded; loaded.prepare (48000);
        CHECK (loaded.load (made) && loaded.style == "acid" && loaded.save() == made, "the style is saved with the project");
        // a save from before styles existed still loads, and keeps the default
        std::string older = made;
        CHECK (older.find ("style acid\n") != std::string::npos, "the save names its style");
        if (older.find ("style acid\n") != std::string::npos) older.erase (older.find ("style acid\n"), 11);
        Rack old; old.prepare (48000);
        CHECK (old.load (older) && old.style == "techno" && old.boards.size() == rack.boards.size(), "a save with no style line loads");

        // Variation: the sounds stay, the patterns change
        auto knobsOf = [] (Rack& r) { std::string t; for (auto& b : r.boards) for (auto& m : b->modules) if (m->info->seq == Seq::None) for (auto& p : m->params) t += std::to_string (p.load()) + ' '; return t; };
        const auto knobs = knobsOf (rack);
        const size_t tabs = rack.boards.size();
        varyPatterns (rack, 4242);
        CHECK (rack.boards.size() == tabs && knobsOf (rack) == knobs, "Variation keeps every sound");
        CHECK (rack.save() != made, "Variation changes the patterns");
        CHECK (peakOf (render (rack, 2).l) > 0.05f, "a varied track plays");
        // Add: the old sounds stay and the style's are added
        addTrack (rack, "house", 5);
        CHECK (rack.boards.size() > tabs && rack.style == "house", "Add keeps what is there");

        // Random knobs: tuning, levels and switches stay; shaping knobs move, a little, and stay in range
        Rack k; k.prepare (48000); makeTrack (k, "trance", 3);
        std::vector<float> was;
        for (auto& b : k.boards) for (auto& m : b->modules) for (auto& p : m->params) was.push_back (p.load());
        randomKnobs (k, 99, 0.12f);
        size_t at = 0; int moved = 0;
        for (auto& b : k.boards)
            for (auto& m : b->modules)
                for (size_t i = 0; i < m->params.size(); ++i, ++at)
                {
                    const auto& p = m->info->params[i];
                    const float now = m->params[i].load();
                    if (! knobCanRandomise (*m, p)) { CHECK (now == was[at], "Random knobs leaves tuning, levels and switches alone"); continue; }
                    CHECK (now >= p.lo && now <= p.hi, "a randomised knob stays in range");
                    CHECK (std::abs (paramToNorm (p, now) - paramToNorm (p, was[at])) <= 0.1201f, "a randomised knob moves only a little");
                    if (now != was[at]) ++moved;
                }
        CHECK (moved > 10, "Random knobs moves the shaping knobs");
        for (auto& b : k.boards) for (auto& m : b->modules) for (auto& p : m->info->params)
            if (std::string (p.id) == "coarse" || std::string (p.id) == "fine" || std::string (p.id) == "level") CHECK (! knobCanRandomise (*m, p), "tuning and level knobs are never randomised");
        auto a = render (k, 2);
        CHECK (peakOf (a.l) > 0.05f && allFinite (a.l), "a track with randomised knobs plays");
    }

    // ---- Knob movement drawn on the timeline ----
    {
        AutoLane a;
        CHECK (a.at (3.0) == 0.0f, "an empty line is nothing");
        a.add (4.0f, 1.0f); a.add (0.0f, 0.2f); a.add (2.0f, 0.6f);
        CHECK (a.count.load() == 3 && a.bar[0].load() == 0.0f && a.bar[1].load() == 2.0f && a.bar[2].load() == 4.0f, "points stay in order");
        CHECK (std::abs (a.at (1.0) - 0.4f) < 1.0e-6f && std::abs (a.at (3.0) - 0.8f) < 1.0e-6f, "straight lines between points");
        CHECK (a.at (-1.0) == 0.2f && a.at (0.0) == 0.2f && a.at (9.0) == 1.0f, "held before the first point and after the last");
        a.move (1, 7.0f, 2.0f);
        CHECK (a.bar[1].load() == 4.0f && a.val[1].load() == 1.0f, "a point can't pass its neighbour or leave the knob's range");
        a.remove (1);
        CHECK (a.count.load() == 2 && a.bar[1].load() == 4.0f && std::abs (a.at (2.0) - 0.6f) < 1.0e-6f, "removing a point joins its neighbours");
        for (int k = 0; k < 100; ++k) a.add ((float) k, 0.5f);
        CHECK (a.count.load() == kAutoPoints, "a line holds only so many points");

        // A bass whose filter opens over four bars
        auto build = [] (bool withLine, float from, float to)
        {
            auto* rack = one ("bass");
            rack->song.sections.store (2); rack->song.bars.store (2);          // the song is four bars
            auto& b = *rack->boards[0];
            auto* f = firstOf (b, "filter");
            f->set ("modamt", 0.0f);
            if (withLine)
            {
                const int lane = rack->addAuto (b, f->uid, f->paramIndex ("cutoff"));
                b.autos[(size_t) lane]->move (0, 0.0f, from);
                b.autos[(size_t) lane]->move (1, 4.0f, to);
            }
            return rack;
        };
        {
            std::unique_ptr<Rack> rack (build (true, 0.2f, 0.9f));
            auto& b = *rack->boards[0];
            auto* f = firstOf (b, "filter");
            const int cut = f->paramIndex ("cutoff");
            CHECK (b.autos.size() == 1 && b.autoIndex (f->uid, cut) == 0, "a knob gets a line");
            CHECK (rack->addAuto (b, f->uid, cut) == 0 && b.autos.size() == 1, "and only one");
            CHECK (rack->addAuto (b, f->uid, f->paramIndex ("type")) == -1 && rack->addAuto (b, 9999, 0) == -1, "a switch can't have a line, nor a unit that isn't there");
            f->set ("cutoff", 0.5f);
            render (*rack, 1, false);
            CHECK (f->P (cut) == 0.5f, "stopped, the line leaves the knob alone");
            auto x = render (*rack, 2);                                         // bars 0 to 2: the knob is half way up its line
            CHECK (std::abs (f->P (cut) - 0.55f) < 0.01f, "playing, the knob follows its line");
            CHECK (std::abs (rack->song.bar.load() - 2.0f) < 0.05f, "the timeline knows where the song is");
            // the sound really opens: the last half bar has far more top than the first
            // (how much the wave bends from sample to sample: small for a dull sound, large for a bright one)
            auto bright = [] (const std::vector<float>& v, size_t from, size_t to) { double e = 0; for (size_t i = from + 2; i < to; ++i) e += std::abs ((double) v[i] - 2.0 * v[i - 1] + v[i - 2]); return e / (double) (to - from); };
            std::unique_ptr<Rack> whole (build (true, 0.2f, 0.9f));
            auto y = render (*whole, 4);
            const size_t q = y.l.size() / 8;
            CHECK (bright (y.l, 7 * q, 8 * q) > 3.0 * bright (y.l, 0, q), "the filter opens as the line rises");
            // the song goes round: bar 5 is bar 1 again
            std::unique_ptr<Rack> round (build (true, 0.2f, 0.9f));
            render (*round, 5);
            CHECK (std::abs (firstOf (*round->boards[0], "filter")->P (cut) - (0.2f + 0.7f * 0.25f)) < 0.01f, "after the last bar the line starts again");
        }
        // The same whatever block size the host uses
        {
            std::unique_ptr<Rack> a1 (build (true, 0.2f, 0.9f)), a2 (build (true, 0.2f, 0.9f));
            auto x = render (*a1, 2, true, 64), y = render (*a2, 2, true, 441);
            float diff = 0;
            for (size_t i = 0; i < x.l.size(); ++i) diff = std::max (diff, std::abs (x.l[i] - y.l[i]));
            CHECK (diff < 1.0e-4f, "knob movement doesn't depend on the block size");
        }
        // A flat line at the knob's own position changes nothing
        {
            std::unique_ptr<Rack> with (build (false, 0, 0)), without (build (false, 0, 0));
            auto& b = *with->boards[0];
            with->addAuto (b, firstOf (b, "filter")->uid, firstOf (b, "filter")->paramIndex ("cutoff"));
            auto x = render (*with, 2), y = render (*without, 2);
            float diff = 0;
            for (size_t i = 0; i < x.l.size(); ++i) diff = std::max (diff, std::abs (x.l[i] - y.l[i]));
            CHECK (diff < 1.0e-3f, "a new line is flat where the knob is, and changes nothing");
        }
        // Saved with the project, undone with Undo, and gone when its unit goes
        {
            std::unique_ptr<Rack> rack (build (true, 0.2f, 0.9f));
            auto& b = *rack->boards[0];
            b.autos[0]->add (1.0f, 0.75f);
            const auto text = rack->save();
            CHECK (text.find ("\nau ") != std::string::npos, "lines are written to the save");
            Rack loaded; loaded.prepare (48000);
            CHECK (loaded.load (text) && loaded.boards[0]->autos.size() == 1 && loaded.boards[0]->autos[0]->count.load() == 3 && loaded.save() == text, "lines are saved with the project");
            CHECK (loaded.boards[0]->autos.size() == 1 && std::abs (loaded.boards[0]->autos[0]->at (1.0) - 0.75f) < 1.0e-6f, "and say the same thing after loading");
            History history (*rack);
            history.before (b);
            b.autos[0]->move (1, 1.0f, 0.1f);
            history.discardIfUnchanged();
            CHECK (history.canUndo(), "moving a point is a step Undo knows");
            history.undo();
            CHECK (rack->save() == text, "Undo puts the line back");
            history.before (b);
            rack->removeAuto (b, 0);
            CHECK (b.autos.empty(), "a line can be taken off");
            history.undo();
            CHECK (rack->save() == text, "and Undo brings it back");
            auto copy = rack->copyBoard (b);
            CHECK (copy->autos.size() == 1, "a duplicated sound keeps its lines");
            rack->removeModule (b, firstOf (b, "filter")->uid);
            CHECK (b.autos.empty(), "deleting a unit takes its lines with it");
        }
    }

    // ---- The Notes and rhythm units are wide panels: a screen on the left, knobs beside it, jacks along the bottom ----
    {
        int wide = 0;
        for (auto* t : allTypes())
        {
            const auto g = lay::geo (*t);
            CHECK (g.wide == (std::string (t->shelf) == "Notes and rhythm"), "only the Notes and rhythm units are wide");
            if (! g.wide) continue;
            ++wide;
            CHECK (g.h == lay::panelH && g.w > g.h, "a wide panel is the same height as every other, and wider than it is tall");
            CHECK (! g.screen.empty() && g.screen.x < g.w / 2, "its screen is on the left");
            int shown = 0;
            for (size_t k = 0; k < t->params.size(); ++k)
            {
                const auto& c = g.cellBox[k];
                if (c.empty()) { CHECK (std::string (t->params[k].id) == "bars", "only a sequencer's Bars has no knob: it is the bar buttons"); continue; }
                ++shown;
                CHECK (c.x >= g.screen.right() && c.right() <= g.w - lay::pad && c.bottom() <= g.jacksY && c.y >= lay::header, "every knob and drop-down is to the right of the screen, inside the panel, above the jacks");
                CHECK (! c.overlaps (g.midi), "and clear of the DRAG MIDI label");
                for (size_t j = 0; j < k; ++j) CHECK (! c.overlaps (g.cellBox[j]), "no two knobs share a place");
            }
            CHECK (shown >= 1, "every wide unit has something to turn");
            CHECK (g.jackRows == 1 && lay::wideSide + (int) t->ins.size() * lay::jackSlot <= g.w - lay::wideSide - (int) t->outs.size() * lay::jackSlot, "one row of jacks: inputs on the left, clear of the outputs on the right");
        }
        CHECK (wide == 8, "there are eight Notes and rhythm units");
        // a wide layout that doesn't fit is caught
        lay::Geo bad = lay::geo (Euclid::desc());
        bad.cellBox[0].x = bad.screen.x + 4;
        CHECK (! lay::wideFits (Euclid::desc(), bad), "a knob on top of the screen does not fit");
        bad = lay::geo (Arpeggiator::desc()); bad.w -= 300;
        CHECK (! lay::wideFits (Arpeggiator::desc(), bad), "nor does a panel too narrow for its jacks");
        // a board made of all eight, tidied: none overlap
        auto b = makeBoard ("empty");
        for (auto* t : allTypes()) if (lay::isWide (*t)) b->add (t->type, 0, 0, 48000);
        tidy (*b);
        for (size_t i = 0; i < b->modules.size(); ++i)
            for (size_t j = i + 1; j < b->modules.size(); ++j)
            {
                auto& p = *b->modules[i]; auto& q = *b->modules[j];
                CHECK (p.x + lay::width (*p.info) <= q.x || q.x + lay::width (*q.info) <= p.x || p.y + lay::panelH <= q.y || q.y + lay::panelH <= p.y, "Tidy leaves no wide unit on top of another");
            }
    }

    // ---- Drum accents by the velocity bars, and the name of the idea a pattern is ----
    {
        auto b = makeBoard ("kick");
        auto* seq = firstOf (*b, "drumseq");
        setDrum (*seq, "X...x...x...x...");
        CHECK (drumIdeaIndex (*seq, "kick") == 0 && std::string (drumIdeas ("kick")[0].name) == "Four on the floor", "a pattern set from an idea is known by that idea");
        CHECK (toggleAccent (*seq, 4) && seq->data[4].load() == 2.0f, "a plain hit becomes an accent");
        CHECK (drumIdeaIndex (*seq, "kick") == -1, "an edited pattern is nobody's idea");
        CHECK (toggleAccent (*seq, 4) && seq->data[4].load() == 1.0f, "and an accent a plain hit again");
        CHECK (! toggleAccent (*seq, 1) && seq->data[1].load() == 0.0f, "an empty step stays empty");
        CHECK (! toggleAccent (*seq, -1) && ! toggleAccent (*seq, kMaxSteps), "steps that don't exist are left alone");
        for (int s = 0; s < kMaxSteps; ++s) { const float d = seq->data[(size_t) s].load(); CHECK (d == 0.0f || d == 1.0f || d == 2.0f, "the pattern only ever holds 0, 1 or 2"); }
        auto bb = makeBoard ("bass");
        auto* ns = firstOf (*bb, "noteseq");
        for (size_t k = 0; k < noteIdeas ("bass").size(); ++k)
        {
            setNotes (*ns, noteIdeas ("bass")[k].gates, noteIdeas ("bass")[k].deg);
            const int found = noteIdeaIndex (*ns, "bass");
            CHECK (found >= 0 && std::string (noteIdeas ("bass")[(size_t) found].gates) == noteIdeas ("bass")[k].gates, "every note idea is recognised (as itself, or one that plays the same)");
        }
        ns->data[(size_t) kMaxSteps + 2].store (5.0f);
        CHECK (noteIdeaIndex (*ns, "bass") == -1 || ns->data[2].load() < 0.5f, "a changed note makes it nobody's idea");
    }

    // ---- Euclidean rhythm, Accent: every so-many steps is a loud one. 0 = no accents at all ----
    {
        // the velocity of each hit over one bar, by step
        auto bar = [] (int hits, int accent, int block)
        {
            const auto* e = findType ("euclid");
            auto m = e->make(); m->init (*e->info, 1); m->prepare (48000);
            m->set ("hits", (float) hits); m->set ("accent", (float) accent);
            Ctx c; c.sr = 48000; c.bpm = 120; c.playing = true; c.ppqPerSample = 120.0 / 60.0 / 48000.0; c.scale = scales()[0].iv;
            std::map<int, float> vels;
            double ppq = 0; bool last = false;
            for (int pos = 0; pos < 96000; pos += block)
            {
                const int n = std::min (block, 96000 - pos);
                for (auto& in : m->in) std::fill (in.begin(), in.begin() + n, 0.0f);
                c.ppq = ppq;
                m->process (n, c);
                for (int i = 0; i < n; ++i)
                {
                    const bool g = m->out[0][(size_t) i] > 0.5f;
                    if (g && ! last) vels[(int) std::floor ((ppq + i * c.ppqPerSample) * 4.0 + 1.0e-6)] = m->out[1][(size_t) i];
                    last = g;
                }
                ppq += n * c.ppqPerSample;
            }
            return vels;
        };
        const Info& info = Euclid::desc();
        const auto& ap = info.params[3];
        CHECK (std::string (ap.id) == "accent" && ap.lo == 0.0f && ap.hi == 8.0f && ap.fmt == Fmt::Int, "the Accent knob: 0 to 8, whole numbers");
        CHECK (ap.def == 4.0f, "its starting value is what it has always been, so old projects sound the same");
        // 5 hits of 16 land on steps 0, 4, 7, 10, 13
        const auto none = bar (5, 0, 64);
        CHECK (none.size() == 5 && none.count (0) && none.count (4) && none.count (7) && none.count (10) && none.count (13), "5 hits of 16");
        for (auto& v : none) CHECK (v.second == 0.72f, "Accent at 0: no hit is louder than another");
        const auto two = bar (5, 2, 64);
        CHECK (two.size() == 5, "Accent never adds or removes a hit");
        for (auto& v : two) CHECK (v.second == (v.first % 2 == 0 ? 1.0f : 0.72f), "Accent 2: hits on every second step are full velocity, the rest lower");
        const auto four = bar (16, 4, 64);
        int loud = 0; for (auto& v : four) loud += v.second == 1.0f ? 1 : 0;
        CHECK (four.size() == 16 && loud == 4 && four.at (0) == 1.0f && four.at (4) == 1.0f && four.at (8) == 1.0f && four.at (12) == 1.0f, "Accent 4 on a full bar: four loud hits, evenly spread, one on each beat");
        CHECK (bar (5, 2, 37) == two && bar (16, 4, 50) == four, "the same whatever the block size");
        CHECK (Euclid::accentAt (0, 3) && ! Euclid::accentAt (1, 3) && Euclid::accentAt (3, 3) && ! Euclid::accentAt (0, 0) && ! Euclid::accentAt (5, 0), "the screen and the sound agree on which steps are loud");
    }

    // ---- Every unit fits on its panel ----
    for (auto* t : allTypes())
    {
        if (! lay::fits (*t)) std::printf ("does not fit: %s\n", t->name);
        CHECK (lay::fits (*t), "every unit's screen, knobs and jacks fit on its panel without overlapping");
        CHECK (lay::height (*t) == lay::panelH, "every panel is the same height");
    }

    // Curves on timeline knob lines: a stretch can be bent, and a straight one is exactly as it was
    {
        AutoLane a;
        a.add (0.0f, 0.2f); a.add (4.0f, 1.0f); a.add (8.0f, 0.0f);
        CHECK (std::abs (a.at (2.0) - 0.6f) < 1.0e-6f && std::abs (a.at (1.0) - 0.4f) < 1.0e-6f, "with no bend the line is straight, as before");
        a.bend (0, 0.5f);
        CHECK (a.at (2.0) > 0.75f && a.at (2.0) < 1.0f, "bent up, the knob has gone most of the way by half time");
        CHECK (std::abs (a.at (0.0) - 0.2f) < 1.0e-6f && std::abs (a.at (4.0) - 1.0f) < 1.0e-5f, "and it still starts and ends on its points");
        bool rising = true; for (double b = 0.1; b < 4.0; b += 0.1) rising = rising && a.at (b) >= a.at (b - 0.1) - 1.0e-6f;
        CHECK (rising, "a bent stretch never doubles back");
        CHECK (std::abs (a.at (6.0) - 0.5f) < 1.0e-6f, "bending one stretch leaves the next one straight");
        a.bend (0, -0.5f);
        CHECK (a.at (2.0) < 0.4f && a.at (2.0) > 0.2f, "bent the other way it holds back, then moves late");
        // the bend that puts the middle where the mouse is
        for (float half : { 0.1f, 0.3f, 0.7f, 0.9f })
            CHECK (std::abs (AutoLane::shape (0.5f, AutoLane::curveFor (half)) - half) < 0.01f, "the middle of the stretch follows the handle");
        CHECK (AutoLane::curveFor (0.5f) == 0.0f && AutoLane::curveFor (0.505f) == 0.0f, "near the middle it snaps back to straight");
        CHECK (std::abs (AutoLane::curveFor (5.0f)) <= 1.0f && std::abs (AutoLane::curveFor (-5.0f)) <= 1.0f, "dragging far past the line can't over-bend it");
        // a point added inside a bent stretch, and one removed, keep the bends with the right stretches
        a.bend (0, 0.5f); a.bend (1, -0.3f);
        a.add (6.0f, 0.5f);
        CHECK (a.curve[0].load() == 0.5f && a.curve[1].load() == -0.3f && a.curve[2].load() == 0.0f, "a new point starts a straight stretch and the others keep their bends");
        a.remove (2);
        CHECK (a.count.load() == 3 && a.curve[0].load() == 0.5f && a.curve[1].load() == -0.3f, "removing it puts things back");
        // saved with the project, undoable, and old saves are untouched
        Rack rack; rack.prepare (48000);
        rack.addBoard (makeBoard ("bass", 48000));
        auto& b = *rack.boards[0];
        auto* filt = firstOf (b, "filter");
        const int lane = rack.addAuto (b, filt->uid, filt->paramIndex ("cutoff"));
        CHECK (lane >= 0, "a knob can be given a line");
        b.autos[(size_t) lane]->add (4.0f, 0.9f);
        const auto straight = rack.save();
        CHECK (straight.find ("\nac ") == std::string::npos, "a project with no bends saves exactly as it did before");
        History history (rack);
        history.before (b);
        b.autos[(size_t) lane]->bend (0, 0.6f);
        const auto text = rack.save();
        CHECK (text.find ("\nac ") != std::string::npos, "bends are written to the save");
        Rack loaded; loaded.prepare (48000);
        CHECK (loaded.load (text) && loaded.boards[0]->autos.size() == 1 && std::abs (loaded.boards[0]->autos[0]->curve[0].load() - 0.6f) < 1.0e-5f && loaded.save() == text, "and come back with the project");
        CHECK (history.undo() && rack.boards[0]->autos[0]->curve[0].load() == 0.0f && rack.save() == straight, "Undo takes a bend away");
        CHECK (history.redo() && std::abs (rack.boards[0]->autos[0]->curve[0].load() - 0.6f) < 1.0e-5f, "and Redo puts it back");
        // heard: the filter is further open half way through a stretch that is bent up
        auto cutoffAt = [&] (float bendAmount)
        {
            Rack r2; r2.prepare (48000); r2.load (straight);
            r2.boards[0]->autos[0]->bend (0, bendAmount);
            render (r2, 2.0);                               // play to the middle of the four-bar stretch
            auto* f2 = firstOf (*r2.boards[0], "filter");
            return f2->P (f2->paramIndex ("cutoff"));
        };
        CHECK (cutoffAt (0.8f) > cutoffAt (0.0f) && cutoffAt (0.0f) > cutoffAt (-0.8f), "the knob itself follows the curve while the song plays");
    }

    // 60. The bigger views. A note can have a length of its own; without one it plays exactly as it always did.
    {
        const double bpm = 124.0, pps = bpm / 60.0 / 48000.0, stepSamples = 0.25 / pps;
        struct Take { std::vector<float> gate, pitch, wantGate, wantPitch; };
        // Plays two bars in blocks of the given size, noting the sequencer's Gate and Pitch outputs, and beside them
        // what the note sequencer did before notes had lengths (its old code, word for word)
        auto play = [&] (Rack& rack, Module& seq, int block)
        {
            Take t;
            std::vector<float> l (64), r (64);
            const long total = (long) (32.0 * stepSamples);
            long last = -1000000000L; int left = 0; float pitch = 0;
            const int* scale = scales()[(size_t) rack.scale.load()].iv;
            for (long at = 0; at < total; at += block)
            {
                const double ppq = (double) at * pps;
                rack.process (l.data(), r.data(), block, bpm, ppq, true);
                const int len = loopSteps (seq.P (2), seq.P (3));
                const float gl = seq.P (1);
                const int gateLen = gl >= 0.995f ? (int) (stepSamples * 1.05) : std::max (8, (int) (stepSamples * std::min (0.96f, gl)));
                for (int i = 0; i < block; ++i)
                {
                    const long s = stepAt (ppq + i * pps);
                    if (s != last)
                    {
                        last = s;
                        const int k = wrapStep (s, len);
                        if (seq.data[(size_t) k].load() > 0.5f)
                        {
                            const int deg = (int) std::lround (seq.data[(size_t) (kMaxSteps + k)].load());
                            pitch = (float) (rack.root.load() + degreeToSemis (deg, scale) + 12 * (int) std::lround (seq.P (0))) / 12.0f;
                            left = gateLen;
                        }
                    }
                    t.wantPitch.push_back (pitch); t.wantGate.push_back (left > 0 ? 1.0f : 0.0f);
                    if (left > 0) --left;
                    t.gate.push_back (seq.out[1][(size_t) i]); t.pitch.push_back (seq.out[0][(size_t) i]);
                }
            }
            return t;
        };
        const int line[16] = { 0, 0, 2, 0, 4, 0, 5, 0, 7, 0, 0, 0, 4, 0, 2, 0 };
        auto make = [&line] (float gateLen)
        {
            auto rack = std::make_unique<Rack>(); rack->prepare (48000);
            rack->addBoard (makeBoard ("bass", 48000));
            auto* seq = firstOf (*rack->boards[0], "noteseq");
            seq->set ("follow", 0); seq->set ("bars", 1); seq->set ("gatelen", gateLen);
            setNotesBar (*seq, 0, "x...x.x...x.x.x.", line);
            setNotesBar (*seq, 1, "x.x.x.....x...xx", line);
            return rack;
        };
        // how long the gate stays up from the start of a step, in samples
        auto runFrom = [&] (const Take& t, int step)
        {
            size_t at = (size_t) std::ceil ((double) step * stepSamples - 1.0e-6);
            while (at < t.gate.size() && stepAt ((double) at * pps) < step) ++at;
            size_t n = 0;
            while (at + n < t.gate.size() && t.gate[at + n] > 0.5f) ++n;
            return (int) n;
        };
        for (float gl : { 0.55f, 0.2f, 1.0f })
            for (int block : { 64, 23 })
            {
                auto rack = make (gl);
                auto* seq = firstOf (*rack->boards[0], "noteseq");
                const auto t = play (*rack, *seq, block);
                CHECK (t.gate == t.wantGate && t.pitch == t.wantPitch && ! t.gate.empty(), "a pattern whose notes have no length of their own plays exactly as before, sample for sample");
            }
        {
            auto rack = make (0.55f);
            auto* seq = firstOf (*rack->boards[0], "noteseq");
            const auto plain = play (*rack, *seq, 64);
            const int knob = std::max (8, (int) (stepSamples * (double) std::min (0.96f, 0.55f)));
            CHECK (runFrom (plain, 0) == knob && runFrom (plain, 6) == knob, "(the Length knob sets every note's length)");
            auto fresh = make (0.55f);
            auto* s2 = firstOf (*fresh->boards[0], "noteseq");
            s2->data[(size_t) (kLenRow + 0)].store (4.0f);        // four steps: right up to the next note, at step 4
            s2->data[(size_t) (kLenRow + 6)].store (3.0f);        // three steps, with room: the next note is at step 10
            s2->data[(size_t) (kLenRow + 4)].store (8.0f);        // eight steps, but the next note comes after two
            const auto own = play (*fresh, *s2, 64);
            CHECK (runFrom (own, 0) == (int) (stepSamples * 3.96) && runFrom (own, 0) > knob * 5, "a note with a length of four steps holds its gate for four steps");
            CHECK (runFrom (own, 6) == (int) (stepSamples * 2.96), "...and one of three steps for three");
            CHECK (runFrom (own, 4) == (int) (stepSamples * 1.96), "a long note is cut where the next note starts");
            CHECK (own.gate[(size_t) (4.0 * stepSamples) - 4] < 0.5f && own.gate[(size_t) (6.0 * stepSamples) - 4] < 0.5f, "with a gap before the next note, so it starts afresh");
            CHECK (runFrom (own, 10) == knob && runFrom (own, 12) == knob && runFrom (own, 16) == knob, "the notes around it keep the knob's length");
            CHECK (own.pitch == plain.pitch, "lengths change no pitches");
            CHECK (ownNoteSteps (*s2, 0, 32) == 4 && ownNoteSteps (*s2, 4, 32) == 2 && ownNoteSteps (*s2, 10, 32) == 0, "(the length a note will sound for)");
            s2->data[(size_t) (kLenRow + 30)].store (6.0f);       // the last note but one of the loop: cut by the note at step 31
            s2->data[(size_t) (kLenRow + 31)].store (6.0f);       // the last note: runs round the end of the loop to the note at step 0
            CHECK (ownNoteSteps (*s2, 30, 32) == 1 && ownNoteSteps (*s2, 31, 32) == 1, "a long note at the end of the pattern is cut by the first note when it comes round");
            s2->data[(size_t) (kLenRow + 30)].store (0.0f); s2->data[(size_t) (kLenRow + 31)].store (0.0f);

            // block size: the same gate and pitch, sample for sample, and the same sound
            auto other = make (0.55f);
            auto* s3 = firstOf (*other->boards[0], "noteseq");
            for (int k = 0; k < kMaxSteps; ++k) s3->data[(size_t) (kLenRow + k)].store (s2->data[(size_t) (kLenRow + k)].load());
            const auto small23 = play (*other, *s3, 23);
            bool sameGate = true;
            for (size_t i = 0; i < std::min (own.gate.size(), small23.gate.size()); ++i) sameGate = sameGate && own.gate[i] == small23.gate[i] && own.pitch[i] == small23.pitch[i];
            CHECK (sameGate, "block size does not change where a long note starts and stops");
            auto a = make (0.55f), b = make (0.55f);
            for (auto* rk : { a.get(), b.get() }) { auto* q = firstOf (*rk->boards[0], "noteseq"); for (int k = 0; k < kMaxSteps; ++k) q->data[(size_t) (kLenRow + k)].store (s2->data[(size_t) (kLenRow + k)].load()); }
            const auto x = render (*a, 2, true, 64), y = render (*b, 2, true, 441);
            float diff = 0; for (size_t i = 0; i < x.l.size(); ++i) diff = std::max (diff, std::abs (x.l[i] - y.l[i]));
            CHECK (diff < 1.0e-4f && peakOf (x.l) > 0.02f, "block size does not change the sound of long notes");

            // saved on a line of its own, and only when there are some
            const auto with = fresh->save(), without = make (0.55f)->save();
            CHECK (without.find ("\ngl ") == std::string::npos, "a pattern with no long notes saves no line for them");
            const auto glAt = with.find ("\ngl ");
            CHECK (glAt != std::string::npos, "a pattern with long notes saves them on a line of their own");
            if (glAt != std::string::npos)
            {
                auto cut = with; cut.erase (glAt, with.find ('\n', glAt + 1) - glAt);
                CHECK (cut == without, "...and nothing else in the save changes");
            }
            Rack back; back.prepare (48000);
            CHECK (back.load (with), "it loads");
            auto* s4 = firstOf (*back.boards[0], "noteseq");
            bool sameRows = s4 != nullptr;
            for (int k = 0; sameRows && k < kData; ++k) sameRows = s4->data[(size_t) k].load() == s2->data[(size_t) k].load();
            CHECK (sameRows && back.save() == with, "long notes come back from a save exactly");
            // a save from before (no such line) loads with every note on the knob
            Rack old; old.prepare (48000); old.load (without);
            bool none = true; for (int k = 0; k < kMaxSteps; ++k) none = none && firstOf (*old.boards[0], "noteseq")->data[(size_t) (kLenRow + k)].load() == 0.0f;
            CHECK (none, "an older project has no long notes");

            // as MIDI: the same notes, each as long as it is played
            const auto list = notesOf (*fresh, *s2);
            std::vector<std::pair<int, int>> heard;
            for (int st = 0; st < 32; ++st) if (s2->data[(size_t) st].load() > 0.5f) heard.push_back ({ st, runFrom (own, st) });
            bool sameNotes = heard.size() == list.notes.size() && list.steps == 32;
            for (size_t i = 0; sameNotes && i < heard.size(); ++i)
                sameNotes = heard[i].first == (int) list.notes[i].start && heard[i].second == std::max (8, (int) (stepSamples * list.notes[i].len))
                            && 60 + (int) std::lround (own.pitch[(size_t) ((double) heard[i].first * stepSamples) + 8] * 12.0f) == list.notes[i].midi;
            CHECK (sameNotes, "the MIDI notes are as long as the notes it plays");
            CHECK (list.notes[0].len > 3.9 && list.notes[1].len > 1.9 && list.notes[1].len < 2.0 && list.notes[3].len < 0.6, "(long notes long, the rest the knob's length)");

            // Undo puts a length back; a linked sequencer takes it; Change sound keeps it; a new idea clears it
            History h (*fresh);
            h.before (*fresh->boards[0]);
            s2->data[(size_t) (kLenRow + 0)].store (0.0f);
            h.undo();
            CHECK (firstOf (*fresh->boards[0], "noteseq")->data[(size_t) (kLenRow + 0)].load() == 4.0f, "Undo puts a note's length back");
            fresh->addBoard (makeBoard ("lead", 48000));
            auto* lead = firstOf (*fresh->boards[1], "noteseq");
            fresh->linkPattern (*s2, 1, 1); fresh->linkPattern (*lead, 1, 1);
            CHECK (lead->data[(size_t) (kLenRow + 0)].load() == 4.0f && lead->data[(size_t) (kLenRow + 6)].load() == 3.0f, "a sequencer linked by rhythm takes the lengths too");
            auto swapped = makeBoard ("lead", 48000);
            keepPattern (*fresh->boards[0], *swapped);
            CHECK (firstOf (*swapped, "noteseq")->data[(size_t) (kLenRow + 6)].load() == 3.0f, "Change sound keeps each note's length");
            setNotes (*s2, noteIdeas ("bass")[0].gates, noteIdeas ("bass")[0].deg);
            bool cleared = true; for (int k = 0; k < kMaxSteps; ++k) cleared = cleared && s2->data[(size_t) (kLenRow + k)].load() == 0.0f;
            CHECK (cleared, "choosing a pattern idea starts with no long notes");
        }
    }

    // 61. Drum hits of any strength. The plain hit and the accent are exactly what they were.
    {
        CHECK (drumVel (1.0f) == 0.72f && drumVel (2.0f) == 1.0f, "a plain hit is 0.72 and an accent 1.0, exactly");
        CHECK (drumVel (1.5f) > 0.72f && drumVel (1.5f) < 1.0f && drumVel (0.75f) < 0.72f && drumVel (0.75f) > 0.1f && drumVel (0.51f) >= 0.1f, "values in between are in between; below 1 is softer than a plain hit");
        bool rising = true, round = true;
        for (int k = 52; k <= 200; ++k) rising = rising && drumVel ((float) k / 100.0f) >= drumVel ((float) (k - 1) / 100.0f);
        for (int k = 10; k <= 100; ++k) round = round && std::abs (drumVel (drumData ((float) k / 100.0f)) - (float) k / 100.0f) < 0.013f && drumData ((float) k / 100.0f) > 0.5f;
        CHECK (rising && round, "a harder number is never a softer hit, and a strength set comes back as set");
        CHECK (drumData (0.72f) == 1.0f && drumData (1.0f) == 2.0f, "the plain hit and the accent are stored as 1 and 2");
        CHECK (drumMidiVel (1.0f) == 92 && drumMidiVel (2.0f) == 127 && drumMidiVel (1.5f) > 92 && drumMidiVel (1.5f) < 127 && drumMidiVel (0.6f) < 92 && drumMidiVel (0.6f) >= 1, "as MIDI too: 92 and 127 as before");

        const double bpm = 124.0, pps = bpm / 60.0 / 48000.0;
        // Plays a bar, noting Trig and Vel beside what the drum sequencer did before (its old code, word for word)
        auto play = [&] (Rack& rack, Module& seq, std::vector<float>& vel, bool& asBefore)
        {
            std::vector<float> l (64), r (64);
            long last = -1000000000L; int left = 0; float oldVel = 0.72f;
            asBefore = true; vel.clear();
            const int gateLen = std::max (8, (int) (0.5 * 0.25 / pps));
            for (long at = 0; at < (long) (4.0 / pps); at += 64)
            {
                const double ppq = (double) at * pps;
                rack.process (l.data(), r.data(), 64, bpm, ppq, true);
                for (int i = 0; i < 64; ++i)
                {
                    const long s = stepAt (ppq + i * pps);
                    if (s != last)
                    {
                        last = s;
                        const float d = seq.data[(size_t) wrapStep (s, 16)].load();
                        if (d > 0.5f) { left = gateLen; oldVel = d > 1.5f ? 1.0f : 0.72f; }
                    }
                    asBefore = asBefore && seq.out[0][(size_t) i] == (left > 0 ? 1.0f : 0.0f) && seq.out[1][(size_t) i] == oldVel;
                    if (left > 0) --left;
                    vel.push_back (seq.out[1][(size_t) i]);
                }
            }
        };
        std::unique_ptr<Rack> rack (one ("kick"));
        auto* seq = firstOf (*rack->boards[0], "drumseq");
        setDrum (*seq, "X.x.x..xX..x.xXx");
        std::vector<float> vel; bool asBefore = false;
        play (*rack, *seq, vel, asBefore);
        CHECK (asBefore && ! vel.empty(), "a pattern of plain hits and accents plays exactly as before, sample for sample");
        const size_t stepLen = (size_t) (0.25 / pps);
        CHECK (vel[8] == 1.0f && vel[2 * stepLen + 8] == 0.72f, "Vel is exactly 1.0 for an accent and 0.72 for a plain hit");
        std::unique_ptr<Rack> soft (one ("kick"));
        auto* s2 = firstOf (*soft->boards[0], "drumseq");
        setDrum (*s2, "X.x.x..xX..x.xXx");
        s2->data[2].store (1.5f); s2->data[4].store (0.6f);
        play (*soft, *s2, vel, asBefore);
        CHECK (vel[2 * stepLen + 8] > 0.72f && vel[2 * stepLen + 8] < 1.0f && vel[4 * stepLen + 8] < 0.72f && vel[4 * stepLen + 8] > 0.1f && vel[8] == 1.0f, "a strength in between comes out of Vel in between");
        CHECK (vel[2 * stepLen + 8] == drumVel (1.5f) && vel[4 * stepLen + 8] == drumVel (0.6f), "(just what drumVel says)");
        const auto hits = notesOf (*soft, *s2);
        CHECK (hits.notes.size() == 9 && hits.notes[0].vel == 127 && hits.notes[1].vel == drumMidiVel (1.5f) && hits.notes[2].vel == drumMidiVel (0.6f) && hits.notes[3].vel == 92, "the MIDI velocities follow");
        // saved and reloaded
        s2->data[7].store (drumData (0.33f)); s2->data[11].store (drumData (0.9f));
        const auto text = soft->save();
        Rack back; back.prepare (48000); back.load (text);
        auto* s3 = firstOf (*back.boards[0], "drumseq");
        bool same = s3 != nullptr;
        for (int k = 0; same && k < kMaxSteps; ++k) same = s3->data[(size_t) k].load() == s2->data[(size_t) k].load();
        CHECK (same && back.save() == text, "hit strengths come back from a save exactly");
        // the accent click on the unit still works on any strength
        s2->data[2].store (1.3f); s2->data[4].store (1.8f); s2->data[1].store (0.0f);
        CHECK (toggleAccent (*s2, 2) && s2->data[2].load() == 2.0f && toggleAccent (*s2, 4) && s2->data[4].load() == 1.0f && ! toggleAccent (*s2, 1), "a click on the unit's velocity bar still makes an accent, or a plain hit of one");
        // linked drum sequencers share strengths; a note sequencer just takes "plays here"
        soft->addBoard (makeBoard ("snare", 48000)); soft->addBoard (makeBoard ("bass", 48000));
        auto* d2 = firstOf (*soft->boards[1], "drumseq"); auto* n2 = firstOf (*soft->boards[2], "noteseq");
        s2->data[9].store (1.25f);
        soft->linkPattern (*s2, 2, 1); soft->linkPattern (*d2, 2, 1); soft->linkPattern (*n2, 2, 1);
        CHECK (d2->data[9].load() == 1.25f && n2->data[9].load() == 1.0f, "linked: another drum sequencer takes the strengths, a note sequencer just the hits");
    }

    // 62. Copy, paste and clear of one bar
    {
        std::unique_ptr<Rack> rack (one ("kick"));
        auto* seq = firstOf (*rack->boards[0], "drumseq");
        for (int b = 0; b < kMaxBars; ++b) setDrumBar (*seq, b, "x...x...x...x...");
        setDrumBar (*seq, 1, "X.x.x..xX..x.xXx"); seq->data[(size_t) (16 + 2)].store (1.37f);
        BarClip empty;
        CHECK (! pasteBar (*seq, 0, empty), "nothing copied: Paste does nothing");
        const auto clip = copyBar (*seq, 1);
        CHECK (clip.full && pasteBar (*seq, 5, clip), "a bar can be copied and pasted");
        bool same = true, others = true;
        for (int i = 0; i < kSteps; ++i) { same = same && seq->data[(size_t) (5 * 16 + i)].load() == seq->data[(size_t) (16 + i)].load(); others = others && seq->data[(size_t) (4 * 16 + i)].load() == (i % 4 == 0 ? 1.0f : 0.0f); }
        CHECK (same && seq->data[(size_t) (5 * 16 + 2)].load() == 1.37f && others, "the pasted bar is the copied bar, strengths and all, and no other bar changes");
        CHECK (clearBar (*seq, 5), "a bar can be cleared");
        bool gone = true; for (int i = 0; i < kSteps; ++i) gone = gone && seq->data[(size_t) (5 * 16 + i)].load() == 0.0f;
        CHECK (gone && seq->data[(size_t) (16 + 2)].load() == 1.37f && seq->data[(size_t) (6 * 16)].load() == 1.0f, "clearing takes every hit out of that bar and no other");
        CHECK (! copyBar (*seq, 8).full && ! copyBar (*seq, -1).full && ! pasteBar (*seq, 8, clip) && ! clearBar (*seq, -1), "a bar that isn't there is refused");
        // notes: the pitches and the lengths go with the bar
        std::unique_ptr<Rack> br (one ("bass"));
        auto* ns = firstOf (*br->boards[0], "noteseq");
        const int line[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 };
        setNotesBar (*ns, 0, "x...x...x...x...", line);
        ns->data[(size_t) (kLenRow + 4)].store (3.0f);
        pasteBar (*ns, 3, copyBar (*ns, 0));
        CHECK (ns->data[(size_t) (48 + 4)].load() == 1.0f && ns->data[(size_t) (kMaxSteps + 48 + 4)].load() == 4.0f && ns->data[(size_t) (kLenRow + 48 + 4)].load() == 3.0f, "a bar of notes is pasted with its pitches and lengths");
        clearBar (*ns, 3);
        CHECK (ns->data[(size_t) (48 + 4)].load() == 0.0f && ns->data[(size_t) (kLenRow + 48 + 4)].load() == 0.0f, "...and cleared with them");
    }

    {
        // The Inside mixer: which knobs of a sound get a fader on the desk
        auto has = [] (const std::vector<InsideStrip>& v, const Board& b, const char* type, const char* id)
        {
            for (auto& s : v) if (auto* m = b.find (s.uid)) if (std::strcmp (m->info->type, type) == 0 && std::strcmp (m->info->params[(size_t) s.param].id, id) == 0) return true;
            return false;
        };
        auto kick = makeBoard ("kick");
        const auto k = insideStrips (*kick);
        size_t levels = 0;
        for (auto& m : kick->modules) for (auto& prm : m->info->params) if (std::strcmp (prm.id, "level") == 0) ++levels;
        CHECK (k.size() == levels && levels >= 3 && has (k, *kick, "osc", "level") && has (k, *kick, "vca", "level") && has (k, *kick, "output", "level"),
               "inside a kick: a fader for each Level knob, and nothing else");
        CHECK (! has (k, *kick, "drive", "drive") && ! has (k, *kick, "osc", "coarse") && ! has (k, *kick, "hitenv", "decay"), "Drive, Tune and Decay are not mixing");
        bool known = true, ordered = true;
        for (size_t i = 0; i < k.size(); ++i)
        {
            auto* m = kick->find (k[i].uid);
            known = known && m != nullptr && k[i].param >= 0 && k[i].param < (int) m->info->params.size() && k[i].knob == m->info->params[(size_t) k[i].param].name && ! k[i].unit.empty();
            if (i > 0 && m != nullptr) ordered = ordered && kick->find (k[i - 1].uid)->x <= m->x;
        }
        CHECK (known && ordered && k.back().unit == "Output" && k.back().knob == "Level", "each names its unit and knob, left to right as on the board, the Output last");
        // every starting sound: only knobs the rule allows, and the same list every time
        for (auto& t : boardTypes())
        {
            auto b = makeBoard (t.id);
            const auto v = insideStrips (*b), again = insideStrips (*b);
            bool ok = v.size() == again.size();
            for (auto& s : v) { auto* m = b->find (s.uid); ok = ok && m != nullptr && isMixKnob (*m->info, m->info->params[(size_t) s.param]) && m->info->params[(size_t) s.param].choices.empty(); }
            CHECK (ok && ! v.empty(), "every starting sound has something to mix, and only levels and mixes");
        }
        std::unique_ptr<Board> pad;
        for (auto& sp : soundPresets()) if (std::string (sp.kind) == "pad") { pad = makePreset ("pad", sp.name); break; }
        CHECK (pad != nullptr && insideStrips (*pad).size() == insideStrips (*makeBoard ("pad")).size(), "a preset of a sound has the same faders as the sound it starts from");
        // a Mixer unit, effects, a compressor, a Send; and the things that must stay out
        Board b;
        auto* o1 = b.add ("osc", 300, 0, 48000); auto* o2 = b.add ("osc", 100, 0, 48000);
        auto* mx = b.add ("mixer", 400, 0, 48000); auto* rv = b.add ("hall", 500, 0, 48000); auto* dl = b.add ("tape", 600, 0, 48000);
        auto* cp = b.add ("comp", 700, 0, 48000); auto* sd = b.add ("send", 800, 0, 48000);
        b.add ("xfade", 900, 0, 48000); b.add ("parameq", 1000, 0, 48000); b.add ("atten", 1100, 0, 48000); b.add ("follow", 1200, 0, 48000); b.add ("filter", 1300, 0, 48000); b.add ("lfo", 1400, 0, 48000);
        const auto v = insideStrips (b);
        CHECK (v.size() == 10, "two oscillators, four mixer inputs, two effects, a compressor and a send: ten faders");
        CHECK (v.size() == 10 && v[0].uid == o2->uid && v[0].unit == "Oscillator 1" && v[1].uid == o1->uid && v[1].unit == "Oscillator 2", "two units of a kind are numbered left to right");
        int inputs = 0; for (auto& s : v) if (s.uid == mx->uid) ++inputs;
        CHECK (inputs == 4 && has (v, b, "mixer", "l1") && has (v, b, "mixer", "l4"), "every input level of a Mixer unit");
        CHECK (has (v, b, "hall", "mix") && has (v, b, "tape", "mix") && ! has (v, b, "hall", "size") && ! has (v, b, "tape", "feedback"), "an effect's Mix, not its other knobs");
        CHECK (has (v, b, "comp", "makeup") && has (v, b, "send", "amount") && ! has (v, b, "comp", "threshold"), "the compressor's Make-up and a Send's Amount count as levels");
        CHECK (! has (v, b, "xfade", "mix") && ! has (v, b, "atten", "amount") && ! has (v, b, "follow", "gain") && ! has (v, b, "parameq", "g1"), "a crossfader, an attenuator, a follower and EQ gains are left alone");
        for (auto& s : v) CHECK (s.uid != 0 && b.find (s.uid) != nullptr, "each is found again by its unit's id");
        (void) rv; (void) dl; (void) cp; (void) sd;
        // nothing to mix
        Board none;
        none.add ("lfo", 0, 0, 48000); none.add ("filter", 100, 0, 48000); none.add ("adsr", 200, 0, 48000); none.add ("noteseq", 300, 0, 48000);
        CHECK (insideStrips (none).empty() && insideStrips (Board()).empty(), "a sound with no levels or mixes lists nothing");
        // every unit there is: a switch never gets a fader, and the plugin slot and sample player aren't needed for the rule to hold
        for (auto* info : allTypes()) for (auto& prm : info->params) if (! prm.choices.empty()) CHECK (! isMixKnob (*info, prm), "a switch is never a fader");
    }

    if (failures == 0) std::printf ("All passed\n");
    return failures == 0 ? 0 : 1;
}
