#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "PluginHost.h"

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
    std::atomic<bool> preview { false };        // the plugin's own Play, for when Cubase is stopped
    std::atomic<bool> hostPlaying { false };
    std::atomic<double> bpm { 124.0 };
    std::atomic<int> selectedTab { 0 };
    std::atomic<int> stateVersion { 0 };        // goes up when a project load replaces the rack
    std::atomic<int> chordsVersion { 0 };       // goes up when the chords or the key change
    uint32_t nextSeed();                        // a fresh number for each Random press

    bool isPlaying() const { return hostPlaying.load() || preview.load(); }

private:
    double ownPpq = 0;
    uint32_t seed = 0x9E3779B9u;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Nebula2ModulationProcessor)
};
