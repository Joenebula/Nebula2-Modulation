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
        CHECK (b.boards.size() == 4 && b.root.load() == 9 && b.scale.load() == 2, "boards and key restored");
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
        seq->set ("gatelen", 1.0f);
        for (int i = 0; i < 16; ++i) { seq->data[(size_t) i].store (1.0f); seq->data[(size_t) (16 + i)].store (0.0f); }
        rack->root.store (9);                      // A, two octaves down = A2 = 110 Hz
        auto a = render (*rack, 1);
        CHECK (std::abs (roughHz (a.l, 24000, 72000, 48000) - 110.0) < 3.0, "bass plays the key note");
        rack->root.store (2);                      // D2 = 73.4 Hz
        a = render (*rack, 1);
        CHECK (std::abs (roughHz (a.l, 24000, 72000, 48000) - 73.4) < 3.0, "changing the key moves the bass");
        for (int i = 0; i < 16; ++i) seq->data[(size_t) (16 + i)].store (7.0f);   // an octave up within the key
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
        randomNotes ("lead", 5, *ns); const float d5 = ns->data[20].load();
        randomNotes ("lead", 5, *ns);
        CHECK (ns->data[20].load() == d5, "the same seed gives the same pattern");
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

    if (failures == 0) std::printf ("All passed\n");
    return failures == 0 ? 0 : 1;
}
