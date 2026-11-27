#include "PluginHost.h"

namespace n2m
{
juce::AudioPluginFormatManager& pluginFormats()
{
    static juce::AudioPluginFormatManager m;
    static bool ready = false;
    if (! ready) { ready = true; m.addFormat (new juce::VST3PluginFormat()); }
    return m;
}

static juce::File recentFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Back2You").getChildFile ("Nebula2 Modulation").getChildFile ("plugins.xml");
}

std::vector<juce::PluginDescription> recentPlugins()
{
    std::vector<juce::PluginDescription> out;
    if (auto xml = juce::parseXML (recentFile()))
        for (auto* e : xml->getChildIterator())
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*e)) out.push_back (d);
        }
    return out;
}

void rememberPlugin (const juce::PluginDescription& d)
{
    auto list = recentPlugins();
    list.erase (std::remove_if (list.begin(), list.end(), [&d] (const juce::PluginDescription& o) { return o.isDuplicateOf (d); }), list.end());
    list.insert (list.begin(), d);
    if (list.size() > 40) list.resize (40);
    juce::XmlElement root ("PLUGINS");
    for (auto& p : list) root.addChildElement (p.createXml().release());
    recentFile().getParentDirectory().createDirectory();
    root.writeTo (recentFile());
}

std::vector<std::pair<juce::String, juce::String>> installedPlugins (bool lookAgain)
{
    static std::vector<std::pair<juce::String, juce::String>> list;
    static bool looked = false;
    if (looked && ! lookAgain) return list;
    looked = true;
    list.clear();
    juce::VST3PluginFormat format;
    for (auto& path : format.searchPathsForPlugins (format.getDefaultLocationsToSearch(), true, false))
    {
        const auto name = juce::File (path).getFileNameWithoutExtension();
        if (name == "Nebula2 Modulation") continue;                  // not inside itself
        bool have = false;
        for (auto& e : list) have = have || e.first == name;
        if (! have) list.push_back ({ name, path });
    }
    std::sort (list.begin(), list.end(), [] (const auto& a, const auto& b) { return a.first.compareIgnoreCase (b.first) < 0; });
    return list;
}

std::vector<juce::PluginDescription> pluginsInFile (const juce::String& path)
{
    std::vector<juce::PluginDescription> out;
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : pluginFormats().getFormats())
        if (f->fileMightContainThisPluginType (path)) f->findAllTypesForFile (found, path);
    for (auto* d : found) out.push_back (*d);
    return out;
}

static std::vector<std::pair<void*, std::function<void (HostedModule*)>>>& closingListeners()
{
    static std::vector<std::pair<void*, std::function<void (HostedModule*)>>> v;
    return v;
}
void addClosingListener (void* owner, std::function<void (HostedModule*)> f) { closingListeners().push_back ({ owner, std::move (f) }); }
void removeClosingListener (void* owner)
{
    auto& v = closingListeners();
    v.erase (std::remove_if (v.begin(), v.end(), [owner] (const auto& e) { return e.first == owner; }), v.end());
}
static void tellClosing (HostedModule* m)
{
    const auto copy = closingListeners();
    for (auto& e : copy) e.second (m);
}

//==============================================================================
const Info& HostedModule::desc()
{
    static const Info i { "plugin", "Plugin slot", "Your plugins", "Holds one of your VST3 plugins",
        { { "p1", "Note 1" }, { "p2", "Note 2" }, { "p3", "Note 3" }, { "p4", "Note 4" }, { "gate", "Gate" }, { "vel", "Vel" }, { "inl", "In L" }, { "inr", "In R" },
          { "m1", "Mod 1" }, { "m2", "Mod 2" }, { "m3", "Mod 3" }, { "m4", "Mod 4" } },
        { { "outl", "Out L" }, { "outr", "Out R" },
          { "nt1", "Note 1" }, { "nt2", "Note 2" }, { "nt3", "Note 3" }, { "nt4", "Note 4" }, { "ngate", "Gate" } },
        { { "level", "Level", 0, 1.5f, 1.0f, Fmt::Percent },
          { "k1", "Knob 1", 0, 1, 0.5f, Fmt::Percent }, { "k2", "Knob 2", 0, 1, 0.5f, Fmt::Percent }, { "k3", "Knob 3", 0, 1, 0.5f, Fmt::Percent }, { "k4", "Knob 4", 0, 1, 0.5f, Fmt::Percent } }, Seq::Host };
    return i;
}

HostedModule::HostedModule() : alive (std::make_shared<bool> (true))
{
    buffer.setSize (2, kBlock);
    for (auto& t : target) t.store (-1);
    for (int k = 0; k < kKnobs; ++k) knobIds.add ({});
}
HostedModule::~HostedModule() { *alive = false; unload(); }

void HostedModule::prepare (double sampleRate)
{
    sr = sampleRate;
    if (plugin == nullptr) return;
    Hold h (guard);
    plugin->releaseResources();
    plugin->setRateAndBufferSizeDetails (sr, kBlock);
    plugin->prepareToPlay (sr, kBlock);
}

juce::String HostedModule::load (const juce::PluginDescription& d, const juce::MemoryBlock* state, bool keepKnobs)
{
    juce::String error;
    auto fresh = pluginFormats().createPluginInstance (d, sr, kBlock, error);
    if (fresh == nullptr)
    {
        status = error.isNotEmpty() ? error : "Couldn't load " + d.name;
        description = d;                              // kept, so the slot still saves which plugin it wanted
        return status;
    }
    fresh->setPlayHead (this);
    fresh->setRateAndBufferSizeDetails (sr, kBlock);
    fresh->prepareToPlay (sr, kBlock);
    if (state != nullptr && state->getSize() > 0) fresh->setStateInformation (state->getData(), (int) state->getSize());
    const int in = juce::jmin (2, fresh->getTotalNumInputChannels()), out = juce::jmax (1, fresh->getTotalNumOutputChannels());
    juce::AudioBuffer<float> room (juce::jmax (2, fresh->getTotalNumInputChannels(), fresh->getTotalNumOutputChannels()), kBlock);
    room.clear();
    tellClosing (this);
    // Which of the new plugin's controls the knobs point at: the saved ones when a project opens, none otherwise
    int found[kKnobs];
    for (int k = 0; k < kKnobs; ++k)
    {
        found[k] = -1;
        if (! keepKnobs) knobIds.set (k, {});
        else if (knobIds[k].isNotEmpty())
            for (int i = 0; i < fresh->getParameters().size() && found[k] < 0; ++i)
                if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*> (fresh->getParameters().getUnchecked (i)))
                    if (hp->getParameterID() == knobIds[k]) found[k] = i;
    }
    for (auto* p : fresh->getParameters()) p->addListener (this);
    {
        Hold h (guard);
        plugin.swap (fresh);
        buffer = std::move (room);
        numIn = in; numOut = out;
        notes = NoteTracker();
        for (int k = 0; k < kKnobs; ++k) { target[k].store (found[k]); sent[k] = -1; }
    }
    touched.store (-1);
    description = d;
    status.clear();
    rememberPlugin (d);
    return {};                                        // the old plugin, if any, is deleted here as "fresh" goes
}

void HostedModule::unload()
{
    if (plugin == nullptr) return;
    tellClosing (this);
    std::unique_ptr<juce::AudioPluginInstance> gone;
    { Hold h (guard); gone.swap (plugin); for (auto& t : target) t.store (-1); }
    for (int k = 0; k < kKnobs; ++k) knobIds.set (k, {});
    touched.store (-1);
}

juce::StringArray HostedModule::presetNames() const
{
    juce::StringArray names;
    if (plugin == nullptr || plugin->getNumPrograms() < 2) return names;
    for (int i = 0; i < plugin->getNumPrograms() && i < 2000; ++i)
    {
        const auto n = plugin->getProgramName (i);
        names.add (n.isNotEmpty() ? n : "Preset " + juce::String (i + 1));
    }
    return names;
}
int HostedModule::currentPreset() const { return plugin != nullptr ? plugin->getCurrentProgram() : -1; }
bool HostedModule::stepPreset (int step)
{
    const int next = presetAfter (currentPreset(), presetNames().size(), step);
    if (next < 0) return false;
    choosePreset (next);
    return true;
}
void HostedModule::choosePreset (int index)
{
    if (plugin == nullptr || index < 0 || index >= plugin->getNumPrograms()) return;
    plugin->setCurrentProgram (index);
    for (auto& s : sent) s = -1;          // the preset moved the controls: the knobs must not push them straight back
}

int HostedModule::numControls() const { return plugin != nullptr ? plugin->getParameters().size() : 0; }
juce::String HostedModule::controlName (int index) const
{
    if (index < 0 || index >= numControls()) return {};
    const auto name = plugin->getParameters().getUnchecked (index)->getName (40);
    return name.isNotEmpty() ? name : "Control " + juce::String (index + 1);
}
float HostedModule::controlValue (int index) const { return index >= 0 && index < numControls() ? plugin->getParameters().getUnchecked (index)->getValue() : 0.0f; }
juce::String HostedModule::controlId (int index) const
{
    if (index < 0 || index >= numControls()) return {};
    if (auto* hp = dynamic_cast<juce::HostedAudioProcessorParameter*> (plugin->getParameters().getUnchecked (index))) return hp->getParameterID();
    return {};
}
void HostedModule::assignKnob (int knob, int index)
{
    if (knob < 0 || knob >= kKnobs) return;
    if (index < 0 || index >= numControls()) index = -1;
    if (index >= 0) params[(size_t) (1 + knob)].store (juce::jlimit (0.0f, 1.0f, controlValue (index)));
    knobIds.set (knob, controlId (index));
    Hold h (guard);
    target[knob].store (index); sent[knob] = -1;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> HostedModule::getPosition() const
{
    PositionInfo p;
    p.setBpm (posBpm);
    p.setPpqPosition (posPpq);
    p.setIsPlaying (posPlaying);
    p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    p.setPpqPositionOfLastBarStart (std::floor (posPpq / 4.0) * 4.0);
    p.setTimeInSeconds (posPpq * 60.0 / juce::jmax (1.0, posBpm));
    p.setTimeInSamples ((juce::int64) (posPpq * 60.0 / juce::jmax (1.0, posBpm) * sr));
    return p;
}

void HostedModule::process (int n, const Ctx& c)
{
    auto silence = [this, n]
    {
        std::fill (out[0].begin(), out[0].begin() + n, 0.0f); std::fill (out[1].begin(), out[1].begin() + n, 0.0f);
        for (int k = 0; k < 4; ++k) std::fill (out[(size_t) (2 + k)].begin(), out[(size_t) (2 + k)].begin() + n, noteOut[k]);
        std::fill (out[6].begin(), out[6].begin() + n, 0.0f);
        heldCount = 0;
    };
    if (! guard.tryLock()) { silence(); return; }
    if (plugin == nullptr) { guard.unlock(); silence(); return; }

    // The plugin inside follows the same tempo and song position as everything else
    posBpm = c.bpm; posPpq = c.ppq; posPlaying = c.playing;

    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), n);
    view.clear();
    if (numIn > 0)
    {
        view.copyFrom (0, 0, in[6].data(), n);
        if (numIn > 1) view.copyFrom (1, 0, (inOn[7] ? in[7] : in[6]).data(), n);
    }

    // Notes from the cables
    midi.clear();
    const char plugged[4] = { inOn[0], inOn[1], inOn[2], inOn[3] };
    for (int i = 0; i < n; ++i)
    {
        const float pitch[4] = { in[0][(size_t) i], in[1][(size_t) i], in[2][(size_t) i], in[3][(size_t) i] };
        const int vel = inOn[5] ? (int) std::lround (in[5][(size_t) i] * 127.0f) : 100;
        notes.step (inOn[4] && in[4][(size_t) i] > 0.5f, pitch, plugged, vel, [this, i] (int note, int velocity)
        {
            midi.addEvent (velocity > 0 ? juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity) : juce::MidiMessage::noteOff (1, note), i);
            if (velocity > 0) notesSent.fetch_add (1, std::memory_order_relaxed);
        });
    }

    // Knobs pointed at the plugin's own controls. A control is only told when its knob (plus anything cabled into
    // its Mod input) has moved, so changes made in the plugin's own window are left alone until then.
    for (int k = 0; k < kKnobs; ++k)
    {
        const int t = target[k].load (std::memory_order_relaxed);
        if (t < 0 || t >= plugin->getParameters().size()) continue;
        const float v = juce::jlimit (0.0f, 1.0f, P (1 + k) + (inOn[(size_t) (8 + k)] ? in[(size_t) (8 + k)][0] : 0.0f));
        if (sent[k] < 0) sent[k] = v;
        else if (std::abs (v - sent[k]) > 1.0e-4f) { plugin->getParameters().getUnchecked (t)->setValue (v); sent[k] = v; }
    }

    plugin->processBlock (view, midi);

    // Notes the plugin itself sends out (Nebula2 Chord's chords, an arpeggiator's notes) become cable signals:
    // the notes held, lowest first, and a gate that dips for a sample on each new note so envelopes restart
    {
        auto it = midi.begin();
        for (int i = 0; i < n; ++i)
        {
            bool fresh = false;
            while (it != midi.end() && (*it).samplePosition <= i)
            {
                const auto m = (*it).getMessage();
                if (m.isNoteOn() || m.isNoteOff())
                {
                    const int note = m.getNoteNumber();
                    int w = 0;
                    for (int k = 0; k < heldCount; ++k) if (heldNotes[k] != note) heldNotes[w++] = heldNotes[k];
                    heldCount = w;
                    if (m.isNoteOn() && heldCount < 16) { heldNotes[heldCount++] = note; fresh = true; }
                }
                else if (m.isAllNotesOff() || m.isAllSoundOff()) heldCount = 0;
                ++it;
            }
            if (heldCount > 0)
            {
                int sorted[16];
                std::copy (heldNotes, heldNotes + heldCount, sorted);
                std::sort (sorted, sorted + heldCount);
                for (int k = 0; k < 4; ++k) noteOut[k] = (float) (sorted[juce::jmin (k, heldCount - 1)] - 60) / 12.0f;
            }
            for (int k = 0; k < 4; ++k) out[(size_t) (2 + k)][(size_t) i] = noteOut[k];
            out[6][(size_t) i] = heldCount > 0 && ! (fresh && gateWas) ? 1.0f : 0.0f;
            gateWas = heldCount > 0;
        }
    }

    const float level = P (0);
    const float* l = view.getReadPointer (0);
    const float* r = view.getReadPointer (numOut > 1 ? 1 : 0);
    for (int i = 0; i < n; ++i)
    {
        const float a = l[i] * level, b = r[i] * level;
        out[0][(size_t) i] = std::isfinite (a) ? a : 0.0f;       // a misbehaving plugin can't poison the rack
        out[1][(size_t) i] = std::isfinite (b) ? b : 0.0f;
    }
    guard.unlock();
}

// Saved as two pieces of text: which plugin it is, and that plugin's own settings (its preset and knobs)
std::string HostedModule::saveExtra() const
{
    if (description.name.isEmpty()) return {};
    juce::MemoryBlock state;
    if (plugin != nullptr) plugin->getStateInformation (state);
    const auto xml = description.createXml()->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
    const auto stateText = state.getSize() > 0 ? juce::Base64::toBase64 (state.getData(), state.getSize()) : juce::String ("-");
    return (juce::Base64::toBase64 (xml) + " " + stateText + " " + juce::Base64::toBase64 (knobIds.joinIntoString ("\n"))).toStdString();
}

void HostedModule::loadExtra (const std::string& text)
{
    const auto parts = juce::StringArray::fromTokens (juce::String (text), " ", "");
    if (parts.isEmpty()) return;
    juce::MemoryOutputStream xmlBytes, stateBytes;
    if (! juce::Base64::convertFromBase64 (xmlBytes, parts[0])) return;
    if (parts.size() > 1 && parts[1] != "-") juce::Base64::convertFromBase64 (stateBytes, parts[1]);
    if (parts.size() > 2)                             // saves from before v0.1.11 have no knobs
    {
        juce::MemoryOutputStream ids;
        if (juce::Base64::convertFromBase64 (ids, parts[2]))
        {
            auto list = juce::StringArray::fromLines (ids.toString());
            for (int k = 0; k < kKnobs; ++k) knobIds.set (k, k < list.size() ? list[k] : juce::String());
        }
    }
    juce::PluginDescription d;
    if (auto xml = juce::parseXML (xmlBytes.toString())) { if (! d.loadFromXml (*xml)) return; } else return;
    juce::MemoryBlock state (stateBytes.getData(), stateBytes.getDataSize());

    // Plugins must be created on the message thread. If the project is being loaded from another thread, do it shortly after.
    if (juce::MessageManager::existsAndIsCurrentThread()) { load (d, &state, true); return; }
    description = d;
    status = "Loading " + d.name;
    juce::MessageManager::callAsync ([this, d, state, flag = alive] { if (*flag) load (d, &state, true); });
}

void registerPluginSlot()
{
    for (auto& e : extraTypes()) if (std::string (e.info->type) == "plugin") return;
    extraTypes().push_back ({ &HostedModule::desc(), [] { return std::unique_ptr<Module> (new HostedModule()); } });
}
} // namespace n2m
