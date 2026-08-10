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

    void encode(const std::array<float, samplesPerFrame>& input,
                float bandwidth,
                EncodedFrame& output) const noexcept;
    void decode(const EncodedFrame& input,
                std::array<float, samplesPerFrame>& output) const noexcept;

    static FrameLayout inspect(const EncodedFrame& frame) noexcept;
    static int getBits(const EncodedFrame& frame, int bitPosition, int bitCount) noexcept;
    static void setBits(EncodedFrame& frame, int bitPosition, int bitCount, int value) noexcept;
    static void flipBit(EncodedFrame& frame, int bitPosition) noexcept;

private:
    using TransformTable = std::array<float, samplesPerFrame * samplesPerFrame>;

    void transform(const std::array<float, samplesPerFrame>& input,
                   std::array<float, samplesPerFrame>& output) const noexcept;

    TransformTable transformTable {};
};
} // namespace atracglitch

