#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "PluginHost.h"
#include "Sampler.h"
#include "History.h"

// The plugin as Cubase sees it: holds the rack, follows Cubase's tempo and transport, saves with the project.
class Nebula2ModulationProcessor : public juce::AudioProcessor
{
public:
    Nebula2ModulationProcessor();

    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nebula2 Modulation"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- For the screen ----
    n2m::Rack rack;
    n2m::History history { rack };              // undo and redo
    std::atomic<bool> preview { false };        // the plugin's own Play, for when Cubase is stopped
    std::atomic<bool> hostPlaying { false };
    std::atomic<double> bpm { 124.0 };
    std::atomic<int> selectedTab { 0 };
    std::atomic<float> zoom { 1.0f };           // how big the board is drawn
    std::atomic<bool> mixerTall { false };      // the mixing desk opened up to show its extra controls
    std::atomic<int> stateVersion { 0 };        // goes up when a project load replaces the rack
    std::atomic<int> chordsVersion { 0 };       // goes up when the chords or the key change
    uint32_t nextSeed();                        // a fresh number for each Random press

    bool isPlaying() const { return hostPlaying.load() || preview.load(); }

    // Sounds: one tab saved as a file, to use again in another project
    static juce::File soundsFolder();                               // Documents / Nebula2 Modulation / Sounds
    static std::vector<juce::File> savedSounds();                   // the files in that folder, A to Z
    bool saveSound (int tab, const juce::File&);
    int loadSound (const juce::File&);                              // adds it as a new tab; returns the tab, or -1
    // A sequencer's pattern, or the chords, as a MIDI file to drag into the host. Returns the file written.
    juce::File writeMidi (const n2m::Module&, const juce::String& name);

    // One tab on its own as an audio file (WAV), to drag into the host: the whole chord progression once through,
    // from bar 1, at the current tempo, without the master fader. Other tabs are silent but still run, so a
    // shared-effects tab is rendered with what feeds it. bars = 0: as long as the chords (4 to 64 bars).
    juce::File renderTab (int tab, int bars = 0);

    // Changing a tab's sound. The pattern, the tab's name and its mixer channel stay; only the sound changes.
    // Each returns false, changing nothing, if it can't be done. All can be undone.
    bool changeSound (int tab, std::unique_ptr<n2m::Board> fresh);
    bool changeToPreset (int tab, const juce::String& kind, const juce::String& preset);   // one of soundPresets()
    bool changeToSample (int tab, const juce::File& audioFile);
    bool changeToPlugin (int tab, const juce::String& name, const juce::String& path = {});   // an instrument
    bool changeToSaved (int tab, const juce::File& soundFile);                              // a .n2msound

    // Your own plugins as ready-made sounds: a new tab with the plugin already in a slot and cabled.
    // An instrument goes straight to the Output; an effect is fed by send bus A. Either way a MIDI keyboard unit
    // is cabled in, so keys or a MIDI track reach the plugin. If the plugin isn't on this computer the tab is
    // still made, with the slot saying so. path: a .vst3 to use instead of looking the name up.
    static juce::StringArray ownPlugins();                          // the names offered in the + menu
    int addPluginSound (const juce::String& name, const juce::String& path = {});

private:
    static BusesProperties makeBuses();
    double ownPpq = 0;
    n2m::KeyEvent keyEvents[512];
    uint32_t seed = 0x9E3779B9u;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Nebula2ModulationProcessor)
};
