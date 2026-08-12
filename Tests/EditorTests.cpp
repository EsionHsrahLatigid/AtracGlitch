#include "Plugin/PluginEditor.h"
#include "Plugin/PluginProcessor.h"

#include <algorithm>
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

bool isPaperTone(juce::Colour colour)
{
    const auto minimum = std::min({ colour.getRed(), colour.getGreen(), colour.getBlue() });
    const auto maximum = std::max({ colour.getRed(), colour.getGreen(), colour.getBlue() });
    // The shared module owns the exact palette test. At this integration
    // boundary, require a bright low-chroma rule that cannot be confused with
    // EHL mid (138) while allowing backend-specific offscreen quantisation.
    return minimum >= 220 && maximum - minimum <= 8;
}

bool isInkTone(juce::Colour colour)
{
    // This remains below EHL low (42), so the body cannot pass with another
    // palette token or a platform accent colour.
    return std::max({ colour.getRed(), colour.getGreen(), colour.getBlue() }) <= 16;
}

template <typename Predicate>
int countMatchingPixels(const juce::Image& image, juce::Rectangle<int> area,
                        Predicate&& predicate)
{
    int count = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (predicate(image.getPixelAt(x, y)))
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
    {
        juce::Graphics graphics(image);
        editor->paint(graphics);
    }
    const auto chromeProbe = editor->getLocalBounds().reduced(ehl::juce_design::Metrics::margin, 0);
    const auto topRuleProbe = chromeProbe.withY(1).withHeight(2);
    require(countMatchingPixels(image, topRuleProbe, isPaperTone)
                == topRuleProbe.getWidth() * topRuleProbe.getHeight(),
            "shared EHL top rule is missing");
    const auto bodyProbe = chromeProbe.withY(ehl::juce_design::Metrics::headerHeight + 4)
                           .withHeight(2);
    require(countMatchingPixels(image, bodyProbe, isInkTone)
                == bodyProbe.getWidth() * bodyProbe.getHeight(),
            "editor body is not EHL ink");

    return 0;
}
