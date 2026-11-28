#include "Sampler.h"

namespace n2m
{
static juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager m;
    static bool ready = false;
    if (! ready) { ready = true; m.registerBasicFormats(); }
    return m;
}

juce::String SampleModule::wildcard() { return formats().getWildcardForAllFormats(); }

bool SampleModule::canRead (const juce::String& path)
{
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    return ext.isNotEmpty() && wildcard().toLowerCase().contains ("*" + ext);
}

const Info& SampleModule::desc()
{
    static const Info i { "sample", "Sample player", "Sources", "Plays an audio file you load or drop on it",
        { { "trig", "Trig" }, { "pitch", "Pitch" }, { "vel", "Vel" } }, { { "outl", "Out L" }, { "outr", "Out R" } },
        { { "start", "Start", 0, 1, 0, Fmt::Percent }, { "length", "Length", 0.01f, 1, 1, Fmt::Percent },
          { "tune", "Tune", -24, 24, 0, Fmt::Semis },
          { "direction", "Play", 0, 1, 0, Fmt::Int, 1, { "Forward", "Reverse" } },
          { "loop", "Repeat", 0, 1, 0, Fmt::Int, 1, { "Once", "Loop" } },
          { "level", "Level", 0, 1.5f, 1, Fmt::Percent } }, Seq::Sample, 6 };
    return i;
}

SampleModule::SampleModule() = default;

juce::String SampleModule::load (const juce::File& f)
{
    file = f;
    if (! f.existsAsFile()) { status = "Can't find " + f.getFileName(); return status; }
    std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (f));
    if (reader == nullptr) { status = f.getFileName() + " isn't an audio file this can read"; return status; }
    // Up to ten minutes; longer files are cut there rather than filling the memory
    const int length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
    if (length < 2) { status = f.getFileName() + " is empty"; return status; }
    auto fresh = std::make_shared<juce::AudioBuffer<float>> (2, length);
    reader->read (fresh.get(), 0, length, 0, true, true);
    if (reader->numChannels < 2) fresh->copyFrom (1, 0, *fresh, 0, 0, length);      // mono: the same on both sides

    std::vector<float> shape (160, 0.0f);
    for (size_t k = 0; k < shape.size(); ++k)
    {
        const int a = (int) ((juce::int64) k * length / (juce::int64) shape.size()), b = juce::jmax (a + 1, (int) ((juce::int64) (k + 1) * length / (juce::int64) shape.size()));
        shape[k] = juce::jmax (fresh->getMagnitude (0, a, b - a), fresh->getMagnitude (1, a, b - a));
    }
    std::vector<float> detail ((size_t) juce::jlimit (160, 8192, length), 0.0f);
    for (size_t k = 0; k < detail.size(); ++k)
    {
        const int a = (int) ((juce::int64) k * length / (juce::int64) detail.size()), b = juce::jmax (a + 1, (int) ((juce::int64) (k + 1) * length / (juce::int64) detail.size()));
        detail[k] = juce::jmax (fresh->getMagnitude (0, a, b - a), fresh->getMagnitude (1, a, b - a));
    }
    {
        Hold h (guard);
        sound = fresh;
        sourceRate = reader->sampleRate;
        active = false; pos = 0;
    }
    peaks = std::move (shape);
    fine = std::move (detail);
    status.clear();
    return {};
}

void SampleModule::clear()
{
    std::shared_ptr<const juce::AudioBuffer<float>> gone;
    { Hold h (guard); gone.swap (sound); active = false; }
    peaks.clear(); fine.clear(); file = juce::File(); status.clear();
}

void SampleModule::process (int n, const Ctx&)
{
    auto silence = [this, n] { std::fill (out[0].begin(), out[0].begin() + n, 0.0f); std::fill (out[1].begin(), out[1].begin() + n, 0.0f); };
    if (! guard.tryLock()) { silence(); return; }
    if (sound == nullptr) { guard.unlock(); silence(); playhead.store (-1.0f); return; }

    const int total = sound->getNumSamples();
    // The part of the file that plays: from Start, for Length of what is left
    const double first = juce::jlimit (0.0, (double) total - 2.0, (double) P (0) * total);
    const double last = juce::jlimit (first + 1.0, (double) total - 1.0, first + (double) P (1) * (total - first));
    const bool reverse = P (3) > 0.5f, loop = P (4) > 0.5f;
    const float level = P (5);
    const float* l = sound->getReadPointer (0);
    const float* r = sound->getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        const bool g = in[0][(size_t) i] > 0.5f;
        if (g && ! lastTrig) { active = true; pos = reverse ? last : first; amp = inOn[2] ? in[2][(size_t) i] : 1.0f; }
        lastTrig = g;
        float a = 0, b = 0;
        if (active)
        {
            const int k = (int) pos; const float f = (float) (pos - k);
            const int k2 = juce::jmin (total - 1, k + 1);
            // A few samples of fade at each end of the part, so starting and stopping never click
            const float edge = (float) juce::jmin (1.0, (pos - first) / 32.0 + 0.03, (last - pos) / 64.0 + 0.02);
            const float gain = juce::jmax (0.0f, edge) * amp * level;
            a = (l[k] + (l[k2] - l[k]) * f) * gain;
            b = (r[k] + (r[k2] - r[k]) * f) * gain;
            // Pitch 0 (middle C) plays the file as recorded; an octave up plays it twice as fast
            const double rate = sourceRate / sr * std::pow (2.0, (double) P (2) / 12.0 + (double) in[1][(size_t) i]);
            pos += reverse ? -rate : rate;
            if (pos >= last || pos < first)
            {
                if (loop) { const double span = last - first; pos = reverse ? pos + span : pos - span; pos = juce::jlimit (first, last - 1.0e-6, pos); }
                else active = false;
            }
        }
        out[0][(size_t) i] = a; out[1][(size_t) i] = b;
    }
    playhead.store (active ? (float) (pos / total) : -1.0f);
    guard.unlock();
}

// Saved as where the file is. The audio itself stays in the file, so projects stay small.
std::string SampleModule::saveExtra() const
{
    if (file == juce::File()) return {};
    return juce::Base64::toBase64 (file.getFullPathName()).toStdString();
}

void SampleModule::loadExtra (const std::string& text)
{
    juce::MemoryOutputStream bytes;
    if (! juce::Base64::convertFromBase64 (bytes, juce::String (text))) return;
    const auto path = bytes.toString();
    if (path.isNotEmpty()) load (juce::File (path));
}

void registerSamplePlayer()
{
    for (auto& e : extraTypes()) if (std::string (e.info->type) == "sample") return;
    // Listed before the plugin slot, so it sits with the other sources
    extraTypes().insert (extraTypes().begin(), { &SampleModule::desc(), [] { return std::unique_ptr<Module> (new SampleModule()); } });
}
} // namespace n2m
