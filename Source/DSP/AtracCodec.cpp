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
constexpr std::array<float, 24> qmfTapHalf {
    -0.00001461907f, -0.00009205479f, -0.000056157569f, 0.00030117269f,
     0.0002422519f,  -0.00085293897f, -0.0005205574f,  0.0020340169f,
     0.00078333891f, -0.0042153862f,  -0.00075614988f, 0.0078402944f,
    -0.000061169922f,-0.01344162f,     0.0024626821f,   0.021736089f,
    -0.007801671f,   -0.034090221f,    0.01880949f,     0.054326009f,
    -0.043596379f,   -0.099384367f,    0.13207909f,     0.46424159f
};

struct TransformTables
{
    std::array<float, 256 * 128> mdct256 {};
    std::array<float, 512 * 256> mdct512 {};
    std::array<float, 48> qmfWindow {};
    std::array<float, 32> sineWindow {};

    TransformTables()
    {
        fillMdct(mdct256, 256);
        fillMdct(mdct512, 512);
        for(std::size_t i = 0; i < qmfTapHalf.size(); ++i)
            qmfWindow[i] = qmfWindow[qmfWindow.size() - 1 - i] = qmfTapHalf[i] * 2.0f;
        for(std::size_t i = 0; i < sineWindow.size(); ++i)
            sineWindow[i] = std::sin((static_cast<float>(i) + 0.5f) * pi / 64.0f);
    }

private:
    template<std::size_t Size>
    static void fillMdct(std::array<float, Size>& table, const std::size_t transformSize)
    {
        const auto coefficients = transformSize / 2;
        for(std::size_t k = 0; k < coefficients; ++k)
        {
            for(std::size_t n = 0; n < transformSize; ++n)
            {
                const auto phase = pi / static_cast<float>(coefficients)
                                 * (static_cast<float>(n) + 0.5f + static_cast<float>(coefficients) * 0.5f)
                                 * (static_cast<float>(k) + 0.5f);
                table[k * transformSize + n] = std::cos(phase);
            }
        }
    }
};

const TransformTables& tables() noexcept
{
    static const TransformTables value;
    return value;
}

float scaleFactor(const int index) noexcept
{
    return std::exp2(static_cast<float>(index) / 3.0f - 21.0f);
}

int scaleFactorIndex(const float peak) noexcept
{
    if(! std::isfinite(peak) || peak <= 0.0f)
        return 0;

    const auto raw = 3.0f * (std::log2(peak) + 21.0f);
    return std::clamp(static_cast<int>(std::ceil(raw)), 0, 63);
}

int signedFromBits(const int raw, const int bits) noexcept
{
    const auto sign = 1 << (bits - 1);
    return (raw & sign) != 0 ? raw - (1 << bits) : raw;
}

template<std::size_t InputSize>
void qmfAnalysis(const float* input,
                 float* lower,
                 float* upper,
                 std::array<float, InputSize + 46>& state) noexcept
{
    std::copy_n(state.begin() + InputSize, 46, state.begin());
    std::copy_n(input, InputSize, state.begin() + 46);

    const auto& window = tables().qmfWindow;
    for(std::size_t j = 0; j < InputSize; j += 2)
    {
        float even = 0.0f;
        float odd = 0.0f;
        for(std::size_t i = 0; i < 24; ++i)
        {
            even += window[2 * i] * state[47 + j - 2 * i];
            odd += window[2 * i + 1] * state[46 + j - 2 * i];
        }
        lower[j / 2] = even + odd;
        upper[j / 2] = even - odd;
    }
}

template<std::size_t InputSize>
void qmfSynthesis(float* output,
                  const float* lower,
                  const float* upper,
                  std::array<float, InputSize + 46>& state) noexcept
{
    auto* newPart = state.data() + 46;
    for(std::size_t i = 0; i < InputSize; i += 4)
    {
        newPart[i] = lower[i / 2] + upper[i / 2];
        newPart[i + 1] = lower[i / 2] - upper[i / 2];
        newPart[i + 2] = lower[i / 2 + 1] + upper[i / 2 + 1];
        newPart[i + 3] = lower[i / 2 + 1] - upper[i / 2 + 1];
    }

    const auto& window = tables().qmfWindow;
    auto* position = state.data();
    for(std::size_t sample = 0; sample < InputSize; sample += 2)
    {
        float first = 0.0f;
        float second = 0.0f;
        for(std::size_t i = 0; i < 48; i += 2)
        {
            first += position[i] * window[i];
            second += position[i + 1] * window[i + 1];
        }
        output[sample] = second;
        output[sample + 1] = first;
        position += 2;
    }
    std::copy_n(state.begin() + InputSize, 46, state.begin());
}

template<std::size_t TransformSize>
void mdctForward(const std::array<float, TransformSize>& input,
                 float* output,
                 const std::array<float, TransformSize * (TransformSize / 2)>& table) noexcept
{
    constexpr auto coefficients = TransformSize / 2;
    constexpr auto atracNormalization = 1.0f / 512.0f;
    for(std::size_t k = 0; k < coefficients; ++k)
    {
        float sum = 0.0f;
        const auto row = k * TransformSize;
        for(std::size_t n = 0; n < TransformSize; ++n)
            sum += input[n] * table[row + n];
        output[k] = sum * atracNormalization;
    }
}

template<std::size_t BandSamples>
void encodeBand(const float* band,
                std::array<float, 32>& previousTail,
                float* spectrum,
                const bool reverseSpectrum) noexcept
{
    constexpr auto transformSize = BandSamples * 2;
    constexpr auto windowStart = BandSamples == 256 ? 112u : 48u;
    std::array<float, transformSize> input {};
    std::copy(previousTail.begin(), previousTail.end(), input.begin() + windowStart);
    std::copy_n(band, BandSamples, input.begin() + windowStart + 32);

    const auto& sine = tables().sineWindow;
    for(std::size_t i = 0; i < 32; ++i)
    {
        const auto value = band[BandSamples - 32 + i];
        previousTail[i] = sine[i] * value;
        input[windowStart + BandSamples + i] = sine[31 - i] * value;
    }

    if constexpr(transformSize == 256)
        mdctForward(input, spectrum, tables().mdct256);
    else
        mdctForward(input, spectrum, tables().mdct512);

    if(reverseSpectrum)
        std::reverse(spectrum, spectrum + BandSamples);
}

template<std::size_t TransformSize>
void imdct(const float* spectrum,
           std::array<float, TransformSize>& output,
           const std::array<float, TransformSize * (TransformSize / 2)>& table) noexcept
{
    constexpr auto coefficients = TransformSize / 2;
    for(std::size_t n = 0; n < TransformSize; ++n)
    {
        float sum = 0.0f;
        for(std::size_t k = 0; k < coefficients; ++k)
            sum += spectrum[k] * table[k * TransformSize + n];
        output[n] = 2.0f * sum;
    }
}

template<std::size_t BandSamples>
void decodeBand(const float* encodedSpectrum,
                std::array<float, 16>& previousOverlap,
                float* output,
                const bool reverseSpectrum) noexcept
{
    constexpr auto transformSize = BandSamples * 2;
    std::array<float, BandSamples> spectrum {};
    std::copy_n(encodedSpectrum, BandSamples, spectrum.begin());
    if(reverseSpectrum)
        std::reverse(spectrum.begin(), spectrum.end());

    std::array<float, transformSize> inverse {};
    if constexpr(transformSize == 256)
        imdct(spectrum.data(), inverse, tables().mdct256);
    else
        imdct(spectrum.data(), inverse, tables().mdct512);

    const auto* central = inverse.data() + BandSamples / 2;
    const auto& sine = tables().sineWindow;
    for(std::size_t i = 0; i < 16; ++i)
    {
        const auto previous = previousOverlap[i];
        const auto current = central[15 - i];
        output[i] = previous * sine[31 - i] - current * sine[i];
        output[31 - i] = previous * sine[i] + current * sine[31 - i];
    }
    for(std::size_t i = 32; i < BandSamples; ++i)
        output[i] = central[i - 16];
    std::copy_n(central + BandSamples - 16, 16, previousOverlap.begin());
}
} // namespace

AtracCodec::AtracCodec()
{
    (void) tables();
    reset();
}

void AtracCodec::reset() noexcept
{
    analysisFirst.fill(0.0f);
    analysisSecond.fill(0.0f);
    analysisHighDelay.fill(0.0f);
    encodeLowTail.fill(0.0f);
    encodeMidTail.fill(0.0f);
    encodeHighTail.fill(0.0f);
    synthesisFirst.fill(0.0f);
    synthesisSecond.fill(0.0f);
    synthesisHighDelay.fill(0.0f);
    decodeLowOverlap.fill(0.0f);
    decodeMidOverlap.fill(0.0f);
    decodeHighOverlap.fill(0.0f);
}

void AtracCodec::encode(const std::array<float, samplesPerFrame>& input,
                        const float bandwidth,
                        EncodedFrame& output) noexcept
{
    output.bytes.fill(0);

    std::array<float, samplesPerFrame> spectrum {};
    std::array<float, samplesPerFrame / 2> middleLow {};
    std::array<float, samplesPerFrame / 4> low {};
    std::array<float, samplesPerFrame / 4> mid {};
    std::array<float, samplesPerFrame / 2> high {};

    std::copy_n(analysisHighDelay.begin() + samplesPerFrame / 2, 39, analysisHighDelay.begin());
    qmfAnalysis<512>(input.data(), middleLow.data(), analysisHighDelay.data() + 39, analysisFirst);
    qmfAnalysis<256>(middleLow.data(), low.data(), mid.data(), analysisSecond);
    std::copy_n(analysisHighDelay.begin(), high.size(), high.begin());

    encodeBand<128>(low.data(), encodeLowTail, spectrum.data(), false);
    encodeBand<128>(mid.data(), encodeMidTail, spectrum.data() + 128, true);
    encodeBand<256>(high.data(), encodeHighTail, spectrum.data() + 256, true);

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
                        std::array<float, samplesPerFrame>& output) noexcept
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

    std::array<float, 128> low {};
    std::array<float, 128> mid {};
    std::array<float, 256> high {};
    std::array<float, 256> middleLow {};
    decodeBand<128>(spectrum.data(), decodeLowOverlap, low.data(), false);
    decodeBand<128>(spectrum.data() + 128, decodeMidOverlap, mid.data(), true);
    decodeBand<256>(spectrum.data() + 256, decodeHighOverlap, high.data(), true);

    qmfSynthesis<256>(middleLow.data(), low.data(), mid.data(), synthesisSecond);
    std::copy_n(synthesisHighDelay.begin() + 256, 39, synthesisHighDelay.begin());
    std::copy(high.begin(), high.end(), synthesisHighDelay.begin() + 39);
    qmfSynthesis<512>(output.data(), middleLow.data(), synthesisHighDelay.data(), synthesisFirst);
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
