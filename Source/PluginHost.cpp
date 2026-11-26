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
    static const Info i { "plugin", "Plugin slot", "Your plugins", "Holds one of your VST3 plugins: an instrument or an effect",
        { { "p1", "Note 1" }, { "p2", "Note 2" }, { "p3", "Note 3" }, { "p4", "Note 4" }, { "gate", "Gate" }, { "vel", "Vel" }, { "inl", "In L" }, { "inr", "In R" } },
        { { "outl", "Out L" }, { "outr", "Out R" } },
        { { "level", "Level", 0, 1.5f, 1.0f, Fmt::Percent } }, Seq::Host };
    return i;
}

HostedModule::HostedModule() : alive (std::make_shared<bool> (true)) { buffer.setSize (2, kBlock); }
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

juce::String HostedModule::load (const juce::PluginDescription& d, const juce::MemoryBlock* state)
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
    {
        Hold h (guard);
        plugin.swap (fresh);
        buffer = std::move (room);
        numIn = in; numOut = out;
        notes = NoteTracker();
    }
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
    { Hold h (guard); gone.swap (plugin); }
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
    auto silence = [this, n] { std::fill (out[0].begin(), out[0].begin() + n, 0.0f); std::fill (out[1].begin(), out[1].begin() + n, 0.0f); };
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

    plugin->processBlock (view, midi);

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
    return (juce::Base64::toBase64 (xml) + " " + juce::Base64::toBase64 (state.getData(), state.getSize())).toStdString();
}

void HostedModule::loadExtra (const std::string& text)
{
    const auto parts = juce::StringArray::fromTokens (juce::String (text), " ", "");
    if (parts.isEmpty()) return;
    juce::MemoryOutputStream xmlBytes, stateBytes;
    if (! juce::Base64::convertFromBase64 (xmlBytes, parts[0])) return;
    if (parts.size() > 1) juce::Base64::convertFromBase64 (stateBytes, parts[1]);
    juce::PluginDescription d;
    if (auto xml = juce::parseXML (xmlBytes.toString())) { if (! d.loadFromXml (*xml)) return; } else return;
    juce::MemoryBlock state (stateBytes.getData(), stateBytes.getDataSize());

    // Plugins must be created on the message thread. If the project is being loaded from another thread, do it shortly after.
    if (juce::MessageManager::existsAndIsCurrentThread()) { load (d, &state); return; }
    description = d;
    status = "Loading " + d.name;
    juce::MessageManager::callAsync ([this, d, state, flag = alive] { if (*flag) load (d, &state); });
}

void registerPluginSlot()
{
    for (auto& e : extraTypes()) if (std::string (e.info->type) == "plugin") return;
    extraTypes().push_back ({ &HostedModule::desc(), [] { return std::unique_ptr<Module> (new HostedModule()); } });
}
} // namespace n2m
