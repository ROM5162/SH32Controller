// Tests du moteur de séquenceur (sans JUCE).
//   g++ -std=c++17 -O1 -Wall -Wextra -fsanitize=address,undefined -I../Source engine_test.cpp -o engine_test && ./engine_test
#include "../Source/Params.h"
#include "../Source/SeqEngine.h"

#include <cstdio>
#include <map>
#include <set>
#include <vector>

using namespace sh32;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  ECHEC (%s:%d) %s : ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct Ev { long long t; int st, d1, d2; };

// Fait tourner le moteur sur [0, totalSamples) avec une taille de bloc donnée, renvoie les événements en temps absolu.
static std::vector<Ev> run (const Pattern& pat, const Settings& s, double bpm, double sr, long long totalSamples,
                            int block, long long stopAt = -1)
{
    Engine e; EventBuffer buf; std::vector<Ev> all;
    const double pps = bpm / (60.0 * sr);
    for (long long pos = 0; pos < totalSamples; pos += block)
    {
        const int n = (int) std::min<long long> (block, totalSamples - pos);
        buf.clear();
        const bool playing = stopAt < 0 || pos < stopAt;
        e.process (pat, s, (double) pos * pps, bpm, sr, n, playing, buf);
        for (int i = 0; i < buf.n; ++i) all.push_back ({ pos + buf.ev[i].sample, buf.ev[i].st, buf.ev[i].d1, buf.ev[i].d2 });
    }
    return all;
}

static int countType (const std::vector<Ev>& v, int type) { int c = 0; for (auto& e : v) if ((e.st & 0xF0) == type) ++c; return c; }

static std::map<int, int> balance (const std::vector<Ev>& v)
{
    std::map<int, int> b;
    for (auto& e : v) { if ((e.st & 0xF0) == 0x90) b[e.d1]++; if ((e.st & 0xF0) == 0x80) b[e.d1]--; }
    return b;
}

static bool noneStuck (const std::vector<Ev>& v) { for (auto& kv : balance (v)) if (kv.second != 0) return false; return true; }

static void testBlockSizeIndependence()
{
    std::printf ("[1] sortie identique quelle que soit la taille de bloc\n");
    Pattern p = makeDemoPattern();
    p.steps[3].tie = true; p.steps[6].tie = true;
    Settings s; s.swing = 30; s.laneOn = true; s.gate = 55;
    const double sr = 48000, bpm = 127.3;
    const long long total = (long long) (sr * 8);
    const auto ref = run (p, s, bpm, sr, total, 64);
    CHECK (ref.size() > 100, "peu d'événements (%zu)", ref.size());
    for (int block : { 100, 256, 512, 777, 1024, 4096 })
    {
        const auto v = run (p, s, bpm, sr, total, block);
        CHECK (v.size() == ref.size(), "bloc %d : %zu événements au lieu de %zu", block, v.size(), ref.size());
        if (v.size() != ref.size()) continue;
        // même suite d'octets, instants à ±1 échantillon (on compare par type d'événement)
        std::map<std::tuple<int,int,int>, std::vector<long long>> a, b;
        for (auto& e : ref) a[{ e.st, e.d1, e.d2 }].push_back (e.t);
        for (auto& e : v)   b[{ e.st, e.d1, e.d2 }].push_back (e.t);
        bool ok = a.size() == b.size();
        for (auto& kv : a)
        {
            auto it = b.find (kv.first);
            if (it == b.end() || it->second.size() != kv.second.size()) { ok = false; break; }
            for (size_t i = 0; i < kv.second.size(); ++i)
                if (std::llabs (kv.second[i] - it->second[i]) > 1) { ok = false; break; }
        }
        CHECK (ok, "bloc %d : instants différents de la référence", block);
    }
}

static void testGateAndSwing()
{
    std::printf ("[2] gate et swing\n");
    Pattern p; for (auto& st : p.steps) { st.on = true; st.note = 60; }
    Settings s; s.gate = 50; s.swing = 0;
    const double sr = 48000, bpm = 120;      // 1/16 = 6000 échantillons
    auto v = run (p, s, bpm, sr, 48000, 512);
    std::vector<long long> on, off;
    for (auto& e : v) { if ((e.st & 0xF0) == 0x90) on.push_back (e.t); if ((e.st & 0xF0) == 0x80) off.push_back (e.t); }
    CHECK (on.size() >= 8, "pas de notes");
    CHECK (std::llabs (on[1] - on[0] - 6000) <= 1, "pas = %lld", on[1] - on[0]);
    CHECK (std::llabs (off[0] - on[0] - 3000) <= 1, "gate 50%% = %lld", off[0] - on[0]);

    s.swing = 50; s.gate = 20;
    v = run (p, s, bpm, sr, 48000, 512);
    on.clear();
    for (auto& e : v) if ((e.st & 0xF0) == 0x90) on.push_back (e.t);
    CHECK (std::llabs (on[1] - on[0] - 9000) <= 1, "swing 50%%: pas impair retardé de %lld", on[1] - on[0]);
    CHECK (std::llabs (on[2] - on[1] - 3000) <= 1, "swing 50%%: pas pair %lld", on[2] - on[1]);
}

static void testPolyChord()
{
    std::printf ("[3] accords (mode poly)\n");
    Pattern p;
    p.addNote (0, 48, 4, false); p.addNote (0, 51, 4, true); p.addNote (0, 55, 4, false);
    p.addNote (8, 56, 1, false); p.addNote (8, 60, 2, false);
    Settings s; s.poly = true; s.gate = 100;
    const double sr = 48000, bpm = 120;
    const auto v = run (p, s, bpm, sr, 96000, 480);
    std::set<long long> t0;
    std::map<int,int> vel;
    for (auto& e : v) if ((e.st & 0xF0) == 0x90 && e.t < 6000) { t0.insert (e.t); vel[e.d1] = e.d2; }
    CHECK (t0.size() == 1, "les 3 notes de l'accord doivent partir ensemble (%zu instants)", t0.size());
    CHECK (vel[51] == 118 && vel[48] == 80, "accent / vélocité");
    long long off48 = -1;
    for (auto& e : v) if ((e.st & 0xF0) == 0x80 && e.d1 == 48 && off48 < 0) off48 = e.t;
    CHECK (off48 > 23000 && off48 <= 24000, "note de 4 pas, gate 100%% : off à %lld", off48);
}

static void testNoStuckNotes()
{
    std::printf ("[4] aucune note bloquée (arrêt, boucle, changement de longueur)\n");
    Pattern p = makeDemoPattern();
    for (int i = 0; i < 16; i += 3) p.steps[(size_t) i].tie = true;
    Settings s; s.gate = 100;
    const double sr = 44100, bpm = 140;

    // arrêt du transport au milieu d'une note
    auto v = run (p, s, bpm, sr, 44100 * 4, 512, 44100 * 2 + 100);
    CHECK (noneStuck (v), "notes restantes après arrêt");
    CHECK (countType (v, 0x90) > 0, "rien joué");

    // boucle : saut de position vers l'arrière
    Engine e; EventBuffer buf; std::vector<Ev> all;
    const double pps = bpm / (60.0 * sr);
    double ppq = 0; long long abs = 0;
    for (int b = 0; b < 400; ++b)
    {
        if (b == 150) ppq = 1.0;                // saut
        buf.clear();
        e.process (p, s, ppq, bpm, sr, 256, true, buf);
        for (int i = 0; i < buf.n; ++i) all.push_back ({ abs + buf.ev[i].sample, buf.ev[i].st, buf.ev[i].d1, buf.ev[i].d2 });
        ppq += 256 * pps; abs += 256;
    }
    buf.clear(); e.process (p, s, ppq, bpm, sr, 256, false, buf);
    for (int i = 0; i < buf.n; ++i) all.push_back ({ abs + buf.ev[i].sample, buf.ev[i].st, buf.ev[i].d1, buf.ev[i].d2 });
    CHECK (noneStuck (all), "notes restantes après saut de position");

    // jitter de l'hôte : positions bruitées de ±1e-4 noire, aucun doublon ni trou
    Engine e2; std::vector<Ev> a2; ppq = 0; abs = 0; unsigned r = 12345;
    for (int b = 0; b < 800; ++b)
    {
        r = r * 1664525u + 1013904223u;
        const double jit = ((int) (r >> 16) % 200 - 100) * 1e-6;
        buf.clear();
        e2.process (p, s, ppq + jit, bpm, sr, 256, true, buf);
        for (int i = 0; i < buf.n; ++i) a2.push_back ({ abs + buf.ev[i].sample, buf.ev[i].st, buf.ev[i].d1, buf.ev[i].d2 });
        ppq += 256 * pps; abs += 256;
    }
    const auto clean = run (p, s, bpm, sr, 256 * 800, 256);
    CHECK (countType (a2, 0x90) >= countType (clean, 0x90) - 1 && countType (a2, 0x90) <= countType (clean, 0x90),
           "gigue : %d notes au lieu de %d", countType (a2, 0x90), countType (clean, 0x90));
}

static void testTie()
{
    std::printf ("[5] liaison (tie)\n");
    Pattern p;
    p.steps[0] = { true, 60, false, true, 64 };     // lié à la suivante, hauteur différente
    p.steps[1] = { true, 64, false, false, 64 };
    p.steps[2] = { true, 60, false, true, 64 };     // lié, même hauteur
    p.steps[3] = { true, 60, false, false, 64 };
    Settings s; s.len = 4; s.gate = 50;
    const auto v = run (p, s, 120, 48000, 48000, 512);
    long long on60a = -1, on64 = -1, off60a = -1, on60b = -1, off60b = -1;
    for (auto& e : v)
    {
        if (e.st == 0x90 && e.d1 == 60 && on60a < 0) on60a = e.t;
        else if (e.st == 0x90 && e.d1 == 64 && on64 < 0) on64 = e.t;
        else if (e.st == 0x80 && e.d1 == 60 && off60a < 0) off60a = e.t;
    }
    CHECK (off60a > on64, "legato : l'extinction (%lld) doit suivre la note suivante (%lld)", off60a, on64);
    // 2e paire (pas 2 -> 3, même hauteur) : off avant le nouveau on
    int seen = 0;
    for (auto& e : v)
        if (e.d1 == 60 && e.t > 11000 && e.t < 18500)
        {
            if (e.st == 0x90) { if (++seen == 1) on60b = e.t; else if (seen == 2) { CHECK (off60b >= 0 && off60b <= e.t, "re-déclenchement : off (%lld) avant on (%lld)", off60b, e.t); } }
            if (e.st == 0x80 && off60b < 0) off60b = e.t;
        }
    CHECK (on60b >= 0, "pas 2 non trouvé");
}

static void testDirections()
{
    std::printf ("[6] sens de lecture\n");
    Pattern p; for (int i = 0; i < 16; ++i) { p.steps[(size_t) i].on = true; p.steps[(size_t) i].note = 40 + i; }
    Settings s; s.len = 4;
    auto order = [&] (int dir) {
        s.dir = dir; std::vector<int> o;
        for (auto& e : run (p, s, 120, 48000, 6000 * 12 - 100, 700)) if ((e.st & 0xF0) == 0x90) o.push_back (e.d1 - 40);
        return o;
    };
    const auto fwd = order (0), rev = order (1), pp = order (2), rnd = order (3);
    const std::vector<int> expF = { 0,1,2,3,0,1,2,3,0,1,2,3 }, expR = { 3,2,1,0,3,2,1,0,3,2,1,0 }, expP = { 0,1,2,3,2,1,0,1,2,3,2,1 };
    CHECK (fwd == expF, "avant"); CHECK (rev == expR, "arrière"); CHECK (pp == expP, "aller-retour");
    CHECK (rnd.size() == 12, "aléatoire"); bool in = true; for (int x : rnd) if (x < 0 || x > 3) in = false; CHECK (in, "aléatoire hors longueur");
    // reproductible : deux exécutions identiques
    CHECK (order (3) == rnd, "aléatoire non reproductible");
}

static void testLaneAndTranspose()
{
    std::printf ("[7] lane, transposition, canal\n");
    Pattern p; p.steps[0] = { true, 60, false, false, 127 }; p.steps[1] = { true, 60, false, false, 0 };
    Settings s; s.len = 2; s.laneOn = true; s.laneCc = 74; s.laneMin = 20; s.laneMax = 100; s.trans = 7; s.channel = 4;
    const auto v = run (p, s, 120, 48000, 24000, 512);
    std::vector<int> cc; std::vector<long long> ccT, onT; int note = -1, chan = -1;
    for (auto& e : v)
    {
        if ((e.st & 0xF0) == 0xB0 && e.d1 == 74) { cc.push_back (e.d2); ccT.push_back (e.t); }
        if ((e.st & 0xF0) == 0x90) { onT.push_back (e.t); note = e.d1; chan = e.st & 0x0F; }
    }
    CHECK (cc.size() >= 2 && cc[0] == 100 && cc[1] == 20, "valeurs de lane %d %d", cc.size() > 0 ? cc[0] : -1, cc.size() > 1 ? cc[1] : -1);
    CHECK (note == 67, "transposition : %d", note);
    CHECK (chan == 4, "canal : %d", chan);
    CHECK (ccT[0] <= onT[0], "la lane doit précéder la note");
    // à instant égal, CC avant note-on dans le même bloc
    bool order = false;
    Engine e; EventBuffer buf; e.process (p, s, 0.0, 120, 48000, 512, true, buf);
    int iCC = -1, iOn = -1; for (int i = 0; i < buf.n; ++i) { if ((buf.ev[i].st & 0xF0) == 0xB0 && iCC < 0) iCC = i; if ((buf.ev[i].st & 0xF0) == 0x90 && iOn < 0) iOn = i; }
    order = iCC >= 0 && iOn > iCC; CHECK (order, "ordre CC/note-on");
}

static void testSerialization()
{
    std::printf ("[8] sérialisation des patterns\n");
    Pattern p = makeDemoPattern(); p.steps[5].tie = true; p.steps[7].lv = 3;
    p.addNote (0, 48, 4, true); p.addNote (4, 60, 2, false);
    Pattern q; q.addNote (9, 9, 1, false);
    CHECK (deserialize (q, serialize (p)), "relecture");
    CHECK (serialize (q) == serialize (p), "aller-retour identique");
    Pattern bad = q;
    CHECK (! deserialize (bad, "n'importe quoi"), "rejet d'un texte invalide");
    CHECK (serialize (bad) == serialize (q), "pattern inchangé après échec");
    CHECK (deserialize (bad, "1,2,3,4,5;|0,200,99,1;") == false, "format incomplet rejeté");
}

static void testParams()
{
    std::printf ("[9] table des paramètres\n");
    std::set<int> ccs; std::set<std::string> ids; int knobs = 0;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = kParams[i];
        CHECK (ids.insert (d.id).second, "id dupliqué %s", d.id);
        if (d.kind != Kind::Toggle) { CHECK (ccs.insert (d.cc).second, "CC dupliqué %d (%s)", d.cc, d.id); CHECK (d.def >= d.minV && d.def <= d.maxV, "défaut hors plage %s", d.id); }
        if (d.kind == Kind::Knob) ++knobs;
    }
    CHECK (knobs == knobCount(), "knobCount");
    std::printf ("    %d paramètres, %d potards\n", kNumParams, knobs);
}

int main()
{
    testParams(); testBlockSizeIndependence(); testGateAndSwing(); testPolyChord();
    testNoStuckNotes(); testTie(); testDirections(); testLaneAndTranspose(); testSerialization();
    std::printf ("\n%d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
