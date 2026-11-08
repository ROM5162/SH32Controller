// Test d'intégration : instancie le vrai SH32Processor, simule un hôte (transport, tempo) et vérifie le MIDI produit.
// Si un 2e argument est donné, ouvre aussi l'éditeur et enregistre des captures PNG dans ce dossier.
//   cmake -DSH32_BUILD_HOST_TESTS=ON ... && ./SH32ProcessorTest [dossier_captures]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cstdio>
#include <set>

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("  ECHEC (ligne %d) %s : ", __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct FakeHost : juce::AudioPlayHead
{
    bool playing = false; double bpm = 120.0, ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setIsPlaying (playing); p.setBpm (bpm); p.setPpqPosition (ppq); return p;
    }
};

struct Ev { long long t; juce::MidiMessage m; };

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 512;

struct Rig
{
    SH32Processor proc;
    FakeHost host;
    long long abs = 0;
    Rig()
    {
        proc.setRateAndBufferSizeDetails (kSr, kBlock);
        proc.setPlayHead (&host);
        proc.prepareToPlay (kSr, kBlock);
    }

    // Traite `blocks` blocs ; `first` est injecté dans le premier bloc.
    std::vector<Ev> run (int blocks, const std::vector<juce::MidiMessage>& first = {})
    {
        std::vector<Ev> out;
        juce::AudioBuffer<float> buf (2, kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            host.ppq = (double) abs * host.bpm / (60.0 * kSr);
            juce::MidiBuffer mb;
            if (b == 0) for (auto& m : first) mb.addEvent (m, 0);
            proc.processBlock (buf, mb);
            for (const auto meta : mb) out.push_back ({ abs + meta.samplePosition, meta.getMessage() });
            abs += kBlock;
        }
        return out;
    }

    void setParam (const char* id, float v)   // v dans l'unité réelle du paramètre
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    float getParam (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); }
};

static int countCC (const std::vector<Ev>& v, int cc) { int n = 0; for (auto& e : v) if (e.m.isController() && e.m.getControllerNumber() == cc) ++n; return n; }
static int firstCCValue (const std::vector<Ev>& v, int cc) { for (auto& e : v) if (e.m.isController() && e.m.getControllerNumber() == cc) return e.m.getControllerValue(); return -1; }
static std::vector<Ev> notesOn (const std::vector<Ev>& v) { std::vector<Ev> o; for (auto& e : v) if (e.m.isNoteOn()) o.push_back (e); return o; }
static int balance (const std::vector<Ev>& v) { int b = 0; for (auto& e : v) { if (e.m.isNoteOn()) ++b; if (e.m.isNoteOff()) --b; } return b; }

//==============================================================================
static void testParameters()
{
    std::printf ("[1] paramètres -> Control Change\n");
    Rig r;
    CHECK (r.proc.getParameters().size() == sh32::kNumParams + 17, "nombre de paramètres hôte : %d", r.proc.getParameters().size());

    auto idle = r.run (4);
    CHECK (idle.empty(), "aucun événement attendu au repos (%zu)", idle.size());

    r.setParam ("cutoff", 50.0f);
    auto v = r.run (2);
    CHECK (countCC (v, 74) == 1 && firstCCValue (v, 74) == 50, "cutoff -> CC74=50 (%d CC)", countCC (v, 74));
    CHECK (v.size() == 1 && v[0].t % kBlock == 0 && v[0].m.getChannel() == 1, "un seul message, canal 1, début de bloc");

    r.setParam ("solo", 1.0f);  v = r.run (1);
    CHECK (countCC (v, 126) == 1 && firstCCValue (v, 126) == 1, "solo ON -> CC126=1");
    r.setParam ("solo", 0.0f);  v = r.run (1);
    CHECK (countCC (v, 127) == 1 && firstCCValue (v, 127) == 0, "solo OFF -> CC127=0");
    r.setParam ("portasw", 1.0f); v = r.run (1);
    CHECK (firstCCValue (v, 65) == 127, "portamento ON -> CC65=127");
    r.setParam ("ftype", 3.0f); v = r.run (1);
    CHECK (firstCCValue (v, 34) == 3, "type de filtre -> CC34=3");
    r.setParam ("o1coarse", 76.0f); v = r.run (1);
    CHECK (firstCCValue (v, 21) == 76, "coarse osc 1 -> CC21=76");

    r.setParam ("ch", 4.0f);        // canal 5
    r.setParam ("reso", 20.0f); v = r.run (1);
    CHECK (v.size() == 1 && v[0].m.getChannel() == 5, "canal MIDI configurable (%d)", v.empty() ? -1 : v[0].m.getChannel());

    r.setParam ("ch", 0.0f); r.run (1);
    r.proc.sendAllRequest.store (true);
    v = r.run (1);
    int ccs = 0; for (auto& e : v) if (e.m.isController()) ++ccs;
    CHECK (ccs == sh32::kNumParams, "« Tout envoyer » : %d CC au lieu de %d", ccs, sh32::kNumParams);
    CHECK (r.run (1).empty(), "pas de répétition après l'envoi");

    r.proc.programRequest.store ((1 << 14) | (2 << 7) | 5);
    v = r.run (1);
    bool pc = false; for (auto& e : v) if (e.m.isProgramChange() && e.m.getProgramChangeNumber() == 5) pc = true;
    CHECK (firstCCValue (v, 0) == 1 && firstCCValue (v, 32) == 2 && pc, "bank MSB/LSB + program change");
}

static void testIncoming()
{
    std::printf ("[2] MIDI entrant : CC -> paramètres, thru\n");
    Rig r;
    r.run (2);
    auto v = r.run (2, { juce::MidiMessage::controllerEvent (1, 71, 90) });
    CHECK ((int) std::lround (r.getParam ("reso")) == 90, "reso mis à jour : %f", r.getParam ("reso"));
    CHECK (countCC (v, 71) == 0, "pas d'écho du CC reçu (%d)", countCC (v, 71));
    CHECK (r.run (2).empty(), "pas d'émission différée");

    v = r.run (1, { juce::MidiMessage::controllerEvent (2, 74, 10) });   // autre canal : ignoré, transmis
    CHECK ((int) std::lround (r.getParam ("cutoff")) == 127 && countCC (v, 74) == 1, "CC d'un autre canal transmis tel quel");

    v = r.run (1, { juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) });
    CHECK (notesOn (v).size() == 1, "note entrante transmise (thru)");
    r.setParam ("thru", 0.0f); r.run (1);
    v = r.run (1, { juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100) });
    CHECK (notesOn (v).empty(), "thru désactivé");
}

static void testSequencerHost()
{
    std::printf ("[3] séquenceur synchronisé sur l'hôte\n");
    Rig r;
    r.setParam ("seqOn", 1.0f);
    r.run (2);
    auto v = r.run (20);
    CHECK (notesOn (v).empty() && balance (v) == 0, "arrêt du transport : silence");

    r.host.playing = true; r.abs = 0; r.host.ppq = 0.0;
    v = r.run (190);                                   // ~2 s = 4 temps = 16 pas
    const auto on = notesOn (v);
    static const int expectNotes[12] = { 48, 60, 48, 51, 58, 55, 48, 60, 48, 51, 63, 62 };
    int count16 = 0; for (auto& e : on) if (e.t < 96000) ++count16;
    CHECK (count16 == 12, "12 notes sur 16 pas (%d)", count16);
    bool seq = on.size() >= 12; for (int i = 0; i < 12 && i < (int) on.size(); ++i) if (on[(size_t) i].m.getNoteNumber() != expectNotes[i]) seq = false;
    CHECK (seq, "suite de notes du pattern de démo");
    CHECK (std::llabs (on[1].t - on[0].t - 12000) <= 1, "pas 1 -> pas 3 : %lld échantillons", on[1].t - on[0].t);
    CHECK (on[0].m.getVelocity() == 118 && on[1].m.getVelocity() == 80, "accent / vélocité");

    r.host.playing = false;                            // arrêt en pleine note
    v = r.run (3);
    CHECK (balance (v) <= 0, "notes coupées à l'arrêt");
    int total = 0; for (auto& e : v) if (e.m.isNoteOn()) --total;
    // bilan global : tout ce qui a été allumé est éteint
    Rig q; q.setParam ("seqOn", 1.0f); q.host.playing = true; q.setParam ("gate", 100.0f);
    auto a = q.run (60); q.host.playing = false; auto b = q.run (3);
    a.insert (a.end(), b.begin(), b.end());
    CHECK (balance (a) == 0, "bilan on/off nul (%d)", balance (a));

    // changement de pattern
    Rig s2; s2.setParam ("seqOn", 1.0f); s2.host.playing = true; s2.setParam ("slot", 1.0f);
    CHECK (notesOn (s2.run (100)).empty(), "pattern 2 vide : aucune note");
}

static void testInternalAndPoly()
{
    std::printf ("[4] horloge interne et mode poly\n");
    Rig r;
    r.setParam ("seqOn", 1.0f); r.setParam ("sync", 1.0f); r.setParam ("bpm", 150.0f);
    r.run (2);
    r.setParam ("freeRun", 1.0f);
    auto v = r.run (60);
    auto on = notesOn (v);
    CHECK (on.size() >= 3, "l'horloge interne joue sans transport hôte (%zu notes)", on.size());
    if (on.size() >= 2) CHECK (std::llabs (on[1].t - on[0].t - 9600) <= 2, "150 BPM : pas 1 -> 3 = %lld (9600 attendu)", on[1].t - on[0].t);

    Rig p;
    auto& bank = p.proc.uiBank();
    bank.p[0].clearPoly();
    bank.p[0].addNote (0, 48, 4, false); bank.p[0].addNote (0, 51, 4, true); bank.p[0].addNote (0, 55, 4, false);
    p.proc.publishBank();
    p.setParam ("seqOn", 1.0f); p.setParam ("mode", 1.0f); p.setParam ("gate", 100.0f);
    p.run (2);
    p.host.playing = true; p.abs = 0;
    v = p.run (40);
    on = notesOn (v);
    std::set<long long> t0; for (auto& e : on) if (e.t < 100) t0.insert (e.t);
    CHECK (on.size() >= 3 && t0.size() == 1, "accord de 3 notes simultané (%zu notes, %zu instants)", on.size(), t0.size());

    // panic
    p.proc.panicRequest.store (true);
    v = p.run (1);
    CHECK (countCC (v, 123) == 1, "PANIC : CC123");
}

static void testState()
{
    std::printf ("[5] sauvegarde / restauration de session\n");
    Rig a;
    a.proc.uiBank().p[2].addNote (3, 50, 2, true);
    a.proc.uiBank().p[2].steps[4] = { true, 70, true, true, 12 };
    a.proc.publishBank();
    a.setParam ("cutoff", 33.0f); a.setParam ("slot", 2.0f); a.setParam ("swing", 20.0f); a.setParam ("laneOn", 1.0f);
    juce::MemoryBlock mb; a.proc.getStateInformation (mb);
    CHECK (mb.getSize() > 100, "état non vide (%zu octets)", mb.getSize());

    Rig b;
    b.run (2);
    b.proc.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK ((int) std::lround (b.getParam ("cutoff")) == 33 && (int) std::lround (b.getParam ("slot")) == 2
           && (int) std::lround (b.getParam ("swing")) == 20 && b.getParam ("laneOn") > 0.5f, "paramètres restaurés");
    CHECK (sh32::serialize (b.proc.uiBank().p[2]) == sh32::serialize (a.proc.uiBank().p[2]), "pattern 3 restauré");
    CHECK (sh32::serialize (b.proc.uiBank().p[0]) == sh32::serialize (a.proc.uiBank().p[0]), "pattern 1 restauré");
    auto v = b.run (3);
    CHECK (v.empty(), "pas de rafale de CC à la restauration (%zu messages)", v.size());

    juce::MemoryBlock junk ("pas du xml", 10);
    b.proc.setStateInformation (junk.getData(), (int) junk.getSize());   // ne doit pas planter
    CHECK (true, "");
}

//==============================================================================
static juce::Component* findButton (juce::Component* c, const juce::String& text)
{
    if (auto* b = dynamic_cast<juce::Button*> (c)) if (b->getButtonText() == text) return c;
    for (auto* ch : c->getChildren()) if (auto* f = findButton (ch, text)) return f;
    return nullptr;
}

static void snapshot (juce::Component& c, const juce::File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static void testEditor (const juce::File& dir)
{
    std::printf ("[6] éditeur : création et captures dans %s\n", dir.getFullPathName().toRawUTF8());
    Rig r;
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc.createEditor());
    CHECK (ed != nullptr, "éditeur créé");
    if (! ed) return;
    ed->setVisible (true);
    pump (150);
    snapshot (*ed, dir.getChildFile ("editor_son.png"));

    if (auto* tab = findButton (ed.get(), juce::String::fromUTF8 ("Séquenceur")))
    {
        static_cast<juce::Button*> (tab)->triggerClick();
        pump (150);
        snapshot (*ed, dir.getChildFile ("editor_seq_mono.png"));

        r.setParam ("mode", 1.0f);
        r.proc.uiBank().p[0].clearPoly();
        r.proc.uiBank().p[0].addNote (0, 48, 4, false); r.proc.uiBank().p[0].addNote (0, 51, 4, true); r.proc.uiBank().p[0].addNote (0, 55, 4, false);
        r.proc.uiBank().p[0].addNote (8, 56, 2, false); r.proc.uiBank().p[0].addNote (8, 60, 2, false); r.proc.uiBank().p[0].addNote (8, 63, 4, false);
        r.proc.publishBank();
        r.setParam ("laneOn", 1.0f);
        pump (200);
        snapshot (*ed, dir.getChildFile ("editor_seq_poly.png"));
    }
    else CHECK (false, "onglet Séquenceur introuvable");
    ed.reset();
}


//==============================================================================
// Souris réelle injectée dans la fenêtre : dessine / supprime / déplace des notes, grille mono, lane.
static juce::Component* findByHeight (juce::Component* c, int h)
{
    if (c->getHeight() == h && c->getWidth() > 900 && c->isShowing()) return c;
    for (auto* ch : c->getChildren()) if (auto* f = findByHeight (ch, h)) return f;
    return nullptr;
}

static void testEditorMouse (const juce::File& dir)
{
    std::printf ("[7] éditeur : édition à la souris (événements réels)\n");
    Rig r;
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc.createEditor());
    ed->addToDesktop (0);
    ed->setVisible (true);
    pump (100);
    auto* peer = ed->getPeer();
    CHECK (peer != nullptr, "fenêtre native");
    if (peer == nullptr) return;

    static_cast<juce::Button*> (findButton (ed.get(), juce::String::fromUTF8 ("Séquenceur")))->triggerClick();
    pump (100);

    constexpr int canvasH = 18 + 24 * 14 + 6 + 56 + 4;           // = SeqCanvas::totalH
    constexpr int keyW = 48, rulerH = 18, rowH = 14, barH = 24 * 14 - 56;
    auto* canvas = findByHeight (ed.get(), canvasH);
    CHECK (canvas != nullptr, "canvas du séquenceur trouvé");
    if (canvas == nullptr) return;
    const float colW = (canvas->getWidth() - keyW) / 16.0f;

    auto at = [&] (float x, float y) { return peer->getComponent().getLocalPoint (canvas, juce::Point<float> (x, y)); };
    auto send = [&] (juce::Point<float> p, int flags) {
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, p, juce::ModifierKeys (flags), 1.0f, 0.0f, juce::Time::currentTimeMillis());
        pump (5);
    };
    const int L = juce::ModifierKeys::leftButtonModifier;
    auto colX = [&] (int c, float frac = 0.3f) { return keyW + (c + frac) * colW; };
    auto rowY = [&] (int note) { return (float) (rulerH + (71 - note) * rowH + rowH / 2); };   // base 48 -> note la plus haute 71
    auto click = [&] (float x, float y, int extra = 0) { send (at (x, y), extra); send (at (x, y), L | extra); send (at (x, y), extra); };
    auto drag  = [&] (float x0, float y0, float x1, float y1) {
        send (at (x0, y0), 0); send (at (x0, y0), L);
        for (int i = 1; i <= 6; ++i) send (at (x0 + (x1 - x0) * i / 6, y0 + (y1 - y0) * i / 6), L);
        send (at (x1, y1), 0);
    };

    // ---- poly
    r.setParam ("mode", 1.0f);
    r.proc.uiBank().p[0].clearPoly(); r.proc.publishBank();
    pump (100);
    auto& pat = r.proc.uiBank().p[0];

    drag (colX (0), rowY (48), colX (3), rowY (48));        // crée C2, longueur 4
    CHECK (pat.numNotes == 1 && pat.notes[0].s == 0 && pat.notes[0].n == 48 && pat.notes[0].l == 4,
           "note créée en tirant : %d note(s) s=%d n=%d l=%d", pat.numNotes, pat.notes[0].s, pat.notes[0].n, pat.notes[0].l);
    drag (colX (0), rowY (51), colX (3), rowY (51));        // crée D#2
    CHECK (pat.numNotes == 2, "2e note");
    click (colX (1), rowY (51));                            // clic sur le corps = suppression
    CHECK (pat.numNotes == 1 && pat.notes[0].n == 48, "suppression par clic (%d note(s))", pat.numNotes);
    drag (colX (1), rowY (48), colX (5), rowY (48));        // déplacement : s = 5 - 1 = 4
    CHECK (pat.notes[0].s == 4 && pat.notes[0].n == 48 && pat.notes[0].l == 4, "déplacement : s=%d n=%d l=%d", pat.notes[0].s, pat.notes[0].n, pat.notes[0].l);
    drag (colX (4), rowY (48), colX (4), rowY (55));        // même pas, 7 demi-tons plus haut
    CHECK (pat.notes[0].s == 4 && pat.notes[0].n == 55, "déplacement vertical : n=%d", pat.notes[0].n);
    auto* accBtn = static_cast<juce::Button*> (findButton (ed.get(), "Accent"));
    accBtn->triggerClick();                                 // outil Accent (Maj+clic n'est pas simulable : JUCE lit le vrai clavier)
    click (colX (5), rowY (55));
    accBtn->triggerClick();
    CHECK (pat.numNotes == 1 && pat.notes[0].a, "accent via l'outil Accent (%d note(s))", pat.numNotes);
    drag (colX (7, 0.8f), rowY (55), colX (6), rowY (55));  // bord droit (pas 7, fx>0.55) tiré vers la gauche : longueur 3
    CHECK (pat.notes[0].l == 3, "redimensionnement : l=%d", pat.notes[0].l);

    snapshot (*ed, dir.getChildFile ("editor_mouse_poly.png"));

    r.setParam ("seqOn", 1.0f);
    r.run (2); r.host.playing = true; r.abs = 0;
    auto on = notesOn (r.run (120));
    CHECK (on.size() >= 1 && on[0].m.getNoteNumber() == 55 && std::llabs (on[0].t - 4 * 6000) <= 1 && on[0].m.getVelocity() == 118,
           "le séquenceur joue ce qui a été dessiné (%zu notes, 1re : %d à %lld)", on.size(), on.empty() ? -1 : on[0].m.getNoteNumber(), on.empty() ? -1 : on[0].t);
    r.host.playing = false; r.run (3);

    // ---- mono
    r.setParam ("mode", 0.0f);
    pump (100);
    auto& st = r.proc.uiBank().p[0].steps;
    const bool wasOn1 = st[1].on;                           // pas 2 éteint dans le pattern de démo
    click (colX (1, 0.5f), (float) (rulerH + barH + 16));
    CHECK (st[1].on != wasOn1, "bouton ● du pas 2");
    click (colX (1, 0.25f), (float) (rulerH + barH + 42));
    CHECK (st[1].acc, "bouton A du pas 2");
    click (colX (1, 0.75f), (float) (rulerH + barH + 42));
    CHECK (st[1].tie, "bouton T du pas 2");
    drag (colX (2, 0.5f), (float) (rulerH + 5), colX (2, 0.5f), (float) (rulerH + 5));
    CHECK (st[2].on && st[2].note >= 82, "note du pas 3 tirée vers le haut : %d", st[2].note);
    drag (colX (2, 0.5f), (float) (rulerH + 5), colX (2, 0.5f), (float) (rulerH + barH - 2));
    CHECK (st[2].note <= 38, "puis vers le bas : %d", st[2].note);

    // ---- lane
    const float laneTop = (float) (rulerH + 24 * 14 + 6);
    click (colX (5, 0.5f), laneTop + 14.0f);
    click (colX (6, 0.5f), laneTop + 42.0f);
    CHECK (st[5].lv >= 93 && st[5].lv <= 97 && st[6].lv >= 30 && st[6].lv <= 34, "lane : pas 6 = %d, pas 7 = %d", st[5].lv, st[6].lv);
    drag (colX (8, 0.5f), laneTop + 28.0f, colX (10, 0.5f), laneTop + 28.0f);
    CHECK (st[8].lv >= 62 && st[9].lv >= 62 && st[10].lv >= 62 && st[8].lv <= 66, "glisser sur la lane peint plusieurs pas (%d %d %d)", st[8].lv, st[9].lv, st[10].lv);

    // ---- pattern suivant : édition isolée
    CHECK (r.proc.uiBank().p[1].numNotes == 0, "les autres patterns ne sont pas touchés");
    snapshot (*ed, dir.getChildFile ("editor_mouse_mono.png"));
    ed.reset();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    testParameters();
    testIncoming();
    testSequencerHost();
    testInternalAndPoly();
    testState();
    if (argc > 1) { testEditor (juce::File (argv[1])); testEditorMouse (juce::File (argv[1])); }

    std::printf ("\n%d vérifications, %d échec(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
