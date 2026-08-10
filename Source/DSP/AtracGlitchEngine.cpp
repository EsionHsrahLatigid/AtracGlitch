#include "AtracGlitchEngine.h"

#include <algorithm>
#include <cmath>

namespace atracglitch
{
namespace
{
constexpr float silenceThreshold = 1.0e-20f;
}

void AtracGlitchEngine::prepare(const double sampleRate,
                                const int maximumBlockSize,
                                const int channels) noexcept
{
    (void) sampleRate;
    (void) maximumBlockSize;
    preparedChannels = std::clamp(channels, 1, maxChannels);
    reset();
}

void AtracGlitchEngine::reset() noexcept
{
    for(auto& frame : inputFrames)
        frame.fill(0.0f);
    for(auto& frame : wetFrames)
        frame.fill(0.0f);
    for(auto& frame : dryFrames)
        frame.fill(0.0f);
    for(auto& frame : previousFrames)
        frame.bytes.fill(0);

    hasPrevious.fill(false);
    framePosition = 0;
    randomStates[0] = activeSeed == 0 ? 1u : activeSeed;
    randomStates[1] = randomStates[0] ^ 0x9e3779b9u;
}

void AtracGlitchEngine::process(float* const* outputs,
                                const float* const* inputs,
                                const int channels,
                                const int samples,
                                const Parameters& parameters) noexcept
{
    const auto channelsToProcess = std::clamp(channels, 0, preparedChannels);
    if(parameters.seed != activeSeed)
    {
        activeSeed = parameters.seed == 0 ? 1u : parameters.seed;
        randomStates[0] = activeSeed;
        randomStates[1] = activeSeed ^ 0x9e3779b9u;
        hasPrevious.fill(false);
    }

    const auto mix = std::clamp(parameters.mix, 0.0f, 1.0f);
    const auto gain = std::exp2(parameters.outputDb / 6.020599913f);

    for(int sample = 0; sample < samples; ++sample)
    {
        for(int channel = 0; channel < channelsToProcess; ++channel)
        {
            const auto input = inputs != nullptr && inputs[channel] != nullptr
                             ? sanitize(inputs[channel][sample]) : 0.0f;
            inputFrames[static_cast<std::size_t>(channel)][framePosition] = input;

            const auto dry = dryFrames[static_cast<std::size_t>(channel)][framePosition];
            const auto wet = wetFrames[static_cast<std::size_t>(channel)][framePosition];
            auto output = (dry + (wet - dry) * mix) * gain;
            if(mix > 0.0f)
                output = protectOutput(output);
            outputs[channel][sample] = sanitize(output);
        }

        ++framePosition;
        if(framePosition == samplesPerFrame)
        {
            renderFrame(parameters, channelsToProcess);
            framePosition = 0;
        }
    }
}

void AtracGlitchEngine::renderFrame(const Parameters& parameters, const int channels) noexcept
{
    for(int channel = 0; channel < channels; ++channel)
    {
        auto encoded = EncodedFrame {};
        codec.encode(inputFrames[static_cast<std::size_t>(channel)], parameters.bandwidth, encoded);

        if(parameters.mode == GlitchMode::freeze)
        {
            if(hasPrevious[static_cast<std::size_t>(channel)]
                && randomUnit(randomStates[static_cast<std::size_t>(channel)]) < std::clamp(parameters.density, 0.0f, 1.0f))
            {
                encoded = previousFrames[static_cast<std::size_t>(channel)];
            }
            else
            {
                previousFrames[static_cast<std::size_t>(channel)] = encoded;
                hasPrevious[static_cast<std::size_t>(channel)] = true;
            }
        }
        else
        {
            previousFrames[static_cast<std::size_t>(channel)] = encoded;
            hasPrevious[static_cast<std::size_t>(channel)] = true;
            mutate(encoded,
                   parameters.mode,
                   parameters.density,
                   parameters.amount,
                   randomStates[static_cast<std::size_t>(channel)]);
        }

        codec.decode(encoded, wetFrames[static_cast<std::size_t>(channel)]);
        dryFrames[static_cast<std::size_t>(channel)] = inputFrames[static_cast<std::size_t>(channel)];
    }
}

void AtracGlitchEngine::mutate(EncodedFrame& frame,
                               const GlitchMode mode,
                               const float density,
                               const float amount,
                               std::uint32_t& randomState) noexcept
{
    if(mode == GlitchMode::clean || mode == GlitchMode::freeze)
        return;

    const auto probability = std::clamp(density, 0.0f, 1.0f);
    const auto strength = std::clamp(amount, 0.0f, 1.0f);
    const auto layout = AtracCodec::inspect(frame);

    if(mode == GlitchMode::scaleFactor)
    {
        const auto maximumDelta = 1 + static_cast<int>(std::lround(strength * 11.0f));
        for(int bfu = 0; bfu < layout.numBfus; ++bfu)
        {
            if(randomUnit(randomState) >= probability)
                continue;
            const auto bit = layout.scaleFactorStart + bfu * 6;
            const auto oldValue = AtracCodec::getBits(frame, bit, 6);
            auto delta = static_cast<int>(nextRandom(randomState) % static_cast<std::uint32_t>(maximumDelta * 2 + 1)) - maximumDelta;
            if(delta == 0)
                delta = oldValue < 63 ? 1 : -1;
            AtracCodec::setBits(frame, bit, 6, std::clamp(oldValue + delta, 0, 63));
        }
        return;
    }

    if(mode == GlitchMode::wordLength)
    {
        const auto flips = 1 + static_cast<int>(std::lround(strength * 3.0f));
        for(int bfu = 0; bfu < layout.numBfus; ++bfu)
        {
            if(randomUnit(randomState) >= probability)
                continue;
            const auto fieldStart = layout.wordLengthStart + bfu * 4;
            for(int i = 0; i < flips; ++i)
                AtracCodec::flipBit(frame, fieldStart + static_cast<int>(nextRandom(randomState) & 3u));
        }
        return;
    }

    const auto bitProbability = probability * (0.001f + strength * 0.019f);
    for(int bit = layout.spectrumStart; bit < layout.spectrumEnd; ++bit)
    {
        if(randomUnit(randomState) < bitProbability)
            AtracCodec::flipBit(frame, bit);
    }
}

std::uint32_t AtracGlitchEngine::nextRandom(std::uint32_t& state) noexcept
{
    if(state == 0)
        state = 0x6d2b79f5u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float AtracGlitchEngine::randomUnit(std::uint32_t& state) noexcept
{
    return static_cast<float>(nextRandom(state) >> 8) * (1.0f / 16777216.0f);
}

float AtracGlitchEngine::sanitize(const float sample) noexcept
{
    if(! std::isfinite(sample) || std::abs(sample) < silenceThreshold)
        return 0.0f;
    return sample;
}

float AtracGlitchEngine::protectOutput(const float sample) noexcept
{
    const auto safe = std::clamp(sample, -8.0f, 8.0f);
    if(std::abs(safe) <= 1.0f)
        return safe;
    return std::copysign(1.0f + 0.25f * std::tanh(std::abs(safe) - 1.0f), safe);
}
} // namespace atracglitch

