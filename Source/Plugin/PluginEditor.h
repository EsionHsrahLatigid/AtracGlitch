#pragma once

#include <ehl/juce_design/EhlDesign.h>
#include <juce_audio_processors/juce_audio_processors.h>

class AtracGlitchAudioProcessor;

class AtracGlitchAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AtracGlitchAudioProcessorEditor(AtracGlitchAudioProcessor&);
    ~AtracGlitchAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultWidth = ehl::juce_design::Metrics::defaultWidth;
    static constexpr int defaultHeight = ehl::juce_design::Metrics::defaultHeight;
    static constexpr int minimumWidth = ehl::juce_design::Metrics::minimumWidth;
    static constexpr int minimumHeight = ehl::juce_design::Metrics::minimumHeight;

private:
    ehl::juce_design::LookAndFeel ehlLookAndFeel;
    juce::Viewport controlsViewport;
    juce::GenericAudioProcessorEditor parameterEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AtracGlitchAudioProcessorEditor)
};
