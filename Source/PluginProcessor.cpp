#include "PluginProcessor.h"
#include "PluginHost.h"
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
    startTimer (250);
}

void Nebula2ModulationProcessor::syncLatency()
{
    const int late = rack.latency.load();
    if (late != getLatencySamples()) setLatencySamples (late);
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
        else if (m.isPitchWheel()) keyEvents[keysIn.count++] = { at, 0, 0, (unsigned char) m.getChannel(), true, 1, (float) (m.getPitchWheelValue() - 8192) / 8192.0f };
        else if (m.isControllerOfType (1)) keyEvents[keysIn.count++] = { at, 0, 0, (unsigned char) m.getChannel(), true, 2, (float) m.getControllerValue() / 127.0f };
        else if (m.isSustainPedalOn() || m.isSustainPedalOff()) keyEvents[keysIn.count++] = { at, 0, 0, (unsigned char) m.getChannel(), m.isSustainPedalOn(), 3, 0.0f };
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

bool Nebula2ModulationProcessor::freezeTab (int tab)
{
    problem.clear();
    if (tab < 0 || tab >= (int) rack.boards.size()) return false;
    auto& b = *rack.boards[(size_t) tab];
    if (b.frozen.load()) return true;
    if (const char* why = rack.cantFreeze (b)) { problem = juce::String (b.name) + " can't be frozen: " + why; return false; }
    history.before (b);
    b.frozen.store (true);
    refreshFrozen (true);
    return true;
}

bool Nebula2ModulationProcessor::unfreezeTab (int tab)
{
    problem.clear();
    if (tab < 0 || tab >= (int) rack.boards.size()) return false;
    auto& b = *rack.boards[(size_t) tab];
    if (! b.frozen.load()) return true;
    history.before (b);
    b.frozen.store (false);
    rack.dropIce (b);
    return true;
}

void Nebula2ModulationProcessor::refreshFrozen (bool now)
{
    const double tempo = bpm.load();
    for (int i = 0; i < (int) rack.boards.size(); ++i)
    {
        auto& b = *rack.boards[(size_t) i];
        if (! b.frozen.load()) { if (b.ice != nullptr) rack.dropIce (b); b.iceWanted.clear(); continue; }
        if (rack.cantFreeze (b) != nullptr) { b.frozen.store (false); rack.dropIce (b); continue; }     // (it has since been given a Send, say)
        const auto state = rack.freezeState (b, tempo);
        if (b.ice != nullptr && b.ice->made == state) continue;
        if (b.ice != nullptr) rack.dropIce (b);            // out of date: its units play until the new audio is ready
        if (! now && b.iceWanted != state) { b.iceWanted = state; continue; }      // still changing: look again next time
        b.iceWanted = state;
        rack.makeIce (i, tempo);
    }
}

juce::File Nebula2ModulationProcessor::renderTab (int tab, int bars)
{
    if (tab < 0 || tab >= (int) rack.boards.size()) return {};
    // A private copy of the whole project, so the one that is playing is never disturbed
    n2m::Rack copy;
    copy.prepare (rack.sr);
    if (! copy.load (rack.save()) || tab >= (int) copy.boards.size()) return {};
    copy.master.store (1.0f);
    copy.song.on.store (false);
    for (size_t i = 0; i < copy.boards.size(); ++i)
    {
        auto& b = *copy.boards[i];
        b.out.store (0); b.mute.store (false); b.solo.store ((int) i == tab);
    }
    if (bars <= 0) bars = juce::jlimit (4, 64, (copy.timeline.total + n2m::kSteps - 1) / n2m::kSteps);
    const double tempo = juce::jlimit (20.0, 400.0, bpm.load()), sr = rack.sr;
    const int total = (int) std::llround (bars * 4.0 * 60.0 / tempo * sr);
    // If a plugin in a slot makes the sound late, everything comes out that much later: play that much longer and
    // leave the wait off the front, so the file starts on bar 1
    juce::AudioBuffer<float> audio (2, total + n2m::kMaxLatency);
    audio.clear();
    const int block = 512;
    int late = 0;
    for (int pos = 0; pos < total + late; pos += block)
    {
        const int n = juce::jmin (block, total + late - pos);
        copy.process (audio.getWritePointer (0, pos), audio.getWritePointer (1, pos), n, tempo, (double) pos * tempo / 60.0 / sr, true);
        late = juce::jlimit (late, n2m::kMaxLatency - 1, copy.latency.load());
    }
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nebula2-Modulation");
    folder.createDirectory();
    auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String (rack.boards[(size_t) tab]->name)) + ".wav");
    file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream> (file);
    if (! stream->openedOk()) return {};
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0));
    if (writer == nullptr) return {};
    stream.release();                                  // the writer owns it now
    if (! writer->writeFromAudioSampleBuffer (audio, late, total)) return {};
    writer.reset();
    return file;
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
    b.solo.store (false); b.mute.store (false); b.plays.store (0xffffffffu); b.frozen.store (false);
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

bool Nebula2ModulationProcessor::changeSound (int tab, std::unique_ptr<n2m::Board> fresh)
{
    if (fresh == nullptr || tab < 0 || tab >= (int) rack.boards.size()) return false;
    auto& old = *rack.boards[(size_t) tab];
    n2m::keepPattern (old, *fresh);
    history.before (old, -1);                          // everything on it is about to go: keep it all for Undo
    rack.applyBoard (tab, n2m::Rack::boardText (*fresh));
    return true;
}

bool Nebula2ModulationProcessor::changeToPreset (int tab, const juce::String& kind, const juce::String& preset)
{
    return changeSound (tab, n2m::makePreset (kind.toStdString(), preset.toStdString(), rack.sr));
}

bool Nebula2ModulationProcessor::changeToSample (int tab, const juce::File& audioFile)
{
    problem = audioFile.getFileName() + " isn't an audio file this can read.";
    if (tab < 0 || tab >= (int) rack.boards.size() || ! audioFile.existsAsFile() || ! n2m::SampleModule::canRead (audioFile.getFullPathName())) return false;
    problem.clear();
    auto fresh = n2m::makeCarrier (*rack.boards[(size_t) tab], "sample", rack.sr);
    if (fresh == nullptr) return false;
    for (auto& m : fresh->modules) if (auto* s = dynamic_cast<n2m::SampleModule*> (m.get())) s->load (audioFile);
    return changeSound (tab, std::move (fresh));
}

bool Nebula2ModulationProcessor::changeToPlugin (int tab, const juce::String& name, const juce::String& path)
{
    if (tab < 0 || tab >= (int) rack.boards.size()) return false;
    juce::String file = path;
    if (file.isEmpty())
    {
        juce::StringArray wanted { name.toLowerCase().removeCharacters (" -_") };
        if (wanted[0] == "nebula2drums") wanted.add ("nebula2");
        for (auto& w : wanted)
            for (auto& p : n2m::installedPlugins())
                if (file.isEmpty() && p.first.toLowerCase().removeCharacters (" -_") == w) file = p.second;
    }
    problem = name + " wasn't found in this computer's VST3 folders. Add a Plugin slot and use Load to browse to it.";
    if (file.isEmpty()) return false;
    const auto found = n2m::pluginsInFile (file);
    problem = name + " couldn't be opened as a VST3 plugin.";
    if (found.empty()) return false;
    // an effect makes no sound of its own
    problem = name + " is an effect, so it can't be a sound on its own. Use + > Your plugins to give it a tab that other sounds feed, or add a Plugin slot in a sound's path.";
    if (! found[0].isInstrument) return false;
    problem = name + " didn't load.";
    auto fresh = n2m::makeCarrier (*rack.boards[(size_t) tab], "plugin", rack.sr);
    if (fresh == nullptr) return false;
    bool loaded = false;
    for (auto& m : fresh->modules) if (auto* h = dynamic_cast<n2m::HostedModule*> (m.get())) loaded = h->load (found[0]).isEmpty();
    if (! loaded) return false;
    problem.clear();
    return changeSound (tab, std::move (fresh));
}

bool Nebula2ModulationProcessor::changeToSaved (int tab, const juce::File& soundFile)
{
    const auto text = soundFile.loadFileAsString();
    if (! text.startsWith ("N2MSOUND ")) return false;
    auto made = n2m::Rack::readBoards (text.toStdString(), rack.sr);
    if (made.empty()) return false;
    for (auto& m : made[0]->modules) { m->patternLink = 0; m->patternShare = 3; std::fill (m->paramLink.begin(), m->paramLink.end(), 0); }
    return changeSound (tab, std::move (made[0]));
}

// Names are compared without spaces, hyphens or capitals: "Nebula2-Glitch" is "Nebula2 Glitch"
static juce::String plainName (const juce::String& s) { return s.toLowerCase().removeCharacters (" -_"); }

juce::StringArray Nebula2ModulationProcessor::ownPlugins()
{
    juce::StringArray names { "Nebula2 Drums", "Nebula2 Voice", "Nebula2 Chord", "Nebula2 Glitch", "Nebula2 EQ", "Nebula2 Warmer", "VenPressorDyn" };
    // and any other Nebula2 plugin found on this computer (but not this one inside itself)
    for (auto& p : n2m::installedPlugins())
    {
        const auto plain = plainName (p.first);
        if (! plain.startsWith ("nebula2") || plain == "nebula2modulation" || plain == "nebula2") continue;
        bool have = false;
        for (auto& n : names) have = have || plainName (n) == plain;
        if (! have) names.add (p.first);
    }
    return names;
}

int Nebula2ModulationProcessor::addPluginSound (const juce::String& name, const juce::String& path)
{
    // Find it in the computer's plugin folders. Nebula2 Drums is installed under the plain name "Nebula2".
    juce::String file = path;
    if (file.isEmpty())
    {
        juce::StringArray wanted { plainName (name) };
        if (wanted[0] == "nebula2drums") wanted.add ("nebula2");
        for (auto& w : wanted)
            for (auto& p : n2m::installedPlugins())
                if (file.isEmpty() && plainName (p.first) == w) file = p.second;
    }
    std::vector<juce::PluginDescription> found;
    if (file.isNotEmpty()) found = n2m::pluginsInFile (file);
    const bool effect = ! found.empty() && ! found[0].isInstrument;

    auto b = n2m::makeBoard ("empty", rack.sr);
    b->name = name.toStdString();
    b->colour = effect ? 1 : 4;
    auto* out = b->modules[0].get();
    auto* slot = b->add ("plugin", 0, 0, rack.sr);
    auto* keys = b->add ("keys", 0, 0, rack.sr);
    if (slot == nullptr || keys == nullptr) return -1;
    keys->set ("snap", 0);            // drum pads and trigger keys must arrive as played
    auto cable = [&b] (n2m::Module* a, const char* o, n2m::Module* c, const char* i) { b->connect (a->uid, a->outIndex (o), c->uid, c->inIndex (i)); };
    cable (keys, "n1", slot, "p1"); cable (keys, "n2", slot, "p2"); cable (keys, "n3", slot, "p3"); cable (keys, "n4", slot, "p4");
    cable (keys, "gate", slot, "gate"); cable (keys, "vel", slot, "vel");
    cable (slot, "outl", out, "left"); cable (slot, "outr", out, "right");
    if (effect)
    {
        // An effect needs sound coming in: everything the Send units on other tabs feed to bus A
        auto* ret = b->add ("return", 0, 0, rack.sr);
        cable (ret, "outl", slot, "inl"); cable (ret, "outr", slot, "inr");
    }
    n2m::tidy (*b);
    auto* hosted = dynamic_cast<n2m::HostedModule*> (slot);
    auto* board = rack.addBoard (std::move (b));
    if (hosted != nullptr)
    {
        if (! found.empty()) hosted->load (found[0]);
        else hosted->status = name + " wasn't found in this computer's VST3 folders. Press Load to find it";
    }
    juce::ignoreUnused (board);
    history.tabAdded ((int) rack.boards.size() - 1);
    return (int) rack.boards.size() - 1;
}

void Nebula2ModulationProcessor::makeTrack (const juce::String& style, bool add)
{
    history.beforeAll();
    if (add) n2m::addTrack (rack, style.toStdString(), nextSeed());
    else n2m::makeTrack (rack, style.toStdString(), nextSeed());
    selectedTab.store (add ? juce::jmax (0, selectedTab.load()) : 0);
    chordsVersion.fetch_add (1);
}

void Nebula2ModulationProcessor::variation()
{
    history.beforeAll();
    n2m::varyPatterns (rack, nextSeed());
}

void Nebula2ModulationProcessor::randomiseKnobs()
{
    history.beforeAll();
    n2m::randomKnobs (rack, nextSeed());
}

int Nebula2ModulationProcessor::remakePart (const juce::String& kind, bool add)
{
    const auto& style = n2m::styleById (rack.style);
    const auto k = kind.toStdString();
    n2m::Rand r (nextSeed());
    // The tab to make again: the one showing if it is this kind of sound, else the first that is
    int tab = -1;
    if (! add)
    {
        const int sel = selectedTab.load();
        if (sel >= 0 && sel < (int) rack.boards.size() && rack.boards[(size_t) sel]->type == k) tab = sel;
        for (int i = 0; tab < 0 && i < (int) rack.boards.size(); ++i) if (rack.boards[(size_t) i]->type == k) tab = i;
    }
    auto part = n2m::partFor (style, k);
    if (tab >= 0) for (auto& p : style.parts) if (k == p.kind && rack.boards[(size_t) tab]->name == p.name) part = p;
    auto fresh = n2m::makeStylePart (part, r, rack.sr);
    if (tab < 0)
    {
        int same = 0;
        for (auto& o : rack.boards) if (o->name == fresh->name || o->name.rfind (fresh->name + " ", 0) == 0) ++same;
        if (same > 0) fresh->name += " " + std::to_string (same + 1);
        rack.addBoard (std::move (fresh));
        history.tabAdded ((int) rack.boards.size() - 1);
        return (int) rack.boards.size() - 1;
    }
    // Made again in place: the tab's name and its mixer channel stay
    auto& old = *rack.boards[(size_t) tab];
    fresh->name = old.name; fresh->colour = old.colour;
    fresh->level.store (old.level.load()); fresh->pan.store (old.pan.load());
    fresh->mute.store (old.mute.load()); fresh->solo.store (old.solo.load());
    fresh->out.store (old.out.load()); fresh->plays.store (old.plays.load());
    history.before (old, -1);
    rack.applyBoard (tab, n2m::Rack::boardText (*fresh));
    return tab;
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
    out.writeBool (mixerTall.load());
    out.writeBool (showAll.load());
    out.writeFloat (zoomAll.load());
    out.writeBool (focusView.load());
    out.writeBool (bigWindow.load());
    out.writeBool (mixerInside.load());
}

void Nebula2ModulationProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream in (data, (size_t) size, false);
    const auto text = in.readString();
    if (rack.load (text.toStdString()))
    {
        selectedTab.store (in.isExhausted() ? 0 : juce::jlimit (0, juce::jmax (0, (int) rack.boards.size() - 1), in.readInt()));
        if (! in.isExhausted()) zoom.store (juce::jlimit (0.5f, 1.5f, in.readFloat()));
        mixerTall.store (! in.isExhausted() && in.readBool());       // projects from before v0.1.17: closed
        showAll.store (! in.isExhausted() && in.readBool());         // projects from before the All view open on their tab, as they were saved
        zoomAll.store (in.isExhausted() ? 0.0f : juce::jlimit (0.0f, 1.5f, in.readFloat()));
        focusView.store (! in.isExhausted() && in.readBool());       // projects from before v0.1.24: everything showing, normal size
        bigWindow.store (! in.isExhausted() && in.readBool());
        mixerInside.store (! in.isExhausted() && in.readBool());     // projects from before the Inside mixer: the desk shows the sounds
        history.clear();                 // the old steps describe a project that is no longer loaded
        stateVersion.fetch_add (1);
    }
}

juce::AudioProcessorEditor* Nebula2ModulationProcessor::createEditor() { return new Nebula2ModulationEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Nebula2ModulationProcessor(); }
