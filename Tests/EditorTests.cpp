#include "Plugin/PluginEditor.h"
#include "Plugin/PluginProcessor.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    AtracGlitchAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());

    require(dynamic_cast<AtracGlitchAudioProcessorEditor*>(editor.get()) != nullptr,
            "createEditor must return the branded ATRAC editor");
    require(editor->getWidth() == AtracGlitchAudioProcessorEditor::defaultWidth,
            "unexpected default editor width");
    require(editor->getHeight() == AtracGlitchAudioProcessorEditor::defaultHeight,
            "unexpected default editor height");
    require(editor->getWantsKeyboardFocus(), "editor should accept keyboard focus");
    require(editor->findChildWithID("atrac-glitch-control-viewport") != nullptr,
            "editor must contain the scrollable parameter surface");

    editor->setBounds(0, 0, AtracGlitchAudioProcessorEditor::minimumWidth,
                      AtracGlitchAudioProcessorEditor::minimumHeight);
    editor->resized();
    auto* viewport = editor->findChildWithID("atrac-glitch-control-viewport");
    require(viewport != nullptr && editor->getLocalBounds().contains(viewport->getBounds()),
            "parameter viewport must remain inside the compact editor");

    juce::Image image(juce::Image::RGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics graphics(image);
    editor->paint(graphics);
    require(image.getPixelAt(0, 2) == ehl::juce_design::Palette::paper(),
            "shared EHL top rule is missing");
    require(image.getPixelAt(0, ehl::juce_design::Metrics::headerHeight + 4)
                == ehl::juce_design::Palette::ink(),
            "editor body is not EHL ink");

    return 0;
}
