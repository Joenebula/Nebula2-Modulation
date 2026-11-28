// Saves PNGs of the plugin window for checking the layout
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>

static void shoot (juce::AudioProcessorEditor& ed, const juce::String& path)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, (float) juce::SystemStats::getEnvironmentVariable ("SNAP_SCALE", "1.5").getDoubleValue());
    juce::File f (path); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
}

static void run (Nebula2ModulationProcessor& p, double seconds)
{
    for (int b = 0; b < (int) (48000 * seconds / 256); ++b) { juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer m; p.processBlock (buf, m); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String dir (argv[1]);
    Nebula2ModulationProcessor p;
    p.setRateAndBufferSizeDetails (48000, 256);
    p.prepareToPlay (48000, 256);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* e = dynamic_cast<Nebula2ModulationEditor*> (ed.get());
    shoot (*ed, dir + "/ui_all.png");                   // first open: every sound on one board
    e->selectTab (0);
    shoot (*ed, dir + "/ui.png");                       // the kick on its own tab

    p.preview.store (true); run (p, 0.7);
    shoot (*ed, dir + "/ui_playing.png");               // playing: step lights and meters
    p.preview.store (false); run (p, 0.1);

    p.rack.boards[0]->out.store (1); p.rack.boards[3]->out.store (2);   // kick to Out 2, bass to Out 3 (neither switched on here, so they show orange)
    e->selectTab (1); shoot (*ed, dir + "/ui_snare.png");
    p.rack.boards[0]->out.store (0); p.rack.boards[3]->out.store (0);
    e->selectTab (3); shoot (*ed, dir + "/ui_bass.png");

    e->selectTab (4);
    p.rack.toggleChordLock (2); p.rack.toggleChordLock (5);
    p.preview.store (true); run (p, 5.0);
    shoot (*ed, dir + "/ui_pad.png");                   // the chord unit, with the playing chord lit and two chords locked
    p.rack.toggleChordLock (2); p.rack.toggleChordLock (5);
    p.preview.store (false); run (p, 0.1);

    // links: the bass pattern in group A, sharing rhythm and notes; two knobs in group B
    for (auto& m : p.rack.boards[3]->modules)
    {
        if (m->info->seq == n2m::Seq::Note) p.rack.linkPattern (*m, 1, 3);
        if (std::string (m->info->type) == "filter") p.rack.linkParam (*m, m->paramIndex ("cutoff"), 2);
        if (std::string (m->info->type) == "drive") p.rack.linkParam (*m, m->paramIndex ("drive"), 2);
    }
    // a four-bar bass pattern, showing bar 2
    for (auto& m : p.rack.boards[3]->modules) if (m->info->seq == n2m::Seq::Note) { m->set ("bars", 2); n2m::randomNotes ("bass", 4242, *m); }
    e->selectTab (3); shoot (*ed, dir + "/ui_bass4.png");

    p.rack.addBoard (n2m::makeBoard ("stab", 48000));
    e->selectTab (5); shoot (*ed, dir + "/ui_stab.png");
    p.rack.addBoard (n2m::makeBoard ("arp", 48000));
    e->selectTab (6); shoot (*ed, dir + "/ui_arp.png");
    {   // a few of the newer units on one board, to see how they draw
        auto eb = n2m::makeBoard ("empty", 48000);
        for (const char* t : { "gate", "stepmod", "scope", "parameq", "send", "ratchet", "keys" }) eb->add (t, 0, 0, 48000);
        if (auto* sc = dynamic_cast<n2m::Scope*> (eb->modules[3].get()))      // give the scope a wave and a note to show
        {
            for (int i = 0; i < n2m::Scope::size; ++i) sc->ring[(size_t) i] = 0.7f * std::sin (0.0575f * (float) i) + 0.2f * std::sin (0.23f * (float) i);
            sc->hz.store (220.0f); sc->level.store (0.7f);
        }
        n2m::setValues (*eb->modules[2], n2m::valueIdeas()[2].v);
        n2m::setDrum (*eb->modules[1], n2m::drumIdeas ("gate")[0].pat);
        int x = 24, y = 170;
        for (size_t k = 1; k < eb->modules.size(); ++k) { auto& m = *eb->modules[k]; m.x = (float) x; m.y = (float) y; x += n2m::lay::width (*m.info) + 16; if (k == 3) { x = 24; y = 430; } }
        p.rack.addBoard (std::move (eb));
        e->selectTab (7); shoot (*ed, dir + "/ui_units.png");
        p.rack.removeBoard (7); p.rack.removeBoard (6);
        e->selectTab (0);
    }
    p.rack.addBoard (n2m::makeBoard ("lead", 48000));
    e->selectTab (6); shoot (*ed, dir + "/ui_lead.png");

    // units added from the menu arrive unconnected
    auto& b = *p.rack.boards[0];
    e->selectTab (0);
    e->addPanel.open();
    shoot (*ed, dir + "/ui_add.png");
    for (auto* t : n2m::allTypes()) if (std::string (t->type) == "tape") e->addPanel.hover (t);
    shoot (*ed, dir + "/ui_add_preview.png");                 // the picture shown beside the row the mouse is on
    e->addPanel.hover (nullptr);
    n2m::AddPanel::setFavourite ("hall", true); n2m::AddPanel::setFavourite ("filter", true);
    shoot (*ed, dir + "/ui_add_favs.png");
    e->addPanel.setSearch ("rev");
    shoot (*ed, dir + "/ui_add_search.png");
    e->addPanel.setSearch ({});
    n2m::AddPanel::setFavourite ("hall", false); n2m::AddPanel::setFavourite ("filter", false);
    e->addPanel.onAdd ({ "filter", "lfo" });
    e->addPanel.setVisible (false);
    juce::ignoreUnused (b);
    shoot (*ed, dir + "/ui_added.png");

    // more room for the board: Focus, and Full screen
    e->setFocus (true);
    shoot (*ed, dir + "/ui_focus.png");
    e->setBigWindow (true);
    shoot (*ed, dir + "/ui_full_focus.png");
    e->setFocus (false);
    shoot (*ed, dir + "/ui_full.png");
    e->setBigWindow (false);

    // the mixing desk opened up
    p.rack.boards[1]->pan.store (-0.4f); p.rack.boards[3]->pan.store (0.6f);
    e->desk().setTall (true);
    shoot (*ed, dir + "/ui_desk_open.png");
    e->desk().setTall (false);
    p.rack.boards[1]->pan.store (0.0f); p.rack.boards[3]->pan.store (0.0f);

    // the song page: off, then built up
    e->songPanel.open();
    shoot (*ed, dir + "/ui_song_off.png");
    e->songPanel.fill (1);
    e->songPanel.setCell (0, 4, false); e->songPanel.setCell (1, 4, false); e->songPanel.setCell (2, 4, false);
    shoot (*ed, dir + "/ui_song.png");
    e->songPanel.setVisible (false);

    e->stepZoom (-1); e->stepZoom (-1);
    shoot (*ed, dir + "/ui_zoomed_out.png");                  // 67%: the whole kick board in view
    e->stepZoom (1); e->stepZoom (1);

    // many tabs: the tab row and the mixer scroll
    for (int i = 0; i < 12; ++i) { auto nb = n2m::makeBoard (i % 2 ? "perc" : "hat", 48000); nb->name += " " + std::to_string (i + 2); p.rack.addBoard (std::move (nb)); }
    e->selectTab (9);
    e->resized();
    shoot (*ed, dir + "/ui_many.png");
    e->songPanel.setSections (16);
    e->songPanel.open();
    shoot (*ed, dir + "/ui_song_many.png");
    e->songPanel.setVisible (false);

    p.rack.addBoard (n2m::makeBoard ("empty", 48000));
    e->selectTab ((int) p.rack.boards.size() - 1);
    shoot (*ed, dir + "/ui_empty.png");

    // A sample player with a sound in it
    {
        const auto wav = juce::File (dir).getChildFile ("snapshot-sound.wav");
        wav.deleteFile();
        {
            juce::AudioBuffer<float> sb (1, 30000);
            for (int i = 0; i < 30000; ++i) sb.setSample (0, i, 0.8f * std::exp (-(float) i / 7000.0f) * (float) std::sin (0.02 * i + 6.0 * std::exp (-i / 900.0)));
            std::unique_ptr<juce::AudioFormatWriter> w (juce::WavAudioFormat().createWriterFor (new juce::FileOutputStream (wav), 44100, 1, 16, {}, 0));
            w->writeFromAudioSampleBuffer (sb, 0, 30000);
        }
        p.rack.addBoard (n2m::makeBoard ("sample", 48000));
        for (auto& m : p.rack.boards.back()->modules) if (auto* sm = dynamic_cast<n2m::SampleModule*> (m.get())) { sm->load (wav); sm->set ("length", 0.6f); }
        e->selectTab ((int) p.rack.boards.size() - 1);
        shoot (*ed, dir + "/ui_sample.png");
        p.rack.removeBoard ((int) p.rack.boards.size() - 1);
        wav.deleteFile();
        e->selectTab ((int) p.rack.boards.size() - 1);
    }

    // Plugin slots: one empty, and (when a .vst3 path is given) one holding a plugin, then that plugin's window
    {
        auto& eb = *p.rack.boards.back();
        p.rack.addModule (eb, "plugin", 160, 14);
        auto* slot = dynamic_cast<n2m::HostedModule*> (p.rack.addModule (eb, "plugin", 160, 280));
        if (argc > 2 && slot != nullptr)
        {
            const auto found = n2m::pluginsInFile (argv[2]);
            if (! found.empty()) slot->load (found[0]);
            slot->assignKnob (0, 0); slot->assignKnob (1, 1);          // two knobs pointed at the plugin's own controls
        }
        e->board().rebuild();
        shoot (*ed, dir + "/ui_slots.png");
        if (argc > 2) { e->selectTab (p.addPluginSound ("Nebula2 Chord", argv[2])); shoot (*ed, dir + "/ui_plugin_sound.png"); }
        if (slot != nullptr && slot->hasPlugin())
        {
            e->hostOverlay.open (*slot, eb.name);
            shoot (*ed, dir + "/ui_plugin_open.png");
            e->hostOverlay.close();
        }
    }
    // Make track: a whole track in a style, from the style bar
    p.rack.style = "acid";
    e->styles().onMake ("acid");
    p.preview.store (true); run (p, 0.7);
    shoot (*ed, dir + "/ui_style_acid.png");
    p.preview.store (false);
    e->showAllSounds();
    e->styles().onMake ("trance");
    shoot (*ed, dir + "/ui_all_trance.png");            // a bigger track, fitted in
    e->stepZoom (1); e->stepZoom (1); e->stepZoom (1);
    shoot (*ed, dir + "/ui_all_zoomed.png");
    // the timeline, with two knobs moving through the song
    {
        e->songPanel.open();
        e->songPanel.fill (1);
        int at = 0;
        for (auto& b : p.rack.boards)
        {
            for (auto& m : b->modules)
                if (std::string (m->info->type) == "filter" && (b->type == "bass" || b->type == "lead"))
                {
                    const int lane = e->songPanel.addLane (at, m->uid, m->paramIndex ("cutoff"));
                    if (lane >= 0) { auto& a = *b->autos[(size_t) lane]; a.move (0, 0.0f, 0.25f); a.add (8.0f, 0.4f); a.add (16.0f, 0.9f); a.add (24.0f, 0.5f); a.bend (1, -0.6f); a.bend (2, 0.6f); }      // two stretches bent, one each way
                }
            ++at;
        }
        p.preview.store (true); run (p, 9.0);
        shoot (*ed, dir + "/ui_timeline.png");
        p.preview.store (false);
        e->songPanel.setVisible (false);
        shoot (*ed, dir + "/ui_all_marks.png");
    }
    // The eight Notes and rhythm units, wide, in rows as on the design picture
    {
        auto nb = n2m::makeBoard ("empty", 48000);
        nb->name = "Notes and rhythm"; nb->colour = 6;
        std::map<std::string, n2m::Module*> u;
        for (const char* t : { "drumseq", "noteseq", "keys", "arp", "chords", "euclid", "ratchet", "swing" }) u[t] = nb->add (t, 0, 0, 48000);
        n2m::setDrum (*u["drumseq"], "X.x.X..xX..x.xXx");
        u["drumseq"]->set ("bars", 1);
        n2m::setNotes (*u["noteseq"], n2m::noteIdeas ("lead")[0].gates, n2m::noteIdeas ("lead")[0].deg);
        u["euclid"]->set ("hits", 5); u["euclid"]->set ("steps", 16); u["euclid"]->set ("accent", 2);
        nb->connect (u["keys"]->uid, 0, nb->modules[0]->uid, 0);          // cabled to the Output, so the keyboard runs and can show its keys
        n2m::tidy (*nb);                                                   // (Tidy puts them all in one long row: a test checks none overlap)
        const char* rows[4][2] = { { "drumseq", "noteseq" }, { "keys", "arp" }, { "chords", "euclid" }, { "ratchet", "swing" } };
        for (int r = 0; r < 4; ++r)
        {
            int x = 16;
            for (auto* t : rows[r]) { u[t]->x = (float) x; u[t]->y = (float) (14 + r * (n2m::lay::panelH + 10)); x += n2m::lay::width (*u[t]->info) + 6; }
        }
        // the Output goes between the keyboard and the arpeggiator, so the keyboard's cable is a short one
        nb->modules[0]->x = u["arp"]->x; nb->modules[0]->y = u["arp"]->y; u["arp"]->x += (float) (n2m::lay::width (*nb->modules[0]->info) + 6);
        p.rack.addBoard (std::move (nb));
        for (auto& m : p.rack.boards.back()->modules) m->live = true;      // (for the picture only: units with no cable to the Output are normally drawn dimmed)
        e->selectTab ((int) p.rack.boards.size() - 1);
        e->setFocus (true); e->setBigWindow (true); e->stepZoom (-1);
        {   // hold a chord on the MIDI keyboard, then let the song run a moment so the playing lights show
            juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer held;
            held.addEvent (juce::MidiMessage::noteOn (1, 50, 0.8f), 0); held.addEvent (juce::MidiMessage::noteOn (1, 53, 0.8f), 1); held.addEvent (juce::MidiMessage::noteOn (1, 57, 0.8f), 2);
            p.processBlock (buf, held);
        }
        p.preview.store (true); run (p, 1.3);
        shoot (*ed, dir + "/ui_notes_rhythm.png");
        p.preview.store (false); run (p, 0.1);
        e->stepZoom (1); e->setFocus (false); e->setBigWindow (false);
    }
    ed.reset();

    // The bigger views: one picture of each, with something worth looking at in it
    {
        Nebula2ModulationProcessor q;
        q.setRateAndBufferSizeDetails (48000, 256);
        q.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (q.createEditor());
        auto* b = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        auto unit = [&q] (int tab, const char* type) -> n2m::Module*
        {
            for (auto& m : q.rack.boards[(size_t) tab]->modules) if (std::string (m->info->type) == type) return m.get();
            return nullptr;
        };
        b->selectTab (3);
        // a four-bar bass line with some long notes
        if (auto* m = unit (3, "noteseq"))
        {
            m->set ("bars", 2);
            const int lines[4][16] = { { 0, 0, 0, 0, 2, 0, 4, 0, 0, 0, 7, 0, 4, 0, 2, 0 }, { 0, 0, 0, 0, 2, 0, 4, 0, 0, 0, 5, 0, 4, 0, 6, 0 },
                                       { -3, 0, 0, 0, -1, 0, 0, 0, 2, 0, 4, 0, 2, 0, 0, 0 }, { -3, 0, 0, 0, -1, 0, 1, 0, 2, 0, 0, 0, 7, 0, 9, 0 } };
            const char* const gates[4] = { "x...x.x...x.x.x.", "x...x.x...x.x.xx", "x...x...x.x.x...", "x...x.x.x...x.x." };
            for (int bar = 0; bar < 4; ++bar) n2m::setNotesBar (*m, bar, gates[bar], lines[bar]);
            // some notes with a length of their own: held for two, three or four steps
            for (int at : { 0, 16, 32, 48 }) m->data[(size_t) (n2m::kLenRow + at)].store (4.0f);
            for (int at : { 6, 22, 36, 56 }) m->data[(size_t) (n2m::kLenRow + at)].store (3.0f);
            for (int at : { 10, 44, 60 }) m->data[(size_t) (n2m::kLenRow + at)].store (2.0f);
            b->bigPanel.open (*q.rack.boards[3], *m);
            q.preview.store (true); run (q, 2.6);
            shoot (*holder, dir + "/ui_big_noteseq.png");
            q.preview.store (false); run (q, 0.1);
            b->setBigWindow (true);
            shoot (*holder, dir + "/ui_big_noteseq_full.png");       // the same with Full screen on
            b->setBigWindow (false);
            m->set ("bars", 3); n2m::randomNotes ("bass", 77, *m);
            shoot (*holder, dir + "/ui_big_noteseq_8.png");          // eight bars, the most there can be
            b->bigPanel.close();
        }
        // a four-bar drum pattern with hits of different strengths
        b->selectTab (0);
        if (auto* m = unit (0, "drumseq"))
        {
            m->set ("bars", 2); n2m::randomDrum ("hat", 99, *m);
            n2m::Rand r (5);
            for (int k = 0; k < 64; ++k) if (m->data[(size_t) k].load() > 0.5f && k % 4 != 0) m->data[(size_t) k].store (n2m::drumData (0.2f + 0.8f * (float) r.upTo (100) / 100.0f));
            b->bigPanel.open (*q.rack.boards[0], *m);
            if (auto* v = b->bigPanel.as<n2m::BigDrumGrid>()) { v->copyBarAt (0); v->selectBar (2); }
            q.preview.store (true); run (q, 1.2);
            shoot (*holder, dir + "/ui_big_drumseq.png");
            q.preview.store (false); run (q, 0.1);
            b->bigPanel.close();
            b->selectTab (0);
            shoot (*holder, dir + "/ui_drum_velocities.png");        // the unit itself, showing those strengths on its small bars
        }
        // the chords of a 16-bar song, one picked
        b->selectTab (4);
        if (auto* m = unit (4, "chords"))
        {
            q.rack.toggleChordLock (5);
            b->bigPanel.open (*q.rack.boards[4], *m);
            if (auto* v = b->bigPanel.as<n2m::BigChords>()) v->select (2);
            q.preview.store (true); run (q, 9.0);
            shoot (*holder, dir + "/ui_big_chords.png");
            q.preview.store (false); run (q, 0.1);
            // ...and the most it has to show: 32 chords
            q.rack.harmony.lenBars = 32; q.rack.harmony.chordBars = 1; q.rack.generateChords (11); q.chordsVersion.fetch_add (1);
            if (auto* v = b->bigPanel.as<n2m::BigChords>()) v->select (19);
            shoot (*holder, dir + "/ui_big_chords_32.png");
            b->bigPanel.close();
        }
        // a board with the other four on it
        auto eb = n2m::makeBoard ("empty", 48000);
        eb->name = "Bigger views"; eb->colour = 2;
        for (const char* t : { "sample", "parameq", "stepmod", "scope" }) eb->add (t, 0, 0, 48000);
        n2m::tidy (*eb);
        q.rack.addBoard (std::move (eb));
        const int tab = (int) q.rack.boards.size() - 1;
        b->selectTab (tab);
        if (auto* sm = dynamic_cast<n2m::SampleModule*> (unit (tab, "sample")))
        {
            // a short loop to load: four hits that die away, each a little lower
            const auto wav = juce::File (dir).getChildFile ("snapshot-loop.wav");
            wav.deleteFile();
            {
                juce::AudioBuffer<float> buf (1, 48000);
                for (int i = 0; i < 48000; ++i)
                {
                    const int hit = i / 12000; const float t = (float) (i % 12000) / 48000.0f;
                    buf.setSample (0, i, 0.9f * std::exp (-t * (9.0f + 3.0f * (float) hit)) * std::sin (6.2832f * (180.0f - 25.0f * (float) hit) * t * (1.0f + 2.0f * std::exp (-t * 30.0f))));
                }
                juce::WavAudioFormat fmt;
                if (auto os = std::unique_ptr<juce::FileOutputStream> (wav.createOutputStream()))
                    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (fmt.createWriterFor (os.get(), 48000, 1, 16, {}, 0))) { os.release(); w->writeFromAudioSampleBuffer (buf, 0, 48000); }
            }
            sm->load (wav);
            sm->set ("start", 0.25f); sm->set ("length", 0.66f);
            sm->playhead.store (0.41f);
            b->bigPanel.open (*q.rack.boards[(size_t) tab], *sm);
            shoot (*holder, dir + "/ui_big_sample.png");
            if (auto* v = b->bigPanel.as<n2m::BigSample>()) v->zoomBy (3.0f, 0.4f);
            shoot (*holder, dir + "/ui_big_sample_zoom.png");
            b->bigPanel.close();
            sm->playhead.store (-1.0f);
        }
        if (auto* m = unit (tab, "parameq"))
        {
            b->bigPanel.open (*q.rack.boards[(size_t) tab], *m);
            if (auto* v = b->bigPanel.as<n2m::BigEq>()) { v->setBand (0, 0.20f, 7.5f); v->setBand (2, 0.70f, -9.0f); v->setWidth (2, 3.0f); }
            shoot (*holder, dir + "/ui_big_parameq.png");
            b->bigPanel.close();
        }
        if (auto* m = unit (tab, "stepmod"))
        {
            m->set ("bars", 1);
            b->bigPanel.open (*q.rack.boards[(size_t) tab], *m);
            if (auto* v = b->bigPanel.as<n2m::BigStepMod>())
            {
                v->drawLine (0, -7, 11, 14); v->drawLine (12, 14, 15, 2); v->drawLine (16, 2, 23, 12); v->drawLine (24, 12, 31, -7);
                v->setHold (5); v->setHold (6); v->setHold (20);
            }
            shoot (*holder, dir + "/ui_big_stepmod.png");
            b->bigPanel.close();
        }
        if (auto* sc = dynamic_cast<n2m::Scope*> (unit (tab, "scope")))
        {
            for (int i = 0; i < n2m::Scope::size; ++i) sc->ring[(size_t) i] = 0.7f * std::sin (0.0575f * (float) i) + 0.2f * std::sin (0.23f * (float) i);
            sc->hz.store (221.5f); sc->level.store (0.7f);
            b->bigPanel.open (*q.rack.boards[(size_t) tab], *sc);
            shoot (*holder, dir + "/ui_big_scope.png");
            b->bigPanel.close();
        }
        shoot (*holder, dir + "/ui_big_units.png");                  // those four units on their board, each with its Expand label
    }
    {
        // The Inside mixer: the desk showing the levels inside one sound
        Nebula2ModulationProcessor q;
        q.setRateAndBufferSizeDetails (48000, 256);
        q.prepareToPlay (48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> holder (q.createEditor());
        auto* b = dynamic_cast<Nebula2ModulationEditor*> (holder.get());
        b->selectTab (1);                                   // the snare: two layers and a Mixer unit
        b->desk().setInside (true);
        b->addUnits ({ "hall" });                           // not cabled yet, so it can't be heard: its strip is dimmed
        for (auto& s : b->desk().insideList()) if (s.unit == "Mixer" && s.knob == "Level 1") b->showUnit (s.uid);      // as after a click on that strip's name
        shoot (*holder, dir + "/ui_inside.png");
        b->desk().setTall (true);
        shoot (*holder, dir + "/ui_inside_tall.png");
        b->desk().setTall (false);
        b->showAllSounds();
        b->reshow (3);
        shoot (*holder, dir + "/ui_inside_all.png");        // on the All view it shows the sound last selected
        q.rack.addBoard (std::make_unique<n2m::Board>());
        b->selectTab ((int) q.rack.boards.size() - 1);
        shoot (*holder, dir + "/ui_inside_none.png");       // a sound with nothing to mix
    }
    return 0;
}
