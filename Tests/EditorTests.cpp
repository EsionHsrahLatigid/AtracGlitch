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

bool isCloseTo(juce::Colour actual, juce::Colour expected)
{
    // Offscreen RGB backends may differ by two codes on the near-white paper
    // channels; this remains far below the smallest EHL palette gap.
    constexpr int channelTolerance = 2;
    return actual.getAlpha() == expected.getAlpha()
           && std::abs(actual.getRed() - expected.getRed()) <= channelTolerance
           && std::abs(actual.getGreen() - expected.getGreen()) <= channelTolerance
           && std::abs(actual.getBlue() - expected.getBlue()) <= channelTolerance;
}

int countClosePixels(const juce::Image& image, juce::Rectangle<int> area,
                     juce::Colour expected)
{
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (isCloseTo(image.getPixelAt(x, y), expected))
                ++count;
    return count;
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
    const auto chromeProbe = editor->getLocalBounds().reduced(ehl::juce_design::Metrics::margin, 0);
    const auto topRuleProbe = chromeProbe.withY(1).withHeight(2);
    require(countClosePixels(image, topRuleProbe, ehl::juce_design::Palette::paper())
                == topRuleProbe.getWidth() * topRuleProbe.getHeight(),
            "shared EHL top rule is missing");
    const auto bodyProbe = chromeProbe.withY(ehl::juce_design::Metrics::headerHeight + 4)
                           .withHeight(2);
    require(countClosePixels(image, bodyProbe, ehl::juce_design::Palette::ink())
                == bodyProbe.getWidth() * bodyProbe.getHeight(),
            "editor body is not EHL ink");

    return 0;
}
