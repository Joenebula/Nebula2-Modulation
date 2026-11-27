#include <set>
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
        ed->selectTab (r.selectedTab.load());           // these checks are about one sound on its own tab
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
            for (int k = 0; k < 8; ++k) ed->stepZoom (1);
            CHECK (std::abs (r.zoom.load() - 1.5f) < 0.01f, "zoom stops at 150%");
            ed->stepZoom (-1); ed->stepZoom (-1);
            CHECK (r.zoom.load() == 1.0f, "and steps back to 100%");
            for (int k = 0; k < 8; ++k) ed->stepZoom (-1);
            CHECK (std::abs (r.zoom.load() - 0.5f) < 0.01f, "one sound alone stops at 50%");
            for (int k = 0; k < 3; ++k) ed->stepZoom (1);
            CHECK (r.zoom.load() == 1.0f, "and comes back to 100%");
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
        ed->selectTab (r.selectedTab.load());           // these checks are about one sound on its own tab
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

        // Notes the plugin sends out come back as cable signals (Nebula2 Chord sends its chords out as MIDI)
        {
            song.playing = true; song.ppq = 0;
            int gateUps = 0; float lastGate = 0; std::set<int> lows, highs;
            for (int blk = 0; blk < 1500; ++blk)
            {
                juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer m;
                r.processBlock (buf, m);
                song.ppq += 256 * song.bpm / 60.0 / 48000.0;
                const float g = slot->out[(size_t) slot->outIndex ("ngate")][0];
                if (g > 0.5f && lastGate <= 0.5f) ++gateUps;
                lastGate = g;
                if (g > 0.5f) { lows.insert ((int) std::lround (slot->out[(size_t) slot->outIndex ("nt1")][0] * 12.0f)); highs.insert ((int) std::lround (slot->out[(size_t) slot->outIndex ("nt4")][0] * 12.0f)); }
            }
            std::printf ("notes out of the instrument: %d gate starts, %d different low notes\n", gateUps, (int) lows.size());
            CHECK (gateUps >= 1 && lows.size() >= 2, "the chords a plugin plays come out of its Note and Gate jacks, changing as the song moves");
            bool apart = false; for (int lo : lows) for (int hi : highs) apart = apart || hi > lo;
            CHECK (apart, "Note 1 is the lowest note and Note 4 a higher one");
            song.playing = false;
            run (r, song, 3.0);
            CHECK (slot->out[(size_t) slot->outIndex ("ngate")][0] < 0.5f, "its gate goes down when the song stops");
        }

        // Knobs on the slot move the plugin's own controls
        CHECK (slot->numControls() > 1 && slot->controlName (0).isNotEmpty(), "the slot can list the plugin's own controls");
        int ctl = -1;       // a smooth control (not a switch): one that takes 0.37 and gives it back
        for (int i = 0; i < slot->numControls() && ctl < 0; ++i)
            if (slot->instance()->getParameters()[i]->getNumSteps() > 50) ctl = i;
        CHECK (ctl >= 0, "the instrument has a smooth control to try");
        if (ctl >= 0)
        {
            auto value = [&] { return slot->controlValue (ctl); };
            slot->instance()->getParameters()[ctl]->setValue (0.2f);
            run (r, song, 0.2);
            CHECK (slot->knobTarget (0) == -1, "knobs start pointed at nothing");
            slot->set ("k1", 0.9f); run (r, song, 0.2);
            CHECK (std::abs (value() - 0.2f) < 0.02f, "a knob pointed at nothing moves nothing");
            slot->assignKnob (0, ctl);
            CHECK (slot->knobTarget (0) == ctl && std::abs (slot->P (slot->paramIndex ("k1")) - 0.2f) < 0.02f, "pointing a knob at a control sets the knob to where the control is");
            run (r, song, 0.2);
            CHECK (std::abs (value() - 0.2f) < 0.02f, "so nothing jumps");
            slot->set ("k1", 0.8f); run (r, song, 0.2);
            std::printf ("knob 1 -> %s: control now at %.2f\n", slot->controlName (ctl).toRawUTF8(), (double) value());
            CHECK (std::abs (value() - 0.8f) < 0.02f, "turning the knob moves the plugin's control");
            // a change made inside the plugin's own window is left alone until the knob moves again
            slot->instance()->getParameters()[ctl]->setValueNotifyingHost (0.5f);
            run (r, song, 0.2);
            CHECK (std::abs (value() - 0.5f) < 0.02f && slot->lastTouched() == ctl, "a change in the plugin's own window stays, and the slot knows which control it was");
            // a cable into Mod 1 moves it too
            auto* macro = r.rack.addModule (b, "macro", 0, 0);
            macro->set ("value", 0.15f);
            r.rack.connect (b, macro->uid, 0, slot->uid, slot->inIndex ("m1"));
            slot->set ("k1", 0.3f); run (r, song, 0.5);
            CHECK (std::abs (value() - 0.45f) < 0.02f, "a cable into Mod 1 adds to the knob");
            macro->set ("value", 1.0f); run (r, song, 0.5);
            CHECK (std::abs (value() - 1.0f) < 0.02f, "and never pushes the control past its end");
            r.rack.disconnectAll (b, macro->uid);
            slot->set ("k1", 0.6f); run (r, song, 0.2);

            // saved with the project
            juce::MemoryBlock saved;
            r.getStateInformation (saved);
            Nebula2ModulationProcessor q2;
            FakeCubase song2; q2.setPlayHead (&song2);
            q2.setRateAndBufferSizeDetails (48000, 256);
            q2.prepareToPlay (48000, 256);
            q2.setStateInformation (saved.getData(), (int) saved.getSize());
            n2m::HostedModule* back = nullptr;
            for (auto& m : q2.rack.boards[0]->modules) if (auto* hm = dynamic_cast<n2m::HostedModule*> (m.get())) back = hm;
            CHECK (back != nullptr && back->knobTarget (0) == ctl && back->knobTarget (1) == -1, "which control a knob points at is saved with the project");
            if (back != nullptr)
            {
                run (q2, song2, 0.2);
                CHECK (std::abs (back->controlValue (ctl) - 0.6f) < 0.02f, "and the control comes back where it was");
                back->set ("k1", 0.1f); run (q2, song2, 0.2);
                CHECK (std::abs (back->controlValue (ctl) - 0.1f) < 0.02f, "and the knob still moves it");
            }
            // a slot saved before knobs existed (two pieces of text, not three) still loads
            const auto extra = juce::StringArray::fromTokens (juce::String (slot->saveExtra()), " ", "");
            CHECK (extra.size() == 3, "a slot saves its plugin, the plugin's settings and its knobs");
            auto* older = dynamic_cast<n2m::HostedModule*> (r.rack.addModule (b, "plugin", 0, 0));
            older->loadExtra ((extra[0] + " " + extra[1]).toStdString());
            CHECK (older->hasPlugin() && older->knobTarget (0) == -1, "a slot saved before knobs existed still loads");
            // loading a different plugin by hand clears the knobs; so does emptying the slot
            slot->load (found[0]);
            CHECK (slot->knobTarget (0) == -1, "loading another plugin into the slot clears its knobs");
            slot->assignKnob (2, ctl); slot->assignKnob (9, ctl); slot->assignKnob (1, 99999);
            CHECK (slot->knobTarget (2) == ctl && slot->knobTarget (1) == -1, "a knob or control that isn't there is ignored");
            slot->unload();
            CHECK (slot->knobTarget (2) == -1 && slot->numControls() == 0, "emptying the slot clears them too");
            run (r, song, 0.2);
        }
    }

    {
        // The song page, through the screen: squares, build up, undo, and saved with the project
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        ed->selectTab (r.selectedTab.load());           // these checks are about one sound on its own tab
        const int tabsNow = (int) r.rack.boards.size();
        CHECK (tabsNow >= 3 && ! r.rack.song.on.load(), "a new project starts with the song page off");
        ed->songPanel.open();
        CHECK (ed->songPanel.isVisible(), "the Song button opens the song page");
        ed->songPanel.fill (1);
        CHECK (r.rack.song.on.load() && (r.rack.boards[0]->plays.load() & 1u) != 0 && (r.rack.boards[2]->plays.load() & 3u) == 0 && (r.rack.boards[2]->plays.load() & 4u) != 0, "Build up switches it on and brings one more sound in each section");
        ed->songPanel.setSections (4); ed->songPanel.setBars (1);
        r.rack.song.sections.store (4);
        ed->songPanel.setCell (0, 1, false);
        // the kick alone: heard in section 1, not in section 2
        for (auto& b : r.rack.boards) b->mute.store (true);
        r.rack.boards[0]->mute.store (false);
        song.playing = true; song.ppq = 0;
        std::vector<float> heard;
        run (r, song, 4.0 * 60.0 / 124.0 * 2.0, &heard);
        const size_t bar = (size_t) (4.0 * 60.0 / 124.0 * 48000.0);
        float first = 0, second = 0;
        for (size_t i = 0; i < bar && i < heard.size(); ++i) first = juce::jmax (first, std::abs (heard[i]));
        for (size_t i = bar + 14000; i < 2 * bar - 512 && i < heard.size(); ++i) second = juce::jmax (second, std::abs (heard[i]));
        CHECK (first > 0.1f && second < 0.002f, "in Cubase the kick plays in its section and rests in the next");
        song.playing = false;
        juce::MemoryBlock saved;
        r.getStateInformation (saved);
        Nebula2ModulationProcessor q;
        q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        CHECK (q.rack.song.on.load() && q.rack.song.sections.load() == 4 && q.rack.song.bars.load() == 1 && q.rack.boards[0]->plays.load() == r.rack.boards[0]->plays.load(), "the song page comes back with the project");
        ed->stepHistory (false);
        CHECK (r.rack.song.bars.load() == 4, "Undo steps back through song page changes");
        while (r.history.canUndo()) ed->stepHistory (false);
        CHECK (! r.rack.song.on.load() && r.rack.boards[2]->plays.load() == 0xffffffffu, "all the way back to off, every sound everywhere");
        // key change from the chord unit's menu is one generate with the setting changed
        r.history.beforeChords();
        r.rack.harmony.lift = 2; r.rack.generateChords (r.rack.harmony.seed);
        CHECK (r.rack.timeline.slots.back().tr == 2, "a key change raises the last part");
        ed->stepHistory (false);
        CHECK (r.rack.harmony.lift == 0 && r.rack.timeline.slots.back().tr == 0, "and Undo takes it away again");
    }

    {
        // A MIDI keyboard plays a sound: keys -> oscillator -> envelope -> output
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        while (r.rack.boards.size() > 0) r.rack.removeBoard (0);
        r.rack.addBoard (n2m::makeBoard ("lead", 48000));
        auto& b = *r.rack.boards[0];
        n2m::Module* seq = nullptr;
        for (auto& m : b.modules) if (std::string (m->info->type) == "noteseq") seq = m.get();
        CHECK (seq != nullptr, "the lead has a note sequencer");
        // put a keyboard where the sequencer was
        auto* keys = r.rack.addModule (b, "keys", 0, 0);
        const auto cables = b.cables;
        for (auto& c : cables)
            if (c.src == seq->uid) r.rack.connect (b, keys->uid, c.srcPort, c.dst, c.dstPort);     // pitch and gate are ports 0 and 1 on both
        r.rack.removeModule (b, seq->uid);
        auto block = [&] (juce::MidiBuffer& m) { juce::AudioBuffer<float> buf (2, 256); r.processBlock (buf, m); return buf.getMagnitude (0, 256); };
        juce::MidiBuffer none;
        float quiet = 0; for (int i = 0; i < 40; ++i) quiet = juce::jmax (quiet, block (none));
        CHECK (quiet < 0.001f, "with no key down the keyboard sound is silent, even though the song isn't playing");
        juce::MidiBuffer down; down.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 10);
        float loud = block (down);
        for (int i = 0; i < 40; ++i) loud = juce::jmax (loud, block (none));
        CHECK (loud > 0.05f, "pressing a key plays the sound with Cubase stopped");
        CHECK (r.rack.keys.count == 1, "the key is held");
        juce::MidiBuffer up; up.addEvent (juce::MidiMessage::noteOff (1, 62), 0);
        block (up);
        for (int i = 0; i < 1500; ++i) block (none);
        CHECK (r.rack.keys.count == 0 && block (none) < 0.002f, "letting go ends it");
        juce::MidiBuffer panic; panic.addEvent (juce::MidiMessage::noteOn (1, 65, (juce::uint8) 100), 0); panic.addEvent (juce::MidiMessage::allNotesOff (1), 100);
        block (panic);
        CHECK (r.rack.keys.count == 0, "all notes off from the host clears held keys");

        // Save this sound, then load it into another project
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("n2m-test-sound.n2msound");
        file.deleteFile();
        b.name = "My lead"; b.solo.store (true); b.plays.store (5u);
        keys->patternLink = 0;
        CHECK (r.saveSound (0, file) && file.existsAsFile(), "a sound saves to a file");
        Nebula2ModulationProcessor q;
        q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
        const int before = (int) q.rack.boards.size();
        const int at = q.loadSound (file);
        CHECK (at == before && (int) q.rack.boards.size() == before + 1, "loading it adds a new tab");
        if (at >= 0)
        {
            auto& nb = *q.rack.boards[(size_t) at];
            CHECK (nb.name == "My lead" && nb.modules.size() == b.modules.size() && nb.cables.size() == b.cables.size(), "with its name, units and cables");
            CHECK (! nb.solo.load() && nb.plays.load() == 0xffffffffu, "it arrives un-soloed and playing in every section");
            bool same = true;
            for (size_t i = 0; i < nb.modules.size(); ++i)
                for (size_t k = 0; k < nb.modules[i]->params.size(); ++k) same = same && std::abs (nb.modules[i]->params[k].load() - b.modules[i]->params[k].load()) < 1.0e-4f;
            CHECK (same, "and every knob where it was");
            CHECK (q.history.canUndo(), "loading a sound can be undone");
            q.history.undo();
            CHECK ((int) q.rack.boards.size() == before, "undo takes the loaded sound away");
        }
        const auto junk = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("n2m-test-junk.n2msound");
        junk.replaceWithText ("not a sound");
        CHECK (q.loadSound (junk) == -1 && q.loadSound (juce::File()) == -1 && ! r.saveSound (99, file), "a file that isn't a sound is refused");
        file.deleteFile(); junk.deleteFile();

        // A pattern as a MIDI file
        Nebula2ModulationProcessor w;
        w.setRateAndBufferSizeDetails (48000, 256); w.prepareToPlay (48000, 256);
        n2m::Module* bassSeq = nullptr; n2m::Module* plain = nullptr; std::string tabName;
        for (auto& bd : w.rack.boards)
            for (auto& m : bd->modules)
            {
                if (bassSeq == nullptr && std::string (m->info->type) == "noteseq") { bassSeq = m.get(); tabName = bd->name; }
                if (std::string (m->info->type) == "output") plain = m.get();
            }
        CHECK (bassSeq != nullptr, "the starting project has a note sequencer");
        if (bassSeq != nullptr)
        {
            const auto want = n2m::notesOf (w.rack, *bassSeq);
            const auto mid = w.writeMidi (*bassSeq, tabName + " notes");
            CHECK (mid.existsAsFile() && mid.getFileExtension() == ".mid", "a pattern writes a MIDI file");
            juce::FileInputStream in (mid);
            juce::MidiFile mf;
            CHECK (in.openedOk() && mf.readFrom (in) && mf.getNumTracks() == 1 && mf.getTimeFormat() == 480, "the host can read it");
            int ons = 0; double end = 0; bool pitches = true;
            if (mf.getNumTracks() == 1)
            {
                const auto& tr = *mf.getTrack (0);
                for (int i = 0; i < tr.getNumEvents(); ++i)
                {
                    const auto& msg = tr.getEventPointer (i)->message;
                    if (msg.isNoteOn()) { pitches = pitches && ons < (int) want.notes.size() && msg.getNoteNumber() == want.notes[(size_t) ons].midi && std::abs (msg.getTimeStamp() - want.notes[(size_t) ons].start * 120.0) < 0.5; ++ons; }
                    end = juce::jmax (end, msg.getTimeStamp());
                }
            }
            CHECK (ons == (int) want.notes.size() && ons > 0 && pitches, "with the pattern's notes, in the right places");
            CHECK (std::abs (end - want.steps * 120.0) < 0.5, "and it is whole bars long");
            mid.deleteFile();
        }
        CHECK (plain != nullptr && w.writeMidi (*plain, "x") == juce::File(), "a unit with no pattern writes nothing");

        // One tab as an audio file
        {
            w.rack.boards[0]->solo.store (false);
            const auto before = w.rack.save();
            const auto wavFile = w.renderTab (0, 2);
            CHECK (wavFile.existsAsFile() && wavFile.getFileExtension() == ".wav", "a tab renders to a WAV file");
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (wavFile));
            CHECK (reader != nullptr && reader->numChannels == 2 && std::abs ((double) reader->lengthInSamples - 2.0 * 4.0 * 60.0 / w.bpm.load() * 48000.0) < 2.0, "it is stereo and exactly as many bars long as asked");
            if (reader != nullptr)
            {
                juce::AudioBuffer<float> got (2, (int) reader->lengthInSamples);
                reader->read (&got, 0, (int) reader->lengthInSamples, 0, true, true);
                const int bar = (int) (reader->lengthInSamples / 2);
                CHECK (got.getMagnitude (0, 0, 4000) > 0.2f && got.getMagnitude (0, bar, 4000) > 0.2f, "with the kick on the first beat of each bar");
                // only that tab: the same render with the kick's level at zero is silent
                const float lvl = w.rack.boards[0]->level.load();
                w.rack.boards[0]->level.store (0.0f);
                const auto quietFile = w.renderTab (0, 1);
                std::unique_ptr<juce::AudioFormatReader> quiet (fm.createReaderFor (quietFile));
                juce::AudioBuffer<float> q (2, quiet != nullptr ? (int) quiet->lengthInSamples : 1);
                if (quiet != nullptr) quiet->read (&q, 0, (int) quiet->lengthInSamples, 0, true, true);
                CHECK (quiet != nullptr && q.getMagnitude (0, q.getNumSamples()) < 1.0e-4f, "no other tab leaks into it");
                w.rack.boards[0]->level.store (lvl);
            }
            CHECK (w.rack.save() == before, "rendering leaves the project exactly as it was");
            CHECK (w.renderTab (99) == juce::File(), "a tab that isn't there renders nothing");
            CHECK (fm.getWildcardForAllFormats().containsIgnoreCase ("*.mp3"), "mp3 files can be read by the sample player");
        }
    }

    {
        // Your own plugins as ready-made sounds
        const auto own = Nebula2ModulationProcessor::ownPlugins();
        CHECK (own.contains ("Nebula2 Drums") && own.contains ("Nebula2 Voice") && own.contains ("Nebula2 Chord") && own.contains ("Nebula2 Glitch"), "the + menu offers Drums, Voice, Chord and Glitch");
        bool self = false; for (auto& n : own) self = self || n.removeCharacters (" -").equalsIgnoreCase ("Nebula2Modulation");
        CHECK (! self, "it never offers this plugin inside itself");
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        const int before = (int) r.rack.boards.size();
        auto slotOf = [&] (int tab) { n2m::HostedModule* h = nullptr; for (auto& m : r.rack.boards[(size_t) tab]->modules) if (auto* x = dynamic_cast<n2m::HostedModule*> (m.get())) h = x; return h; };
        auto has = [&] (int tab, const char* type) { for (auto& m : r.rack.boards[(size_t) tab]->modules) if (std::string (m->info->type) == type) return true; return false; };

        // one that isn't on this computer: the tab is still made, and says what to do
        const int missing = r.addPluginSound ("Nebula2 Voice No Such");
        CHECK (missing == before && r.rack.boards[(size_t) missing]->name == "Nebula2 Voice No Such", "a plugin that isn't installed still gets its tab");
        CHECK (slotOf (missing) != nullptr && ! slotOf (missing)->hasPlugin() && slotOf (missing)->status.contains ("Press Load"), "with an empty slot that says how to find it");
        CHECK (slotOf (missing)->live && has (missing, "keys"), "already cabled to the Output, with a MIDI keyboard into it");

        if (argc > 2)
        {
            // an instrument (Nebula2 Chord, built here)
            const int tab = r.addPluginSound ("Nebula2 Chord", argv[2]);
            auto* slot = slotOf (tab);
            CHECK (slot != nullptr && slot->hasPlugin() && slot->live && ! has (tab, "return"), "an instrument arrives loaded and cabled straight to the Output");
            for (auto& b : r.rack.boards) b->mute.store (true);
            r.rack.boards[(size_t) tab]->mute.store (false);
            song.playing = true; song.ppq = 0;
            CHECK (run (r, song, 2.0) > 0.02f, "and is heard when the song plays");
            // a key from Cubase reaches the plugin, as played
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer key; key.addEvent (juce::MidiMessage::noteOn (1, 66, (juce::uint8) 100), 0);
            const int sentBefore = slot->notesSent.load();
            r.processBlock (buf, key);
            CHECK (slot->notesSent.load() == sentBefore + 1, "a key or MIDI track note reaches the plugin");
            juce::MidiBuffer up; up.addEvent (juce::MidiMessage::noteOff (1, 66), 0);
            r.processBlock (buf, up);
            song.playing = false;
            // found by name in the computer's plugin folders, when it is there
            bool installed = false; for (auto& p : n2m::installedPlugins()) installed = installed || p.first.removeCharacters (" -").equalsIgnoreCase ("Nebula2Chord");
            if (installed)
            {
                const int byName = r.addPluginSound ("Nebula2 Chord");
                CHECK (slotOf (byName) != nullptr && slotOf (byName)->hasPlugin(), "an installed plugin is found by its name");
            }
            else std::printf ("(Nebula2 Chord isn't in this machine's plugin folders: find-by-name not checked)\n");
            const int count = (int) r.rack.boards.size();
            r.history.undo();
            CHECK ((int) r.rack.boards.size() == count - 1, "Undo takes a plugin sound away again");
        }
        if (argc > 3)
        {
            // an effect (Nebula2 Glitch, built here): fed by send bus A
            const int tab = r.addPluginSound ("Nebula2 Glitch", argv[3]);
            auto* slot = slotOf (tab);
            CHECK (slot != nullptr && slot->hasPlugin() && slot->live && has (tab, "return"), "an effect arrives loaded, with a Return feeding it");
            std::printf ("effect: %s loaded in a slot\n", slot != nullptr ? slot->pluginName().toRawUTF8() : "?");
            if (slot != nullptr)
            {
                // the plugin's own presets can be picked from the slot
                const auto names = slot->presetNames();
                std::printf ("its own presets: %d%s%s\n", names.size(), names.size() > 0 ? ", first: " : "", names.size() > 0 ? names[0].toRawUTF8() : "");
                if (names.size() > 1)
                {
                    slot->choosePreset (names.size() - 1);
                    CHECK (slot->currentPreset() == names.size() - 1, "picking one of a plugin's own presets selects it");
                    slot->choosePreset (0);
                    CHECK (slot->currentPreset() == 0, "and another");
                }
                slot->choosePreset (-1); slot->choosePreset (99999);
                CHECK (slot->hasPlugin(), "a preset that isn't there is ignored");
            }
            // the kick, sent to bus A, comes out of the effect's tab
            for (auto& b : r.rack.boards) b->mute.store (true);
            r.rack.boards[(size_t) tab]->mute.store (false);
            song.playing = true; song.ppq = 0;
            CHECK (run (r, song, 2.0) < 0.001f, "with nothing sent to it, the effect's tab is silent");
            auto& kick = *r.rack.boards[0];
            n2m::Module* outMod = nullptr; for (auto& m : kick.modules) if (std::string (m->info->type) == "output") outMod = m.get();
            n2m::Cable into {}; for (auto& c : kick.cables) if (c.dst == outMod->uid && c.dstPort == 0) into = c;
            auto* send = r.rack.addModule (kick, "send", 0, 0);
            send->set ("amount", 1.0f);
            r.rack.connect (kick, into.src, into.srcPort, send->uid, send->inIndex ("inl"));
            song.ppq = 0;
            CHECK (run (r, song, 2.0) > 0.02f, "a sound sent to bus A is heard through the effect");
            song.playing = false;
            // saved and reopened with the project
            juce::MemoryBlock saved; r.getStateInformation (saved);
            Nebula2ModulationProcessor q; q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
            q.setStateInformation (saved.getData(), (int) saved.getSize());
            n2m::HostedModule* back = nullptr;
            if (tab < (int) q.rack.boards.size()) for (auto& m : q.rack.boards[(size_t) tab]->modules) if (auto* x = dynamic_cast<n2m::HostedModule*> (m.get())) back = x;
            CHECK (back != nullptr && back->hasPlugin(), "and it comes back with the project");
        }
    }

    {
        // Change sound: the tab's sound changes, its pattern and mixer channel stay
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        ed->selectTab (r.selectedTab.load());           // these checks are about one sound on its own tab
        auto unit = [&] (int tab, const char* type) { n2m::Module* f = nullptr; for (auto& m : r.rack.boards[(size_t) tab]->modules) if (f == nullptr && std::string (m->info->type) == type) f = m.get(); return f; };
        auto pattern = [&] (int tab) { std::string p; for (auto* m : n2m::patternUnits (*r.rack.boards[(size_t) tab])) for (int s = 0; s < 16; ++s) p += m->data[(size_t) s].load() > 0.5f ? 'x' : '.'; return p; };
        auto alone = [&] (int tab) { for (auto& b : r.rack.boards) b->mute.store (true); r.rack.boards[(size_t) tab]->mute.store (false); song.playing = true; song.ppq = 0; const float v = run (r, song, 2.0); song.playing = false; run (r, song, 1.0); return v; };
        n2m::setDrum (*unit (0, "drumseq"), "X..x..x...x..x..");
        r.rack.boards[0]->level.store (0.7f);
        const auto was = pattern (0);
        const float tuneBefore = unit (0, "osc")->P (unit (0, "osc")->paramIndex ("coarse"));
        ed->selectTab (-1);
        CHECK (r.changeToPreset (0, "kick", "Deep"), "a kick can be changed to another kick preset");
        ed->selectTab (0);
        CHECK (pattern (0) == was && r.rack.boards[0]->name == "Kick" && std::abs (r.rack.boards[0]->level.load() - 0.7f) < 1.0e-4f, "its pattern, name and level stay");
        CHECK (unit (0, "osc")->P (unit (0, "osc")->paramIndex ("coarse")) != tuneBefore && alone (0) > 0.05f, "and it is a different kick that still plays");
        ed->stepHistory (false);
        CHECK (unit (0, "osc")->P (unit (0, "osc")->paramIndex ("coarse")) == tuneBefore && pattern (0) == was, "Undo brings the old sound back");
        // to a different kind of sound altogether
        ed->selectTab (-1);
        CHECK (r.changeToPreset (0, "perc", "Bongo") && r.rack.boards[0]->type == "perc" && pattern (0) == was, "a kick can become percussion and keep its pattern");
        CHECK (! r.changeToPreset (0, "kick", "No such preset") && ! r.changeToPreset (99, "kick", "Deep"), "a preset or tab that isn't there changes nothing");
        // to a sample
        const auto wav = r.renderTab (0, 1);      // (this tab's own sound: it starts with a hit, so every retrigger is heard)
        CHECK (r.changeToSample (0, wav), "a sound can be changed to a sample");
        CHECK (unit (0, "sample") != nullptr && unit (0, "drumseq") != nullptr && pattern (0) == was && r.rack.boards[0]->type == "perc", "the sample is played by the same pattern, and the tab keeps its kind for pattern Ideas");
        CHECK (alone (0) > 0.02f, "and the sample is heard");
        CHECK (! r.changeToSample (0, juce::File ("/no/such/file.wav")) && ! r.changeToSample (0, juce::File ("/tmp/notaudio.txt")), "a file that isn't audio changes nothing");
        // a bass line to a sample: the notes play the sample at pitch
        const auto bassWas = pattern (3);
        CHECK (r.changeToSample (3, wav) && unit (3, "noteseq") != nullptr && pattern (3) == bassWas, "a bass line changed to a sample keeps its notes");
        bool pitched = false; for (auto& c : r.rack.boards[3]->cables) pitched = pitched || (c.src == unit (3, "noteseq")->uid && c.dst == unit (3, "sample")->uid && c.dstPort == unit (3, "sample")->inIndex ("pitch"));
        CHECK (pitched, "and they are cabled to the sample's Pitch");
        if (argc > 2)
        {
            // to a plugin (Nebula2 Chord, built here)
            CHECK (r.changeToPlugin (0, "Nebula2 Chord", argv[2]), "a sound can be changed to a plugin");
            auto* slot = dynamic_cast<n2m::HostedModule*> (unit (0, "plugin"));
            CHECK (slot != nullptr && slot->hasPlugin() && slot->live && pattern (0) == was, "the plugin is loaded, cabled, and played by the same pattern");
            const int sent = slot != nullptr ? slot->notesSent.load() : 0;
            alone (0);
            CHECK (slot != nullptr && slot->notesSent.load() >= sent + 4, "the pattern's hits reach the plugin as notes");
            ed->stepHistory (false);
            CHECK (unit (0, "sample") != nullptr && unit (0, "plugin") == nullptr, "Undo goes back to the sample");
            CHECK (alone (0) > 0.02f, "with its file still loaded");
        }
        if (argc > 3) CHECK (! r.changeToPlugin (0, "Nebula2 Glitch", argv[3]) && unit (0, "plugin") == nullptr && r.problem.contains ("is an effect"), "an effect can't be a tab's sound: nothing changes, and it says why");
        CHECK (! r.changeToPlugin (0, "No Such Plugin Anywhere") && r.problem.contains ("wasn't found"), "a plugin that isn't installed changes nothing, and it says so");
        // to a saved sound
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("n2m-test-change.n2msound");
        r.saveSound (4, file);
        const auto before4 = pattern (0);
        CHECK (r.changeToSaved (0, file) && unit (0, "multiosc") != nullptr && r.rack.boards[0]->name == "Kick", "a sound can be changed to one of your saved sounds");
        CHECK (! r.changeToSaved (0, wav), "a file that isn't a saved sound changes nothing");
        juce::ignoreUnused (before4);
        file.deleteFile(); wav.deleteFile();
        // and it all survives a save
        juce::MemoryBlock saved; r.getStateInformation (saved);
        Nebula2ModulationProcessor q; q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        CHECK (q.rack.save() == r.rack.save(), "a changed sound saves and reopens with the project");
    }

    {
        // The mixing desk opens up to show pan and more, and closes again
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        ed->selectTab (r.selectedTab.load());           // these checks are about one sound on its own tab
        const int closed = ed->desk().getHeight(), boardH = ed->board().getParentComponent()->getParentComponent()->getHeight();
        CHECK (! r.mixerTall.load() && closed == n2m::MixerStrip::heightFor (false), "the desk starts closed");
        ed->desk().setTall (true);
        CHECK (ed->desk().getHeight() == n2m::MixerStrip::heightFor (true) && ed->desk().getHeight() > closed + 60, "MORE makes the desk taller");
        CHECK (ed->desk().getBottom() == ed->getHeight() && ed->desk().getY() >= 0, "it still sits at the bottom of the window");
        // pan: all the way left leaves nothing on the right
        for (auto& b : r.rack.boards) b->mute.store (true);
        r.rack.boards[0]->mute.store (false);
        auto sides = [&] (float& l, float& rr)
        {
            l = rr = 0; song.playing = true; song.ppq = 0;
            for (int i = 0; i < 200; ++i) { juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer m; r.processBlock (buf, m); song.ppq += 256 * song.bpm / 60.0 / 48000.0; l = juce::jmax (l, buf.getMagnitude (0, 0, 256)); rr = juce::jmax (rr, buf.getMagnitude (1, 0, 256)); }
            song.playing = false;
        };
        float l = 0, rr = 0;
        sides (l, rr);
        CHECK (l > 0.05f && std::abs (l - rr) < 0.01f, "in the centre the kick is the same on both sides");
        ed->desk().setPan (0, -1.0f);
        sides (l, rr);
        CHECK (l > 0.05f && rr < 0.001f, "panned left it is only on the left");
        ed->desk().setPan (0, 0.5f);
        sides (l, rr);
        CHECK (rr > l * 1.5f && l > 0.01f, "panned half right it leans right");
        ed->desk().setPan (0, 0.02f);
        CHECK (r.rack.boards[0]->pan.load() == 0.0f, "near the middle it snaps to the centre");
        ed->desk().setPan (99, 1.0f);
        // saved with the project, and it closes again
        ed->desk().setPan (0, -0.3f);
        juce::MemoryBlock saved; r.getStateInformation (saved);
        Nebula2ModulationProcessor q; q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        CHECK (q.mixerTall.load() && std::abs (q.rack.boards[0]->pan.load() + 0.3f) < 1.0e-4f, "an opened desk and its pans come back with the project");
        ed->desk().setTall (false);
        CHECK (ed->desk().getHeight() == closed && ed->board().getParentComponent()->getParentComponent()->getHeight() == boardH, "LESS closes it and gives the board its room back");
        juce::MemoryBlock old;
        { juce::MemoryOutputStream o (old, false); o.writeString (juce::String (r.rack.save())); o.writeInt (0); o.writeFloat (1.0f); }
        q.setStateInformation (old.getData(), (int) old.getSize());
        CHECK (! q.mixerTall.load(), "a project from before this loads with the desk closed");
    }

    {
        // Favourite units are remembered, and only real units count
        const auto before = n2m::AddPanel::favourites();
        n2m::AddPanel::setFavourite ("hall", true);
        n2m::AddPanel::setFavourite ("lfo", true);
        n2m::AddPanel::setFavourite ("no-such-unit", true);
        auto now = n2m::AddPanel::favourites();
        auto has = [&now] (const char* t) { return std::find (now.begin(), now.end(), t) != now.end(); };
        CHECK (has ("hall") && has ("lfo") && ! has ("no-such-unit"), "starred units are kept as favourites; made-up ones are not");
        n2m::AddPanel::setFavourite ("hall", true);
        now = n2m::AddPanel::favourites();
        CHECK (std::count (now.begin(), now.end(), std::string ("hall")) == 1, "starring one twice keeps it once");
        n2m::AddPanel::setFavourite ("hall", false); n2m::AddPanel::setFavourite ("lfo", false);
        now = n2m::AddPanel::favourites();
        CHECK (! has ("hall") && ! has ("lfo"), "un-starring removes it");
        for (auto& t : before) n2m::AddPanel::setFavourite (t, true);
    }

    // The All view: every sound on one board
    {
        Nebula2ModulationProcessor r;
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        auto& all = ed->all();
        const int n = (int) r.rack.boards.size();
        CHECK (r.showAll.load() && all.isVisible(), "a new project opens on the All view");
        for (int i = 0; i < n; ++i)
        {
            auto* v = all.viewOf (i);
            CHECK (v != nullptr && v->board == r.rack.boards[(size_t) i].get(), "each sound has its own area");
            if (v == nullptr) continue;
            for (auto& m : r.rack.boards[(size_t) i]->modules) CHECK (v->compFor (m->uid) != nullptr, "every unit of every sound is on the board");
            // no two areas overlap
            for (int k = 0; k < i; ++k) CHECK (! all.areaOf (i).intersects (all.areaOf (k)), "the areas don't overlap");
            CHECK (all.getLocalBounds().contains (all.areaOf (i)), "every area is on the board");
        }
        // fitted: at the fit zoom the whole thing is no bigger than the window it scrolls in
        auto* vp = all.findParentComponentOfClass<juce::Viewport>();
        CHECK (vp != nullptr && r.zoomAll.load() == 0.0f, "it starts fitted");
        int right = 0, bottom = 0;
        for (int i = 0; i < n; ++i) { right = juce::jmax (right, all.areaOf (i).getRight()); bottom = juce::jmax (bottom, all.areaOf (i).getBottom()); }
        CHECK ((float) right * all.zoomNow() <= (float) vp->getWidth() && (float) bottom * all.zoomNow() <= (float) vp->getHeight(), "fitted, everything is in view");
        // cabling works on the All view just as on a tab, and Undo takes it back
        auto& bass = *r.rack.boards[3];
        auto* bv = all.viewOf (3);
        n2m::Module *drive = nullptr, *out = nullptr;
        for (auto& m : bass.modules) { if (std::string (m->info->type) == "drive") drive = m.get(); if (std::string (m->info->type) == "output") out = m.get(); }
        const size_t cables = bass.cables.size();
        auto* c1 = bv->compFor (out->uid); auto* c2 = bv->compFor (drive->uid);
        bv->beginCable (*c1, false, 1, c1->getPosition() + c1->jackCentre (false, 1));
        bv->endCable (c2->getPosition() + c2->jackCentre (true, 0));
        CHECK (bass.cables.size() == cables + 1, "a cable can be made on the All view");
        ed->stepHistory (false);
        CHECK (bass.cables.size() == cables && all.isVisible() && all.viewOf (3) != nullptr && all.viewOf (3)->board == &bass, "Undo takes it back and the All view stays up");
        // zooming in steps up from the fit, and the zoom figure puts it back
        const float fit = all.zoomNow();
        ed->stepZoom (1);
        CHECK (r.zoomAll.load() > fit && std::abs (all.zoomNow() - r.zoomAll.load()) < 1.0e-6f, "zooming in steps up from the fit");
        ed->stepZoom (-1); ed->stepZoom (-1);
        CHECK (all.zoomNow() < fit, "and out goes below it");
        r.zoomAll.store (0.0f); all.layout();
        CHECK (std::abs (all.zoomNow() - fit) < 1.0e-4f, "back to fitted");
        // Add units goes to the selected sound, and that sound's area grows
        r.selectedTab.store (2);
        const size_t mods = r.rack.boards[2]->modules.size();
        const int h = all.viewOf (2)->getHeight();
        ed->addUnits ({ "filter" });
        CHECK (r.rack.boards[2]->modules.size() == mods + 1 && all.viewOf (2)->compFor (r.rack.boards[2]->modules.back()->uid) != nullptr && all.viewOf (2)->getHeight() > h, "Add units lands on the selected sound");
        // a tab shows one sound alone; All brings them all back
        ed->selectTab (1);
        CHECK (! r.showAll.load() && ed->board().isVisible() && ! all.isVisible() && ed->board().board == r.rack.boards[1].get() && all.viewOf (0) == nullptr, "a tab shows that sound alone");
        ed->showAllSounds();
        CHECK (r.showAll.load() && all.isVisible() && ed->board().board == nullptr && all.viewOf (n - 1) != nullptr, "All shows them together again");
        // which view is up is saved with the project; a project from before the All view opens on its tab
        juce::MemoryBlock z; r.getStateInformation (z);
        Nebula2ModulationProcessor other; other.setStateInformation (z.getData(), (int) z.getSize());
        CHECK (other.showAll.load(), "the view is saved with the project");
        juce::MemoryBlock old; { juce::MemoryOutputStream o (old, false); o.writeString (juce::String (r.rack.save())); o.writeInt (1); o.writeFloat (0.8f); o.writeBool (false); }
        Nebula2ModulationProcessor before; before.setStateInformation (old.getData(), (int) old.getSize());
        CHECK (! before.showAll.load() && before.selectedTab.load() == 1 && before.zoomAll.load() == 0.0f, "an older project opens on its tab");
    }

    // The style bar: Make track, Variation, Random knobs and the part chips, as the screen does them
    {
        Nebula2ModulationProcessor r;
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        auto click = [ed] (juce::Rectangle<int> area)
        {
            auto& bar = ed->styles();
            const auto at = area.getCentre().toFloat();
            bar.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, &bar, &bar, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false));
        };
        const auto starter = r.rack.save();
        int acid = 0;
        for (int i = 0; i < (int) n2m::styles().size(); ++i) if (std::string (n2m::styles()[(size_t) i].id) == "acid") acid = i;
        click (ed->styles().styleChip (acid));
        CHECK (r.rack.style == "acid" && r.rack.save().find ("board kick|Kick|") != std::string::npos && r.rack.boards.size() == 5, "picking a style only chooses it");
        const auto chosen = r.rack.save();
        CHECK (chosen != starter, "the chosen style is part of the project");
        click (ed->styles().makeArea());
        CHECK (r.rack.boards.size() >= 4 && r.rack.save() != chosen, "Make track makes a new track");
        bool acidBass = false;
        for (auto& b : r.rack.boards) acidBass = acidBass || b->name == "Acid bass";
        CHECK (acidBass, "the track is in the chosen style");
        CHECK (r.showAll.load() && ed->all().isVisible() && ! ed->board().isVisible(), "a new project opens with every sound on one board");
        CHECK (ed->all().viewOf (0) != nullptr && ed->all().viewOf (0)->board == r.rack.boards[0].get() && ed->all().viewOf ((int) r.rack.boards.size() - 1) != nullptr && ed->all().viewOf ((int) r.rack.boards.size()) == nullptr, "the new track's sounds are all showing");
        const auto made = r.rack.save();
        const size_t tabs = r.rack.boards.size();
        click (ed->styles().variationArea());
        CHECK (r.rack.save() != made && r.rack.boards.size() == tabs, "Variation changes the patterns and keeps the sounds");
        ed->stepHistory (false);
        CHECK (r.rack.save() == made, "Undo takes a Variation back");
        click (ed->styles().knobsArea());
        CHECK (r.rack.save() != made, "Random knobs moves knobs");
        ed->stepHistory (false);
        CHECK (r.rack.save() == made, "Undo takes Random knobs back");
        // A part chip makes that sound again in place: same tab, same mixer channel
        int kick = 0;
        r.rack.boards[0]->level.store (0.33f);
        click (ed->styles().partChip (kick));
        CHECK (r.rack.boards.size() == tabs && r.rack.boards[0]->type == "kick" && std::abs (r.rack.boards[0]->level.load() - 0.33f) < 1.0e-6f, "a part chip makes that sound again and keeps its channel");
        // In Add mode it adds another
        click (ed->styles().modeArea (true));
        CHECK (r.addMode.load(), "Add mode");
        click (ed->styles().partChip (kick));
        CHECK (r.rack.boards.size() == tabs + 1 && r.rack.boards.back()->name == "Kick 2", "in Add mode a part chip adds another");
        click (ed->styles().makeArea());
        CHECK (r.rack.boards.size() > tabs + 1, "in Add mode Make track adds to what is there");
        click (ed->styles().modeArea (false));
        click (ed->styles().makeArea());
        CHECK (r.rack.boards.size() < tabs + 4, "in Replace mode it starts again");
        ed->stepHistory (false); ed->stepHistory (false); ed->stepHistory (false); ed->stepHistory (false); ed->stepHistory (false);
        CHECK (r.rack.save() == chosen && ! r.history.canUndo(), "undo walks all the way back");
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer m;
        r.preview.store (true);
        float peak = 0;
        for (int b = 0; b < 400; ++b) { r.processBlock (buf, m); peak = juce::jmax (peak, buf.getMagnitude (0, 256)); }
        CHECK (peak > 0.05f, "the track plays");
    }

    if (failures == 0) std::printf ("All passed\n");
    return failures == 0 ? 0 : 1;
}
