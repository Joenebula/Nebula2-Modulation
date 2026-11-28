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
        CHECK (lost != nullptr && lost->hasSample() && lost->usingCopy() && lost->status.isEmpty() && lost->fileName() == "beep.wav" && u.rack.boards[0]->modules.size() == b.modules.size(), "a file that has moved: the copy kept in the project plays instead");
        {
            // The file travels with the project
            const auto text = s.rack.save();
            const auto xLine = n2m::Rack::extraIn (text, smp->uid);
            CHECK (smp->keepsCopy() && ! smp->usingCopy() && text.find ("\nxd " + std::to_string (smp->uid) + " ") != std::string::npos, "a small file is kept inside the project, on a line of its own");
            CHECK (xLine == juce::Base64::toBase64 (wav.getFullPathName()).toStdString(), "and the line older versions read still holds just where the file is");
            CHECK (back != nullptr && ! back->usingCopy() && back->keepsCopy(), "while the file is where it was, the file itself is used");
            // the copy sounds exactly like the file
            auto play = [] (Nebula2ModulationProcessor& pr)
            {
                FakeCubase c; pr.setPlayHead (&c); c.playing = true;
                std::vector<float> y; run (pr, c, 0.4, &y);
                pr.setPlayHead (nullptr);
                return y;
            };
            Nebula2ModulationProcessor fresh;
            fresh.setRateAndBufferSizeDetails (48000, 256); fresh.prepareToPlay (48000, 256);
            wav.getParentDirectory().getChildFile ("moved.wav").copyFileTo (wav);        // (back for a moment: this one reads the file)
            fresh.setStateInformation (saved.getData(), (int) saved.getSize());
            wav.deleteFile();
            const auto fromFile = play (fresh), fromCopy = play (u);
            CHECK (fromFile.size() == fromCopy.size() && fromFile == fromCopy && juce::FloatVectorOperations::findMinAndMax (fromCopy.data(), (int) fromCopy.size()).getEnd() > 0.2f, "the copy sounds exactly like the file");
            // saved again from the copy, it still travels
            juce::MemoryBlock again; u.getStateInformation (again);
            Nebula2ModulationProcessor v;
            v.setRateAndBufferSizeDetails (48000, 256); v.prepareToPlay (48000, 256);
            v.setStateInformation (again.getData(), (int) again.getSize());
            CHECK (v.rack.save() == u.rack.save() && u.rack.save() == text, "a project opened from its copy saves the same again");
            n2m::SampleModule* third = nullptr;
            for (auto& m : v.rack.boards[0]->modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) third = x;
            CHECK (third != nullptr && third->hasSample() && third->usingCopy(), "and opens from the copy again");
            // a project saved before this (no copy line): the file is asked for by name, as it always was
            n2m::Rack older; older.prepare (48000);
            CHECK (older.load (n2m::Rack::withoutExtra (text, -1)) && older.save() == text, "(taking nothing out leaves the text alone)");
            std::string oldText; { std::istringstream in (text); std::string line; while (std::getline (in, line)) if (line.compare (0, 3, "xd ") != 0) oldText += line + "\n"; }
            CHECK (older.load (oldText), "a project saved before copies were kept still loads");
            n2m::SampleModule* om = nullptr;
            for (auto& m : older.boards[0]->modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) om = x;
            CHECK (om != nullptr && ! om->hasSample() && om->status == "Can't find beep.wav" && older.boards[0]->modules.size() == b.modules.size(), "a missing file with no copy is named, and nothing else is lost");
            // a saved sound carries the file too
            const auto soundFile = dir.getChildFile ("Beep.n2msound");
            CHECK (u.saveSound (0, soundFile), "(the sound saves)");
            Nebula2ModulationProcessor w;
            w.setRateAndBufferSizeDetails (48000, 256); w.prepareToPlay (48000, 256);
            const int tab = w.loadSound (soundFile);
            n2m::SampleModule* ws = nullptr;
            if (tab >= 0) for (auto& m : w.rack.boards[(size_t) tab]->modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) ws = x;
            CHECK (ws != nullptr && ws->hasSample() && ws->usingCopy() && std::abs (ws->seconds() - 0.25) < 0.001, "a saved sound carries its sample with it");
            // a copy that is damaged: the player still says which file it wants
            std::string broken = oldText; broken.insert (broken.rfind ("end\n"), "xd " + std::to_string (smp->uid) + " AAAAAAAA\n");
            CHECK (older.load (broken), "(damaged copy loads)");
            om = nullptr; for (auto& m : older.boards[0]->modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) om = x;
            CHECK (om != nullptr && ! om->hasSample() && om->status == "Can't find beep.wav", "a damaged copy is ignored and the file is named");
            // Too big to keep (over 10 MB): only where it is, and a plain message if it has gone
            const auto big = dir.getChildFile ("long.wav");
            {
                juce::AudioBuffer<float> lb (2, 48000 * 60);
                for (int i = 0; i < lb.getNumSamples(); ++i) { const float y = 0.4f * (float) std::sin (i * 0.05); lb.setSample (0, i, y); lb.setSample (1, i, y); }
                std::unique_ptr<juce::AudioFormatWriter> wr (juce::WavAudioFormat().createWriterFor (new juce::FileOutputStream (big), 48000, 2, 16, {}, 0));
                wr->writeFromAudioSampleBuffer (lb, 0, lb.getNumSamples());
            }
            CHECK (big.getSize() > n2m::SampleModule::kKeepBytes && ws != nullptr && ws->load (big).isEmpty() && ws->hasSample() && ! ws->keepsCopy(), "a big file loads but isn't kept in the project");
            const auto bigText = w.rack.save();
            CHECK (bigText.size() < 200000 && bigText.find ("xd " + std::to_string (ws != nullptr ? ws->uid : 0) + " big") != std::string::npos, "the project stays small and notes that the file was too big to keep");
            CHECK (older.load (bigText), "(loads with the big file there)");
            om = nullptr; for (auto& m : older.boards[(size_t) tab]->modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) om = x;
            CHECK (om != nullptr && om->hasSample() && ! om->usingCopy(), "with the big file where it was, it plays");
            big.deleteFile();
            CHECK (older.load (bigText), "(loads with the big file gone)");
            om = nullptr; for (auto& m : older.boards[(size_t) tab]->modules) if (auto* x = dynamic_cast<n2m::SampleModule*> (m.get())) om = x;
            CHECK (om != nullptr && ! om->hasSample() && om->status.contains ("long.wav") && om->status.contains ("too big"), "with it gone, the player says the file was too big to keep and which file it wants");
        }
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
            CHECK (kick.modules.size() == 7 && view.compFor (drive->uid) != nullptr && view.getNumChildComponents() == 7 + 1 /* the units, and the hidden cover that locks a frozen board */, "one more removes the Filter, and the screen shows exactly what is left");
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

        // The wide Notes and rhythm units: what their screens' clicks do, done through the same functions
        {
            ed->selectTab (0);
            n2m::Module* ds = nullptr;
            for (auto& m : r.rack.boards[0]->modules) if (std::string (m->info->type) == "drumseq") ds = m.get();
            auto seqOf = [&view] (n2m::Module* m) { auto* c = view.compFor (m->uid); return c != nullptr ? c->seq.get() : nullptr; };
            CHECK (ds != nullptr && seqOf (ds) != nullptr, "the kick's drum sequencer is on screen");
            if (ds != nullptr && seqOf (ds) != nullptr)
            {
                const int bars = ds->paramIndex ("bars");
                auto* c = view.compFor (ds->uid);
                CHECK (c->getWidth() == n2m::lay::width (*ds->info) && c->getHeight() == n2m::lay::panelH && c->getWidth() > c->getHeight(), "a Notes and rhythm unit is a wide panel of the usual height");
                CHECK (c->jackCentre (true, 0).x > c->getWidth() / 2 && c->jackCentre (true, 0).y > n2m::lay::wideJacksY && c->midiArea().getX() < c->getWidth() / 2 && c->midiArea().getY() > n2m::lay::wideJacksY, "outputs at the bottom right, DRAG MIDI at the bottom left");
                // the idea drop-down names the pattern, and choosing another changes it
                n2m::setDrum (*ds, n2m::drumIdeas ("kick")[0].pat);
                CHECK (seqOf (ds)->ideaName() == "Four on the floor", "the idea drop-down shows the idea's name");
                seqOf (ds)->chooseIdea (3);
                CHECK (seqOf (ds)->ideaName() == juce::String (n2m::drumIdeas ("kick")[3].name) && ds->data[3].load() == 1.0f, "choosing an idea from the drop-down sets the pattern");
                ed->stepHistory (false);
                CHECK (seqOf (ds)->ideaName() == "Four on the floor", "and Undo puts the old one back");
                // velocity bars: a click turns a hit into an accent and back; an empty step stays empty
                CHECK (ds->data[4].load() == 1.0f && ds->data[1].load() == 0.0f, "(the fifth step is a plain hit, the second is empty)");
                seqOf (ds)->toggleAccentAt (4);
                CHECK (ds->data[4].load() == 2.0f, "a click on a hit's velocity bar makes it an accent");
                ed->stepHistory (false);
                CHECK (ds->data[4].load() == 1.0f, "Undo takes the accent away");
                seqOf (ds)->toggleAccentAt (4); seqOf (ds)->toggleAccentAt (4);
                CHECK (ds->data[4].load() == 1.0f, "a second click makes it a plain hit again");
                seqOf (ds)->toggleAccentAt (1);
                CHECK (ds->data[1].load() == 0.0f, "a step with no hit has no bar to click");
                seqOf (ds)->toggleAccentAt (0);
                CHECK (seqOf (ds)->ideaName() == "Your own", "once edited, the pattern is no longer called by the idea's name");
                seqOf (ds)->toggleAccentAt (0);
                // bar buttons: how long the pattern is, and which bar is on show
                seqOf (ds)->setBars (2);
                CHECK (ds->P (bars) == 2.0f, "the 4 button makes the pattern four bars long");
                seqOf (ds)->showPage (3);
                CHECK (seqOf (ds)->shownPage() == 3, "any of its bars can be put on show");
                seqOf (ds)->toggleAccentAt (3 * n2m::kSteps + 4);
                CHECK (ds->data[(size_t) (3 * n2m::kSteps + 4)].load() == 2.0f, "and edited");
                seqOf (ds)->toggleAccentAt (3 * n2m::kSteps + 4);
                seqOf (ds)->setBars (0);
                CHECK (ds->P (bars) == 0.0f && seqOf (ds)->shownPage() == 0, "back to one bar, the first bar is on show again");
                ed->stepHistory (false);
                CHECK (ds->P (bars) == 2.0f, "Undo brings the length back");
                seqOf (ds)->setBars (0);
                // a real click, to be sure the buttons are where the functions say
                auto click = [&] (juce::Point<int> at)
                {
                    auto* sv = seqOf (ds);
                    const auto ev = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, sv, sv, juce::Time::getCurrentTime(), at.toFloat(), juce::Time::getCurrentTime(), 1, false);
                    sv->mouseDown (ev);
                };
                click (seqOf (ds)->barButton (1).getCentre());
                CHECK (ds->P (bars) == 1.0f, "clicking the 2 button gives two bars");
                click (seqOf (ds)->barButton (1).getCentre());
                CHECK (ds->P (bars) == 1.0f && seqOf (ds)->shownPage() == 1, "clicking it again shows the next bar");
                click (seqOf (ds)->pageMark (0).getCentre());
                CHECK (seqOf (ds)->shownPage() == 0, "and a bar's mark shows that bar");
                click (seqOf (ds)->barButton (0).getCentre());
                click ({ 4 * n2m::lay::stepW + 8, seqOf (ds)->velRow().getCentreY() });
                CHECK (ds->data[4].load() == 2.0f, "a click on the fifth velocity bar accents the fifth step");
                click ({ 4 * n2m::lay::stepW + 8, seqOf (ds)->velRow().getCentreY() });
                click ({ 1 * n2m::lay::stepW + 8, seqOf (ds)->stepRow().getCentreY() });
                CHECK (ds->data[1].load() == 1.0f, "a click on a step still puts a hit there");
                click ({ 1 * n2m::lay::stepW + 8, seqOf (ds)->stepRow().getCentreY() }); click ({ 1 * n2m::lay::stepW + 8, seqOf (ds)->stepRow().getCentreY() });
                CHECK (ds->data[1].load() == 0.0f, "then an accent, then nothing, as before");
                n2m::setDrum (*ds, n2m::drumIdeas ("kick")[0].pat);
            }
            // the note sequencer: a click in the grid sets the step's note, a click on the note takes it away
            ed->selectTab (3);
            n2m::Module* ns = nullptr;
            for (auto& m : r.rack.boards[3]->modules) if (m->info->seq == n2m::Seq::Note) ns = m.get();
            CHECK (ns != nullptr && seqOf (ns) != nullptr, "the bass's note sequencer is on screen");
            if (ns != nullptr && seqOf (ns) != nullptr)
            {
                auto* sv = seqOf (ns);
                auto click = [&] (juce::Point<int> at)
                {
                    const auto ev = juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, sv, sv, juce::Time::getCurrentTime(), at.toFloat(), juce::Time::getCurrentTime(), 1, false);
                    sv->mouseDown (ev);
                };
                const float was = ns->data[0].load(), wasDeg = ns->data[(size_t) n2m::kMaxSteps].load();
                const juce::Point<int> top (8, 36 + 3);
                click (top);
                CHECK (ns->data[0].load() == 1.0f && ns->data[(size_t) n2m::kMaxSteps].load() == 14.0f, "a click at the top of the grid puts the step's note at the top");
                click (top);
                CHECK (ns->data[0].load() == 0.0f, "a click on the note takes it away");
                click ({ 8, sv->nameRow().getCentreY() });
                CHECK (ns->data[0].load() == 1.0f, "a click on its name brings it back");
                ns->data[0].store (was); ns->data[(size_t) n2m::kMaxSteps].store (wasDeg);
                CHECK (view.compFor (ns->uid)->linkArea().getY() > n2m::lay::wideJacksY && ! view.compFor (ns->uid)->linkArea().intersects (view.compFor (ns->uid)->midiArea()), "its Link label sits beside DRAG MIDI");
            }
            // the chord unit: the keyboard lights the notes of a chord, and only the tiles and buttons take clicks
            ed->selectTab (4);
            for (auto& m : r.rack.boards[4]->modules)
                if (auto* c = view.compFor (m->uid))
                    if (c->chords != nullptr)
                    {
                        const auto lit = c->chords->litNotes();
                        CHECK (lit.size() == 5, "the chord keyboard lights the bass note and four notes of a chord");
                        CHECK (c->chords->hitTest (n2m::ChordView::tile (0).getCentreX(), n2m::ChordView::tile (0).getCentreY()), "a chord tile takes clicks");
                        CHECK (! c->chords->hitTest (c->chords->keyboardArea().getCentreX(), c->chords->keyboardArea().getCentreY()), "the keyboard is only a picture: the panel can be picked up by it");
                        const auto ma = c->midiArea() - c->chords->getPosition();
                        CHECK (! ma.isEmpty() && ! c->chords->hitTest (ma.getCentreX(), ma.getCentreY()), "DRAG MIDI is not covered by the chord screen");
                        // the longest song there can be: 32 chords, four rows of tiles
                        const auto kept = r.rack.harmony;
                        r.rack.harmony.lenBars = 32; r.rack.harmony.chordBars = 1;
                        r.rack.generateChords (r.rack.harmony.seed);
                        CHECK (r.rack.harmony.names.size() == 32, "the test song has 32 chords");
                        CHECK (! n2m::ChordView::tile (31).intersects (c->chords->keyboardArea()) && c->chords->keyboardArea().getHeight() >= 40, "with every tile row in use there is still room for the keyboard");
                        r.rack.harmony = kept; r.rack.rebuildChords();
                    }
            // the units that only show things each have their screen
            {
                auto nb = n2m::makeBoard ("empty", 48000);
                for (const char* t : { "keys", "arp", "euclid", "ratchet", "swing" }) nb->add (t, 0, 0, 48000);
                n2m::tidy (*nb);
                r.rack.addBoard (std::move (nb));
                const int tab = (int) r.rack.boards.size() - 1;
                ed->selectTab (tab);
                int screens = 0;
                for (auto& m : r.rack.boards[(size_t) tab]->modules) if (auto* c = view.compFor (m->uid)) if (c->screen != nullptr) { ++screens; c->screen->tick(); }
                CHECK (screens == 5, "the keyboard, arpeggiator, Euclidean rhythm, Ratchet and Swing each have a screen");
                const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                CHECK (img.isValid(), "and they draw");
                ed->selectTab (-1);
                r.rack.removeBoard (tab);
            }
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
            {
                // Undo: pointing a knob, emptying the slot and loading a plugin are each a step
                r.history.clear();
                slot->assignKnob (0, -1); slot->set ("k1", 0.6f);
                slot->instance()->getParameters()[ctl]->setValue (0.6f);
                r.history.beforeUnit (b, slot->uid, false);
                slot->assignKnob (1, ctl);
                CHECK (slot->knobTarget (1) == ctl && r.history.canUndo(), "pointing a knob at a control is a step for Undo");
                slot->instance()->getParameters()[ctl]->setValue (0.33f);          // meanwhile, a change in the plugin's own window
                auto* inside = slot->instance();
                r.history.undo();
                CHECK (slot->knobTarget (1) == -1 && slot->instance() == inside, "Undo points the knob at nothing again, without loading the plugin again");
                CHECK (std::abs (value() - 0.33f) < 0.02f, "and what was changed in the plugin's own window stays");
                r.history.redo();
                CHECK (slot->knobTarget (1) == ctl && slot->instance() == inside, "Redo points it again");
                run (r, song, 0.2);
                CHECK (std::abs (value() - 0.33f) < 0.02f, "and the control doesn't jump");
                r.history.clear();
                // emptying the slot
                slot->instance()->getParameters()[ctl]->setValue (0.71f);
                const auto name = slot->pluginName();
                r.history.beforeUnit (b, slot->uid);
                slot->unload();
                CHECK (! slot->hasPlugin() && slot->saveExtra().empty(), "an emptied slot is saved as empty");
                r.history.undo();
                CHECK (b.find (slot->uid) == slot && slot->hasPlugin() && slot->pluginName() == name, "Undo brings the emptied slot's plugin back");
                CHECK (std::abs (value() - 0.71f) < 0.02f && slot->knobTarget (1) == ctl, "with its settings and its knobs");
                song.playing = true; song.ppq = 0;
                CHECK (run (r, song, 1.0) > 0.001f, "and it plays again");
                song.playing = false; run (r, song, 3.0);
                r.history.redo();
                CHECK (! slot->hasPlugin(), "Redo empties it again");
                // loading into an empty slot
                r.history.beforeUnit (b, slot->uid);
                slot->load (found[0]);
                r.history.undo();
                CHECK (! slot->hasPlugin(), "Undo of loading a plugin leaves the slot empty");
                r.history.redo();
                CHECK (slot->hasPlugin() && slot->pluginName() == name, "and Redo loads it again");
                r.history.clear();
            }
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
        if (argc > 3) CHECK (! r.changeToPlugin (0, "Nebula2 Glitch", argv[3]) && unit (0, "plugin") == nullptr && r.problem.contains ("is an effect") && r.problem.startsWith ("Nebula2 Glitch") && r.problem.contains ("Plugin slot"), "an effect can't be a tab's sound: nothing changes, and it says why");
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

    // Every unit, the plugin slot and sample player included, fits on its panel
    for (auto* t : n2m::allTypes())
    {
        if (! n2m::lay::fits (*t)) std::printf ("does not fit: %s\n", t->name);
        CHECK (n2m::lay::fits (*t), "every unit fits on its panel without overlapping");
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
        CHECK (std::abs (all.zoomNow() - fit) < 0.01f, "back to fitted");
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

    // The timeline: a knob's line, drawn with the mouse on the Song page
    {
        Nebula2ModulationProcessor r;
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        auto& song = ed->songPanel;
        auto event = [&song] (juce::Point<int> at, bool right)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier,
                                     0, 0, 0, 0, 0, &song, &song, juce::Time::getCurrentTime(), at.toFloat(), juce::Time::getCurrentTime(), 1, false);
        };
        song.open();
        auto& bass = *r.rack.boards[3];
        n2m::Module* filter = nullptr;
        for (auto& m : bass.modules) if (std::string (m->info->type) == "filter") filter = m.get();
        const int cut = filter->paramIndex ("cutoff");
        const auto before = r.rack.save();
        const int lane = song.addLane (3, filter->uid, cut);
        CHECK (lane == 0 && bass.autos.size() == 1 && bass.autos[0]->count.load() == 2, "+ Knob gives the knob a line");
        CHECK (song.addLane (3, filter->uid, cut) == 0 && bass.autos.size() == 1 && song.addLane (3, filter->uid, filter->paramIndex ("type")) == -1, "one line a knob, and none for a switch");
        CHECK (song.getLocalBounds().contains (song.laneArea (3, 0)) && song.laneArea (3, 0).getY() == song.cell (3, 0).getBottom() && song.cell (4, 0).getY() == song.laneArea (3, 0).getBottom(), "the line has its own row, under its sound");
        auto& a = *bass.autos[0];
        // click on the row: a point lands there, on the nearest quarter bar
        song.mouseDown (event (song.pointAt (3, 0, 2.1f, 0.8f), false));
        song.mouseUp (event (song.pointAt (3, 0, 2.1f, 0.8f), false));
        CHECK (a.count.load() == 3 && a.bar[1].load() == 2.0f && std::abs (a.val[1].load() - 0.8f) < 0.03f, "a click on the line adds a point there");
        // drag it
        song.mouseDown (event (song.pointAt (3, 0, 2.0f, a.val[1].load()), false));
        song.mouseDrag (event (song.pointAt (3, 0, 5.0f, 0.3f), false));
        song.mouseUp (event (song.pointAt (3, 0, 5.0f, 0.3f), false));
        CHECK (a.count.load() == 3 && a.bar[1].load() == 5.0f && std::abs (a.val[1].load() - 0.3f) < 0.03f, "dragging a point moves it");
        // it plays: half way to the point the knob is half way there
        const float start = a.val[0].load();
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
        r.preview.store (true);
        const int blocks = (int) (2.5 * 4.0 * 60.0 / r.bpm.load() * 48000.0 / 256.0);
        for (int b = 0; b < blocks; ++b) r.processBlock (buf, midi);
        r.preview.store (false);
        CHECK (std::abs (n2m::paramToNorm (filter->info->params[(size_t) cut], filter->P (cut)) - (start + (0.3f - start) * 0.5f)) < 0.03f, "the knob moves along its line as the song plays");
        // Undo: back before the drag, then before the click
        ed->stepHistory (false);
        CHECK (bass.autos.size() == 1 && bass.autos[0]->count.load() == 3 && bass.autos[0]->bar[1].load() == 2.0f, "Undo takes the drag back");
        ed->stepHistory (false);
        CHECK (bass.autos.size() == 1 && bass.autos[0]->count.load() == 2, "and then the point");
        // right-click a point removes it, but the last one stays
        song.mouseDown (event (song.pointAt (3, 0, 0.0f, bass.autos[0]->val[0].load()), true));
        CHECK (bass.autos[0]->count.load() == 1, "right-click removes a point");
        song.mouseDown (event (song.pointAt (3, 0, bass.autos[0]->bar[0].load(), bass.autos[0]->val[0].load()), true));
        CHECK (bass.autos[0]->count.load() == 1, "the last point stays");
        // the squares still work with a line in the way
        r.rack.song.on.store (true);
        song.mouseDown (event (song.cell (4, 1).getCentre(), false));
        song.mouseUp (event (song.cell (4, 1).getCentre(), false));
        CHECK (((r.rack.boards[4]->plays.load() >> 1) & 1u) == 0, "a section square under a line still switches");
        // taking the line off
        song.mouseDown (event (song.removeChip (3, 0).getCentre(), false));
        CHECK (bass.autos.empty(), "the cross takes the line off");
        while (r.history.canUndo()) ed->stepHistory (false);
        r.rack.song.on.store (false);                    // (switched on above without Undo knowing)
        CHECK (r.rack.save() == before, "and Undo walks all of it back");
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

    {
        // More room for the board: Focus, and Full screen
        Nebula2ModulationProcessor r;
        r.setRateAndBufferSizeDetails (48000, 256); r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        const int w0 = ed->getWidth(), h0 = ed->getHeight();
        const auto board0 = ed->boardArea();
        CHECK (! r.focusView.load() && ! r.bigWindow.load(), "a new project starts at the normal size with everything showing");
        ed->setFocus (true);
        CHECK (! ed->desk().isVisible() && ed->boardArea().getHeight() > board0.getHeight() + 150 && ed->boardArea().getBottom() == ed->getHeight(), "Focus hides the style bar and the mixing desk and gives the board their room");
        CHECK (ed->getWidth() == w0 && ed->getHeight() == h0, "without changing the window's size");
        ed->setFocus (false);
        CHECK (ed->desk().isVisible() && ed->boardArea() == board0, "Focus off puts everything back exactly");
        ed->setBigWindow (true);
        CHECK (ed->getWidth() > w0 + 300 && ed->getHeight() > h0 + 100, "Full screen makes the plugin's window much bigger");
        CHECK (ed->boardArea().getWidth() == ed->getWidth() && ed->boardArea().getHeight() > board0.getHeight() + 100, "and the board fills the extra room");
        CHECK (ed->desk().getBottom() == ed->getHeight() && ed->desk().getWidth() == ed->getWidth(), "the mixing desk stays along the bottom, full width");
        ed->setFocus (true);
        const auto biggest = ed->boardArea();
        CHECK (biggest.getHeight() > ed->getHeight() - 120, "both together: nearly the whole window is board");
        // saved with the project
        juce::MemoryBlock saved; r.getStateInformation (saved);
        Nebula2ModulationProcessor q; q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        CHECK (q.focusView.load() && q.bigWindow.load(), "Focus and Full screen come back with the project");
        std::unique_ptr<juce::AudioProcessorEditor> holder2 (q.createEditor());
        CHECK (holder2->getWidth() == ed->getWidth() && holder2->getHeight() == ed->getHeight(), "and the window reopens at that size");
        ed->setFocus (false); ed->setBigWindow (false);
        CHECK (ed->getWidth() == w0 && ed->getHeight() == h0 && ed->boardArea() == board0, "Normal size puts the window back as it was");
    }

    {
        // The bigger views: each opens for its unit, edits it, can be undone, and closes itself when its unit goes
        Nebula2ModulationProcessor r;
        r.setRateAndBufferSizeDetails (48000, 256); r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        auto& big = ed->bigPanel;
        auto unit = [&r] (int tab, const char* type) -> n2m::Module*
        {
            for (auto& m : r.rack.boards[(size_t) tab]->modules) if (std::string (m->info->type) == type) return m.get();
            return nullptr;
        };
        auto event = [] (juce::Component& c, juce::Point<float> at, juce::Point<float> downAt, bool right = false)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier,
                                     0, 0, 0, 0, 0, &c, &c, juce::Time::getCurrentTime(), downAt, juce::Time::getCurrentTime(), 1, at != downAt);
        };
        auto drag = [&event] (juce::Component& c, juce::Point<float> from, juce::Point<float> to, bool right = false)
        {
            c.mouseDown (event (c, from, from, right));
            for (int k = 1; k <= 4; ++k) c.mouseDrag (event (c, from + (to - from) * ((float) k / 4.0f), from, right));
            c.mouseUp (event (c, to, from, right));
        };
        auto click = [&event] (juce::Component& c, juce::Point<float> at, bool right = false) { c.mouseDown (event (c, at, at, right)); c.mouseUp (event (c, at, at, right)); };
        {   // a board with the other kinds of unit on it
            auto eb = n2m::makeBoard ("empty", 48000);
            for (const char* t : { "sample", "parameq", "stepmod", "scope", "filter", "arp" }) eb->add (t, 0, 0, 48000);
            n2m::tidy (*eb);
            r.rack.addBoard (std::move (eb));
        }
        const int extra = (int) r.rack.boards.size() - 1;

        // Which units have one, and where its label sits
        for (auto* info : n2m::allTypes())
        {
            const std::string t = info->type;
            const bool want = t == "noteseq" || t == "drumseq" || t == "chords" || t == "sample" || t == "parameq" || t == "stepmod" || t == "scope";
            CHECK (n2m::BigPanel::has (*info) == want, "the seven units on the list have a bigger view, and no others");
        }
        ed->selectTab (3);
        for (int tab : { 0, 3, 4, extra })
        {
            ed->selectTab (tab);
            for (auto& m : r.rack.boards[(size_t) tab]->modules)
            {
                auto* comp = ed->board().compFor (m->uid);
                if (comp == nullptr) { CHECK (false, "every unit is on its board"); continue; }
                const auto ea = comp->expandArea();
                if (! n2m::BigPanel::has (*m->info)) { CHECK (ea.isEmpty(), "a unit with no bigger view has no Expand label"); continue; }
                CHECK (! ea.isEmpty() && comp->getLocalBounds().reduced (10, 0).contains (ea) && ea.getBottom() <= n2m::lay::header, "the Expand label sits in the unit's top line, clear of the corner screws");
                CHECK (! ea.intersects (comp->linkArea()) && ! ea.intersects (comp->midiArea()), "clear of LINK and DRAG MIDI");
                bool overChild = false;
                for (auto* ch : comp->getChildren()) overChild = overChild || (ch->isVisible() && ch->getBounds().intersects (ea));
                CHECK (! overChild, "and over none of the unit's own controls");
                // a click on it opens that unit's view
                const auto at = ea.getCentre().toFloat();
                comp->mouseDown (event (*comp, at, at));
                CHECK (big.isOpen() && big.isVisible() && big.content() != nullptr && big.content()->unitId() == m->uid && big.content()->alive(), "a click on Expand opens that unit's bigger view");
                CHECK (big.getBounds() == ed->getLocalBounds() && ed->getLocalBounds().contains (big.card()) && big.card().reduced (10).contains (big.content()->getBounds()), "it is a card over the whole window, with the view inside it");
                CHECK (big.content()->getWidth() > 1000 && big.content()->getHeight() > 500, "with plenty of room at the normal window size");
                (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f);
                big.close();
                CHECK (! big.isOpen() && ! big.isVisible(), "Close closes it");
            }
        }

        // ---- 1. The piano roll
        ed->selectTab (3);
        auto* notes = unit (3, "noteseq");
        notes->set ("bars", 2);          // four bars
        const int quiet[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        for (int b = 0; b < 4; ++b) n2m::setNotesBar (*notes, b, "x...............", quiet);
        r.history.clear();
        CHECK (big.open (*r.rack.boards[3], *notes) && big.as<n2m::BigNoteRoll>() != nullptr && big.as<n2m::BigDrumGrid>() == nullptr, "the note sequencer opens a piano roll");
        if (auto* v = big.as<n2m::BigNoteRoll>())
        {
            CHECK (v->bars() == 4 && v->grid().getWidth() > 900 && v->cell (63, 14).getRight() <= (float) v->grid().getRight() + 0.5f && v->cell (0, -7).getBottom() <= (float) v->grid().getBottom() + 0.5f, "all four bars and three octaves are in view at once");
            v->setNote (20, 4);
            CHECK (notes->data[20].load() == 1.0f && notes->data[(size_t) (n2m::kMaxSteps + 20)].load() == 4.0f && notes->data[(size_t) (n2m::kLenRow + 20)].load() == 0.0f, "a note can be added anywhere in the four bars");
            v->setNoteLength (20, 6);
            CHECK (notes->data[(size_t) (n2m::kLenRow + 20)].load() == 6.0f && v->noteRect (20).getWidth() > v->cell (20, 4).getWidth() * 5.0f, "it can be given a length of its own, and is drawn that long");
            ed->stepHistory (false);
            CHECK (notes->data[(size_t) (n2m::kLenRow + 20)].load() == 0.0f && notes->data[20].load() == 1.0f, "Undo takes the length away again");
            CHECK (big.isOpen() && big.as<n2m::BigNoteRoll>() == v && v->alive(), "and the view stays open, because its unit is still there");
            ed->stepHistory (false);
            CHECK (notes->data[20].load() == 0.0f && big.isOpen(), "Undo again takes the note away");
            ed->stepHistory (true); ed->stepHistory (true);
            CHECK (notes->data[20].load() == 1.0f && notes->data[(size_t) (n2m::kLenRow + 20)].load() == 6.0f, "Redo brings both back");
            // with the mouse: click an empty place, drag its end, drag it up, click it away
            click (*v, v->cell (40, 2).getCentre());
            CHECK (notes->data[40].load() == 1.0f && notes->data[(size_t) (n2m::kMaxSteps + 40)].load() == 2.0f, "a click on an empty place adds the note there");
            const auto nr = v->noteRect (40);
            drag (*v, { nr.getRight() - 1.0f, nr.getCentreY() }, { v->cell (44, 2).getRight() - 1.0f, nr.getCentreY() });
            CHECK (notes->data[(size_t) (n2m::kLenRow + 40)].load() == 5.0f && notes->data[(size_t) (n2m::kMaxSteps + 40)].load() == 2.0f, "dragging a note's right-hand end sets its own length");
            drag (*v, v->noteRect (40).getCentre(), v->cell (41, 9).getCentre().withX (v->noteRect (40).getCentreX()));
            CHECK (notes->data[(size_t) (n2m::kMaxSteps + 40)].load() == 9.0f && notes->data[(size_t) (n2m::kLenRow + 40)].load() == 5.0f && notes->data[40].load() == 1.0f, "dragging a note up changes its pitch and keeps its length");
            click (*v, v->noteRect (40).getCentre(), true);
            CHECK (notes->data[(size_t) (n2m::kLenRow + 40)].load() == 0.0f && notes->data[40].load() == 1.0f, "a right-click puts it back on the Length knob");
            const bool couldUndo = r.history.canUndo();
            click (*v, v->noteRect (40).getCentre());
            CHECK (notes->data[40].load() == 0.0f && couldUndo, "a click on a note takes it away");
            ed->stepHistory (false);
            CHECK (notes->data[40].load() == 1.0f && notes->data[(size_t) (n2m::kMaxSteps + 40)].load() == 9.0f, "and Undo brings it back");
            // one drag on an empty place: a new note, as long as the drag
            drag (*v, v->cell (50, 0).getCentre(), v->cell (53, 0).getCentre().translated (v->cell (0, 0).getWidth() * 0.4f, 0.0f));
            CHECK (notes->data[50].load() == 1.0f && notes->data[(size_t) (n2m::kLenRow + 50)].load() == 4.0f, "pressing on an empty place and dragging right draws a long note");
            v->setBars (3);
            CHECK (v->bars() == 8 && notes->P (notes->paramIndex ("bars")) == 3.0f, "the pattern can be made eight bars long from here");
            v->setBars (2);
            // copy and paste a bar
            v->copyBarAt (1); CHECK (v->canPaste() && v->pasteBarAt (3), "a bar of notes can be copied and pasted");
            CHECK (notes->data[(size_t) (48 + 4)].load() == 1.0f && notes->data[(size_t) (n2m::kLenRow + 48 + 4)].load() == 6.0f, "with its lengths");
            ed->stepHistory (false);
            CHECK (notes->data[(size_t) (48 + 4)].load() == 0.0f, "Undo takes the pasted bar back out");
            // the unit underneath shows the same pattern, and the song plays on with the view open
            r.preview.store (true);
            for (int k = 0; k < 60; ++k) { juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer mm; r.processBlock (buf, mm); }
            big.tick();
            CHECK (big.isOpen() && notes->playStep.load() >= 0, "the song keeps playing while the view is open");
            (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f);
            r.preview.store (false);
            for (int k = 0; k < 8; ++k) { juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer mm; r.processBlock (buf, mm); }
        }
        // Escape, and a click outside the card, both close it
        CHECK (ed->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)) && ! big.isOpen(), "Escape closes the view");
        big.open (*r.rack.boards[3], *notes);
        big.mouseDown (event (big, { 3.0f, 3.0f }, { 3.0f, 3.0f }));
        CHECK (! big.isOpen(), "a click outside the card closes it");
        big.open (*r.rack.boards[3], *notes);
        big.mouseDown (event (big, big.card().getCentre().toFloat(), big.card().getCentre().toFloat()));
        CHECK (big.isOpen(), "a click on the card does not");
        big.close();

        // ---- 2. The drum grid
        ed->selectTab (0);
        auto* drums = unit (0, "drumseq");
        drums->set ("bars", 2);
        for (int b = 0; b < 8; ++b) n2m::setDrumBar (*drums, b, "x...x...x...x...");
        n2m::setDrumBar (*drums, 1, "X.x.x..xX..x.xXx");
        CHECK (big.open (*r.rack.boards[0], *drums) && big.as<n2m::BigDrumGrid>() != nullptr, "the drum sequencer opens a grid");
        if (auto* v = big.as<n2m::BigDrumGrid>())
        {
            CHECK (v->bars() == 4 && v->row (3).getBottom() <= v->getHeight() && v->tile (63).getRight() <= (float) v->getWidth() + 0.5f && ! v->row (0).intersects (v->row (1)), "all four bars are on show, one under another");
            v->toggleHit (33);
            CHECK (drums->data[33].load() == 1.0f, "a step can be given a hit");
            v->setVelocity (33, 0.4f);
            CHECK (std::abs (n2m::drumVel (drums->data[33].load()) - 0.4f) < 0.013f, "and the hit a strength");
            v->setVelocity (34, 0.9f);
            CHECK (drums->data[34].load() == 0.0f, "a step with no hit is left alone");
            ed->stepHistory (false);
            CHECK (drums->data[33].load() == 1.0f && big.isOpen(), "Undo puts the strength back");
            ed->stepHistory (false);
            CHECK (drums->data[33].load() == 0.0f, "and Undo again takes the hit away");
            // the mouse: click a step; drag in the bars underneath across three hits
            click (*v, v->tile (37).getCentre());
            CHECK (drums->data[37].load() == 1.0f, "a click on a step puts a hit there");
            click (*v, v->tile (37).getCentre());
            CHECK (drums->data[37].load() == 0.0f, "another click takes it away");
            const auto a = v->velLane (16), c = v->velLane (20);
            drag (*v, { a.getCentreX(), a.getY() + 2.0f }, { c.getCentreX(), c.getBottom() - 4.0f });
            CHECK (drums->data[16].load() == 2.0f && n2m::drumVel (drums->data[20].load()) < 0.2f, "dragging in the bars under the steps sets how hard each hit is: the top is an accent, the bottom very soft");
            CHECK (n2m::drumVel (drums->data[18].load()) < 1.0f && n2m::drumVel (drums->data[18].load()) > n2m::drumVel (drums->data[20].load()) && drums->data[17].load() == 0.0f, "each hit the drag passes over takes the strength at that point; empty steps stay empty");
            ed->stepHistory (false);
            CHECK (drums->data[16].load() == 2.0f && drums->data[18].load() == 1.0f && drums->data[20].load() == 1.0f, "the whole drag is one Undo step");
            // copy, paste and clear the picked bar
            v->selectBar (1); v->copyBarAt (v->selectedBar());
            v->selectBar (3);
            CHECK (v->selectedBar() == 3 && v->pasteBarAt (3), "a bar can be picked, copied and pasted over another");
            bool same = true; for (int i = 0; i < 16; ++i) same = same && drums->data[(size_t) (48 + i)].load() == drums->data[(size_t) (16 + i)].load();
            CHECK (same, "the pasted bar is the copied one");
            ed->stepHistory (false);
            CHECK (drums->data[(size_t) (48 + 2)].load() == 0.0f, "Undo takes the paste back");
            v->clearBarAt (0);
            bool empty = true; for (int i = 0; i < 16; ++i) empty = empty && drums->data[(size_t) i].load() == 0.0f;
            CHECK (empty && drums->data[16].load() == 2.0f, "Clear bar empties that bar only");
            ed->stepHistory (false);
            CHECK (drums->data[0].load() == 1.0f, "and can be undone");
            // a bar of drums can't be pasted into notes
            big.close(); ed->selectTab (3); big.open (*r.rack.boards[3], *notes);
            CHECK (big.as<n2m::BigNoteRoll>() != nullptr && ! big.as<n2m::BigNoteRoll>()->canPaste() && ! big.as<n2m::BigNoteRoll>()->pasteBarAt (0), "a bar copied from drums is not pasted into notes");
        }
        big.close();

        // saved with the project: the lengths and the strengths
        {
            drums->data[5].store (n2m::drumData (0.33f));
            juce::MemoryBlock saved; r.getStateInformation (saved);
            Nebula2ModulationProcessor q; q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
            q.setStateInformation (saved.getData(), (int) saved.getSize());
            n2m::Module* n2 = nullptr; n2m::Module* d2 = nullptr;
            for (auto& m : q.rack.boards[3]->modules) if (std::string (m->info->type) == "noteseq") n2 = m.get();
            for (auto& m : q.rack.boards[0]->modules) if (std::string (m->info->type) == "drumseq") d2 = m.get();
            bool same = n2 != nullptr && d2 != nullptr;
            for (int k = 0; same && k < n2m::kData; ++k) same = n2->data[(size_t) k].load() == notes->data[(size_t) k].load() && d2->data[(size_t) k].load() == drums->data[(size_t) k].load();
            CHECK (same && n2->data[(size_t) (n2m::kLenRow + 20)].load() == 6.0f && d2->data[5].load() == drums->data[5].load(), "what the bigger views change is saved with the project");
        }

        // ---- 3. The chords
        ed->selectTab (4);
        auto* chordUnit = unit (4, "chords");
        CHECK (big.open (*r.rack.boards[4], *chordUnit) && big.as<n2m::BigChords>() != nullptr, "the chord generator opens the chords view");
        if (auto* v = big.as<n2m::BigChords>())
        {
            auto& h = r.rack.harmony;
            const int n = (int) h.seq.size();
            bool inside = n > 0;
            for (int i = 0; i < n; ++i) inside = inside && v->getLocalBounds().contains (v->tile (i)) && (i == 0 || ! v->tile (i).intersects (v->tile (i - 1)));
            CHECK (inside, "every chord of the song has its own tile in view");
            v->select (1);
            CHECK (v->selected() == 1, "a chord can be picked");
            auto laidOut = [v]
            {
                const auto chips = v->chips();
                bool ok = ! chips.empty(); bool kinds[5] {};
                for (size_t i = 0; i < chips.size(); ++i)
                {
                    ok = ok && v->getLocalBounds().contains (chips[i].r) && chips[i].r.getY() > v->tile (v->selected()).getBottom() && chips[i].text.isNotEmpty();
                    for (size_t j = 0; j < i; ++j) ok = ok && ! chips[i].r.intersects (chips[j].r);
                    kinds[chips[i].kind] = true;
                }
                return ok && kinds[0] && kinds[1] && kinds[2] && kinds[3] && kinds[4];
            };
            CHECK (laidOut(), "its colour, bass note, change to, length and lock are laid out as buttons, none overlapping, all in view");
            const auto before = h.seq[1];
            const auto nameBefore = h.names[1];
            const int otherDeg = (before.deg + 3) % 7;
            v->setDegree (otherDeg);
            CHECK (h.seq[1].deg == otherDeg && h.names[1] != nameBefore, "Change to makes it another chord of the key");
            ed->stepHistory (false);
            CHECK (h.seq[1].deg == before.deg && h.names[1] == nameBefore && big.isOpen(), "Undo puts the chord back, with the view still open");
            v->setBars (4);
            CHECK (h.seq[1].bars == 4 && r.rack.timeline.slots[1].len == 64, "Length sets how long it lasts");
            ed->stepHistory (false);
            CHECK (h.seq[1].bars == before.bars, "(undone)");
            v->toggleLock();
            CHECK (h.seq[1].locked, "Lock locks it");
            ed->stepHistory (false);
            CHECK (! h.seq[1].locked, "(undone)");
            const auto colours = v->colourNames();
            CHECK (colours.size() > 1 && v->bassNames().size() >= 1 && v->degreeNames().size() == 7, "the choices are the ones the chord's menu has");
            int otherColour = -1;
            for (auto& c : v->chips()) if (c.kind == 0 && ! c.on && otherColour < 0) otherColour = c.value;
            v->setColour (otherColour);
            CHECK (h.names[1] != nameBefore && h.seq[1].deg == before.deg, "Colour changes the kind of chord, not which step of the key it is");
            ed->stepHistory (false);
            CHECK (h.names[1] == nameBefore, "(undone)");
            if (v->bassNames().size() > 1)
            {
                v->setBass (1);
                CHECK (h.seq[1].inv == 1, "Bass note puts another note of the chord at the bottom");
                ed->stepHistory (false);
                CHECK (h.seq[1].inv == before.inv, "(undone)");
            }
            // with the mouse: click a tile to pick it, click a button to change it
            click (*v, v->tile (2).getCentre().toFloat());
            CHECK (v->selected() == 2, "a click on a chord picks it");
            for (auto& c : v->chips()) if (c.kind == 3 && c.value == 1) { click (*v, c.r.getCentre().toFloat()); break; }
            CHECK (h.seq[2].bars == 1, "a click on a button changes the picked chord");
            ed->stepHistory (false);
            // the most there can be: 32 chords still fit
            r.rack.harmony.lenBars = 32; r.rack.harmony.chordBars = 1; r.rack.generateChords (11); r.chordsVersion.fetch_add (1);
            v->select (31);
            bool fit = h.seq.size() == 32;
            for (int i = 0; i < 32 && fit; ++i) fit = v->getLocalBounds().contains (v->tile (i)) && v->tile (i).getHeight() >= 48;
            CHECK (fit && laidOut(), "a song of 32 chords still fits, with the buttons under it");
            (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f);
        }
        big.close();

        // ---- 4 to 7, on the extra board
        ed->selectTab (extra);
        auto& eb = *r.rack.boards[(size_t) extra];
        const auto wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("n2m-test-bigview.wav");
        wav.deleteFile();
        {
            juce::AudioBuffer<float> buf (1, 24000);
            for (int i = 0; i < 24000; ++i) buf.setSample (0, i, 0.8f * std::sin (0.05f * (float) i) * (1.0f - (float) i / 24000.0f));
            juce::WavAudioFormat fmt;
            if (auto os = std::unique_ptr<juce::FileOutputStream> (wav.createOutputStream()))
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (fmt.createWriterFor (os.get(), 48000, 1, 16, {}, 0))) { os.release(); w->writeFromAudioSampleBuffer (buf, 0, 24000); }
        }
        auto* sampler = dynamic_cast<n2m::SampleModule*> (unit (extra, "sample"));
        CHECK (sampler != nullptr && sampler->load (wav).isEmpty() && sampler->fine.size() > sampler->peaks.size(), "a loaded sample keeps a fine picture of itself for zooming");
        CHECK (big.open (eb, *sampler) && big.as<n2m::BigSample>() != nullptr, "the sample player opens the wave view");
        if (auto* v = big.as<n2m::BigSample>())
        {
            v->setRegion (0.25f, 0.75f);
            CHECK (std::abs (sampler->P (0) - 0.25f) < 1.0e-4f && std::abs (v->regionEnd() - 0.75f) < 1.0e-3f, "Start and Length follow the part set");
            ed->stepHistory (false);
            CHECK (sampler->P (0) == 0.0f && sampler->P (1) == 1.0f && big.isOpen(), "Undo puts them back");
            v->setRegion (0.2f, 0.8f);
            // drag the end in, then the start in
            const float y = (float) v->well().getCentreY();
            drag (*v, { v->xFor (0.8f) - 2.0f, y }, { v->xFor (0.6f), y });
            CHECK (std::abs (v->regionEnd() - 0.6f) < 0.01f && std::abs (v->regionStart() - 0.2f) < 1.0e-4f, "dragging the end moves the end and leaves the start");
            drag (*v, { v->xFor (0.2f) + 2.0f, y }, { v->xFor (0.4f), y });
            CHECK (std::abs (v->regionStart() - 0.4f) < 0.01f && std::abs (v->regionEnd() - 0.6f) < 0.01f, "dragging the start moves the start and leaves the end");
            drag (*v, { v->xFor (0.4f) + 2.0f, y }, { v->xFor (0.95f), y });
            CHECK (v->regionStart() < v->regionEnd(), "the start can't be dragged past the end");
            v->zoomBy (4.0f, 0.5f);
            CHECK (std::abs ((v->viewEnd() - v->viewStart()) - 0.25f) < 1.0e-3f && v->viewStart() < 0.5f && v->viewEnd() > 0.5f, "zooming in shows a quarter of the file, around the same place");
            CHECK (std::abs (v->atFor (v->xFor (0.5f)) - 0.5f) < 1.0e-3f, "(places on screen and in the file agree when zoomed)");
            v->zoomBy (0.01f, 0.5f);
            CHECK (v->viewStart() == 0.0f && v->viewEnd() == 1.0f, "zooming out stops at the whole file");
            v->zoomBy (1000000.0f, 0.5f); v->showWhole();
            CHECK (v->viewStart() == 0.0f && v->viewEnd() == 1.0f, "Whole file shows it all again");
            sampler->playhead.store (0.5f); big.tick();
            (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f);
            sampler->playhead.store (-1.0f);
        }
        big.close();
        wav.deleteFile();

        auto* eq = unit (extra, "parameq");
        CHECK (big.open (eb, *eq) && big.as<n2m::BigEq>() != nullptr, "the parametric EQ opens the curve view");
        if (auto* v = big.as<n2m::BigEq>())
        {
            CHECK (std::abs (v->curveDb (0.3f)) < 1.0e-6f, "a flat EQ draws a flat line");
            v->setBand (1, 0.3f, 6.0f);
            CHECK (eq->P (3) == 0.3f && eq->P (4) == 6.0f, "a band's point sets its Freq and Gain");
            CHECK (std::abs (v->curveDb (0.3f) - 6.0f) < 0.3f && std::abs (v->curveDb (0.95f)) < 1.0f, "the curve drawn is the curve the unit makes: 6 dB up at the band, flat far away");
            ed->stepHistory (false);
            CHECK (eq->P (4) == 0.0f && big.isOpen(), "Undo puts the band back");
            v->setWidth (2, 4.0f);
            CHECK (eq->P (8) == 4.0f, "its width can be set");
            ed->stepHistory (false);
            const auto p0 = v->pointOf (0);
            const auto pl = v->plot().toFloat();
            drag (*v, p0, { pl.getX() + 0.6f * pl.getWidth(), pl.getCentreY() - 0.25f * pl.getHeight() });
            CHECK (std::abs (eq->P (0) - 0.6f) < 0.01f && std::abs (eq->P (1) - 0.5f * n2m::BigEq::topDb) < 0.3f, "dragging a point: across is where, up is how much");
            const float w0 = eq->P (2);
            drag (*v, v->pointOf (0), v->pointOf (0).translated (0.0f, -60.0f), true);
            CHECK (eq->P (2) > w0 && std::abs (eq->P (0) - 0.6f) < 0.01f, "a right-button drag up makes the band wider and leaves it where it is");
            juce::MouseWheelDetails wheel; wheel.deltaX = 0; wheel.deltaY = -0.3f; wheel.isReversed = false; wheel.isSmooth = false; wheel.isInertial = false;
            const float w1 = eq->P (2);
            v->mouseWheelMove (event (*v, v->pointOf (0), v->pointOf (0)), wheel);
            CHECK (eq->P (2) < w1, "the mouse wheel over a point changes its width");
            ed->stepHistory (false);
            CHECK (std::abs (eq->P (2) - w1) < 1.0e-5f, "(one Undo step)");
            (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f);
        }
        big.close();

        auto* mod = unit (extra, "stepmod");
        mod->set ("bars", 1);
        CHECK (big.open (eb, *mod) && big.as<n2m::BigStepMod>() != nullptr, "the step modulator opens the drawing view");
        if (auto* v = big.as<n2m::BigStepMod>())
        {
            CHECK (v->bars() == 2 && v->pointFor (31, 14).x < (float) v->grid().getRight(), "both bars are in view");
            v->drawLine (0, -7, 21, 14);
            bool ramp = true;
            for (int st = 0; st <= 21; ++st) ramp = ramp && mod->data[(size_t) st].load() == 1.0f && (int) mod->data[(size_t) (n2m::kMaxSteps + st)].load() == st - 7;
            CHECK (ramp, "one stroke sets every step it passes over");
            ed->stepHistory (false);
            CHECK (mod->data[0].load() == 0.0f && big.isOpen(), "and is one Undo step");
            // a fast stroke of the mouse, a few movements for many steps: none is missed
            drag (*v, v->pointFor (2, -7), v->pointFor (29, 14));
            bool drawn = true, rising = true;
            for (int st = 2; st <= 29; ++st) { drawn = drawn && mod->data[(size_t) st].load() == 1.0f; rising = rising && (st == 2 || mod->data[(size_t) (n2m::kMaxSteps + st)].load() >= mod->data[(size_t) (n2m::kMaxSteps + st - 1)].load()); }
            CHECK (drawn && rising && mod->data[(size_t) (n2m::kMaxSteps + 2)].load() == -7.0f && mod->data[(size_t) (n2m::kMaxSteps + 29)].load() == 14.0f, "dragging across the steps draws a shape like a pencil, with no step skipped");
            drag (*v, v->pointFor (10, 0), v->pointFor (12, 0), true);
            CHECK (mod->data[10].load() == 0.0f && mod->data[11].load() == 0.0f && mod->data[12].load() == 0.0f && mod->data[13].load() == 1.0f, "a right-drag switches steps off");
            v->setValue (11, 3);
            CHECK (mod->data[11].load() == 1.0f && mod->data[(size_t) (n2m::kMaxSteps + 11)].load() == 3.0f, "one step can be set on its own");
            v->setHold (11);
            CHECK (mod->data[11].load() == 0.0f, "and switched off again");
            (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f);
        }
        big.close();

        auto* scope = dynamic_cast<n2m::Scope*> (unit (extra, "scope"));
        CHECK (scope != nullptr && big.open (eb, *scope) && big.as<n2m::BigScope>() != nullptr, "the scope opens the large scope");
        if (auto* v = big.as<n2m::BigScope>())
        {
            for (int i = 0; i < n2m::Scope::size; ++i) scope->ring[(size_t) i] = 0.7f * std::sin (0.0575f * (float) i);
            scope->hz.store (220.0f); scope->level.store (0.7f);
            big.tick();
            v->setFrozen (true);
            CHECK (v->frozen(), "Freeze holds the picture");
            (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f);
            v->setFrozen (false);
        }

        // ---- Full screen: the view grows with the window
        {
            const auto normal = big.content()->getBounds();
            ed->setBigWindow (true);
            CHECK (big.getBounds() == ed->getLocalBounds() && ed->getLocalBounds().contains (big.card()) && big.card().reduced (10).contains (big.content()->getBounds()), "Full screen: the card still sits inside the window");
            CHECK (big.content()->getWidth() > normal.getWidth() + 250 && big.content()->getHeight() > normal.getHeight() + 80, "and the view takes the extra room");
            (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.25f);
            big.close();
            for (auto pick : { std::pair<int, const char*> { 3, "noteseq" }, { 0, "drumseq" }, { 4, "chords" }, { extra, "sample" }, { extra, "parameq" }, { extra, "stepmod" } })
            {
                ed->selectTab (pick.first);
                CHECK (big.open (*r.rack.boards[(size_t) pick.first], *unit (pick.first, pick.second)), "each view opens with Full screen on");
                CHECK (big.card().reduced (10).contains (big.content()->getBounds()) && big.content()->getWidth() > 1400, "and is laid out from the bigger window");
                if (auto* v = big.as<n2m::BigChords>()) { bool ok = true; for (auto& c : v->chips()) ok = ok && v->getLocalBounds().contains (c.r); for (int i = 0; i < 32; ++i) ok = ok && v->getLocalBounds().contains (v->tile (i)); CHECK (ok, "the chords and their buttons all fit"); }
                if (auto* v = big.as<n2m::BigDrumGrid>()) CHECK (v->row (v->bars() - 1).getBottom() <= v->getHeight() && v->tile (15).getRight() <= (float) v->getWidth() + 0.5f, "the drum grid fits");
                if (auto* v = big.as<n2m::BigNoteRoll>()) CHECK (v->getLocalBounds().contains (v->grid()), "the piano roll fits");
                (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.25f);
                big.close();
            }
            ed->setBigWindow (false);
        }

        // ---- The view closes itself when its unit goes
        // deleted from its right-click menu (this is what that menu item does)
        ed->selectTab (extra);
        big.open (eb, *mod);
        {
            const int uid = mod->uid;
            ed->board().willChange (uid); r.rack.removeModule (eb, uid); ed->board().rebuild();
            CHECK (! big.isOpen() && ! big.isVisible(), "deleting the unit closes its bigger view");
            ed->stepHistory (false);
            CHECK (unit (extra, "stepmod") != nullptr && ! big.isOpen(), "Undo brings the unit back (its view stays closed)");
        }
        // gone by any other road: nothing is drawn from it, and the next tick closes it
        mod = unit (extra, "stepmod");
        big.open (eb, *mod);
        auto* stale = big.content();
        r.rack.removeModule (eb, mod->uid);
        CHECK (big.isOpen() && ! stale->alive(), "a unit taken away behind the view's back is noticed");
        (void) big.createComponentSnapshot (big.getLocalBounds(), true, 0.5f);       // (the panel alone: the board under it is drawn again by whoever took the unit away)
        if (auto* v = big.as<n2m::BigStepMod>()) { v->drawLine (0, 0, 5, 5); v->setBars (3); v->pasteBarAt (0); click (*v, { 300.0f, 300.0f }); }
        big.tick();
        CHECK (! big.isOpen(), "and the view closes itself without touching it");
        ed->selectTab (extra);
        // Undo of the unit's own creation
        ed->addUnits ({ "stepmod" });
        mod = unit (extra, "stepmod");
        CHECK (mod != nullptr && big.open (eb, *mod), "a unit just added opens its view");
        ed->stepHistory (false);
        CHECK (unit (extra, "stepmod") == nullptr && ! big.isOpen(), "undoing the unit's creation closes its view");
        // its tab deleted
        big.open (eb, *eq);
        CHECK (big.isOpen(), "(open on the extra tab)");
        r.history.tabRemoving (extra); ed->reshow (-1); r.rack.removeBoard (extra); ed->reshow (0);
        CHECK (! big.isOpen(), "deleting the tab closes the view of a unit on it");
        ed->stepHistory (false);
        CHECK ((int) r.rack.boards.size() == extra + 1 && ! big.isOpen(), "(the tab comes back with Undo)");
        // Change sound
        ed->selectTab (0);
        drums = unit (0, "drumseq");
        big.open (*r.rack.boards[0], *drums);
        ed->reshow (-1);
        CHECK (r.changeToPreset (0, "kick", "Deep"), "(the kick changes to another kick)");
        ed->reshow (0);
        CHECK (! big.isOpen(), "Change sound closes the view: its unit was replaced");
        // Make track
        drums = unit (0, "drumseq");
        big.open (*r.rack.boards[0], *drums);
        ed->styles().onMake ("techno");
        CHECK (! big.isOpen(), "Make track closes it");
        // a project load
        ed->selectTab (0);
        drums = unit (0, "drumseq");
        big.open (*r.rack.boards[0], *drums);
        CHECK (big.isOpen(), "(open before the load)");
        {
            juce::MemoryBlock saved; r.getStateInformation (saved);
            r.setStateInformation (saved.getData(), (int) saved.getSize());
            (void) big.createComponentSnapshot (big.getLocalBounds(), true, 0.5f);       // the panel drawn before the screen has caught up: must be safe
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
            CHECK (! big.isOpen() && ! big.isVisible(), "loading a project closes it");
        }
    }

    {
        // The Inside mixer: the desk showing the levels and mixes inside the selected sound
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        auto& desk = ed->desk();
        ed->selectTab (0);
        auto& kick = *r.rack.boards[0];
        auto event = [&desk] (juce::Point<int> at) { return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at.toFloat(), juce::ModifierKeys::leftButtonModifier, 0, 0, 0, 0, 0, &desk, &desk, juce::Time::getCurrentTime(), at.toFloat(), juce::Time::getCurrentTime(), 1, false); };
        auto picture = [&desk] { return desk.createComponentSnapshot (desk.getLocalBounds(), true, 1.0f); };
        auto differ = [] (const juce::Image& a, const juce::Image& b)
        {
            if (a.getBounds() != b.getBounds()) return true;
            for (int y = 0; y < a.getHeight(); y += 2) for (int x = 0; x < a.getWidth(); x += 2) if (a.getPixelAt (x, y) != b.getPixelAt (x, y)) return true;
            return false;
        };
        auto sameList = [] (const std::vector<n2m::InsideStrip>& a, const std::vector<n2m::InsideStrip>& b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) if (a[i].uid != b[i].uid || a[i].param != b[i].param || a[i].unit != b[i].unit || a[i].knob != b[i].knob) return false;
            return true;
        };
        auto stripOf = [&desk] (const char* type, const char* knob) -> int
        {
            const auto list = desk.insideList();
            auto* b = desk.boardFor()->board;
            for (int i = 0; i < (int) list.size(); ++i)
                if (auto* m = b->find (list[(size_t) i].uid))
                    if (std::string (m->info->type) == type && std::string (m->info->params[(size_t) list[(size_t) i].param].id) == knob) return i;
            return -1;
        };
        // what a hand on fader i does: press, drag to a position (0 bottom .. 1 top), let go
        auto dragFader = [&] (int i, float from, float to)
        {
            const auto f = n2m::MixerStrip::insideFader (desk.insideStrip (i));
            auto at = [f] (float n) { return juce::Point<int> (f.getCentreX() - 6, f.getY() + juce::roundToInt ((1.0f - n) * (float) f.getHeight())); };
            desk.mouseDown (event (at (from))); desk.mouseDrag (event (at (to))); desk.mouseUp (event (at (to)));
        };
        auto loudness = [&]
        {
            for (auto& b : r.rack.boards) b->mute.store (true);
            r.rack.boards[0]->mute.store (false);
            song.playing = true; song.ppq = 0;
            const float pk = run (r, song, 1.0);
            song.playing = false; run (r, song, 0.3);
            return pk;
        };

        CHECK (! r.mixerInside.load(), "the desk starts on Sounds");
        const auto soundsPicture = picture();
        const int deskH = desk.getHeight();
        desk.setInside (true);
        CHECK (r.mixerInside.load() && desk.getHeight() == deskH && differ (soundsPicture, picture()), "Inside changes what the desk shows, not its size");
        CHECK (desk.insideHeading() == "Inside: Kick", "the heading says which sound");
        CHECK (! desk.insideList().empty() && sameList (desk.insideList(), n2m::insideStrips (kick)), "Inside lists the strips for the current tab");
        const int oscStrip = stripOf ("osc", "level"), outStrip = stripOf ("output", "level"), vcaStrip = stripOf ("vca", "level");
        CHECK (oscStrip >= 0 && outStrip >= 0 && vcaStrip >= 0, "(the kick has an oscillator, a VCA and an Output to mix)");
        const auto oscS = desk.insideList()[(size_t) oscStrip], outS = desk.insideList()[(size_t) outStrip], vcaS = desk.insideList()[(size_t) vcaStrip];
        auto* osc = kick.find (oscS.uid); auto* out = kick.find (outS.uid); auto* vca = kick.find (vcaS.uid);
        const auto& outParam = out->info->params[(size_t) outS.param];

        // a fader is the knob: same position, same range
        const float outBefore = out->P (outS.param);
        CHECK (std::abs (desk.insideNorm (outS) - n2m::paramToNorm (outParam, outBefore)) < 1.0e-5f, "a fader sits where its knob is");
        const float loud = loudness();
        const std::string saveBefore = r.rack.save();
        const int stepsBefore = r.history.canUndo() ? 1 : 0;
        dragFader (outStrip, n2m::paramToNorm (outParam, outBefore), 0.25f);
        CHECK (std::abs (out->P (outS.param) - n2m::paramFromNorm (outParam, 0.25f)) < 0.02f && out->P (outS.param) < outBefore - 0.1f, "moving a fader turns the knob");
        CHECK (std::abs (desk.insideNorm (outS) - 0.25f) < 0.02f, "...and the fader shows where it was put");
        const float quiet = loudness();
        CHECK (loud > 0.05f && quiet < loud * 0.6f && quiet > 0.001f, "the sound's level follows the fader");
        CHECK (r.history.canUndo() && stepsBefore == 0, "a fader move is one step for Undo");
        const auto lowPicture = picture();
        ed->stepHistory (false);
        CHECK (std::abs (out->P (outS.param) - outBefore) < 1.0e-5f && r.rack.save() == saveBefore, "Undo puts the knob back");
        CHECK (std::abs (desk.insideNorm (outS) - n2m::paramToNorm (outParam, outBefore)) < 1.0e-5f && differ (lowPicture, picture()), "...and the fader with it");
        CHECK (! r.history.canUndo(), "(one step, now undone)");
        ed->stepHistory (true);
        CHECK (std::abs (out->P (outS.param) - n2m::paramFromNorm (outParam, 0.25f)) < 0.02f, "Redo moves it again");
        ed->stepHistory (false);
        // a press that moves nothing leaves no step
        {
            const auto f = n2m::MixerStrip::insideFader (desk.insideStrip (outStrip));
            const juce::Point<int> where (f.getCentreX() - 6, f.getY() + juce::roundToInt ((1.0f - desk.insideNorm (outS)) * (float) f.getHeight()));
            const float was = out->P (outS.param);
            desk.mouseDown (event (where)); desk.mouseUp (event (where));
            CHECK (std::abs (out->P (outS.param) - was) < 0.02f, "(a press on the handle hardly moves it)");
            out->params[(size_t) outS.param].store (was);
        }
        // only that knob moves
        {
            const float oscWas = osc->P (oscS.param), vcaWas = vca->P (vcaS.param), levelWas = kick.level.load();
            dragFader (outStrip, 0.5f, 0.9f);
            CHECK (osc->P (oscS.param) == oscWas && vca->P (vcaS.param) == vcaWas && kick.level.load() == levelWas, "one fader moves one knob: no other knob, and not the sound's own channel");
            ed->stepHistory (false);
        }

        // the knob turned on the board: the fader follows
        {
            ed->desk().tick();
            const auto before = picture();
            auto* comp = ed->board().compFor (oscS.uid);
            auto* cell = comp != nullptr ? comp->cellFor (oscS.param) : nullptr;
            CHECK (cell != nullptr, "(the oscillator's Level knob is on the board)");
            if (cell != nullptr)
            {
                cell->setNorm (0.15f);
                CHECK (std::abs (desk.insideNorm (oscS) - 0.15f) < 1.0e-4f && differ (before, picture()), "turning the knob on the board moves the fader");
                // ...and by the timeline or anything else that sets the knob without the screen knowing
                const auto mid = picture();
                osc->params[(size_t) oscS.param].store (n2m::paramFromNorm (osc->info->params[(size_t) oscS.param], 0.9f));
                juce::MessageManager::getInstance()->runDispatchLoopUntil (120);         // the screen's own timer notices
                CHECK (std::abs (desk.insideNorm (oscS) - 0.9f) < 1.0e-4f && differ (mid, picture()), "a knob moved from elsewhere moves the fader too");
            }
        }
        // linked knobs follow a fader as they follow a knob
        {
            osc->paramLink[(size_t) oscS.param] = 2; vca->paramLink[(size_t) vcaS.param] = 2;
            dragFader (oscStrip, 0.5f, 0.3f);
            CHECK (std::abs (desk.insideNorm (oscS) - 0.3f) < 0.02f && std::abs (desk.insideNorm (vcaS) - desk.insideNorm (oscS)) < 1.0e-4f, "a linked knob follows the fader");
            ed->stepHistory (false);
            osc->paramLink[(size_t) oscS.param] = 0; vca->paramLink[(size_t) vcaS.param] = 0;
        }
        // a double-click puts a knob back where it starts
        {
            dragFader (outStrip, 0.5f, 0.1f);
            const auto f = n2m::MixerStrip::insideFader (desk.insideStrip (outStrip));
            desk.mouseDoubleClick (event (f.getCentre()));
            CHECK (std::abs (out->P (outS.param) - outParam.def) < 1.0e-5f, "a double-click on a fader puts its knob back where it starts");
        }

        // More and Less still work: taller just gives longer faders
        {
            const int shortFader = n2m::MixerStrip::insideFader (desk.insideStrip (0)).getHeight();
            desk.setTall (true);
            const int longFader = n2m::MixerStrip::insideFader (desk.insideStrip (0)).getHeight();
            CHECK (desk.getHeight() == n2m::MixerStrip::heightFor (true) && longFader > shortFader + 60, "MORE gives longer faders");
            dragFader (outStrip, 0.5f, 0.7f);
            CHECK (std::abs (desk.insideNorm (outS) - 0.7f) < 0.01f, "...that still move their knobs");
            ed->stepHistory (false);
            (void) picture();
            desk.setTall (false);
            CHECK (n2m::MixerStrip::insideFader (desk.insideStrip (0)).getHeight() == shortFader, "LESS puts them back");
        }

        // the name shows the unit on the board
        {
            const auto nameAt = n2m::MixerStrip::insideName (desk.insideStrip (outStrip)).getCentre();
            CHECK (! ed->board().flashing (outS.uid), "(nothing outlined yet)");
            desk.mouseDown (event (nameAt)); desk.mouseUp (event (nameAt));
            auto* comp = ed->board().compFor (outS.uid);
            auto* holderComp = ed->board().getParentComponent();
            CHECK (ed->board().flashing (outS.uid) && comp != nullptr && ed->viewArea().contains (holderComp->getLocalArea (comp, comp->getLocalBounds())), "clicking a strip's name brings its unit into view and outlines it");
            CHECK (std::abs (out->P (outS.param) - outParam.def) < 1.0e-5f, "...and changes nothing");
        }

        // a unit added: its strip appears, dimmed until it can be heard; a unit removed: its strip goes
        {
            const size_t had = desk.insideList().size();
            ed->addUnits ({ "noise", "hall" });
            CHECK (desk.insideList().size() == had + 2 && stripOf ("noise", "level") >= 0 && stripOf ("hall", "mix") >= 0, "adding units adds their strips");
            (void) picture();
            const auto noiseS = desk.insideList()[(size_t) stripOf ("noise", "level")];
            CHECK (! kick.find (noiseS.uid)->live, "(not cabled: it can't be heard)");
            // press its fader, then take the unit away under the hand
            const auto f = n2m::MixerStrip::insideFader (desk.insideStrip (stripOf ("noise", "level")));
            desk.mouseDown (event (f.getCentre()));
            ed->board().willChange (noiseS.uid);
            r.rack.removeModule (kick, noiseS.uid);
            ed->board().rebuild();
            const std::string after = r.rack.save();
            desk.mouseDrag (event (f.getCentre().translated (0, -20)));
            desk.mouseDrag (event (f.getCentre().translated (0, 10)));
            desk.mouseUp (event (f.getCentre()));
            (void) picture(); desk.tick();
            CHECK (desk.insideList().size() == had + 1 && stripOf ("noise", "level") < 0 && desk.insideNorm (noiseS) < 0.0f, "removing a unit removes its strip");
            CHECK (r.rack.save() == after, "...and a hand still on that fader moves nothing");
            CHECK (! desk.moveInside (noiseS, 0.5f) && r.rack.save() == after, "a strip whose unit has gone can't be moved");
            ed->stepHistory (false);
            CHECK (stripOf ("noise", "level") >= 0, "Undo brings the unit and its strip back");
            for (int k = 0; k < 2 && desk.insideList().size() != had; ++k) ed->stepHistory (false);       // (the press on the fader, then adding the units)
            CHECK (desk.insideList().size() == had && sameList (desk.insideList(), n2m::insideStrips (kick)), "...and going further back takes the added units away again");
            (void) picture();
        }

        // another tab: another list
        ed->selectTab (1);
        CHECK (desk.insideHeading() == "Inside: " + juce::String (r.rack.boards[1]->name) && sameList (desk.insideList(), n2m::insideStrips (*r.rack.boards[1])), "changing tab swaps the list");
        (void) picture();
        // Change sound
        {
            ed->reshow (-1);
            (void) picture();                                   // drawn while the boards are let go: must be safe
            CHECK (! desk.moveInside (desk.insideList()[0], 0.5f), "nothing moves while the board is let go");
            CHECK (r.changeToPreset (1, "kick", "Deep"), "(the snare becomes a kick)");
            ed->reshow (1);
            CHECK (sameList (desk.insideList(), n2m::insideStrips (*r.rack.boards[1])) && stripOf ("osc", "level") >= 0, "Change sound makes the list again");
            const auto s = desk.insideList()[(size_t) stripOf ("osc", "level")];
            CHECK (desk.moveInside (s, 0.4f) && std::abs (desk.insideNorm (s) - 0.4f) < 1.0e-4f, "...and its faders work");
            (void) picture();
        }
        // on the All view it is the sound last selected
        {
            ed->showAllSounds();
            ed->reshow (3);
            auto& bass = *r.rack.boards[3];
            CHECK (desk.insideHeading() == "Inside: " + juce::String (bass.name) && sameList (desk.insideList(), n2m::insideStrips (bass)), "on the All view, Inside shows the sound last selected");
            const auto list = desk.insideList();
            CHECK (! list.empty() && desk.boardFor() != nullptr && desk.boardFor()->board == &bass, "(that sound's board is on screen)");
            const auto s = list.back();
            const float was = desk.insideNorm (s);
            dragFader ((int) list.size() - 1, 0.5f, was > 0.5f ? 0.2f : 0.8f);
            CHECK (std::abs (desk.insideNorm (s) - (was > 0.5f ? 0.2f : 0.8f)) < 0.02f, "its faders move its knobs there too");
            ed->stepHistory (false);
            CHECK (std::abs (desk.insideNorm (s) - was) < 1.0e-5f, "...and Undo puts them back");
            (void) picture();
            ed->selectTab (0);
        }
        // more strips than fit: the wheel moves along them
        {
            std::vector<std::string> many (14, "noise");
            ed->addUnits (many);
            const int last = (int) desk.insideList().size() - 1;
            const int edge = desk.getWidth() - 12 - 112 - 10;            // where the master's part starts
            CHECK (desk.insideStrip (last).getRight() > edge, "(more strips than fit)");
            const int x0 = desk.insideStrip (0).getX();
            juce::MouseWheelDetails w; w.deltaX = 0; w.deltaY = -1.0f; w.isReversed = false; w.isSmooth = false; w.isInertial = false;
            for (int k = 0; k < 12; ++k) desk.mouseWheelMove (event ({ 300, 60 }), w);
            CHECK (desk.insideStrip (0).getX() < x0 && desk.insideStrip (last).getRight() <= edge && desk.insideStrip (last).getRight() > edge - 40, "the wheel scrolls to the last strip and stops there");
            (void) picture();
            dragFader (last, 0.5f, 0.2f);
            CHECK (std::abs (desk.insideNorm (desk.insideList()[(size_t) last]) - 0.2f) < 0.02f, "a scrolled strip's fader is where it is drawn");
            ed->stepHistory (false); ed->stepHistory (false);
            (void) picture();
            CHECK (desk.insideStrip (0).getX() == x0, "with fewer strips again it scrolls back");
        }
        // a sound with nothing to mix
        {
            r.rack.addBoard (std::make_unique<n2m::Board>());
            const int t = (int) r.rack.boards.size() - 1;
            ed->reshow (t);
            CHECK (desk.insideList().empty() && desk.insideHeading().startsWith ("Inside: "), "a sound with nothing to mix has no strips");
            (void) picture();
            dragFader (0, 0.5f, 0.2f);
            ed->selectTab (0);
        }
        // the master is where it was, and still works
        {
            const float was = r.rack.master.load();
            const juce::Point<int> at (desk.getWidth() - 12 - 112 + 20, 70);
            desk.mouseDown (event (at)); desk.mouseDrag (event (at.translated (0, 20))); desk.mouseUp (event (at.translated (0, 20)));
            CHECK (r.rack.master.load() != was, "the master fader is the same in both");
            r.rack.master.store (was);
        }
        // remembered with the project
        juce::MemoryBlock saved; r.getStateInformation (saved);
        Nebula2ModulationProcessor q; q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
        CHECK (! q.mixerInside.load(), "(a new project is on Sounds)");
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        CHECK (q.mixerInside.load(), "Inside is remembered with the project");
        desk.setInside (false);
        CHECK (! r.mixerInside.load() && ! differ (soundsPicture.getClippedImage ({ 0, 0, 80, 40 }), picture().getClippedImage ({ 0, 0, 80, 40 })), "Sounds puts the desk back as it was");
        juce::MemoryBlock back; r.getStateInformation (back);
        q.setStateInformation (back.getData(), (int) back.getSize());
        CHECK (! q.mixerInside.load(), "...and so is Sounds");
        q.mixerInside.store (true);
        juce::MemoryBlock old;
        {
            // a project saved before the Inside mixer: everything up to the window size, and no more
            juce::MemoryOutputStream o (old, false);
            o.writeString (juce::String (r.rack.save())); o.writeInt (0); o.writeFloat (1.0f); o.writeBool (true); o.writeBool (false); o.writeFloat (0.0f); o.writeBool (false); o.writeBool (false);
        }
        q.setStateInformation (old.getData(), (int) old.getSize());
        CHECK (! q.mixerInside.load() && q.mixerTall.load(), "a project from before this loads on Sounds");
        // a project loaded while Inside is up: the list is the new project's
        desk.setInside (true);
        r.setStateInformation (saved.getData(), (int) saved.getSize());
        (void) picture();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        CHECK (r.mixerInside.load() && sameList (desk.insideList(), n2m::insideStrips (*r.rack.boards[(size_t) r.selectedTab.load()])), "after a project load the list is the loaded project's");
        (void) picture();
    }

    {
        // A sound made late by a plugin in a slot: the host is told, and a rendered tab still starts on bar 1.
        // (A stand-in unit that says it is late and is: no plugin here reports any lateness.)
        struct Lag : n2m::Module
        {
            static const n2m::Info& desc() { static const n2m::Info i { "testlag", "Lag", "Utilities", "test only", { { "in", "In" } }, { { "out", "Out" } }, { { "samples", "Samples", 0, 4000, 0, n2m::Fmt::Int } } }; return i; }
            int latency() const override { return (int) P (0); }
            void process (int n, const n2m::Ctx&) override
            {
                const int d = (int) P (0);
                for (int i = 0; i < n; ++i) { ring[(size_t) at] = in[0][(size_t) i]; out[0][(size_t) i] = ring[(size_t) ((at - d) & 8191)]; at = (at + 1) & 8191; }
            }
            std::array<float, 8192> ring {}; int at = 0;
        };
        n2m::extraTypes().push_back ({ &Lag::desc(), [] { return std::unique_ptr<n2m::Module> (new Lag()); } });
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        while (r.rack.boards.size() > 0) r.rack.removeBoard (0);
        r.rack.addBoard (n2m::makeBoard ("bass", 48000)); r.rack.addBoard (n2m::makeBoard ("hat", 48000));
        auto& b = *r.rack.boards[0];
        for (auto& c : std::vector<n2m::Cable> (b.cables)) if (c.dst == b.outputs[0]->uid && c.dstPort == 1) r.rack.disconnect (b, c.src, c.srcPort, c.dst, c.dstPort);
        const auto plainWav = r.renderTab (0, 2);
        juce::MemoryBlock plainBytes; plainWav.loadFileAsData (plainBytes);
        song.playing = true; run (r, song, 0.2); r.syncLatency();
        CHECK (r.getLatencySamples() == 0, "a project with nothing late tells the host no lateness");
        auto* lag = r.rack.addModule (b, "testlag", 0, 0);
        for (auto& c : std::vector<n2m::Cable> (b.cables)) if (c.dst == b.outputs[0]->uid) { r.rack.disconnect (b, c.src, c.srcPort, c.dst, c.dstPort); r.rack.connect (b, c.src, c.srcPort, lag->uid, 0); }
        r.rack.connect (b, lag->uid, 0, b.outputs[0]->uid, 0);
        lag->set ("samples", 480);
        run (r, song, 0.2); r.syncLatency();
        CHECK (r.getLatencySamples() == 480, "a late unit: the host is told how late, so Cubase can line the track up");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        const auto lateWav = r.renderTab (0, 2);
        juce::MemoryBlock lateBytes; lateWav.loadFileAsData (lateBytes);
        CHECK (plainBytes.getSize() > 100000 && lateBytes == plainBytes, "a rendered tab leaves the wait off the front: it still starts on bar 1");
        lag->set ("samples", 0);
        run (r, song, 0.2);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (600);          // the processor's own timer tells the host
        CHECK (r.getLatencySamples() == 0, "and when nothing is late any more the host is told that too, without being asked");
        song.playing = false;
        n2m::extraTypes().pop_back();
    }

    {
        // Freeze a tab, through the screen
        Nebula2ModulationProcessor r;
        FakeCubase song; r.setPlayHead (&song);
        r.setRateAndBufferSizeDetails (48000, 256);
        r.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (r.createEditor());
        auto* ed = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        const int tab = 3;
        ed->selectTab (tab);
        auto& b = *r.rack.boards[(size_t) tab];
        auto alone = [&] (double seconds, std::vector<float>* keep = nullptr)
        {
            for (auto& o : r.rack.boards) o->solo.store (o.get() == &b);
            song.playing = true; song.ppq = 0;
            const float v = run (r, song, seconds, keep);
            song.playing = false; run (r, song, 2.0);
            for (auto& o : r.rack.boards) o->solo.store (false);
            return v;
        };
        const float liveLevel = alone (4.0);
        const float liveLoad = b.load.load();
        const auto textBefore = n2m::Rack::boardText (b);
        r.history.clear();
        CHECK (! ed->board().locked(), "(a board starts unlocked)");
        ed->tabBar().toggleFreeze (tab);
        ed->board().tick();
        CHECK (b.frozen.load() && b.iced() && r.problem.isEmpty(), "Freeze in the tab's menu freezes the sound and makes its audio");
        CHECK (ed->board().locked(), "a frozen sound's board is locked");
        auto* unitComp = ed->board().compFor (b.modules[0]->uid);
        CHECK (unitComp != nullptr && ed->board().getComponentAt (unitComp->getBounds().getCentre()) != unitComp && dynamic_cast<n2m::ModuleComp*> (ed->board().getComponentAt (unitComp->getBounds().getCentre())) == nullptr, "a click on a frozen board reaches none of its units");
        CHECK (! ed->board().isInterestedInFileDrag ({ "kick.wav" }), "and nothing can be dropped on it");
        const size_t unitsBefore = b.modules.size();
        ed->addUnits ({ "hall" });
        CHECK (b.modules.size() == unitsBefore, "and no unit can be added to it");
        std::vector<float> frozenAudio;
        const float frozenLevel = alone (4.0, &frozenAudio);
        std::printf ("freeze: level live %.3f frozen %.3f, load live %.4f frozen %.4f\n", (double) liveLevel, (double) frozenLevel, (double) liveLoad, (double) b.load.load());
        CHECK (liveLevel > 0.05f && std::abs (frozenLevel - liveLevel) < 0.25f * liveLevel, "frozen, it sounds as loud as it did");
        // the mixer still works
        b.level.store (b.level.load() * 0.5f);
        const float halfLevel = alone (4.0);
        CHECK (std::abs (halfLevel - 0.5f * frozenLevel) < 0.02f, "the mixer's fader still works on a frozen sound");
        b.level.store (b.level.load() * 2.0f);
        b.mute.store (true);
        song.playing = true; song.ppq = 0; for (auto& o : r.rack.boards) o->mute.store (o.get() == &b ? true : true);
        CHECK (run (r, song, 1.0) == 0.0f, "and mute");
        for (auto& o : r.rack.boards) o->mute.store (false);
        song.playing = false; run (r, song, 2.0);
        // tempo: the old audio goes at once, and new audio is made when the tempo has settled
        r.refreshFrozen();
        CHECK (b.iced(), "(nothing has changed: the audio is kept)");
        song.bpm = 140; song.playing = true; run (r, song, 0.1); song.playing = false;
        r.refreshFrozen();
        CHECK (b.frozen.load() && ! b.iced(), "a change of tempo: the out-of-date audio is dropped at once, and the units play meanwhile");
        r.refreshFrozen();
        CHECK (b.iced() && std::abs (b.ice->perQ - 60.0 / 140.0 * 48000.0) < 1.0e-6, "and it is made again at the new tempo once the tempo has settled");
        r.history.beforeChords(); r.rack.setKey ((r.rack.root.load() + 2) % 12, r.rack.scale.load());
        r.refreshFrozen(); r.refreshFrozen();
        CHECK (b.iced() && b.ice->made == r.rack.freezeState (b, 140.0), "a change of key makes it again too");
        song.bpm = 124; song.playing = true; run (r, song, 0.1); song.playing = false; run (r, song, 2.0);
        r.refreshFrozen (true);
        // saved with the project
        juce::MemoryBlock saved; r.getStateInformation (saved);
        Nebula2ModulationProcessor q;
        FakeCubase song2; q.setPlayHead (&song2);
        q.setRateAndBufferSizeDetails (48000, 256); q.prepareToPlay (48000, 256);
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        auto& qb = *q.rack.boards[(size_t) tab];
        CHECK (qb.frozen.load() && ! qb.iced(), "a frozen sound is saved frozen; its audio is made again after loading");
        q.refreshFrozen(); q.refreshFrozen();
        CHECK (qb.iced(), "(made again)");
        for (auto& o : q.rack.boards) o->solo.store (o.get() == &qb);
        song2.playing = true;
        CHECK (run (q, song2, 2.0) > 0.05f, "and it plays");
        // Unfreeze, and Undo
        ed->tabBar().toggleFreeze (tab);
        ed->board().tick();
        CHECK (! b.frozen.load() && ! b.iced() && ! ed->board().locked(), "Unfreeze brings the board back, unlocked");
        CHECK (n2m::Rack::boardText (b) == textBefore, "exactly as it was");
        ed->stepHistory (false);
        CHECK (b.frozen.load() && ed->board().locked(), "Undo of Unfreeze freezes it again");
        r.refreshFrozen(); r.refreshFrozen();
        CHECK (b.iced(), "(and its audio is made again)");
        ed->stepHistory (true);
        r.refreshFrozen();
        CHECK (! b.frozen.load() && ! b.iced() && ! ed->board().locked(), "Redo unfreezes");
        // a sound that can't be frozen says why
        r.rack.addModule (b, "send", 0, 0);
        CHECK (! r.freezeTab (tab) && ! b.frozen.load() && r.problem.startsWith (juce::String (b.name) + " can't be frozen") && r.problem.contains ("Send"), "a sound that feeds a shared effect can't be frozen, and it says why");
        // a loaded sound arrives unfrozen
        r.freezeTab (0);
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("n2m-frozen.n2msound");
        CHECK (r.saveSound (0, file), "(saves)");
        const int loaded = r.loadSound (file);
        CHECK (loaded >= 0 && ! r.rack.boards[(size_t) loaded]->frozen.load(), "a saved sound loads unfrozen");
        file.deleteFile();
        if (argc > 2)
        {
            // a sound that is a plugin in a slot freezes too
            CHECK (r.changeToPlugin (1, "Nebula2 Chord", argv[2]), "(a plugin as the sound)");
            auto& pb = *r.rack.boards[1];
            CHECK (r.freezeTab (1) && pb.iced(), "a sound made by a plugin in a slot can be frozen");
            for (auto& o : r.rack.boards) o->solo.store (o.get() == &pb);
            song.playing = true; song.ppq = 0;
            CHECK (run (r, song, 3.0) > 0.01f, "and its kept audio plays");
            song.playing = false;
        }
    }

    if (failures == 0) std::printf ("All passed\n");
    return failures == 0 ? 0 : 1;
}
