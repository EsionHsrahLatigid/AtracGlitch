#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace atracglitch
{
constexpr std::size_t soundUnitBytes = 212;
constexpr std::size_t samplesPerFrame = 512;
constexpr std::size_t maxBfus = 52;

struct EncodedFrame
{
    std::array<std::uint8_t, soundUnitBytes> bytes {};
};

struct FrameLayout
{
    int numBfus = 0;
    int wordLengthStart = 16;
    int scaleFactorStart = 16;
    int spectrumStart = 16;
    int spectrumEnd = 16;
};

class AtracCodec
{
public:
    AtracCodec();

    void reset() noexcept;

    void encode(const std::array<float, samplesPerFrame>& input,
                float bandwidth,
                EncodedFrame& output) noexcept;
    void decode(const EncodedFrame& input,
                std::array<float, samplesPerFrame>& output) noexcept;

    static FrameLayout inspect(const EncodedFrame& frame) noexcept;
    static int getBits(const EncodedFrame& frame, int bitPosition, int bitCount) noexcept;
    static void setBits(EncodedFrame& frame, int bitPosition, int bitCount, int value) noexcept;
    static void flipBit(EncodedFrame& frame, int bitPosition) noexcept;

private:
    std::array<float, samplesPerFrame + 46> analysisFirst {};
    std::array<float, samplesPerFrame / 2 + 46> analysisSecond {};
    std::array<float, samplesPerFrame / 2 + 39> analysisHighDelay {};
    std::array<float, 32> encodeLowTail {};
    std::array<float, 32> encodeMidTail {};
    std::array<float, 32> encodeHighTail {};

    std::array<float, samplesPerFrame + 46> synthesisFirst {};
    std::array<float, samplesPerFrame / 2 + 46> synthesisSecond {};
    std::array<float, samplesPerFrame / 2 + 39> synthesisHighDelay {};
    std::array<float, 16> decodeLowOverlap {};
    std::array<float, 16> decodeMidOverlap {};
    std::array<float, 16> decodeHighOverlap {};
};
} // namespace atracglitch
