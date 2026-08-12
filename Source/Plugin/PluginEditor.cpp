#include "PluginEditor.h"
#include "PluginProcessor.h"

AtracGlitchAudioProcessorEditor::AtracGlitchAudioProcessorEditor(AtracGlitchAudioProcessor& owner)
    : AudioProcessorEditor(owner), parameterEditor(owner)
{
    setLookAndFeel(&ehlLookAndFeel);
    parameterEditor.setLookAndFeel(&ehlLookAndFeel);
    parameterEditor.setName("ATRAC Glitch parameter grid");
    parameterEditor.setDescription("All ATRAC codec-corruption parameters in one compact grid.");
    parameterEditor.setComponentID("atrac-glitch-generic-controls");
    controlsViewport.setComponentID("atrac-glitch-control-viewport");
    controlsViewport.setViewedComponent(&parameterEditor, false);
    controlsViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(controlsViewport);

    setName("ATRAC Glitch editor");
    setComponentID("atrac-glitch-editor");
    setTitle("ATRAC Glitch");
    setDescription("Monochrome 8-bit ATRAC codec-corruption editor");
    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(minimumWidth, minimumHeight,
                    ehl::juce_design::Metrics::maximumWidth,
                    ehl::juce_design::Metrics::maximumHeight);
    setSize(defaultWidth, defaultHeight);
}

AtracGlitchAudioProcessorEditor::~AtracGlitchAudioProcessorEditor()
{
    parameterEditor.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void AtracGlitchAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    ehl::juce_design::paintEditorChrome(graphics, getLocalBounds(),
                                         "ATRAC Glitch", "ATRAC CODEC CORRUPTION");
}

void AtracGlitchAudioProcessorEditor::resized()
{
    const auto body = getLocalBounds()
                          .withTrimmedTop(ehl::juce_design::Metrics::headerHeight + 8)
                          .reduced(ehl::juce_design::Metrics::margin, 8);
    controlsViewport.setBounds(body);
    parameterEditor.setBounds(0, 0, body.getWidth(),
                              juce::jmax(body.getHeight(), parameterEditor.getHeight()));
}
