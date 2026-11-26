// The plugin playing in a fake Cubase: transport, tempo, saving with the project. Prints "All passed".
#include "PluginProcessor.h"
#include "PluginEditor.h"
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

    // ---- The screen: cabling by dragging, the way the mouse does it ----
    {
        Nebula2ModulationProcessor r;
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        CHECK (ed != nullptr, "the screen opens");
        auto& view = ed->board();
        auto& kick = *r.rack.boards[0];
        CHECK (view.board == &kick, "opens on the first tab");
        const size_t cablesBefore = kick.cables.size();

        // Add a Filter from the menu: it arrives unconnected and changes nothing
        ed->addUnits ({ "filter" });
        n2m::Module* filter = kick.modules.back().get();
        CHECK (std::string (filter->info->type) == "filter" && ! filter->live, "an added unit arrives unconnected");
        CHECK (kick.cables.size() == cablesBefore, "adding a unit doesn't touch the cabling");

        n2m::Module *drive = nullptr, *out = nullptr;
        for (auto& m : kick.modules) { if (std::string (m->info->type) == "drive") drive = m.get(); if (std::string (m->info->type) == "output") out = m.get(); }
        auto jack = [&view] (n2m::Module* m, bool isOut, int port) { auto* c = view.compFor (m->uid); return c->getPosition() + c->jackCentre (isOut, port); };
        auto dragCable = [&view] (n2m::Module* from, bool isOut, int port, juce::Point<int> a, juce::Point<int> b)
        {
            view.beginCable (*view.compFor (from->uid), isOut, port, a);
            view.dragCable ((a + b) / 2);
            view.endCable (b);
        };

        // Pull the plug out of the Output and drop it into the Filter's input
        dragCable (out, false, 0, jack (out, false, 0), jack (filter, false, 0));
        bool driveToFilter = false, driveToOut = false;
        for (auto& c : kick.cables) { driveToFilter = driveToFilter || (c.src == drive->uid && c.dst == filter->uid); driveToOut = driveToOut || (c.src == drive->uid && c.dst == out->uid); }
        CHECK (driveToFilter && ! driveToOut, "a plug pulled from one input can be dropped into another");
        CHECK (! filter->live, "the Filter still isn't heard: nothing leads from it to the Output");

        // Drag from the Filter's output to the Output: the path is complete again
        dragCable (filter, true, 0, jack (filter, true, 0), jack (out, false, 0));
        CHECK (filter->live && drive->live && kick.cables.size() == cablesBefore + 1, "cabling the Filter through to the Output brings it to life");

        // Dragging backwards, from an empty input to an output, also connects
        dragCable (filter, false, 1, jack (filter, false, 1), jack (drive, true, 0));
        CHECK (kick.cables.size() == cablesBefore + 2, "a cable can be dragged from an input back to an output");

        // Dropping a plug on nothing removes the cable; output-to-output and input-to-input do nothing
        dragCable (filter, false, 1, jack (filter, false, 1), { 5, 5 });
        CHECK (kick.cables.size() == cablesBefore + 1, "dropping a plug on empty board removes the cable");
        dragCable (filter, true, 0, jack (filter, true, 0), jack (drive, true, 0));
        CHECK (kick.cables.size() == cablesBefore + 1, "an output can't be plugged into an output");

        // It still plays through the new path
        r.preview.store (true);
        FakeCubase stopped; r.setPlayHead (&stopped);
        CHECK (run (r, stopped, 1.0) > 0.05f, "the re-cabled kick still sounds");

        // Switching tabs shows only that sound
        ed->selectTab (2);
        CHECK (view.board == r.rack.boards[2].get(), "another tab shows another board");
        CHECK (view.compFor (drive->uid) == nullptr || view.compFor (drive->uid)->module.info != drive->info, "and none of the kick's modules");
    }

    if (failures == 0) std::printf ("All passed\n");
    return failures == 0 ? 0 : 1;
}
