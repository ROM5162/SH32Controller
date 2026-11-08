#pragma once
// Table des paramètres du Roland SH-32 pilotables en Control Change.
// Reconstruite depuis la page « MIDI Transmit/Receive Setting List » (p.115) du manuel
// officiel, mode « Mod2 ». À vérifier sur l'instrument (voir README).
// Fichier indépendant de JUCE.

#include <cstring>

namespace sh32
{
enum class Kind { Knob, Choice, Toggle };

struct ParamDef
{
    const char* id;
    const char* label;
    const char* section;
    Kind kind;
    int cc;                          // Knob / Choice
    int minV, maxV, def;
    bool bipolar;                    // affiché en (valeur - 64)
    int onCC, onVal, offCC, offVal;  // Toggle
    bool semitones;                  // affiche « st »
};

constexpr ParamDef knob (const char* id, const char* label, const char* sec, int cc,
                         int mn, int mx, int def, bool bip = false, bool semi = false)
{
    return { id, label, sec, Kind::Knob, cc, mn, mx, def, bip, 0, 0, 0, 0, semi };
}

constexpr ParamDef choice (const char* id, const char* label, const char* sec, int cc, int count)
{
    return { id, label, sec, Kind::Choice, cc, 0, count - 1, 0, false, 0, 0, 0, 0, false };
}

constexpr ParamDef toggle (const char* id, const char* label, const char* sec,
                           int onCC, int onVal, int offCC, int offVal)
{
    return { id, label, sec, Kind::Toggle, onCC, 0, 1, 0, false, onCC, onVal, offCC, offVal, false };
}

inline constexpr ParamDef kParams[] =
{
    // ---- Patch
    knob ("level",   "Level",       "Patch", 7,   0, 127, 100),
    knob ("pan",     "Pan",         "Patch", 10,  0, 127, 64, true),
    knob ("porta",   "Porta Time",  "Patch", 5,   0, 127, 0),
    knob ("oscbal",  "Osc 1:2",     "Patch", 8,   1, 127, 64, true),
    toggle ("solo",    "Solo",        "Patch", 126, 1,   127, 0),
    toggle ("unison",  "Unison",      "Patch", 70,  1,   70,  0),
    toggle ("portasw", "Portamento",  "Patch", 65,  127, 65,  0),
    toggle ("legato",  "Legato",      "Patch", 68,  127, 68,  0),

    // ---- Pitch Env (Osc 1 & 2)
    knob ("penvA", "Attack", "Pitch Env", 26, 0, 127, 0),
    knob ("penvD", "Decay",  "Pitch Env", 27, 0, 127, 0),

    // ---- Oscillateur 1
    knob ("o1pe",     "Pitch Env", "Oscillateur 1", 25, 1, 127, 64, true),
    knob ("o1pwm",    "PWM Depth", "Oscillateur 1", 78, 0, 127, 0),
    knob ("o1coarse", "Coarse",    "Oscillateur 1", 21, 40, 88, 64, true, true),
    knob ("o1fine",   "Fine",      "Oscillateur 1", 77, 14, 114, 64, true),

    // ---- Oscillateur 2
    knob ("o2pe",     "Pitch Env", "Oscillateur 2", 57, 1, 127, 64, true),
    knob ("o2pwm",    "PWM Depth", "Oscillateur 2", 79, 0, 127, 0),
    knob ("o2coarse", "Coarse",    "Oscillateur 2", 53, 40, 88, 64, true, true),
    knob ("o2fine",   "Fine",      "Oscillateur 2", 76, 14, 114, 64, true),

    // ---- Filtre
    choice ("ftype",  "Type",       "Filtre", 34, 5),
    knob ("cutoff",   "Cutoff",     "Filtre", 74, 0, 127, 127),
    knob ("reso",     "Resonance",  "Filtre", 71, 0, 127, 0),
    knob ("keyf",     "Key Follow", "Filtre", 30, 44, 84, 64, true),
    knob ("fenv",     "Env Depth",  "Filtre", 81, 1, 127, 64, true),
    knob ("fA",       "Attack",     "Filtre", 82, 0, 127, 0),
    knob ("fD",       "Decay",      "Filtre", 83, 0, 127, 0),
    knob ("fS",       "Sustain",    "Filtre", 28, 0, 127, 127),
    knob ("fR",       "Release",    "Filtre", 29, 0, 127, 0),

    // ---- Ampli
    knob ("aA", "Attack",  "Ampli", 73, 0, 127, 0),
    knob ("aD", "Decay",   "Ampli", 75, 0, 127, 0),
    knob ("aS", "Sustain", "Ampli", 31, 0, 127, 127),
    knob ("aR", "Release", "Ampli", 72, 0, 127, 0),

    // ---- LFO 1
    choice ("l1w", "Wave",  "LFO 1", 15, 7),
    knob ("l1rate", "Rate",   "LFO 1", 16, 0, 127, 0),
    knob ("l1fade", "Fade",   "LFO 1", 20, 0, 127, 0),
    knob ("l1o1",   "Osc 1",  "LFO 1", 18, 1, 127, 64, true),
    knob ("l1o2",   "Osc 2",  "LFO 1", 50, 1, 127, 64, true),
    knob ("l1f",    "Filter", "LFO 1", 19, 1, 127, 64, true),
    knob ("l1a",    "Amp",    "LFO 1", 80, 1, 127, 64, true),
    knob ("l1p",    "Pan",    "LFO 1", 9,  1, 127, 64, true),

    // ---- LFO 2
    choice ("l2w", "Wave",  "LFO 2", 47, 7),
    knob ("l2rate", "Rate",   "LFO 2", 17, 0, 127, 0),
    knob ("l2fade", "Fade",   "LFO 2", 52, 0, 127, 0),
    knob ("l2o1",   "Osc 1",  "LFO 2", 22, 1, 127, 64, true),
    knob ("l2o2",   "Osc 2",  "LFO 2", 54, 1, 127, 64, true),
    knob ("l2f",    "Filter", "LFO 2", 23, 1, 127, 64, true),
    knob ("l2a",    "Amp",    "LFO 2", 24, 1, 127, 64, true),
    knob ("l2p",    "Pan",    "LFO 2", 41, 1, 127, 64, true),

    // ---- Effets
    knob ("fxins",   "INS-FX Intens.", "Effets", 93, 0, 127, 0),
    knob ("fxrev",   "Rev/Delay Lvl",  "Effets", 94, 0, 127, 0),
    knob ("fxfbk",   "Delay Fbk",      "Effets", 13, 0, 98,  0),
    knob ("revsend", "Rev Send (part)", "Effets", 91, 0, 127, 0),
};

inline constexpr int kNumParams = (int) (sizeof (kParams) / sizeof (kParams[0]));

// Nombre de paramètres « potard » (utilisés pour la lane du séquenceur).
constexpr int knobCount()
{
    int n = 0;
    for (const auto& p : kParams) if (p.kind == Kind::Knob) ++n;
    return n;
}

// Retourne le paramètre « potard » d'ordre o (0 .. knobCount()-1).
inline const ParamDef& knobByOrdinal (int o)
{
    int n = 0;
    for (const auto& p : kParams)
        if (p.kind == Kind::Knob) { if (n == o) return p; ++n; }
    return kParams[0];
}
} // namespace sh32
