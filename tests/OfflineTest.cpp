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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    FakeCubase host;
    Nebula2ModulationProcessor p;
    p.setPlayHead (&host);
    p.setRateAndBufferSizeDetails (48000, 256);
    p.prepareToPlay (48000, 256);

    CHECK (p.rack.boards.size() == 5, "a new project opens with a starter set of sounds");
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
    p.rack.setKey (7, p.rack.scale.load()); p.rack.generateChords (99); p.rack.boards[0]->name = "Big kick"; p.rack.boards[2]->mute.store (true);
    p.rack.addBoard (n2m::makeBoard ("lead", 48000));
    p.selectedTab.store (5);
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    Nebula2ModulationProcessor q;
    q.setRateAndBufferSizeDetails (48000, 256);
    q.prepareToPlay (48000, 256);
    q.setStateInformation (saved.getData(), (int) saved.getSize());
    CHECK (q.rack.boards.size() == 6 && q.rack.root.load() == 7, "sounds and key come back with the project");
    CHECK (q.rack.harmony.names == p.rack.harmony.names && ! q.rack.harmony.names.empty(), "the chords come back with the project");
    CHECK (q.rack.boards[0]->name == "Big kick" && q.rack.boards[2]->mute.load(), "names and mutes come back");
    CHECK (q.selectedTab.load() == 5, "reopens on the tab you were on");
    CHECK (q.rack.save() == p.rack.save(), "the reloaded project is identical");
    const char junk[] = "not a project";
    q.setStateInformation (junk, (int) sizeof junk);
    CHECK (q.rack.boards.size() == 6, "bad project data is ignored, not loaded");

    // ---- Sample player ----
    {
        // A test sound: a quarter of a second of 440 Hz, mono, at 44.1 kHz (so the player has to convert the rate)
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("n2m-test");
        dir.createDirectory();
        const auto wav = dir.getChildFile ("beep.wav");
        wav.deleteFile();
        {
            juce::AudioBuffer<float> b (1, 11025);
            for (int i = 0; i < 11025; ++i) b.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0));
            std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (new juce::FileOutputStream (wav), 44100, 1, 16, {}, 0));
            w->writeFromAudioSampleBuffer (b, 0, 11025);
        }
        Nebula2ModulationProcessor s;
        FakeCubase song; s.setPlayHead (&song);
        s.setRateAndBufferSizeDetails (48000, 256);
        s.prepareToPlay (48000, 256);
        while (s.rack.boards.size() > 0) s.rack.removeBoard (0);
        s.rack.addBoard (n2m::makeBoard ("sample", 48000));
        auto& b = *s.rack.boards[0];
        n2m::SampleModule* smp = nullptr;
        for (auto& m : b.modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) smp = x;
        CHECK (smp != nullptr && smp->live && ! smp->hasSample(), "a Sample tab arrives with an empty player, already cabled");
        song.playing = true;
        CHECK (run (s, song, 0.5) == 0.0f, "an empty sample player is silent");
        CHECK (n2m::SampleModule::canRead ("kick.wav") && n2m::SampleModule::canRead ("Loop.AIFF") && ! n2m::SampleModule::canRead ("notes.txt") && ! n2m::SampleModule::canRead ("thing.vst3"), "it knows an audio file from other files");
        CHECK (smp->load (dir.getChildFile ("missing.wav")).isNotEmpty() && smp->status.isNotEmpty() && ! smp->hasSample(), "a missing file says so");
        CHECK (smp->load (wav).isEmpty() && smp->hasSample() && smp->fileName() == "beep.wav", "the file loads");
        CHECK (std::abs (smp->seconds() - 0.25) < 0.001 && smp->peaks.size() > 50, "it knows how long the sound is and has its shape to draw");

        // count how often the wave crosses zero in a stretch, to read its pitch
        auto pitchOf = [&] (double seconds)
        {
            std::vector<float> y; song.ppq = 0; song.playing = true;
            run (s, song, seconds, &y);
            song.playing = false; run (s, song, 1.0);
            int n = 0; size_t first = 0, last = 0;
            for (size_t i = 1; i < y.size(); ++i) if (y[i - 1] <= 0 && y[i] > 0) { if (n++ == 0) first = i; last = i; }
            return n > 1 ? (double) (n - 1) * 48000.0 / (double) (last - first) : 0.0;
        };
        CHECK (std::abs (pitchOf (0.2) - 440.0) < 3.0, "played at its own pitch, whatever rate the file was recorded at");
        smp->set ("tune", 12);
        CHECK (std::abs (pitchOf (0.1) - 880.0) < 6.0, "Tune up 12 plays it an octave higher");
        smp->set ("tune", 0);
        // a note sequencer's Pitch plays it at different notes
        auto* ns = s.rack.addModule (b, "noteseq", 0, 0);
        for (int i = 0; i < n2m::kMaxSteps; ++i) { ns->data[(size_t) i].store (1.0f); ns->data[(size_t) (n2m::kMaxSteps + i)].store (7.0f); }
        ns->set ("octave", 0);
        s.rack.setKey (0, 6);                                            // C major: seven steps up is the C above
        s.rack.connect (b, ns->uid, ns->outIndex ("pitch"), smp->uid, smp->inIndex ("pitch"));
        CHECK (std::abs (pitchOf (0.1) - 880.0) < 6.0, "a Pitch cable plays the sample at the note it is sent");
        s.rack.disconnectAll (b, ns->uid);

        // once: it stops at the end. Loop: it keeps going.
        std::vector<float> once; song.ppq = 0; song.playing = true; run (s, song, 0.45, &once);
        float tailPeak = 0; for (size_t i = (size_t) (0.30 * 48000); i < once.size(); ++i) tailPeak = juce::jmax (tailPeak, std::abs (once[i]));
        CHECK (tailPeak == 0.0f, "played once, it stops at the end of the file");
        song.playing = false; run (s, song, 1.0);
        smp->set ("loop", 1);
        std::vector<float> looped; song.ppq = 0; song.playing = true; run (s, song, 0.45, &looped);
        tailPeak = 0; for (size_t i = (size_t) (0.30 * 48000); i < looped.size(); ++i) tailPeak = juce::jmax (tailPeak, std::abs (looped[i]));
        CHECK (tailPeak > 0.2f, "looped, it keeps going");
        song.playing = false; smp->set ("loop", 0); run (s, song, 1.0);
        // Length: only the first part plays
        smp->set ("length", 0.2f);
        std::vector<float> part; song.ppq = 0; song.playing = true; run (s, song, 0.3, &part);
        float late = 0; for (size_t i = (size_t) (0.08 * 48000); i < part.size(); ++i) late = juce::jmax (late, std::abs (part[i]));
        CHECK (late == 0.0f && juce::FloatVectorOperations::findMinAndMax (part.data(), 2000).getEnd() > 0.2f, "Length shortens what plays");
        song.playing = false; smp->set ("length", 1.0f); run (s, song, 1.0);
        // no click at the very start or end: the first and last samples are near silence
        std::vector<float> whole; song.ppq = 0; song.playing = true; run (s, song, 0.4, &whole);
        CHECK (std::abs (whole[0]) < 0.05f, "it starts without a click");
        song.playing = false; run (s, song, 1.0);
        // Level at zero is silent
        smp->set ("level", 0.0f); song.ppq = 0; song.playing = true;
        CHECK (run (s, song, 0.3) == 0.0f, "Level at 0% is silent");
        smp->set ("level", 1.0f); song.playing = false; run (s, song, 1.0);

        // Saved with the project (the project remembers where the file is)
        juce::MemoryBlock saved; s.getStateInformation (saved);
        Nebula2ModulationProcessor t;
        t.setRateAndBufferSizeDetails (48000, 256); t.prepareToPlay (48000, 256);
        t.setStateInformation (saved.getData(), (int) saved.getSize());
        n2m::SampleModule* back = nullptr;
        for (auto& m : t.rack.boards[0]->modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) back = x;
        CHECK (back != nullptr && back->hasSample() && back->fileName() == "beep.wav", "the sample comes back with the project");
        // If the file has gone, the player says which file it wants and the rest still loads
        wav.moveFileTo (dir.getChildFile ("moved.wav"));
        Nebula2ModulationProcessor u;
        u.setRateAndBufferSizeDetails (48000, 256); u.prepareToPlay (48000, 256);
        u.setStateInformation (saved.getData(), (int) saved.getSize());
        n2m::SampleModule* lost = nullptr;
        for (auto& m : u.rack.boards[0]->modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) lost = x;
        CHECK (lost != nullptr && ! lost->hasSample() && lost->status.contains ("beep.wav") && u.rack.boards[0]->modules.size() == b.modules.size(), "a file that has moved is named, and nothing else is lost");
        // Undoing a knob keeps the loaded sample
        s.history.before (b); smp->set ("tune", 7);
        s.history.undo();
        n2m::SampleModule* still = nullptr;
        for (auto& m : b.modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) still = x;
        CHECK (still == smp && still->hasSample() && still->P (2) == 0.0f, "undoing a knob on a sample player keeps its sample loaded");
        dir.deleteRecursively();
    }

    // ---- Separate outputs to the host's mixer ----
    {
        Nebula2ModulationProcessor m;
        CHECK (m.getBusCount (false) == n2m::kOuts && m.getBus (false, 0)->isEnabled() && ! m.getBus (false, 1)->isEnabled(), "one main output, and 15 more that start switched off");
        CHECK (m.getBus (false, 1)->getName() == "Out 2", "the outputs are named for the host's mixer");
        auto layout = m.getBusesLayout();
        layout.outputBuses.getReference (1) = juce::AudioChannelSet::stereo();     // the host switches Out 2 on
        CHECK (m.setBusesLayout (layout), "the host can switch an output on");
        FakeCubase song; m.setPlayHead (&song);
        m.setRateAndBufferSizeDetails (48000, 256);
        m.prepareToPlay (48000, 256);
        for (size_t i = 1; i < m.rack.boards.size(); ++i) m.rack.boards[i]->mute.store (true);
        m.rack.boards[0]->out.store (1);
        song.playing = true;
        float mainPeak = 0, out2Peak = 0;
        for (int b = 0; b < 200; ++b)
        {
            juce::AudioBuffer<float> buf (m.getTotalNumOutputChannels(), 256); juce::MidiBuffer midi;
            m.processBlock (buf, midi);
            mainPeak = juce::jmax (mainPeak, buf.getMagnitude (0, 0, 256));
            out2Peak = juce::jmax (out2Peak, buf.getMagnitude (2, 0, 256), buf.getMagnitude (3, 0, 256));
            song.ppq += 256 * 124.0 / 60.0 / 48000.0;
        }
        CHECK (m.getTotalNumOutputChannels() == 4, "four channels: main and Out 2");
        CHECK (mainPeak == 0.0f && out2Peak > 0.2f, "the kick comes out of Out 2, not the main output");
    }

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

        // Undo, through the screen: each step back redraws the board from what the rack now holds
        {
            const std::string now = r.rack.save();
            ed->stepHistory (false);                                     // the failed output-to-output drag left nothing to undo; this undoes the dropped plug
            CHECK (kick.cables.size() == cablesBefore + 2 && view.compFor (filter->uid) != nullptr, "Undo brings back the cable that was pulled out");
            ed->stepHistory (false); ed->stepHistory (false); ed->stepHistory (false);
            CHECK (kick.cables.size() == cablesBefore && ! kick.find (filter->uid)->live, "three more undos: back to the Filter just added and uncabled");
            ed->stepHistory (false);
            CHECK (kick.modules.size() == 7 && view.compFor (drive->uid) != nullptr && view.getNumChildComponents() == 7, "one more removes the Filter, and the screen shows exactly what is left");
            for (int k = 0; k < 5; ++k) ed->stepHistory (true);
            CHECK (r.rack.save() == now, "Redo, five times, is back where we were");
            filter = kick.modules.back().get();                          // the Filter was made afresh on the way back
            CHECK (std::string (filter->info->type) == "filter" && filter->live, "with the Filter cabled in and heard");
            r.preview.store (true);
            FakeCubase idle; r.setPlayHead (&idle);
            CHECK (run (r, idle, 1.0) > 0.05f, "and it plays");
            r.preview.store (false);
        }

        // The board is wider and taller than the window, so it scrolls both ways
        auto* vp = view.findParentComponentOfClass<juce::Viewport>();
        CHECK (vp != nullptr && view.getWidth() > vp->getWidth() + 200 && view.getHeight() > vp->getHeight() + 100, "the board has room to scroll sideways and down");
        vp->setViewPosition (200, 120);
        if (vp->getViewPositionX() != 200 || vp->getViewPositionY() != 120)
            std::printf ("view %d x %d, window %d x %d, position %d, %d\n", view.getWidth(), view.getHeight(), vp->getWidth(), vp->getHeight(), vp->getViewPositionX(), vp->getViewPositionY());
        CHECK (vp->getViewPositionX() == 200 && vp->getViewPositionY() == 120, "it scrolls left to right and up and down");
        vp->setViewPosition (0, 0);

        // Zoom: the board is drawn smaller, still scrolls over all of itself, and cabling still works
        {
            const int fullW = view.getWidth();
            ed->stepZoom (-1); ed->stepZoom (-1);
            CHECK (std::abs (r.zoom.load() - 0.67f) < 0.01f, "two steps out is 67%");
            auto* holder = view.getParentComponent();
            CHECK (holder != nullptr && std::abs (holder->getWidth() - juce::roundToInt ((float) view.getWidth() * 0.67f)) <= 1, "the scroll area is the size the board is drawn at");
            CHECK (view.getWidth() >= fullW, "zoomed out, the board is at least as wide as before (more of it fits)");
            const size_t n0 = kick.cables.size();
            auto* c1 = view.compFor (filter->uid); auto* c2 = view.compFor (drive->uid);
            view.beginCable (*c1, false, 1, c1->getPosition() + c1->jackCentre (false, 1));
            view.endCable (c2->getPosition() + c2->jackCentre (true, 0));
            CHECK (kick.cables.size() == n0 + 1, "cables can be made while zoomed out");
            ed->stepHistory (false);
            for (int k = 0; k < 6; ++k) ed->stepZoom (1);
            CHECK (std::abs (r.zoom.load() - 1.25f) < 0.01f, "zoom stops at 125%");
            ed->stepZoom (-1);
            CHECK (r.zoom.load() == 1.0f, "and steps back to 100%");
            // zoom is kept with the project
            ed->stepZoom (-1);
            juce::MemoryBlock z; r.getStateInformation (z);
            Nebula2ModulationProcessor other; other.setStateInformation (z.getData(), (int) z.getSize());
            CHECK (std::abs (other.zoom.load() - 0.8f) < 0.01f, "the zoom is saved with the project");
            ed->stepZoom (1);
        }

        // The chord unit's tiles line up eight to a row, and its menu can be opened for any chord
        {
            CHECK (n2m::ChordView::tile (0).getX() < n2m::ChordView::tile (1).getX() && n2m::ChordView::tile (8).getY() > n2m::ChordView::tile (0).getY() && n2m::ChordView::tile (8).getX() == n2m::ChordView::tile (0).getX(), "chord tiles run eight to a row");
            ed->selectTab (4);                                           // the Pad tab holds the chord unit
            n2m::ChordView* cv = nullptr;
            for (auto& m : r.rack.boards[4]->modules) if (auto* c = view.compFor (m->uid)) if (c->chords != nullptr) cv = c->chords.get();
            CHECK (cv != nullptr, "the Pad tab shows the chord unit");
            // (the menu itself can't be opened here: this machine has no screen. Its actions are tested in the engine test.)
            ed->selectTab (0);
        }

        // Every unit has a picture for the Add units menu
        {
            int made = 0;
            for (auto* t : n2m::allTypes()) { const auto img = ed->addPanel.preview (*t); if (img.isValid() && img.getWidth() == n2m::lay::width (*t) && img.getHeight() == n2m::lay::height (*t)) ++made; }
            CHECK (made == (int) n2m::allTypes().size() && made >= 50, "a preview picture for every unit in the menu");
        }

        // The list of installed plugins never includes this plugin itself
        {
            const auto list = n2m::installedPlugins (true);
            bool self = false, sorted = true;
            for (size_t k = 0; k < list.size(); ++k) { self = self || list[k].first == "Nebula2 Modulation"; if (k > 0) sorted = sorted && list[k - 1].first.compareIgnoreCase (list[k].first) <= 0; }
            CHECK (! self && sorted, "installed plugins: A to Z, without this plugin itself");
            std::printf ("installed plugins found here: %d%s\n", (int) list.size(), list.empty() ? "" : (" (first: " + list[0].first + ")").toRawUTF8());
        }

        // Switching tabs shows only that sound
        ed->selectTab (2);
        CHECK (view.board == r.rack.boards[2].get(), "another tab shows another board");
        CHECK (view.compFor (drive->uid) == nullptr || view.compFor (drive->uid)->module.info != drive->info, "and none of the kick's modules");
    }

    // ---- Plugin slots: another VST3 running inside. Pass plugin paths on the command line. ----
    // Argument 1: a plugin that plays by itself when the song plays (this plugin's own .vst3 does).
    // Argument 2: an instrument that plays notes sent to it (Nebula2 Chord does).
    if (argc > 1)
    {
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        while (r.rack.boards.size() > 0) r.rack.removeBoard (0);
        r.rack.addBoard (n2m::makeBoard ("empty", 48000));
        auto& b = *r.rack.boards[0];
        auto* out = b.modules[0].get();

        CHECK (n2m::findType ("plugin") != nullptr, "the plugin slot is offered");
        auto* slot = dynamic_cast<n2m::HostedModule*> (r.rack.addModule (b, "plugin", 0, 0));
        CHECK (slot != nullptr && ! slot->hasPlugin(), "a new slot is empty");
        r.rack.connect (b, slot->uid, 0, out->uid, 0); r.rack.connect (b, slot->uid, 1, out->uid, 1);
        song.playing = true;
        CHECK (run (r, song, 0.5) == 0.0f, "an empty slot is silent");

        const auto found = n2m::pluginsInFile (argv[1]);
        CHECK (! found.empty(), "the .vst3 file is recognised");
        CHECK (n2m::pluginsInFile ("/nowhere/nothing.vst3").empty(), "a missing file is not");
        if (! found.empty())
        {
            const auto err = slot->load (found[0]);
            CHECK (err.isEmpty() && slot->hasPlugin(), "the plugin loads into the slot");
            CHECK (slot->pluginName() == found[0].name, "the slot shows the plugin's name");
            song.ppq = 0;
            CHECK (run (r, song, 2.0) > 0.05f, "the plugin inside plays, in time with the song");
            song.playing = false;
            run (r, song, 6.0);
            CHECK (run (r, song, 0.5) < 0.001f, "and stops when the song stops");

            // The level knob at zero is silent
            slot->set ("level", 0.0f);
            song.playing = true; song.ppq = 0;
            CHECK (run (r, song, 1.0) == 0.0f, "slot level at 0% is silent");
            slot->set ("level", 1.0f);
            song.playing = false; run (r, song, 6.0);

            // Saved with the project, plugin and all
            juce::MemoryBlock saved;
            r.getStateInformation (saved);
            Nebula2ModulationProcessor q2;
            FakeCubase song2; q2.setPlayHead (&song2);
            q2.setRateAndBufferSizeDetails (48000, 256);
            q2.prepareToPlay (48000, 256);
            q2.setStateInformation (saved.getData(), (int) saved.getSize());
            n2m::HostedModule* back = nullptr;
            for (auto& m : q2.rack.boards[0]->modules) if (auto* hm = dynamic_cast<n2m::HostedModule*> (m.get())) back = hm;
            CHECK (back != nullptr && back->hasPlugin() && back->pluginName() == found[0].name, "the plugin comes back with the project");
            song2.playing = true;
            CHECK (run (q2, song2, 2.0) > 0.05f, "and plays");

            // Its own window opens inside ours and closes again
            std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
            auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
            ed->hostOverlay.open (*slot, "Sound");
            CHECK (ed->hostOverlay.isVisible() && ed->hostOverlay.showing == slot && slot->instance()->getActiveEditor() != nullptr, "Open shows the plugin's own window");
            ed->hostOverlay.close();
            CHECK (! ed->hostOverlay.isVisible() && slot->instance()->getActiveEditor() == nullptr, "Back closes it");
            // Emptying the slot while its window is open closes the window first
            ed->hostOverlay.open (*slot, "Sound");
            slot->unload();
            CHECK (! ed->hostOverlay.isVisible() && ! slot->hasPlugin(), "emptying the slot closes its window");
            // Deleting a slot whose window is open is safe too
            slot->load (found[0]);
            ed->hostOverlay.open (*slot, "Sound");
            r.rack.removeModule (b, slot->uid);
            CHECK (! ed->hostOverlay.isVisible(), "deleting the slot closes its window");
            ed->board().rebuild();
            song.playing = true; song.ppq = 0;
            CHECK (run (r, song, 0.5) == 0.0f, "and the board is silent again");
            song.playing = false;
        }

        // A plugin that can't be found leaves the slot empty with a reason, and doesn't break the board
        auto* bad = dynamic_cast<n2m::HostedModule*> (r.rack.addModule (b, "plugin", 0, 0));
        juce::PluginDescription missing; missing.name = "Not Installed"; missing.pluginFormatName = "VST3"; missing.fileOrIdentifier = "/nowhere/Not Installed.vst3";
        CHECK (bad->load (missing).isNotEmpty() && ! bad->hasPlugin() && bad->status.isNotEmpty(), "a plugin that isn't installed says so");
        CHECK (run (r, song, 0.2) == 0.0f, "and the rest keeps working");
    }

    if (argc > 2)
    {
        // An instrument played by the board's own cables: note sequencer -> plugin slot -> output
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        while (r.rack.boards.size() > 0) r.rack.removeBoard (0);
        r.rack.addBoard (n2m::makeBoard ("empty", 48000));
        auto& b = *r.rack.boards[0];
        auto* out = b.modules[0].get();
        auto* slot = dynamic_cast<n2m::HostedModule*> (r.rack.addModule (b, "plugin", 0, 0));
        auto* seq = r.rack.addModule (b, "noteseq", 0, 0);
        n2m::setNotes (*seq, n2m::noteIdeas ("lead")[1].gates, n2m::noteIdeas ("lead")[1].deg);
        r.rack.connect (b, slot->uid, 0, out->uid, 0); r.rack.connect (b, slot->uid, 1, out->uid, 1);
        const auto found = n2m::pluginsInFile (argv[2]);
        CHECK (! found.empty() && slot->load (found[0]).isEmpty(), "the instrument loads");
        song.playing = true;
        const float alone = run (r, song, 2.0);
        CHECK (slot->notesSent.load() == 0, "no cables into the slot, no notes sent");
        song.playing = false; run (r, song, 8.0);
        r.rack.connect (b, seq->uid, seq->outIndex ("pitch"), slot->uid, slot->inIndex ("p1"));
        r.rack.connect (b, seq->uid, seq->outIndex ("gate"), slot->uid, slot->inIndex ("gate"));
        song.playing = true; song.ppq = 0;
        const float played = run (r, song, 2.0);
        std::printf ("instrument: %s, level without notes %.4f, with notes from the sequencer %.4f\n", found.empty() ? "?" : found[0].name.toRawUTF8(), (double) alone, (double) played);
        // (Nebula2 Chord also plays by itself when the song plays, so loudness alone can't prove the notes arrived)
        CHECK (played > 0.02f, "the instrument sounds");
        // two seconds at 124 BPM is a little over one bar of 16th notes
        CHECK (slot->notesSent.load() >= 16 && slot->notesSent.load() <= 18, "every note from the sequencer's cables reaches the instrument");
        song.playing = false;
        run (r, song, 10.0);
        CHECK (run (r, song, 0.5) < 0.002f, "the notes end when the song stops: nothing left hanging");
    }

    if (failures == 0) std::printf ("All passed\n");
    return failures == 0 ? 0 : 1;
}
