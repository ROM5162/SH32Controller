#pragma once
// Moteur de séquenceur : calcule les événements MIDI d'un bloc audio à partir de la position
// (PPQ) fournie par l'hôte. Aucune dépendance à JUCE, aucune allocation dans process().
//
// Principe : le pas k débute à la position  k / rate  (en noires) + swing éventuel. Pour chaque
// bloc [ppqStart, ppqEnd) on émet les pas dont l'instant tombe dans l'intervalle. Le résultat
// ne dépend donc pas de la taille des blocs, et suit boucles / sauts de l'hôte.

#include "Pattern.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace sh32
{
struct Settings
{
    bool enabled  = true;
    int  rate     = 4;       // pas par noire : 1, 2, 3, 4, 6, 8
    int  len      = 16;      // 1..16
    int  dir      = 0;       // 0 avant, 1 arrière, 2 aller-retour, 3 aléatoire
    int  swing    = 0;       // 0..50 (%)
    int  gate     = 60;      // 10..100 (%)
    int  trans    = 0;       // demi-tons
    bool poly     = false;
    bool laneOn   = false;
    int  laneCc   = 74;
    int  laneMin  = 0;
    int  laneMax  = 127;
    int  channel  = 0;       // 0..15
};

struct MidiEv
{
    int     sample = 0;
    uint8_t st = 0, d1 = 0, d2 = 0;
    int     prio = 1;        // 0 = note-off, 1 = CC, 2 = note-on (ordre à instant égal)
};

struct EventBuffer
{
    static constexpr int cap = 4096;
    MidiEv ev[cap];
    int n = 0;

    void clear() { n = 0; }
    void push (int sample, uint8_t st, uint8_t d1, uint8_t d2, int prio)
    {
        if (n >= cap) return;
        ev[n].sample = sample; ev[n].st = st; ev[n].d1 = d1; ev[n].d2 = d2; ev[n].prio = prio;
        ++n;
    }
    // tri par instant puis priorité (insertion stable, sans allocation)
    void sort()
    {
        for (int i = 1; i < n; ++i)
        {
            const MidiEv x = ev[i];
            const long long kx = (long long) x.sample * 3 + x.prio;
            int j = i - 1;
            while (j >= 0 && (long long) ev[j].sample * 3 + ev[j].prio > kx) { ev[j + 1] = ev[j]; --j; }
            ev[j + 1] = x;
        }
    }
};

class Engine
{
public:
    Engine() { reset(); }

    void reset()
    {
        for (auto& a : active) a.used = false;
        haveLast = false;
        curStep.store (-1);
    }

    int currentStep() const { return curStep.load(); }

    // Note-off immédiat de toutes les notes en cours.
    void flush (EventBuffer& out, int sample)
    {
        for (auto& a : active)
            if (a.used)
            {
                out.push (sample, (uint8_t) (0x80 | a.ch), (uint8_t) a.note, 0, 0);
                a.used = false;
            }
    }

    void process (const Pattern& pat, const Settings& s, double ppqStart, double bpm, double sr,
                  int numSamples, bool playing, EventBuffer& out)
    {
        if (! playing || ! s.enabled || numSamples <= 0)
        {
            flush (out, 0);
            haveLast = false;
            curStep.store (-1);
            return;
        }

        if (bpm < 1.0) bpm = 120.0;
        const double pps = bpm / (60.0 * sr);                  // noires par échantillon

        if (haveLast)
        {
            if (std::fabs (ppqStart - lastEnd) < 0.02) ppqStart = lastEnd;   // continuité : on ignore la gigue de l'hôte
            else flush (out, 0);                                              // saut / boucle
        }
        haveLast = true;

        const double ppqEnd = ppqStart + numSamples * pps;
        lastEnd = ppqEnd;

        ctxStart = ppqStart; ctxPps = pps; ctxN = numSamples;
        msPpq = bpm / 60000.0;

        const int    L      = clampInt (s.len, 1, kSteps);
        const double d      = 1.0 / (double) clampInt (s.rate, 1, 16);     // durée d'un pas, en noires
        const double swingF = clampInt (s.swing, 0, 50) / 100.0;

        long long k0 = (long long) std::floor (ppqStart / d) - 1;
        const long long k1 = (long long) std::ceil (ppqEnd / d) + 1;
        if (k0 < 0) k0 = 0;

        for (long long k = k0; k <= k1; ++k)
        {
            const double T = (double) k * d + ((k & 1) ? swingF * d : 0.0);
            if (T < ppqStart || T >= ppqEnd) continue;

            const int idx  = stepIndex (k, L, s.dir);
            const int nIdx = stepIndex (k + 1, L, s.dir);
            curStep.store (idx);
            scheduleStep (pat, s, idx, nIdx, k, T, d, swingF, L, out);
        }

        // note-off arrivant à échéance dans ce bloc
        for (auto& a : active)
            if (a.used && a.offPpq < ppqEnd)
            {
                out.push (toSample (a.offPpq), (uint8_t) (0x80 | a.ch), (uint8_t) a.note, 0, 0);
                a.used = false;
            }

        out.sort();
    }

    static int stepIndex (long long k, int L, int dir)
    {
        if (L <= 1) return 0;
        switch (dir)
        {
            case 1:  return L - 1 - (int) (k % L);
            case 2:  { const int P = 2 * L - 2; const int m = (int) (k % P); return m < L ? m : P - m; }
            case 3:  {
                uint32_t h = (uint32_t) ((unsigned long long) k * 2654435761ULL);
                h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
                return (int) (h % (uint32_t) L);
            }
            default: return (int) (k % L);
        }
    }

private:
    struct Active { int note = 0, ch = 0; double offPpq = 0; bool used = false; };
    static constexpr int kMaxActive = 1024;

    Active active[kMaxActive];
    std::atomic<int> curStep { -1 };
    bool   haveLast = false;
    double lastEnd = 0, ctxStart = 0, ctxPps = 1e-5, msPpq = 0;
    int    ctxN = 0;

    int toSample (double ppq) const
    {
        const double v = (ppq - ctxStart) / ctxPps;
        if (v <= 0.0) return 0;
        const int s = (int) (v + 1e-4);   // arrondi : évite 5999.9999 -> 5999
        return s >= ctxN ? ctxN - 1 : s;
    }

    void noteOn (EventBuffer& out, int ch, int note, int vel, double T, double offPpq)
    {
        const int sm = toSample (T);
        for (auto& a : active)
            if (a.used && a.note == note && a.ch == ch)
            {
                out.push (sm, (uint8_t) (0x80 | ch), (uint8_t) note, 0, 0);   // re-déclenchement propre
                a.used = false;
            }
        out.push (sm, (uint8_t) (0x90 | ch), (uint8_t) note, (uint8_t) vel, 2);
        for (auto& a : active)
            if (! a.used) { a.used = true; a.note = note; a.ch = ch; a.offPpq = offPpq; return; }
        // table pleine : on coupe tout de suite plutôt que de laisser une note bloquée
        out.push (toSample (offPpq), (uint8_t) (0x80 | ch), (uint8_t) note, 0, 0);
    }

    void scheduleStep (const Pattern& pat, const Settings& s, int idx, int nIdx, long long k,
                       double T, double d, double swingF, int L, EventBuffer& out)
    {
        const int ch = clampInt (s.channel, 0, 15);
        const auto& st = pat.steps[(size_t) idx];

        if (s.laneOn)
        {
            const int v = s.laneMin + (st.lv * (s.laneMax - s.laneMin) + 63) / 127;
            out.push (toSample (T), (uint8_t) (0xB0 | ch), (uint8_t) clampInt (s.laneCc, 0, 127),
                      (uint8_t) clampInt (v, 0, 127), 1);
        }

        if (s.poly)
        {
            for (int i = 0; i < pat.numNotes; ++i)
            {
                const auto& p = pat.notes[(size_t) i];
                if (p.s != idx) continue;
                const int len = std::min (p.l, L - p.s);
                if (len < 1) continue;
                const int note = clampInt (p.n + s.trans, 0, 127);
                double off = T + (len - 1) * d + d * s.gate / 100.0;
                off = std::max (T + 5.0 * msPpq, std::min (off, T + len * d - msPpq));
                noteOn (out, ch, note, p.a ? 118 : 80, T, off);
            }
            return;
        }

        if (! st.on) return;
        const int note = clampInt (st.note + s.trans, 0, 127);
        double off = T + d * s.gate / 100.0;
        if (st.tie)
        {
            const auto& nx = pat.steps[(size_t) nIdx];
            if (nx.on)
            {
                const double nStart = (double) (k + 1) * d + (((k + 1) & 1) ? swingF * d : 0.0);
                const int nNote = clampInt (nx.note + s.trans, 0, 127);
                off = (nNote == note) ? nStart - msPpq : nStart + 2.0 * msPpq;   // même note : re-déclenche ; sinon legato
            }
            else off = (double) (k + 1) * d;
        }
        off = std::max (off, T + 5.0 * msPpq);
        noteOn (out, ch, note, st.acc ? 118 : 80, T, off);
    }
};
} // namespace sh32
