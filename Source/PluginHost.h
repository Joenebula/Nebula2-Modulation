#pragma once
// Nebula2 Modulation: the plugin slot. A unit that holds another VST3 (an instrument or an effect)
// and joins it to the board: notes in from cables, audio in and out on jacks.
#include <juce_audio_processors/juce_audio_processors.h>
#include "Presets.h"

namespace n2m
{
juce::AudioPluginFormatManager& pluginFormats();

// Plugins loaded before, remembered between sessions so they can be picked from a list
std::vector<juce::PluginDescription> recentPlugins();
void rememberPlugin (const juce::PluginDescription&);
// Every VST3 found in the computer's usual plugin folders, as (name, path), A to Z. Found by looking at the
// folders only: no plugin is loaded to make this list, so one bad plugin can't hurt it. Kept after the first look;
// pass true to look again.
std::vector<std::pair<juce::String, juce::String>> installedPlugins (bool lookAgain = false);
// What a .vst3 file holds (one file can hold several plugins)
std::vector<juce::PluginDescription> pluginsInFile (const juce::String& path);

class HostedModule : public Module, private juce::AudioPlayHead
{
public:
    HostedModule();
    ~HostedModule() override;
    static const Info& desc();

    void prepare (double sampleRate) override;
    void process (int n, const Ctx&) override;
    std::string saveExtra() const override;
    void loadExtra (const std::string&) override;

    // Message thread. Returns an empty string when it worked, otherwise why it didn't.
    juce::String load (const juce::PluginDescription&, const juce::MemoryBlock* state = nullptr);
    void unload();

    bool hasPlugin() const { return plugin != nullptr; }
    juce::String pluginName() const { return plugin != nullptr ? description.name : juce::String(); }
    juce::String status;                               // shown on the unit when a load fails
    std::atomic<int> notesSent { 0 };                  // notes started in the plugin so far
    juce::AudioPluginInstance* instance() const { return plugin.get(); }   // message thread only, for opening its window

private:
    juce::Optional<PositionInfo> getPosition() const override;

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription description;
    TryLock guard;                                     // audio skips a chunk while a plugin is being swapped
    double sr = 48000;
    int numIn = 0, numOut = 2;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    NoteTracker notes;
    double posBpm = 120, posPpq = 0;
    bool posPlaying = false;
    std::shared_ptr<bool> alive;
};

// A screen showing a hosted plugin's window asks to be told before that plugin goes away, so it can close the window first
void addClosingListener (void* owner, std::function<void (HostedModule*)>);
void removeClosingListener (void* owner);

// Adds the plugin slot to the list of unit types. Safe to call more than once.
void registerPluginSlot();
} // namespace n2m
