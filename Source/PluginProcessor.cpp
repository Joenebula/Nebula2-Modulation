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
    n2m::registerSamplePlayer();
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
    const int n = buffer.getNumSamples();
    // Keys from a MIDI keyboard or a MIDI track, for any MIDI keyboard units on the boards
    n2m::MidiIn keysIn { keyEvents, 0 };
    for (const auto meta : midi)
    {
        if (keysIn.count >= 512) break;
        const auto m = meta.getMessage();
        const int at = juce::jlimit (0, juce::jmax (0, n - 1), meta.samplePosition);
        if (m.isNoteOn()) keyEvents[keysIn.count++] = { at, (unsigned char) m.getNoteNumber(), (unsigned char) juce::jmax (1, (int) m.getVelocity()), (unsigned char) m.getChannel(), true };
        else if (m.isNoteOff()) keyEvents[keysIn.count++] = { at, (unsigned char) m.getNoteNumber(), 0, (unsigned char) m.getChannel(), false };
        else if (m.isAllNotesOff() || m.isAllSoundOff()) keyEvents[keysIn.count++] = { at, 255, 0, (unsigned char) m.getChannel(), false };
    }
    midi.clear();

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
    rack.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, tempo, playing ? ppq : ownPpq, playing || own, &outs, &keysIn);
    if (own) ownPpq += n * tempo / 60.0 / getSampleRate();
}

juce::File Nebula2ModulationProcessor::soundsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Nebula2 Modulation").getChildFile ("Sounds");
}

std::vector<juce::File> Nebula2ModulationProcessor::savedSounds()
{
    std::vector<juce::File> out;
    for (auto& f : soundsFolder().findChildFiles (juce::File::findFiles, false, "*.n2msound")) out.push_back (f);
    std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });
    return out;
}

bool Nebula2ModulationProcessor::saveSound (int tab, const juce::File& file)
{
    if (tab < 0 || tab >= (int) rack.boards.size()) return false;
    file.getParentDirectory().createDirectory();
    return file.replaceWithText ("N2MSOUND 1\n" + juce::String (n2m::Rack::boardText (*rack.boards[(size_t) tab])) + "end\n");
}

int Nebula2ModulationProcessor::loadSound (const juce::File& file)
{
    const auto text = file.loadFileAsString();
    if (! text.startsWith ("N2MSOUND ")) return -1;
    auto made = n2m::Rack::readBoards (text.toStdString(), rack.sr);
    if (made.empty()) return -1;
    auto& b = *made[0];
    // It arrives on its own: not soloed, in every section, and not tied to this project's link groups
    b.solo.store (false); b.mute.store (false); b.plays.store (0xffffffffu);
    for (auto& m : b.modules) { m->patternLink = 0; m->patternShare = 3; std::fill (m->paramLink.begin(), m->paramLink.end(), 0); }
    rack.addBoard (std::move (made[0]));
    history.tabAdded ((int) rack.boards.size() - 1);
    return (int) rack.boards.size() - 1;
}

juce::File Nebula2ModulationProcessor::writeMidi (const n2m::Module& m, const juce::String& name)
{
    const auto list = n2m::notesOf (rack, m);
    if (list.notes.empty()) return {};
    const double ticksPerStep = 120.0;                 // 480 to the beat
    juce::MidiMessageSequence track;
    auto add = [&track] (juce::MidiMessage msg, double t) { msg.setTimeStamp (std::round (t)); track.addEvent (msg); };
    add (juce::MidiMessage::textMetaEvent (3, name), 0);
    add (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (6.0e7 / juce::jmax (20.0, bpm.load()))), 0);
    add (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    for (auto& nt : list.notes)
    {
        const int note = juce::jlimit (0, 127, nt.midi);
        add (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, nt.vel)), nt.start * ticksPerStep);
        add (juce::MidiMessage::noteOff (1, note), (nt.start + nt.len) * ticksPerStep - 1.0);
    }
    add (juce::MidiMessage::endOfTrack(), list.steps * ticksPerStep);      // so the part is whole bars long
    track.sort();
    track.updateMatchedPairs();
    juce::MidiFile file;
    file.setTicksPerQuarterNote (480);
    file.addTrack (track);
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nebula2-Modulation");
    folder.createDirectory();
    auto out = folder.getChildFile (juce::File::createLegalFileName (name.isNotEmpty() ? name : juce::String ("Pattern")) + ".mid");
    out.deleteFile();
    juce::FileOutputStream stream (out);
    if (! stream.openedOk() || ! file.writeTo (stream)) return {};
    return out;
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
