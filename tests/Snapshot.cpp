// Saves PNGs of the plugin window for checking the layout
#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    shoot (*ed, dir + "/ui.png");                       // first open: the kick board

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
        for (const char* t : { "gate", "stepmod", "scope", "parameq", "send", "ratchet" }) eb->add (t, 0, 0, 48000);
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
    e->addPanel.onAdd ({ "filter", "lfo" });
    e->addPanel.setVisible (false);
    juce::ignoreUnused (b);
    shoot (*ed, dir + "/ui_added.png");

    e->stepZoom (-1); e->stepZoom (-1);
    shoot (*ed, dir + "/ui_zoomed_out.png");                  // 67%: the whole kick board in view
    e->stepZoom (1); e->stepZoom (1);

    // many tabs: the tab row and the mixer scroll
    for (int i = 0; i < 12; ++i) { auto nb = n2m::makeBoard (i % 2 ? "perc" : "hat", 48000); nb->name += " " + std::to_string (i + 2); p.rack.addBoard (std::move (nb)); }
    e->selectTab (9);
    e->resized();
    shoot (*ed, dir + "/ui_many.png");

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
        p.rack.addModule (eb, "plugin", 24, 160);
        auto* slot = dynamic_cast<n2m::HostedModule*> (p.rack.addModule (eb, "plugin", 24, 360));
        if (argc > 2 && slot != nullptr)
        {
            const auto found = n2m::pluginsInFile (argv[2]);
            if (! found.empty()) slot->load (found[0]);
        }
        e->board().rebuild();
        shoot (*ed, dir + "/ui_slots.png");
        if (slot != nullptr && slot->hasPlugin())
        {
            e->hostOverlay.open (*slot, eb.name);
            shoot (*ed, dir + "/ui_plugin_open.png");
            e->hostOverlay.close();
        }
    }
    return 0;
}
