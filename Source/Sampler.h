#pragma once
// Nebula2 Modulation: the sample player. Holds one audio file and plays it on each trigger.
#include <juce_audio_formats/juce_audio_formats.h>
#include "Presets.h"

namespace n2m
{
class SampleModule : public Module
{
public:
    SampleModule();
    static const Info& desc();

    void prepare (double sampleRate) override { sr = sampleRate; }
    void process (int n, const Ctx&) override;
    std::string saveExtra() const override;
    void loadExtra (const std::string&) override;
    std::string saveCopy() const override;
    void loadCopy (const std::string&) override;

    // Returns an empty string when it worked, otherwise why it didn't
    juce::String load (const juce::File&);
    void clear();

    // A file up to this size is also kept inside the project (and inside a saved sound), so it travels with it.
    // When the project opens, the file itself is used if it is still where it was; otherwise the copy.
    static constexpr juce::int64 kKeepBytes = 10 * 1024 * 1024;
    bool keepsCopy() const { return kept != nullptr; }      // the file is small enough to travel with the project
    bool usingCopy() const { return fromCopy; }             // the file wasn't found, so the copy in the project is playing

    bool hasSample() const { return sound != nullptr; }
    juce::String fileName() const { return file.getFileName(); }
    double seconds() const { return sound != nullptr && sourceRate > 0 ? sound->getNumSamples() / sourceRate : 0.0; }
    juce::String status;                     // shown on the unit when a file can't be loaded
    std::vector<float> peaks;                // the shape of the sound, for drawing (message thread)
    std::vector<float> fine;                 // the same in much finer slices, for the bigger view to zoom into
    std::atomic<float> playhead { -1.0f };   // 0..1 through the file while it plays, -1 when silent

    static bool canRead (const juce::String& path);
    static juce::String wildcard();

private:
    juce::String read (juce::AudioFormatReader*, const juce::String& name);
    std::shared_ptr<const juce::MemoryBlock> kept;         // the file as it is on disk, when small enough
    mutable std::string keptText;                          // the same as text, made the first time it is saved
    bool fromCopy = false;
    std::shared_ptr<const juce::AudioBuffer<float>> sound;
    juce::File file;
    double sourceRate = 44100, sr = 48000, pos = 0;
    bool active = false, lastTrig = false;
    float amp = 1;
    TryLock guard;
};

// Adds the sample player to the list of unit types. Safe to call more than once.
void registerSamplePlayer();
} // namespace n2m
