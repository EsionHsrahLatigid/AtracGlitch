#pragma once

#include "AtracCodec.h"

#include <array>
#include <cstdint>

namespace atracglitch
{
enum class GlitchMode
{
    clean = 0,
    scaleFactor,
    spectrum,
    wordLength,
    freeze
};

struct Parameters
{
    GlitchMode mode = GlitchMode::scaleFactor;
    float density = 0.08f;
    float amount = 0.35f;
    float bandwidth = 1.0f;
    float mix = 1.0f;
    float outputDb = -3.0f;
    std::uint32_t seed = 1;
};

class AtracGlitchEngine
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int latencySamples = static_cast<int>(samplesPerFrame);

    void prepare(double sampleRate, int maximumBlockSize, int channels) noexcept;
    void reset() noexcept;
    void process(float* const* outputs,
                 const float* const* inputs,
                 int channels,
                 int samples,
                 const Parameters& parameters) noexcept;

    static void mutate(EncodedFrame& frame,
                       GlitchMode mode,
                       float density,
                       float amount,
                       std::uint32_t& randomState) noexcept;

private:
    static std::uint32_t nextRandom(std::uint32_t& state) noexcept;
    static float randomUnit(std::uint32_t& state) noexcept;
    static float sanitize(float sample) noexcept;
    static float protectOutput(float sample) noexcept;

    void renderFrame(const Parameters& parameters, int channels) noexcept;

    AtracCodec codec;
    std::array<std::array<float, samplesPerFrame>, maxChannels> inputFrames {};
    std::array<std::array<float, samplesPerFrame>, maxChannels> wetFrames {};
    std::array<std::array<float, samplesPerFrame>, maxChannels> dryFrames {};
    std::array<EncodedFrame, maxChannels> previousFrames {};
    std::array<bool, maxChannels> hasPrevious {};
    std::array<std::uint32_t, maxChannels> randomStates { 1u, 0x9e3779b9u };
    std::size_t framePosition = 0;
    std::uint32_t activeSeed = 1;
    int preparedChannels = 2;
};
} // namespace atracglitch

