#pragma once

#include "PluginProcessor.h"

// Fenêtre du plugin. Le contenu est défini dans PluginEditor.cpp.
class SH32Editor : public juce::AudioProcessorEditor
{
public:
    explicit SH32Editor (SH32Processor&);
    ~SH32Editor() override;

    void resized() override;

private:
    std::unique_ptr<juce::Component> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SH32Editor)
};
