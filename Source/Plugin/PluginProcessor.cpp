#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto stateType = "AtracGlitchState";

float loadFloat(const juce::AudioProcessorValueTreeState& parameters, const char* id) noexcept
{
    return parameters.getRawParameterValue(id)->load();
}
} // namespace

AtracGlitchAudioProcessor::AtracGlitchAudioProcessor()
    : AudioProcessor(createBusesProperties()),
      parameters(*this, nullptr, stateType, createParameterLayout())
{
    setLatencySamples(atracglitch::AtracGlitchEngine::latencySamples);
}

AtracGlitchAudioProcessor::BusesProperties AtracGlitchAudioProcessor::createBusesProperties()
{
    return BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

void AtracGlitchAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    engine.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void AtracGlitchAudioProcessor::releaseResources()
{
    engine.reset();
}

bool AtracGlitchAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && input == output;
}

void AtracGlitchAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const auto inputChannels = getTotalNumInputChannels();
    const auto outputChannels = getTotalNumOutputChannels();
    for(auto channel = inputChannels; channel < outputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    float* outputs[2] { nullptr, nullptr };
    const float* inputs[2] { nullptr, nullptr };
    const auto channels = std::min(buffer.getNumChannels(), 2);
    for(int channel = 0; channel < channels; ++channel)
    {
        inputs[channel] = buffer.getReadPointer(channel);
        outputs[channel] = buffer.getWritePointer(channel);
    }

    engine.process(outputs, inputs, channels, buffer.getNumSamples(), currentParameters());
}

juce::AudioProcessorEditor* AtracGlitchAudioProcessor::createEditor()
{
    return new AtracGlitchAudioProcessorEditor(*this);
}

void AtracGlitchAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if(auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void AtracGlitchAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if(xml == nullptr || ! xml->hasTagName(parameters.state.getType()))
        return;

    const auto tree = juce::ValueTree::fromXml(*xml);
    if(tree.isValid())
    {
        parameters.replaceState(tree);
        engine.reset();
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AtracGlitchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> layout;
    layout.push_back(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode", juce::StringArray { "Clean Codec", "SF Jitter", "Spectrum", "Word Length", "Freeze" }, 1));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>("density", "Density", 0.0f, 1.0f, 0.08f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>("amount", "Amount", 0.0f, 1.0f, 0.35f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>("bandwidth", "Bandwidth", 0.0f, 1.0f, 1.0f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    layout.push_back(std::make_unique<juce::AudioParameterFloat>(
        "output",
        "Output",
        juce::NormalisableRange<float>(-24.0f, 6.0f, 0.1f),
        -3.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    layout.push_back(std::make_unique<juce::AudioParameterInt>("seed", "Seed", 1, 2147483647, 1));
    return { layout.begin(), layout.end() };
}

atracglitch::Parameters AtracGlitchAudioProcessor::currentParameters() const noexcept
{
    atracglitch::Parameters snapshot;
    snapshot.mode = static_cast<atracglitch::GlitchMode>(std::clamp(
        static_cast<int>(std::lround(loadFloat(parameters, "mode"))), 0, 4));
    snapshot.density = loadFloat(parameters, "density");
    snapshot.amount = loadFloat(parameters, "amount");
    snapshot.bandwidth = loadFloat(parameters, "bandwidth");
    snapshot.mix = loadFloat(parameters, "mix");
    snapshot.outputDb = loadFloat(parameters, "output");
    snapshot.seed = static_cast<std::uint32_t>(std::max(1, static_cast<int>(std::lround(loadFloat(parameters, "seed")))));
    return snapshot;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AtracGlitchAudioProcessor();
}
