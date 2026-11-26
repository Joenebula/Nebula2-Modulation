// The plugin playing in a fake Cubase: transport, tempo, saving with the project. Prints "All passed".
#include "PluginProcessor.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond, what) do { if (! (cond)) { ++failures; std::printf ("FAIL: %s (line %d)\n", what, __LINE__); } } while (0)

struct FakeCubase : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setBpm (bpm); i.setPpqPosition (ppq); i.setIsPlaying (playing);
        return i;
    }
    double bpm = 124, ppq = 0; bool playing = false;
};

static float run (Nebula2ModulationProcessor& p, FakeCubase& host, double seconds, std::vector<float>* keep = nullptr)
{
    float peak = 0;
    for (int b = 0; b < (int) (48000 * seconds / 256); ++b)
    {
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer m;
        p.processBlock (buf, m);
        peak = juce::jmax (peak, buf.getMagnitude (0, 256));
        if (keep) keep->insert (keep->end(), buf.getReadPointer (0), buf.getReadPointer (0) + 256);
        if (host.playing) host.ppq += 256 * host.bpm / 60.0 / 48000.0;
    }
    return peak;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    FakeCubase host;
    Nebula2ModulationProcessor p;
    p.setPlayHead (&host);
    p.setRateAndBufferSizeDetails (48000, 256);
    p.prepareToPlay (48000, 256);

    CHECK (p.rack.boards.size() == 4, "a new project opens with a starter set of sounds");
    CHECK (run (p, host, 1.0) == 0.0f, "silent while Cubase is stopped");

    host.playing = true;
    p.rack.boards[0]->solo.store (true);      // the kick alone: its hits restart the wave, so it repeats exactly
    std::vector<float> first;
    CHECK (run (p, host, 2.0, &first) > 0.1f, "plays when Cubase plays");
    CHECK (p.hostPlaying.load() && p.isPlaying(), "knows Cubase is playing");
    host.bpm = 140; run (p, host, 0.2);
    CHECK (std::abs (p.bpm.load() - 140.0) < 0.01, "follows Cubase's tempo");
    host.playing = false; host.bpm = 124;
    run (p, host, 5.0);
    CHECK (run (p, host, 0.5) < 0.001f, "falls silent after Cubase stops");

    // The plugin's own Play, for trying things while Cubase is stopped
    p.preview.store (true);
    CHECK (run (p, host, 1.0) > 0.1f, "its own Play works while Cubase is stopped");
    p.preview.store (false);
    run (p, host, 5.0);

    // Starting from bar 1 twice gives the same audio
    host.ppq = 0; host.playing = true;
    std::vector<float> second;
    run (p, host, 2.0, &second);
    host.playing = false; run (p, host, 5.0);
    p.rack.boards[0]->solo.store (false);
    float diff = 0;
    for (size_t i = 0; i < juce::jmin (first.size(), second.size()); ++i) diff = juce::jmax (diff, std::abs (first[i] - second[i]));
    CHECK (diff < 0.001f, "the kick sounds the same every time round");

    // Saving with the Cubase project
    p.rack.root.store (7); p.rack.boards[0]->name = "Big kick"; p.rack.boards[2]->mute.store (true);
    p.rack.addBoard (n2m::makeBoard ("lead", 48000));
    p.selectedTab.store (4);
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    Nebula2ModulationProcessor q;
    q.setRateAndBufferSizeDetails (48000, 256);
    q.prepareToPlay (48000, 256);
    q.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (q.rack.boards.size() == 5 && q.rack.root.load() == 7, "sounds and key come back with the project");
    CHECK (q.rack.boards[0]->name == "Big kick" && q.rack.boards[2]->mute.load(), "names and mutes come back");
    CHECK (q.selectedTab.load() == 4, "reopens on the tab you were on");
    CHECK (q.rack.save() == p.rack.save(), "the reloaded project is identical");
    const char junk[] = "not a project";
    q.setStateInformation (junk, (int) sizeof junk);
    CHECK (q.rack.boards.size() == 5, "bad project data is ignored, not loaded");

    if (failures == 0) std::printf ("All passed\n");
    return failures == 0 ? 0 : 1;
}
