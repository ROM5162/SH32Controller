#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const int kRateTable[6] = { 1, 2, 3, 4, 6, 8 };   // pas par noire : 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32

juce::StringArray numberNames (int from, int to)
{
    juce::StringArray a;
    for (int i = from; i <= to; ++i) a.add (juce::String (i));
    return a;
}
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SH32Processor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- paramètres du synthé (un par CC)
    for (const auto& d : sh32::kParams)
    {
        const ParameterID pid { d.id, 1 };
        switch (d.kind)
        {
            case sh32::Kind::Knob:
            {
                const bool bip = d.bipolar, semi = d.semitones;
                layout.add (std::make_unique<AudioParameterInt> (
                    pid, d.label, d.minV, d.maxV, d.def,
                    AudioParameterIntAttributes().withStringFromValueFunction ([bip, semi] (int v, int) -> String
                    {
                        if (! bip) return String (v);
                        const int x = v - 64;
                        const String s = (x > 0 ? "+" : "") + String (x);
                        return semi ? s + " st" : s;
                    })));
                break;
            }
            case sh32::Kind::Choice:
                layout.add (std::make_unique<AudioParameterChoice> (pid, d.label, numberNames (0, d.maxV), d.def));
                break;
            case sh32::Kind::Toggle:
                layout.add (std::make_unique<AudioParameterBool> (pid, d.label, d.def != 0));
                break;
        }
    }

    // ---- réglages globaux et séquenceur
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    layout.add (std::make_unique<AudioParameterChoice> (id ("ch"),   "MIDI Channel", numberNames (1, 16), 0));
    layout.add (std::make_unique<AudioParameterBool>   (id ("thru"), "MIDI Thru", true));
    layout.add (std::make_unique<AudioParameterBool>   (id ("sendOnPlay"), "Send All On Play", false));

    layout.add (std::make_unique<AudioParameterBool>   (id ("seqOn"), "Sequencer", false));
    layout.add (std::make_unique<AudioParameterChoice> (id ("sync"), "Seq Sync", StringArray { "Host", "Internal" }, 0));
    layout.add (std::make_unique<AudioParameterInt>    (id ("bpm"), "Internal BPM", 20, 300, 120));
    layout.add (std::make_unique<AudioParameterBool>   (id ("freeRun"), "Internal Play", false));
    layout.add (std::make_unique<AudioParameterChoice> (id ("rate"), "Seq Rate", StringArray { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" }, 3));
    layout.add (std::make_unique<AudioParameterChoice> (id ("len"), "Seq Length", numberNames (1, 16), 15));
    layout.add (std::make_unique<AudioParameterChoice> (id ("dir"), "Seq Direction", StringArray { "Forward", "Backward", "Ping-Pong", "Random" }, 0));
    layout.add (std::make_unique<AudioParameterInt>    (id ("swing"), "Seq Swing", 0, 50, 0));
    layout.add (std::make_unique<AudioParameterInt>    (id ("gate"),  "Seq Gate", 10, 100, 60));
    layout.add (std::make_unique<AudioParameterInt>    (id ("trans"), "Seq Transpose", -24, 24, 0));
    layout.add (std::make_unique<AudioParameterChoice> (id ("mode"), "Seq Mode", StringArray { "Mono", "Poly" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (id ("slot"), "Pattern", numberNames (1, 8), 0));
    layout.add (std::make_unique<AudioParameterBool>   (id ("laneOn"), "Lane On", false));

    StringArray laneNames;
    int cutoffOrdinal = 0;
    for (int o = 0; o < sh32::knobCount(); ++o)
    {
        const auto& d = sh32::knobByOrdinal (o);
        laneNames.add (String (d.section) + " - " + d.label);
        if (std::strcmp (d.id, "cutoff") == 0) cutoffOrdinal = o;
    }
    layout.add (std::make_unique<AudioParameterChoice> (id ("laneParam"), "Lane Target", laneNames, cutoffOrdinal));

    return layout;
}

//==============================================================================
SH32Processor::SH32Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SH32", createLayout())
{
    ccToParam.fill (-1);
    lastSent.fill (-1);

    for (int i = 0; i < sh32::kNumParams; ++i)
    {
        const auto& d = sh32::kParams[i];
        paramRaw[(size_t) i] = apvts.getRawParameterValue (d.id);
        paramPtr[(size_t) i] = apvts.getParameter (d.id);
        if (d.kind != sh32::Kind::Toggle && d.cc >= 0 && d.cc < 128)
            ccToParam[(size_t) d.cc] = i;
    }

    g.ch = apvts.getRawParameterValue ("ch");           g.seqOn = apvts.getRawParameterValue ("seqOn");
    g.sync = apvts.getRawParameterValue ("sync");       g.bpm = apvts.getRawParameterValue ("bpm");
    g.freeRun = apvts.getRawParameterValue ("freeRun"); g.rate = apvts.getRawParameterValue ("rate");
    g.len = apvts.getRawParameterValue ("len");         g.dir = apvts.getRawParameterValue ("dir");
    g.swing = apvts.getRawParameterValue ("swing");     g.gate = apvts.getRawParameterValue ("gate");
    g.trans = apvts.getRawParameterValue ("trans");     g.mode = apvts.getRawParameterValue ("mode");
    g.laneOn = apvts.getRawParameterValue ("laneOn");   g.laneParam = apvts.getRawParameterValue ("laneParam");
    g.slot = apvts.getRawParameterValue ("slot");       g.thru = apvts.getRawParameterValue ("thru");
    g.sendOnPlay = apvts.getRawParameterValue ("sendOnPlay");

    bankUi.p[0] = sh32::makeDemoPattern();
    bankPending = bankUi;
    bankAudio   = bankUi;
}

juce::AudioProcessorEditor* SH32Processor::createEditor() { return new SH32Editor (*this); }

bool SH32Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

void SH32Processor::prepareToPlay (double, int)
{
    scratch.ensureSize (16384);
    engine.reset();
    lastInit = false;
    wasPlaying = false;
    lastFree = false;
    freePpq = 0.0;
}

void SH32Processor::publishBank()
{
    const juce::SpinLock::ScopedLockType sl (bankLock);
    bankPending = bankUi;
    bankDirty.store (true);
}

int SH32Processor::currentValue (int i) const
{
    const float v = paramRaw[(size_t) i]->load();
    if (sh32::kParams[i].kind == sh32::Kind::Toggle) return v >= 0.5f ? 1 : 0;
    return (int) std::lround (v);
}

void SH32Processor::applyIncomingCC (int i, int value)
{
    const auto& d = sh32::kParams[i];
    const int v = sh32::clampInt (value, d.minV, d.maxV);
    lastSent[(size_t) i] = v;                                  // évite de renvoyer la valeur à l'instrument (écho)
    auto* p = paramPtr[(size_t) i];
    p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
}

//==============================================================================
void SH32Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int n  = buffer.getNumSamples();
    const int ch = juce::jlimit (0, 15, (int) std::lround (g.ch->load()));

    if (bankDirty.load())
    {
        const juce::SpinLock::ScopedTryLockType tl (bankLock);
        if (tl.isLocked()) { bankAudio = bankPending; bankDirty.store (false); }
    }

    if (! lastInit)
    {
        for (int i = 0; i < sh32::kNumParams; ++i) lastSent[(size_t) i] = currentValue (i);
        lastInit = true;
    }

    scratch.clear();

    // ---- MIDI entrant : CC du SH-32 -> paramètres ; le reste est transmis si « Thru »
    const bool thru = g.thru->load() >= 0.5f;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        bool consumed = false;

        if (msg.isController() && msg.getChannel() - 1 == ch)
        {
            const int pi = ccToParam[(size_t) msg.getControllerNumber()];
            if (pi >= 0) { applyIncomingCC (pi, msg.getControllerValue()); consumed = true; }
        }
        if (! consumed && thru) scratch.addEvent (msg, meta.samplePosition);
    }

    // ---- clavier à l'écran
    keyboardState.processNextMidiBuffer (scratch, 0, n, true);

    auto emitCC = [&] (int cc, int val, int sample)
    {
        scratch.addEvent (juce::MidiMessage::controllerEvent (ch + 1, cc, val), sample);
    };

    // ---- transport
    bool hostPlaying = false, havePpq = false;
    double hostBpm = 0.0, hostPpq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) hostBpm = *b;
            if (auto q = pos->getPpqPosition()) { hostPpq = *q; havePpq = true; }
        }

    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const bool internal = g.sync->load() >= 0.5f;
    bool playing; double ppq, bpm;
    if (internal)
    {
        playing = g.freeRun->load() >= 0.5f;
        bpm = (double) g.bpm->load();
        if (playing && ! lastFree) freePpq = 0.0;
        lastFree = playing;
        ppq = freePpq;
        if (playing) freePpq += n * bpm / (60.0 * sr);
    }
    else
    {
        playing = hostPlaying && havePpq;
        bpm = hostBpm > 0.0 ? hostBpm : 120.0;
        ppq = hostPpq;
        lastFree = false;
    }

    // ---- demandes de l'interface
    bool sendAll = sendAllRequest.exchange (false);
    if (playing && ! wasPlaying && g.sendOnPlay->load() >= 0.5f) sendAll = true;
    wasPlaying = playing;

    const bool panic = panicRequest.exchange (false);
    if (panic)
    {
        evbuf.clear();
        engine.flush (evbuf, 0);
        for (int i = 0; i < evbuf.n; ++i)
            scratch.addEvent (juce::MidiMessage ((int) evbuf.ev[i].st, (int) evbuf.ev[i].d1, (int) evbuf.ev[i].d2), 0);
        emitCC (123, 0, 0);
        emitCC (64, 0, 0);
        keyboardState.allNotesOff (ch + 1);
    }

    const int pr = programRequest.exchange (-1);
    if (pr >= 0)
    {
        emitCC (0,  (pr >> 14) & 127, 0);
        emitCC (32, (pr >> 7) & 127, 0);
        scratch.addEvent (juce::MidiMessage::programChange (ch + 1, pr & 127), 0);
    }

    // ---- paramètres modifiés (interface ou automation) -> CC
    for (int i = 0; i < sh32::kNumParams; ++i)
    {
        const int cur = currentValue (i);
        if (! sendAll && cur == lastSent[(size_t) i]) continue;
        lastSent[(size_t) i] = cur;

        const auto& d = sh32::kParams[i];
        if (d.kind == sh32::Kind::Toggle)
        {
            if (cur) emitCC (d.onCC,  d.onVal,  0);
            else     emitCC (d.offCC, d.offVal, 0);
        }
        else emitCC (d.cc, cur, 0);
    }

    // ---- séquenceur
    sh32::Settings s;
    s.enabled = g.seqOn->load() >= 0.5f;
    s.rate    = kRateTable[juce::jlimit (0, 5, (int) std::lround (g.rate->load()))];
    s.len     = juce::jlimit (1, 16, (int) std::lround (g.len->load()) + 1);
    s.dir     = juce::jlimit (0, 3, (int) std::lround (g.dir->load()));
    s.swing   = (int) std::lround (g.swing->load());
    s.gate    = (int) std::lround (g.gate->load());
    s.trans   = (int) std::lround (g.trans->load());
    s.poly    = g.mode->load() >= 0.5f;
    s.laneOn  = g.laneOn->load() >= 0.5f;
    s.channel = ch;
    {
        const auto& ld = sh32::knobByOrdinal ((int) std::lround (g.laneParam->load()));
        s.laneCc = ld.cc; s.laneMin = ld.minV; s.laneMax = ld.maxV;
    }

    const int slot = juce::jlimit (0, sh32::kSlots - 1, (int) std::lround (g.slot->load()));
    evbuf.clear();
    engine.process (bankAudio.p[(size_t) slot], s, ppq, bpm, sr, n, playing, evbuf);
    for (int i = 0; i < evbuf.n; ++i)
        scratch.addEvent (juce::MidiMessage ((int) evbuf.ev[i].st, (int) evbuf.ev[i].d1, (int) evbuf.ev[i].d2),
                          evbuf.ev[i].sample);

    midi.swapWith (scratch);
}

//==============================================================================
void SH32Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int i = 0; i < sh32::kSlots; ++i)
        state.setProperty ("pat" + juce::String (i), juce::String (sh32::serialize (bankUi.p[(size_t) i])), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SH32Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName (apvts.state.getType())) return;
        const auto vt = juce::ValueTree::fromXml (*xml);

        apvts.replaceState (vt);

        for (int i = 0; i < sh32::kSlots; ++i)
        {
            const auto key = "pat" + juce::String (i);
            if (vt.hasProperty (key))
                sh32::deserialize (bankUi.p[(size_t) i], vt.getProperty (key).toString().toStdString());
        }
        publishBank();
        lastInit = false;      // pas de rafale de CC à la restauration de la session
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SH32Processor(); }
