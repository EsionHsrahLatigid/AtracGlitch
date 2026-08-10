#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
juce::AudioProcessorParameter* findParameter(juce::AudioPluginInstance& instance,
                                              const juce::String& name)
{
    for(auto* parameter : instance.getParameters())
        if(parameter != nullptr && parameter->getName(128) == name)
            return parameter;
    return nullptr;
}
}

int main(int argc, char** argv)
{
    if(argc != 3)
    {
        std::cerr << "usage: AtracGlitchPluginLoadTests <vst3-bundle> <expected-name>\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::AudioPluginFormatManager manager;
    juce::addHeadlessDefaultFormatsToManager(manager);

    juce::AudioPluginFormat* vst3 = nullptr;
    for(auto* format : manager.getFormats())
        if(format != nullptr && format->getName().containsIgnoreCase("VST3"))
            vst3 = format;
    if(vst3 == nullptr)
    {
        std::cerr << "VST3 host format is unavailable\n";
        return 3;
    }

    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3->findAllTypesForFile(descriptions, juce::String(argv[1]));
    if(descriptions.isEmpty())
    {
        std::cerr << "no VST3 type found in bundle\n";
        return 4;
    }

    auto* description = descriptions.getFirst();
    if(description == nullptr || (description->name != argv[2] && description->descriptiveName != argv[2]))
    {
        std::cerr << "unexpected plug-in name\n";
        return 5;
    }

    juce::String error;
    auto instance = manager.createPluginInstance(*description, 48000.0, 128, error);
    if(instance == nullptr)
    {
        std::cerr << "VST3 instantiation failed: " << error << '\n';
        return 6;
    }
    instance->prepareToPlay(48000.0, 128);
    if(instance->getLatencySamples() != 512)
    {
        std::cerr << "hosted latency contract is not 512 samples\n";
        return 7;
    }

    constexpr const char* requiredParameters[] {
        "Mode", "Density", "Amount", "Bandwidth", "Mix", "Output", "Seed"
    };
    for(const auto* name : requiredParameters)
    {
        if(findParameter(*instance, name) == nullptr)
        {
            std::cerr << "missing hosted parameter: " << name << '\n';
            return 8;
        }
    }

    auto* density = findParameter(*instance, "Density");
    density->setValueNotifyingHost(0.73f);
    juce::MemoryBlock state;
    instance->getStateInformation(state);
    density->setValueNotifyingHost(0.0f);
    instance->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if(state.isEmpty() || std::abs(density->getValue() - 0.73f) > 1.0e-4f)
    {
        std::cerr << "hosted state did not round-trip\n";
        return 9;
    }

    double energy = 0.0;
    float peak = 0.0f;
    bool finite = true;
    for(int block = 0; block < 16; ++block)
    {
        juce::AudioBuffer<float> audio(2, 128);
        juce::MidiBuffer midi;
        for(int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            const auto position = block * audio.getNumSamples() + sample;
            const auto value = 0.3f * std::sin(static_cast<float>(position) * 0.071f);
            audio.setSample(0, sample, value);
            audio.setSample(1, sample, value * 0.8f);
        }

        instance->processBlock(audio, midi);
        for(int channel = 0; channel < audio.getNumChannels(); ++channel)
            for(int sample = 0; sample < audio.getNumSamples(); ++sample)
            {
                const auto value = audio.getSample(channel, sample);
                finite &= std::isfinite(value);
                peak = std::max(peak, std::abs(value));
                energy += static_cast<double>(value) * value;
            }
    }
    instance->releaseResources();

    if(! finite || energy <= 1.0e-5 || peak > 1.26f)
    {
        std::cerr << "hosted render contract failed: finite=" << finite
                  << " energy=" << energy << " peak=" << peak << '\n';
        return 10;
    }

    std::cout << "loaded=" << description->name
              << " latency=" << instance->getLatencySamples()
              << " energy=" << energy
              << " peak=" << peak << '\n';
    return 0;
}
