#pragma once
// Nebula2 Modulation: styles. A style is a recipe for a whole track: which sounds, which presets, which kinds
// of pattern and which mood of chords. Make track builds every tab from it in one go, cabled and playing.
// Everything is made from the starting sounds, presets and pattern Ideas in Presets.h. No JUCE.
#include "Presets.h"

namespace n2m
{
// One sound of a style. kind: one of boardTypes(). presets: names from soundPresets() for that kind (one is
// picked). ideas: positions in that kind's Ideas list (one is picked); empty = made up in the style of the sound.
// chance: how often the part is in the track at all (1 = always).
struct StylePart
{
    const char* kind; const char* name;
    std::vector<const char*> presets;
    std::vector<int> ideas;
    float chance = 1.0f;
};
struct Style { const char* id; const char* name; const char* mood; std::vector<StylePart> parts; };

inline const std::vector<Style>& styles()
{
    static const std::vector<Style> s {
        { "techno", "Techno", "techno", {
            { "kick", "Kick", { "Punchy", "Hard" }, { 0, 1, 9 } },
            { "clap", "Clap", { "Tight", "Dark" }, { 0, 2 } },
            { "hat", "Hi-hat", { "Closed", "Tick" }, { 2, 8, 11 } },
            { "hat", "Open hat", { "Open" }, { 0, 4 } },
            { "perc", "Percussion", { "Blip", "Zap", "Woodblock" }, { 0, 3, 9 } },
            { "bass", "Bass", { "Sub", "Standard" }, { 1, 8, 0 } },
            { "stab", "Stab", { "Short", "Standard" }, {}, 0.7f },
            { "pad", "Pad", { "Dark" }, {}, 0.5f } } },
        { "hardtechno", "Hard techno", "techno", {
            { "kick", "Kick", { "Hard" }, { 0, 7, 9 } },
            { "clap", "Clap", { "Dark", "Tight" }, { 0 } },
            { "hat", "Hi-hat", { "Tick" }, { 11, 2 } },
            { "hat", "Open hat", { "Open" }, { 0 } },
            { "perc", "Percussion", { "Zap", "Tom" }, { 3, 0 } },
            { "bass", "Bass", { "Growl" }, { 1, 8 } },
            { "lead", "Lead", { "Hollow" }, { 4, 10 }, 0.6f } } },
        { "acid", "Acid", "dark", {
            { "kick", "Kick", { "Punchy" }, { 0, 1 } },
            { "clap", "Clap", { "Standard" }, { 0, 1 } },
            { "hat", "Hi-hat", { "Closed" }, { 0, 6 } },
            { "hat", "Open hat", { "Open" }, { 0 }, 0.6f },
            { "bass", "Acid bass", { "Acid" }, { 5, 12, 2 } },
            { "lead", "Lead", { "Bright" }, { 10, 1 }, 0.5f },
            { "pad", "Pad", { "Dark" }, {}, 0.4f } } },
        { "techhouse", "Tech house", "emotional", {
            { "kick", "Kick", { "Punchy", "Standard" }, { 0, 2 } },
            { "clap", "Clap", { "Tight" }, { 0 } },
            { "hat", "Hi-hat", { "Closed" }, { 0, 4, 3 } },
            { "hat", "Shaker", { "Shaker" }, { 2, 9 } },
            { "perc", "Percussion", { "Bongo", "Woodblock" }, { 1, 6, 10 } },
            { "bass", "Bass", { "Pluck", "Standard" }, { 4, 7, 11 } },
            { "stab", "Stab", { "Short" }, {}, 0.7f } } },
        { "house", "House", "emotional", {
            { "kick", "Kick", { "Standard" }, { 0 } },
            { "clap", "Clap", { "Standard", "Big room" }, { 0 } },
            { "hat", "Hi-hat", { "Closed" }, { 0, 1 } },
            { "hat", "Open hat", { "Open" }, { 0 } },
            { "bass", "Bass", { "Standard", "Square" }, { 0, 7, 4 } },
            { "stab", "Stab", { "Standard", "Bright" }, {} },
            { "pad", "Pad", { "Standard", "Lush" }, {}, 0.7f } } },
        { "deephouse", "Deep house", "deep", {
            { "kick", "Kick", { "Deep" }, { 0 } },
            { "clap", "Clap", { "Dark" }, { 0 } },
            { "hat", "Shaker", { "Shaker" }, { 3, 1 } },
            { "bass", "Bass", { "Sub" }, { 10, 13, 0 } },
            { "pad", "Pad", { "Lush", "Dark" }, {} },
            { "stab", "Stab", { "Standard" }, {}, 0.6f },
            { "lead", "Lead", { "Soft" }, { 13, 3 }, 0.4f } } },
        { "melodic", "Melodic", "sad", {
            { "kick", "Kick", { "Deep", "Standard" }, { 0 } },
            { "clap", "Clap", { "Big room" }, { 0 } },
            { "hat", "Hi-hat", { "Closed" }, { 0, 4 } },
            { "bass", "Bass", { "Standard" }, { 1, 0 } },
            { "arp", "Arp", { "Soft", "Standard" }, {} },
            { "pad", "Pad", { "Lush", "Slow swell" }, {} },
            { "lead", "Lead", { "Soft" }, { 13, 7, 2 } },
            { "strings", "Strings", { "Standard", "Dark" }, {}, 0.4f } } },
        { "trance", "Trance", "dramatic", {
            { "kick", "Kick", { "Punchy" }, { 0 } },
            { "clap", "Clap", { "Big room" }, { 0 } },
            { "hat", "Open hat", { "Open" }, { 0 } },
            { "hat", "Hi-hat", { "Closed" }, { 2 } },
            { "bass", "Bass", { "Standard" }, { 1, 8 } },
            { "lead", "Lead", { "Bright", "Wide" }, { 4, 0, 1 } },
            { "arp", "Arp", { "Bright" }, {} },
            { "pad", "Pad", { "Bright", "Lush" }, {} },
            { "strings", "Strings", { "Huge" }, {}, 0.6f } } },
        { "dub", "Dub", "deep", {
            { "kick", "Kick", { "Deep", "Long boom" }, { 0 } },
            { "snare", "Rim", { "Rim" }, { 0, 7 } },
            { "hat", "Hi-hat", { "Closed" }, { 0, 8 } },
            { "bass", "Bass", { "Sub" }, { 10, 13 } },
            { "stab", "Dub chord", { "Big" }, {} },
            { "pad", "Pad", { "Dark" }, {} } } },
        { "electro", "Electro", "eerie", {
            { "kick", "Kick", { "Punchy" }, { 3, 10, 6 } },
            { "snare", "Snare", { "Short", "Bright" }, { 0, 1 } },
            { "hat", "Hi-hat", { "Closed" }, { 1, 2, 5 } },
            { "perc", "Percussion", { "Zap", "Blip" }, { 2, 8 } },
            { "bass", "Bass", { "Square" }, { 4, 11, 9 } },
            { "lead", "Lead", { "Hollow" }, { 2, 9 } } } },
        { "dnb", "Drum & bass", "dark", {
            { "kick", "Kick", { "Punchy" }, { 11, 6 } },
            { "snare", "Snare", { "Bright" }, { 0, 4 } },
            { "hat", "Hi-hat", { "Closed" }, { 1, 8 } },
            { "perc", "Percussion", { "Tom" }, { 2, 5 }, 0.5f },
            { "bass", "Bass", { "Sub", "Growl" }, { 13, 10, 3 } },
            { "pad", "Pad", { "Dark", "Slow swell" }, {} },
            { "lead", "Lead", { "Soft" }, { 13 }, 0.3f } } },
        { "garage", "Garage", "emotional", {
            { "kick", "Kick", { "Standard" }, { 12, 6 } },
            { "snare", "Snare", { "Rim", "Short" }, { 10, 0 } },
            { "hat", "Hi-hat", { "Closed" }, { 3, 5 } },
            { "perc", "Percussion", { "Woodblock" }, { 9 } },
            { "bass", "Bass", { "Square", "Sub" }, { 11, 3 } },
            { "stab", "Stab", { "Short", "Bright" }, {} },
            { "pad", "Pad", { "Standard" }, {}, 0.5f } } },
        { "minimal", "Minimal", "eerie", {
            { "kick", "Kick", { "Tight click", "Standard" }, { 0 } },
            { "hat", "Hi-hat", { "Tick" }, { 8, 0 } },
            { "perc", "Percussion", { "Blip", "Woodblock" }, { 5, 9, 4 } },
            { "perc", "Percussion 2", { "Bongo" }, { 2, 8 } },
            { "bass", "Bass", { "Pluck" }, { 4, 0, 13 } },
            { "stab", "Stab", { "Short" }, {}, 0.4f } } },
        { "ambient", "Ambient", "sad", {
            { "pad", "Pad", { "Lush", "Slow swell" }, {} },
            { "strings", "Strings", { "Huge", "Dark" }, {} },
            { "arp", "Arp", { "Soft" }, {} },
            { "bass", "Bass", { "Sub" }, { 13 } },
            { "lead", "Lead", { "Soft" }, { 13 }, 0.6f },
            { "perc", "Percussion", { "Blip" }, { 5 }, 0.4f } } } };
    return s;
}

inline const Style& styleById (const std::string& id)
{
    for (auto& s : styles()) if (id == s.id) return s;
    return styles()[0];
}

// How loud each kind of sound starts on the mixing desk, so a new track is roughly balanced
inline float startLevel (const std::string& kind, const std::string& name)
{
    if (name == "Open hat" || name == "Shaker") return 0.32f;
    if (kind == "kick") return 0.8f;
    if (kind == "snare" || kind == "bass") return 0.6f;
    if (kind == "clap") return 0.5f;
    if (kind == "hat") return 0.42f;
    if (kind == "perc" || kind == "arp" || kind == "strings") return 0.38f;
    if (kind == "pad") return 0.48f;
    return 0.45f;                    // lead, stab
}

// The part a style uses for a kind of sound. A style with no such part still gets a plain one.
inline StylePart partFor (const Style& style, const std::string& kind)
{
    for (auto& p : style.parts) if (kind == p.kind) return p;
    StylePart plain { "empty", "Sound", {}, {} };
    for (auto& t : boardTypes()) if (kind == t.id) { plain.kind = t.id; plain.name = t.name; }
    return plain;
}

// A new pattern for a sound, in the style: one of the part's Ideas for bar 1 (or one made up in the style of
// the sound), then four bars that develop it.
inline void stylePattern (const StylePart& part, Board& b, Rand& r)
{
    const std::string kind = b.type;
    for (auto* m : patternUnits (b))
    {
        if (m->info->seq == Seq::Chord) continue;                  // the chords belong to the song
        const bool drum = m->info->seq == Seq::Drum;
        const int pick = part.ideas.empty() ? -1 : part.ideas[(size_t) r.upTo ((int) part.ideas.size())];
        if (drum)
        {
            char base[17] = "................";
            const auto& ideas = drumIdeas (kind);
            if (pick >= 0 && pick < (int) ideas.size()) std::copy (ideas[(size_t) pick].pat, ideas[(size_t) pick].pat + 16, base);
            else randomDrumBar (kind, r, base);
            evolveDrum (kind, r, base, *m);
        }
        else
        {
            char g[17] = "................";
            int d[16] {};
            const auto& ideas = noteIdeas (kind);
            if (pick >= 0 && pick < (int) ideas.size()) { std::copy (ideas[(size_t) pick].gates, ideas[(size_t) pick].gates + 16, g); std::copy (ideas[(size_t) pick].deg, ideas[(size_t) pick].deg + 16, d); }
            else randomNotesBar (kind, r, g, d);
            evolveNotes (r, g, d, *m);
        }
        m->set ("bars", 2.0f);                                     // four bars
    }
}

// One sound of a style, cabled and with its pattern
inline std::unique_ptr<Board> makeStylePart (const StylePart& part, Rand& r, double sr)
{
    std::unique_ptr<Board> b;
    if (! part.presets.empty()) b = makePreset (part.kind, part.presets[(size_t) r.upTo ((int) part.presets.size())], sr);
    if (b == nullptr) b = makeBoard (part.kind, sr);
    b->name = part.name;
    b->level.store (startLevel (b->type, b->name));
    stylePattern (part, *b, r);
    return b;
}

// Every sound of a style. The same seed always gives the same track.
inline std::vector<std::unique_ptr<Board>> makeStyleBoards (const Style& style, uint32_t seed, double sr)
{
    Rand r (seed);
    std::vector<std::unique_ptr<Board>> made;
    for (auto& part : style.parts)
    {
        const bool in = part.chance >= 1.0f || r.chance (part.chance);
        Rand own (seed + 104729u * (uint32_t) (&part - &style.parts[0] + 1));
        if (in) made.push_back (makeStylePart (part, own, sr));
    }
    return made;
}

// Make track: the whole rack becomes a new track in this style, with chords in the style's mood.
inline void makeTrack (Rack& rack, const std::string& styleId, uint32_t seed)
{
    const auto& style = styleById (styleId);
    rack.style = style.id;
    rack.replaceBoards (makeStyleBoards (style, seed, rack.sr));
    rack.setMood (style.mood);
    rack.generateChords (seed);
}

// Add: the style's sounds are added to what is already there
inline void addTrack (Rack& rack, const std::string& styleId, uint32_t seed)
{
    const auto& style = styleById (styleId);
    rack.style = style.id;
    for (auto& b : makeStyleBoards (style, seed, rack.sr)) rack.addBoard (std::move (b));
}

// Variation: every sound stays, every pattern is made again in the style
inline void varyPatterns (Rack& rack, uint32_t seed)
{
    const auto& style = styleById (rack.style);
    Hold h (rack.lock);
    int n = 0;
    for (auto& b : rack.boards)
    {
        Rand r (seed + 7919u * (uint32_t) ++n);
        // The part with this tab's name if the style has one (Open hat is not Hi-hat), else the one for its kind
        StylePart part = partFor (style, b->type);
        for (auto& p : style.parts) if (b->type == p.kind && b->name == p.name) part = p;
        stylePattern (part, *b, r);
    }
}

// Random knobs: every sound-shaping knob moves a little from where it is. Tuning, levels, lengths and
// switches are left alone, so the track stays in key, in time and balanced. amount: the most a knob can
// move, as a share of its travel.
inline bool knobCanRandomise (const Module& m, const Param& p)
{
    if (! p.choices.empty() || p.fmt == Fmt::Int || p.fmt == Fmt::Semis || p.fmt == Fmt::Cents || p.fmt == Fmt::Octaves || p.fmt == Fmt::Db) return false;
    const std::string shelf = m.info->shelf, id = p.id;
    if (shelf == "Notes and rhythm" || shelf == "Utilities") return false;
    if (m.info->seq != Seq::None) return false;
    for (const char* no : { "level", "coarse", "fine", "tune", "octave", "mix", "feedback" }) if (id == no) return false;
    return true;
}
inline void randomKnobs (Rack& rack, uint32_t seed, float amount = 0.12f)
{
    Rand r (seed);
    for (auto& b : rack.boards)
        for (auto& m : b->modules)
            for (size_t k = 0; k < m->info->params.size(); ++k)
            {
                const auto& p = m->info->params[k];
                if (! knobCanRandomise (*m, p)) continue;
                const float n = paramToNorm (p, m->params[k].load()) + (r.next() * 2.0f - 1.0f) * amount;
                m->params[k].store (paramFromNorm (p, std::min (1.0f, std::max (0.0f, n))));
            }
}
} // namespace n2m
