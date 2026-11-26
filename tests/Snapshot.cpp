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

int main (int, char** argv)
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

    e->selectTab (1); shoot (*ed, dir + "/ui_snare.png");
    e->selectTab (3); shoot (*ed, dir + "/ui_bass.png");

    p.rack.addBoard (n2m::makeBoard ("lead", 48000));
    e->selectTab (4); shoot (*ed, dir + "/ui_lead.png");

    // units added from the menu arrive unconnected
    auto& b = *p.rack.boards[0];
    e->selectTab (0);
    e->addPanel.open();
    shoot (*ed, dir + "/ui_add.png");
    e->addPanel.onAdd ({ "filter", "lfo" });
    e->addPanel.setVisible (false);
    juce::ignoreUnused (b);
    shoot (*ed, dir + "/ui_added.png");

    // many tabs: the tab row and the mixer scroll
    for (int i = 0; i < 12; ++i) { auto nb = n2m::makeBoard (i % 2 ? "perc" : "hat", 48000); nb->name += " " + std::to_string (i + 2); p.rack.addBoard (std::move (nb)); }
    e->selectTab (9);
    e->resized();
    shoot (*ed, dir + "/ui_many.png");

    p.rack.addBoard (n2m::makeBoard ("empty", 48000));
    e->selectTab ((int) p.rack.boards.size() - 1);
    shoot (*ed, dir + "/ui_empty.png");
    return 0;
}
