#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>

#include "Params.h"
#include "Pattern.h"
#include "SeqEngine.h"

//==============================================================================
// SH-32 Controller : plugin VST3 « instrument » sans audio, qui produit du MIDI.
//  - chaque paramètre du SH-32 est un paramètre hôte (automatisable) émis en Control Change ;
//  - un séquenceur mono / polyphonique synchronisé sur le transport de l'hôte ;
//  - les CC reçus (potards du SH-32 renvoyés par le MIDI) mettent à jour les paramètres.
//==============================================================================
class SH32Processor : public juce::AudioProcessor
{
public:
    SH32Processor();
    ~SH32Processor() override = default;

    // --- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SH-32 Controller"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- accès pour l'éditeur (thread message)
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    sh32::PatternBank& uiBank() { return bankUi; }
    void publishBank();                     // à appeler après toute modification de uiBank()

    int  currentStepForUi() const { return engine.currentStep(); }

    // requêtes UI -> audio
    std::atomic<bool> sendAllRequest { false };
    std::atomic<bool> panicRequest   { false };
    std::atomic<int>  programRequest { -1 };     // (bankMSB << 14) | (bankLSB << 7) | programme

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    struct Globals
    {
        std::atomic<float> *ch = nullptr, *seqOn = nullptr, *sync = nullptr, *bpm = nullptr, *freeRun = nullptr,
                           *rate = nullptr, *len = nullptr, *dir = nullptr, *swing = nullptr, *gate = nullptr,
                           *trans = nullptr, *mode = nullptr, *laneOn = nullptr, *laneParam = nullptr,
                           *slot = nullptr, *thru = nullptr, *sendOnPlay = nullptr;
    } g;

    std::array<std::atomic<float>*, (size_t) sh32::kNumParams>        paramRaw {};
    std::array<juce::RangedAudioParameter*, (size_t) sh32::kNumParams> paramPtr {};
    std::array<int, 128>                                               ccToParam {};
    std::array<int, (size_t) sh32::kNumParams>                         lastSent {};
    std::atomic<bool> lastInit { false };   // remis à false par le thread message (restauration de session)
    bool wasPlaying = false, lastFree = false;
    double freePpq = 0.0;

    // banques de patterns : édition (UI), file d'attente, copie lue par le thread audio
    sh32::PatternBank bankUi, bankPending, bankAudio;
    juce::SpinLock    bankLock;
    std::atomic<bool> bankDirty { false };

    sh32::Engine      engine;
    sh32::EventBuffer evbuf;
    juce::MidiBuffer  scratch;

    int  currentValue (int paramIndex) const;
    void applyIncomingCC (int paramIndex, int value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SH32Processor)
};
