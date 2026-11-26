#include "PluginProcessor.h"
#include "PluginEditor.h"

// The main mix, plus 15 separate stereo outputs that start switched off. In Cubase they are switched on
// from the instrument's "Activate Outputs" list, and each then gets its own channel in the MixConsole.
juce::AudioProcessor::BusesProperties Nebula2ModulationProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int k = 1; k < n2m::kOuts; ++k) b = b.withOutput ("Out " + juce::String (k + 1), juce::AudioChannelSet::stereo(), false);
    return b;
}

Nebula2ModulationProcessor::Nebula2ModulationProcessor()
    : AudioProcessor (makeBuses())
{
    n2m::registerPluginSlot();
    n2m::loadStarter (rack);
}

void Nebula2ModulationProcessor::prepareToPlay (double sampleRate, int)
{
    rack.prepare (sampleRate);
}

bool Nebula2ModulationProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int k = 1; k < layouts.outputBuses.size(); ++k)
        if (! layouts.outputBuses[k].isDisabled() && layouts.outputBuses[k] != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void Nebula2ModulationProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    const int n = buffer.getNumSamples();

    // Cubase's tempo and position, when it gives them
    double tempo = bpm.load(), ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) tempo = *b;
            if (auto p = pos->getPpqPosition()) ppq = *p;
            playing = pos->getIsPlaying();
        }
    if (tempo < 20.0 || tempo > 400.0) tempo = 124.0;
    bpm.store (tempo);
    hostPlaying.store (playing);

    // When Cubase plays, follow it. Otherwise the plugin's own Play runs from bar 1.
    if (playing) ownPpq = 0.0;
    const bool own = ! playing && preview.load();
    if (! own) ownPpq = 0.0;

    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }
    // Hand the rack each separate output the host has switched on
    n2m::Outs outs;
    for (int k = 1; k < juce::jmin (n2m::kOuts, getBusCount (false)); ++k)
    {
        auto* bus = getBus (false, k);
        if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() < 2) continue;
        const int first = getChannelIndexInProcessBlockBuffer (false, k, 0);
        if (first + 1 >= buffer.getNumChannels()) continue;
        outs.l[k] = buffer.getWritePointer (first);
        outs.r[k] = buffer.getWritePointer (first + 1);
    }
    rack.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, tempo, playing ? ppq : ownPpq, playing || own, &outs);
    if (own) ownPpq += n * tempo / 60.0 / getSampleRate();
}

uint32_t Nebula2ModulationProcessor::nextSeed()
{
    seed = seed * 1664525u + 1013904223u;
    return seed ^ (uint32_t) juce::Time::getMillisecondCounter();
}

void Nebula2ModulationProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream out (dest, false);
    out.writeString (juce::String (rack.save()));
    out.writeInt (selectedTab.load());
    out.writeFloat (zoom.load());
}

void Nebula2ModulationProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream in (data, (size_t) size, false);
    const auto text = in.readString();
    if (rack.load (text.toStdString()))
    {
        selectedTab.store (in.isExhausted() ? 0 : juce::jlimit (0, juce::jmax (0, (int) rack.boards.size() - 1), in.readInt()));
        if (! in.isExhausted()) zoom.store (juce::jlimit (0.5f, 1.25f, in.readFloat()));
        history.clear();                 // the old steps describe a project that is no longer loaded
        stateVersion.fetch_add (1);
    }
}

juce::AudioProcessorEditor* Nebula2ModulationProcessor::createEditor() { return new Nebula2ModulationEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Nebula2ModulationProcessor(); }
