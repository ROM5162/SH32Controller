#pragma once
// Données d'un pattern (mono : 16 pas ; poly : liste de notes avec longueur) et sérialisation texte.
// Tailles fixes : la structure est copiable sans allocation (utile côté thread audio).
// Fichier indépendant de JUCE.

#include <array>
#include <cstdlib>
#include <string>
#include <vector>

namespace sh32
{
constexpr int kSteps    = 16;
constexpr int kMaxNotes = 256;
constexpr int kSlots    = 8;
constexpr int kNoteMin  = 36;   // plage d'édition du mode mono (C1..C4, comme la version web)
constexpr int kNoteMax  = 84;

struct Step
{
    bool on  = false;
    int  note = 48;
    bool acc = false;
    bool tie = false;
    int  lv  = 64;       // valeur de la lane, 0..127
};

struct PNote
{
    int  s = 0;          // pas de départ 0..15
    int  n = 60;         // note MIDI
    int  l = 1;          // longueur en pas
    bool a = false;      // accent
};

struct Pattern
{
    std::array<Step, kSteps>   steps {};
    std::array<PNote, kMaxNotes> notes {};
    int numNotes = 0;

    void clearMono() { for (auto& s : steps) s = Step {}; }
    void clearPoly() { numNotes = 0; }

    int findNote (int col, int note) const
    {
        for (int i = 0; i < numNotes; ++i)
            if (notes[(size_t) i].n == note && col >= notes[(size_t) i].s && col < notes[(size_t) i].s + notes[(size_t) i].l)
                return i;
        return -1;
    }

    bool overlaps (int skip, int s, int n, int l) const
    {
        for (int i = 0; i < numNotes; ++i)
        {
            if (i == skip) continue;
            const auto& p = notes[(size_t) i];
            if (p.n == n && p.s < s + l && s < p.s + p.l) return true;
        }
        return false;
    }

    int addNote (int s, int n, int l, bool a)
    {
        if (numNotes >= kMaxNotes) return -1;
        notes[(size_t) numNotes] = { s, n, l, a };
        return numNotes++;
    }

    void removeNote (int idx)
    {
        if (idx < 0 || idx >= numNotes) return;
        for (int i = idx; i < numNotes - 1; ++i) notes[(size_t) i] = notes[(size_t) i + 1];
        --numNotes;
    }
};

struct PatternBank
{
    std::array<Pattern, kSlots> p {};
};

inline Pattern makeDemoPattern()
{
    static const int notes[16] = { 48,48,60,48,51,48,58,55,48,48,60,48,51,48,63,62 };
    static const int on[16]    = { 1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1 };
    static const int acc[16]   = { 1,0,0,0,0,0,1,0,1,0,0,0,0,0,1,0 };
    Pattern p;
    for (int i = 0; i < kSteps; ++i)
    {
        p.steps[(size_t) i].on   = on[i] != 0;
        p.steps[(size_t) i].note = notes[i];
        p.steps[(size_t) i].acc  = acc[i] != 0;
        p.steps[(size_t) i].lv   = 40 + (i * 5 % 8) * 11;
    }
    return p;
}

// ---------------------------------------------------------------------------------------------
// Sérialisation : "steps|notes" ; steps = "on,note,acc,tie,lv;..." ; notes = "s,n,l,a;..."
// ---------------------------------------------------------------------------------------------
inline std::vector<std::string> splitStr (const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == sep) { out.push_back (cur); cur.clear(); }
        else cur.push_back (c);
    }
    out.push_back (cur);
    return out;
}

inline int clampInt (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline std::string serialize (const Pattern& p)
{
    std::string r;
    for (int i = 0; i < kSteps; ++i)
    {
        const auto& s = p.steps[(size_t) i];
        r += std::to_string (s.on ? 1 : 0) + "," + std::to_string (s.note) + "," + std::to_string (s.acc ? 1 : 0)
           + "," + std::to_string (s.tie ? 1 : 0) + "," + std::to_string (s.lv) + ";";
    }
    r += "|";
    for (int i = 0; i < p.numNotes; ++i)
    {
        const auto& n = p.notes[(size_t) i];
        r += std::to_string (n.s) + "," + std::to_string (n.n) + "," + std::to_string (n.l) + "," + std::to_string (n.a ? 1 : 0) + ";";
    }
    return r;
}

inline bool deserialize (Pattern& p, const std::string& text)
{
    const auto halves = splitStr (text, '|');
    if (halves.size() != 2) return false;

    Pattern q;
    const auto steps = splitStr (halves[0], ';');
    for (int i = 0; i < kSteps && i < (int) steps.size(); ++i)
    {
        const auto f = splitStr (steps[(size_t) i], ',');
        if (f.size() != 5) return false;
        auto& s = q.steps[(size_t) i];
        s.on   = std::atoi (f[0].c_str()) != 0;
        s.note = clampInt (std::atoi (f[1].c_str()), 0, 127);
        s.acc  = std::atoi (f[2].c_str()) != 0;
        s.tie  = std::atoi (f[3].c_str()) != 0;
        s.lv   = clampInt (std::atoi (f[4].c_str()), 0, 127);
    }
    for (const auto& item : splitStr (halves[1], ';'))
    {
        if (item.empty()) continue;
        const auto f = splitStr (item, ',');
        if (f.size() != 4) return false;
        const int s = clampInt (std::atoi (f[0].c_str()), 0, kSteps - 1);
        const int n = clampInt (std::atoi (f[1].c_str()), 0, 127);
        const int l = clampInt (std::atoi (f[2].c_str()), 1, kSteps - s);
        q.addNote (s, n, l, std::atoi (f[3].c_str()) != 0);
    }
    p = q;
    return true;
}
} // namespace sh32
