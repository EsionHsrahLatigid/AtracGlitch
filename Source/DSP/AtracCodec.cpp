#include "AtracCodec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace atracglitch
{
namespace
{
constexpr std::array<int, 8> bfuAmounts { 20, 28, 32, 36, 40, 44, 48, 52 };
constexpr std::array<int, maxBfus> specsPerBfu {
    8, 8, 8, 8, 4, 4, 4, 4, 8, 8, 8, 8, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 7, 7, 7, 7, 9, 9, 9, 9, 10, 10, 10, 10,
    12, 12, 12, 12, 12, 12, 12, 12, 20, 20, 20, 20, 20, 20, 20, 20
};
constexpr std::array<int, maxBfus> bfuStarts {
    0, 8, 16, 24, 32, 36, 40, 44, 48, 56, 64, 72, 80, 86, 92, 98, 104, 110, 116, 122,
    128, 134, 140, 146, 152, 159, 166, 173, 180, 189, 198, 207, 216, 226, 236, 246,
    256, 268, 280, 292, 304, 316, 328, 340, 352, 372, 392, 412, 432, 452, 472, 492
};
constexpr float pi = 3.14159265358979323846f;

float scaleFactor(const int index) noexcept
{
    return std::exp2((static_cast<float>(index) - 15.0f) / 3.0f);
}

int scaleFactorIndex(const float peak) noexcept
{
    if(! std::isfinite(peak) || peak <= 0.0f)
        return 0;

    const auto raw = 15.0f + 3.0f * std::log2(peak);
    return std::clamp(static_cast<int>(std::ceil(raw)), 0, 63);
}

int signedFromBits(const int raw, const int bits) noexcept
{
    const auto sign = 1 << (bits - 1);
    return (raw & sign) != 0 ? raw - (1 << bits) : raw;
}
} // namespace

AtracCodec::AtracCodec()
{
    const auto normalization = std::sqrt(2.0f / static_cast<float>(samplesPerFrame));
    for(std::size_t k = 0; k < samplesPerFrame; ++k)
    {
        for(std::size_t n = 0; n < samplesPerFrame; ++n)
        {
            const auto phase = pi / static_cast<float>(samplesPerFrame)
                             * (static_cast<float>(n) + 0.5f)
                             * (static_cast<float>(k) + 0.5f);
            transformTable[k * samplesPerFrame + n] = normalization * std::cos(phase);
        }
    }
}

void AtracCodec::transform(const std::array<float, samplesPerFrame>& input,
                           std::array<float, samplesPerFrame>& output) const noexcept
{
    for(std::size_t k = 0; k < samplesPerFrame; ++k)
    {
        float sum = 0.0f;
        const auto row = k * samplesPerFrame;
        for(std::size_t n = 0; n < samplesPerFrame; ++n)
            sum += input[n] * transformTable[row + n];
        output[k] = sum;
    }
}

void AtracCodec::encode(const std::array<float, samplesPerFrame>& input,
                        const float bandwidth,
                        EncodedFrame& output) const noexcept
{
    output.bytes.fill(0);

    std::array<float, samplesPerFrame> spectrum {};
    transform(input, spectrum);

    const auto amountIndex = std::clamp(static_cast<int>(std::lround(std::clamp(bandwidth, 0.0f, 1.0f) * 7.0f)), 0, 7);
    const auto numBfus = bfuAmounts[static_cast<std::size_t>(amountIndex)];

    // Valid long-block mode byte for FFmpeg's ATRAC1 parser: low=2, mid=2, high=3.
    output.bytes[0] = 0xac;
    output.bytes[1] = static_cast<std::uint8_t>(amountIndex << 5);

    std::array<int, maxBfus> wordLengths {};
    std::array<int, maxBfus> scaleIndices {};
    std::array<float, maxBfus> peaks {};

    auto remainingBits = static_cast<int>(soundUnitBytes * 8) - (numBfus * 10 + 32);
    for(int bfu = 0; bfu < numBfus; ++bfu)
    {
        const auto start = bfuStarts[static_cast<std::size_t>(bfu)];
        const auto count = specsPerBfu[static_cast<std::size_t>(bfu)];
        float peak = 0.0f;
        for(int i = 0; i < count; ++i)
            peak = std::max(peak, std::abs(spectrum[static_cast<std::size_t>(start + i)]));

        peaks[static_cast<std::size_t>(bfu)] = peak;
        scaleIndices[static_cast<std::size_t>(bfu)] = scaleFactorIndex(peak);
        if(peak > 1.0e-7f && remainingBits >= count * 2)
        {
            wordLengths[static_cast<std::size_t>(bfu)] = 2;
            remainingBits -= count * 2;
        }
    }

    while(remainingBits > 0)
    {
        int bestBfu = -1;
        float bestScore = -1.0f;
        for(int bfu = 0; bfu < numBfus; ++bfu)
        {
            const auto wordLength = wordLengths[static_cast<std::size_t>(bfu)];
            const auto cost = specsPerBfu[static_cast<std::size_t>(bfu)];
            if(wordLength < 2 || wordLength >= 16 || cost > remainingBits)
                continue;

            const auto score = peaks[static_cast<std::size_t>(bfu)]
                             / std::exp2(static_cast<float>(wordLength - 1))
                             / std::sqrt(static_cast<float>(cost));
            if(score > bestScore)
            {
                bestScore = score;
                bestBfu = bfu;
            }
        }

        if(bestBfu < 0)
            break;

        ++wordLengths[static_cast<std::size_t>(bestBfu)];
        remainingBits -= specsPerBfu[static_cast<std::size_t>(bestBfu)];
    }

    auto wordLengthBit = 16;
    const auto scaleFactorBit = wordLengthBit + numBfus * 4;
    auto spectrumBit = scaleFactorBit + numBfus * 6;

    for(int bfu = 0; bfu < numBfus; ++bfu)
    {
        const auto wordLength = wordLengths[static_cast<std::size_t>(bfu)];
        setBits(output, wordLengthBit + bfu * 4, 4, wordLength == 0 ? 0 : wordLength - 1);
        setBits(output, scaleFactorBit + bfu * 6, 6, scaleIndices[static_cast<std::size_t>(bfu)]);
    }

    for(int bfu = 0; bfu < numBfus; ++bfu)
    {
        const auto wordLength = wordLengths[static_cast<std::size_t>(bfu)];
        if(wordLength == 0)
            continue;

        const auto start = bfuStarts[static_cast<std::size_t>(bfu)];
        const auto count = specsPerBfu[static_cast<std::size_t>(bfu)];
        const auto maximum = (1 << (wordLength - 1)) - 1;
        const auto scale = scaleFactor(scaleIndices[static_cast<std::size_t>(bfu)]);
        for(int i = 0; i < count; ++i)
        {
            const auto normalized = spectrum[static_cast<std::size_t>(start + i)] / std::max(scale, 1.0e-12f);
            const auto quantized = std::clamp(static_cast<int>(std::lround(normalized * static_cast<float>(maximum))),
                                              -maximum, maximum);
            setBits(output, spectrumBit, wordLength, quantized);
            spectrumBit += wordLength;
        }
    }
}

void AtracCodec::decode(const EncodedFrame& input,
                        std::array<float, samplesPerFrame>& output) const noexcept
{
    std::array<float, samplesPerFrame> spectrum {};
    const auto layout = inspect(input);
    std::array<int, maxBfus> wordLengths {};
    std::array<int, maxBfus> scaleIndices {};

    for(int bfu = 0; bfu < layout.numBfus; ++bfu)
    {
        const auto idwl = getBits(input, layout.wordLengthStart + bfu * 4, 4);
        wordLengths[static_cast<std::size_t>(bfu)] = idwl == 0 ? 0 : idwl + 1;
        scaleIndices[static_cast<std::size_t>(bfu)] = getBits(input, layout.scaleFactorStart + bfu * 6, 6);
    }

    auto bit = layout.spectrumStart;
    constexpr auto totalBits = static_cast<int>(soundUnitBytes * 8);
    for(int bfu = 0; bfu < layout.numBfus; ++bfu)
    {
        const auto wordLength = wordLengths[static_cast<std::size_t>(bfu)];
        if(wordLength == 0)
            continue;

        const auto start = bfuStarts[static_cast<std::size_t>(bfu)];
        const auto count = specsPerBfu[static_cast<std::size_t>(bfu)];
        const auto maximum = (1 << (wordLength - 1)) - 1;
        const auto scale = scaleFactor(scaleIndices[static_cast<std::size_t>(bfu)]);
        for(int i = 0; i < count; ++i)
        {
            if(bit + wordLength > totalBits)
                break;
            const auto quantized = signedFromBits(getBits(input, bit, wordLength), wordLength);
            spectrum[static_cast<std::size_t>(start + i)] = std::clamp(
                static_cast<float>(quantized) * scale / static_cast<float>(maximum), -32.0f, 32.0f);
            bit += wordLength;
        }
    }

    transform(spectrum, output);
    for(auto& sample : output)
    {
        if(! std::isfinite(sample))
            sample = 0.0f;
        sample = std::clamp(sample, -8.0f, 8.0f);
    }
}

FrameLayout AtracCodec::inspect(const EncodedFrame& frame) noexcept
{
    FrameLayout layout;
    const auto amountIndex = getBits(frame, 8, 3);
    layout.numBfus = bfuAmounts[static_cast<std::size_t>(std::clamp(amountIndex, 0, 7))];
    layout.scaleFactorStart = layout.wordLengthStart + layout.numBfus * 4;
    layout.spectrumStart = layout.scaleFactorStart + layout.numBfus * 6;

    auto bit = layout.spectrumStart;
    for(int bfu = 0; bfu < layout.numBfus; ++bfu)
    {
        const auto idwl = getBits(frame, layout.wordLengthStart + bfu * 4, 4);
        const auto wordLength = idwl == 0 ? 0 : idwl + 1;
        bit += wordLength * specsPerBfu[static_cast<std::size_t>(bfu)];
    }
    layout.spectrumEnd = std::min(bit, static_cast<int>(soundUnitBytes * 8));
    return layout;
}

int AtracCodec::getBits(const EncodedFrame& frame, const int bitPosition, const int bitCount) noexcept
{
    int value = 0;
    for(int i = 0; i < bitCount; ++i)
    {
        const auto position = bitPosition + i;
        if(position < 0 || position >= static_cast<int>(soundUnitBytes * 8))
            return value;
        const auto byte = frame.bytes[static_cast<std::size_t>(position >> 3)];
        value = (value << 1) | ((byte >> (7 - (position & 7))) & 1);
    }
    return value;
}

void AtracCodec::setBits(EncodedFrame& frame,
                         const int bitPosition,
                         const int bitCount,
                         const int value) noexcept
{
    for(int i = 0; i < bitCount; ++i)
    {
        const auto position = bitPosition + i;
        if(position < 0 || position >= static_cast<int>(soundUnitBytes * 8))
            return;
        const auto shift = 7 - (position & 7);
        const auto mask = static_cast<std::uint8_t>(1u << shift);
        const auto bit = (value >> (bitCount - 1 - i)) & 1;
        auto& byte = frame.bytes[static_cast<std::size_t>(position >> 3)];
        byte = bit != 0 ? static_cast<std::uint8_t>(byte | mask)
                        : static_cast<std::uint8_t>(byte & static_cast<std::uint8_t>(~mask));
    }
}

void AtracCodec::flipBit(EncodedFrame& frame, const int bitPosition) noexcept
{
    if(bitPosition < 0 || bitPosition >= static_cast<int>(soundUnitBytes * 8))
        return;
    frame.bytes[static_cast<std::size_t>(bitPosition >> 3)] ^=
        static_cast<std::uint8_t>(1u << (7 - (bitPosition & 7)));
}
} // namespace atracglitch
