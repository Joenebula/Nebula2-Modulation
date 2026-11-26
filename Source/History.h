#pragma once
// Nebula2 Modulation: undo and redo. Plain C++, no JUCE.
//
// The screen calls one of the "before" functions just before it changes something. Undo puts that back.
// A board is put back in place (see Rack::applyBoard), so modules that didn't change keep running:
// undoing a knob never reloads a plugin sitting in a slot on the same board.
#include "Engine.h"

namespace n2m
{
class History
{
public:
    explicit History (Rack& r) : rack (r) {}

    // Before changing anything on a board (knob, pattern, cable, adding or moving a unit, its mixer channel).
    // removing: the id of a unit about to be deleted, so its full state is kept and it can come back.
    // If the board has linked knobs or patterns, the other tabs holding the rest of those groups are kept too,
    // because the change will reach them.
    void before (const Board& board, int removing = 0)
    {
        Entry e; e.kind = Kind::Boards;
        const auto wantExtra = std::function<bool (int)> ([removing] (int uid) { return uid == removing; });
        const auto none = std::function<bool (int)> ([] (int) { return false; });
        std::vector<int> groups, patternGroups;
        for (auto& m : board.modules)
        {
            for (int g : m->paramLink) if (g > 0) groups.push_back (g);
            if (m->patternLink > 0) patternGroups.push_back (m->patternLink);
        }
        for (size_t i = 0; i < rack.boards.size(); ++i)
        {
            auto& b = *rack.boards[i];
            bool keep = &b == &board;
            if (! keep)
                for (auto& m : b.modules)
                {
                    for (int g : m->paramLink) keep = keep || (g > 0 && std::find (groups.begin(), groups.end(), g) != groups.end());
                    keep = keep || (m->patternLink > 0 && std::find (patternGroups.begin(), patternGroups.end(), m->patternLink) != patternGroups.end());
                }
            if (keep) e.boards.push_back ({ (int) i, Rack::boardText (b, &b == &board ? &wantExtra : &none) });
        }
        push (std::move (e));
    }

    // Before changing the chords or the key
    void beforeChords() { push (chordsNow()); }

    // After a tab has been added at this position
    void tabAdded (int index) { Entry e; e.kind = Kind::TabAdded; e.a = index; push (std::move (e)); }
    // Just before the tab at this position is deleted
    void tabRemoving (int index)
    {
        if (index < 0 || index >= (int) rack.boards.size()) return;
        Entry e; e.kind = Kind::TabRemoved; e.a = index; e.text = Rack::boardText (*rack.boards[(size_t) index]);
        push (std::move (e));
    }
    // After a tab has moved
    void tabMoved (int from, int to) { Entry e; e.kind = Kind::TabMoved; e.a = from; e.b = to; push (std::move (e)); }

    // Drops the last "before" if nothing came of it: a knob clicked but not turned, a cable picked up and put back
    void discardIfUnchanged()
    {
        if (undos.empty() || undos.back().kind != Kind::Boards) return;
        const auto none = std::function<bool (int)> ([] (int) { return false; });
        for (auto& p : undos.back().boards)
        {
            if (p.first < 0 || p.first >= (int) rack.boards.size()) return;
            if (Rack::boardText (*rack.boards[(size_t) p.first], &none) != p.second) return;
        }
        undos.pop_back();
    }

    bool canUndo() const { return ! undos.empty(); }
    bool canRedo() const { return ! redos.empty(); }
    bool undo() { return step (undos, redos); }
    bool redo() { return step (redos, undos); }
    void clear() { undos.clear(); redos.clear(); }

    int showTab = -1;          // after an undo or redo: the tab it happened on, for the screen to show

private:
    enum class Kind { Boards, Chords, TabAdded, TabRemoved, TabMoved };
    struct Entry
    {
        Kind kind = Kind::Boards;
        std::vector<std::pair<int, std::string>> boards;
        int a = 0, b = 0;
        std::string text;
        Harmony harmony; int root = 0, scale = 0;
    };

    Entry chordsNow() const { Entry e; e.kind = Kind::Chords; e.harmony = rack.harmony; e.root = rack.root.load(); e.scale = rack.scale.load(); return e; }

    void push (Entry e)
    {
        undos.push_back (std::move (e));
        if (undos.size() > 100) undos.erase (undos.begin());
        redos.clear();
    }

    // Puts an entry back and returns the entry that would reverse it
    Entry apply (const Entry& e)
    {
        Entry back;
        switch (e.kind)
        {
            case Kind::Boards:
            {
                back.kind = Kind::Boards;
                for (auto& p : e.boards)
                {
                    if (p.first < 0 || p.first >= (int) rack.boards.size()) continue;
                    // Units that are about to disappear keep their full state, so the reverse can bring them back
                    const auto staying = Rack::uidsIn (p.second);
                    const auto wantExtra = std::function<bool (int)> ([&staying] (int uid) { return std::find (staying.begin(), staying.end(), uid) == staying.end(); });
                    back.boards.push_back ({ p.first, Rack::boardText (*rack.boards[(size_t) p.first], &wantExtra) });
                    rack.applyBoard (p.first, p.second);
                    showTab = p.first;
                }
                if (! e.boards.empty()) showTab = e.boards[0].first;
                break;
            }
            case Kind::Chords:
                back = chordsNow();
                rack.harmony = e.harmony;
                rack.setKey (e.root, e.scale);          // also works the chords out again as notes
                break;
            case Kind::TabAdded:                        // reverse of adding: take it away again, remembering it
                back.kind = Kind::TabRemoved; back.a = e.a;
                if (e.a >= 0 && e.a < (int) rack.boards.size())
                {
                    back.text = Rack::boardText (*rack.boards[(size_t) e.a]);
                    rack.removeBoard (e.a);
                }
                showTab = std::max (0, std::min (e.a, (int) rack.boards.size() - 1));
                break;
            case Kind::TabRemoved:                      // reverse of deleting: put it back where it was
            {
                back.kind = Kind::TabAdded; back.a = e.a;
                auto made = Rack::readBoards (e.text, rack.sr);
                if (! made.empty())
                {
                    rack.addBoard (std::move (made[0]));
                    rack.moveBoard ((int) rack.boards.size() - 1, std::min (e.a, (int) rack.boards.size() - 1));
                }
                showTab = std::min (e.a, (int) rack.boards.size() - 1);
                break;
            }
            case Kind::TabMoved:
                back.kind = Kind::TabMoved; back.a = e.b; back.b = e.a;
                rack.moveBoard (e.b, e.a);
                showTab = e.a;
                break;
        }
        return back;
    }

    bool step (std::vector<Entry>& from, std::vector<Entry>& to)
    {
        if (from.empty()) return false;
        Entry e = std::move (from.back());
        from.pop_back();
        to.push_back (apply (e));
        return true;
    }

    Rack& rack;
    std::vector<Entry> undos, redos;
};
} // namespace n2m
