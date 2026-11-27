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

class HostedModule : public Module, private juce::AudioPlayHead, private juce::AudioProcessorParameter::Listener
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
    // keepKnobs: a project being opened keeps what its knobs pointed at; loading a different plugin by hand clears them.
    juce::String load (const juce::PluginDescription&, const juce::MemoryBlock* state = nullptr, bool keepKnobs = false);
    void unload();

    bool hasPlugin() const { return plugin != nullptr; }
    juce::String pluginName() const { return plugin != nullptr ? description.name : juce::String(); }
    juce::String status;                               // shown on the unit when a load fails
    std::atomic<int> notesSent { 0 };                  // notes started in the plugin so far
    juce::AudioPluginInstance* instance() const { return plugin.get(); }   // message thread only, for opening its window

    // Knobs 1 to 4 on the slot can each be pointed at one of the plugin's own controls. A cable into the matching
    // Mod input then moves that control too. Message thread.
    static constexpr int kKnobs = 4;
    int numControls() const;
    juce::String controlName (int index) const;
    float controlValue (int index) const;              // 0..1
    void assignKnob (int knob, int index);             // -1 = not set. The knob takes the control's position, so nothing jumps
    int knobTarget (int knob) const { return knob >= 0 && knob < kKnobs ? target[knob].load() : -1; }
    int lastTouched() const { return touched.load(); } // the control last moved in the plugin's own window, or -1

    // The plugin's own presets, where it offers a list of them. Message thread.
    juce::StringArray presetNames() const;
    int currentPreset() const;
    void choosePreset (int index);
    // Which preset comes after (step 1) or before (step -1) this one, going round at the ends. -1 if there is no list.
    static int presetAfter (int current, int count, int step) { return count < 2 ? -1 : (((current < 0 ? 0 : current) + step) % count + count) % count; }
    bool stepPreset (int step);      // the next or the previous of the plugin's own presets. False if it offers no list

private:
    juce::Optional<PositionInfo> getPosition() const override;
    void parameterValueChanged (int index, float) override { touched.store (index); }
    void parameterGestureChanged (int, bool) override {}
    juce::String controlId (int index) const;

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
    std::atomic<int> target[kKnobs], touched { -1 };
    int heldNotes[16] {}, heldCount = 0;               // notes the plugin is sending out right now
    float noteOut[4] { 0, 0, 0, 0 }; bool gateWas = false;
    float sent[kKnobs] { -1, -1, -1, -1 };             // what each knob last told the plugin (-1 = nothing yet)
    juce::StringArray knobIds;                         // the plugin's own ids for those controls: what is saved
};

// A screen showing a hosted plugin's window asks to be told before that plugin goes away, so it can close the window first
void addClosingListener (void* owner, std::function<void (HostedModule*)>);
void removeClosingListener (void* owner);

// Adds the plugin slot to the list of unit types. Safe to call more than once.
void registerPluginSlot();
} // namespace n2m
