#include "DSP/AtracCodec.h"
#include "DSP/AtracGlitchEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

namespace
{
bool expect(const bool condition, const char* message)
{
    if(! condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

std::array<float, atracglitch::samplesPerFrame> makeSignal()
{
    std::array<float, atracglitch::samplesPerFrame> signal {};
    constexpr float pi = 3.14159265358979323846f;
    for(std::size_t i = 0; i < signal.size(); ++i)
    {
        const auto t = static_cast<float>(i) / 48000.0f;
        signal[i] = 0.55f * std::sin(2.0f * pi * 440.0f * t)
                  + 0.20f * std::sin(2.0f * pi * 3100.0f * t);
    }
    return signal;
}

bool testBitPacking()
{
    atracglitch::EncodedFrame frame;
    atracglitch::AtracCodec::setBits(frame, 5, 6, 0x2d);
    bool ok = expect(atracglitch::AtracCodec::getBits(frame, 5, 6) == 0x2d,
                     "MSB-first bit field did not round-trip");
    atracglitch::AtracCodec::flipBit(frame, 7);
    ok &= expect(atracglitch::AtracCodec::getBits(frame, 5, 6) != 0x2d,
                 "bit flip did not change the field");
    return ok;
}

bool testCodecRoundTrip()
{
    atracglitch::AtracCodec codec;
    constexpr std::size_t frameCount = 8;
    std::array<float, atracglitch::samplesPerFrame * frameCount> source {};
    std::array<float, atracglitch::samplesPerFrame * frameCount> decoded {};
    for(std::size_t i = 0; i < source.size(); ++i)
    {
        const auto t = static_cast<float>(i) / 44100.0f;
        source[i] = 0.55f * std::sin(2.0f * 3.14159265358979323846f * 440.0f * t)
                  + 0.20f * std::sin(2.0f * 3.14159265358979323846f * 3100.0f * t);
    }

    atracglitch::EncodedFrame frame;
    for(std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        std::array<float, atracglitch::samplesPerFrame> inputFrame {};
        std::array<float, atracglitch::samplesPerFrame> outputFrame {};
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(frameIndex * atracglitch::samplesPerFrame),
                    atracglitch::samplesPerFrame,
                    inputFrame.begin());
        codec.encode(inputFrame, 1.0f, frame);
        codec.decode(frame, outputFrame);
        std::copy(outputFrame.begin(), outputFrame.end(),
                  decoded.begin() + static_cast<std::ptrdiff_t>(frameIndex * atracglitch::samplesPerFrame));
    }

    const auto layout = atracglitch::AtracCodec::inspect(frame);
    bool ok = true;
    ok &= expect(frame.bytes[0] == 0xac, "encoder did not emit the valid long-block header");
    ok &= expect(layout.numBfus == 52, "full bandwidth did not select all 52 BFUs");
    ok &= expect(layout.spectrumStart == 536, "52-BFU spectrum offset is incorrect");
    ok &= expect(layout.spectrumEnd <= static_cast<int>(atracglitch::soundUnitBytes * 8),
                 "encoded spectrum exceeded a 212-byte Sound Unit");

    ok &= expect(std::all_of(decoded.begin(), decoded.end(), [](const float value) { return std::isfinite(value); }),
                 "decoded stream contains a non-finite sample");

    constexpr auto delay = static_cast<std::size_t>(atracglitch::AtracGlitchEngine::codecDelaySamples);
    const auto compared = source.size() - delay;
    const auto sourceEnergy = std::inner_product(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(compared),
                                                  source.begin(), 0.0f);
    const auto decodedEnergy = std::inner_product(decoded.begin() + static_cast<std::ptrdiff_t>(delay), decoded.end(),
                                                   decoded.begin() + static_cast<std::ptrdiff_t>(delay), 0.0f);
    const auto dot = std::inner_product(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(compared),
                                        decoded.begin() + static_cast<std::ptrdiff_t>(delay), 0.0f);
    const auto correlation = dot / std::sqrt(std::max(1.0e-20f, sourceEnergy * decodedEnergy));
    ok &= expect(decodedEnergy > 0.01f, "decoded stream unexpectedly contains silence");
    ok &= expect(correlation > 0.98f, "clean ATRAC1 round-trip lost delay-compensated correlation");
    return ok;
}

bool testAllBandwidthsAndDamagedDecode()
{
    constexpr std::array expectedBfus { 20, 28, 32, 36, 40, 44, 48, 52 };
    constexpr std::array modes {
        atracglitch::GlitchMode::scaleFactor,
        atracglitch::GlitchMode::spectrum,
        atracglitch::GlitchMode::wordLength
    };

    atracglitch::AtracCodec codec;
    const auto source = makeSignal();
    bool ok = true;
    for(std::size_t bandwidth = 0; bandwidth < expectedBfus.size(); ++bandwidth)
    {
        atracglitch::EncodedFrame clean;
        codec.encode(source,
                     static_cast<float>(bandwidth) / static_cast<float>(expectedBfus.size() - 1),
                     clean);
        const auto layout = atracglitch::AtracCodec::inspect(clean);
        ok &= expect(layout.numBfus == expectedBfus[bandwidth], "bandwidth selected the wrong BFU count");
        ok &= expect(layout.spectrumEnd <= static_cast<int>(atracglitch::soundUnitBytes * 8),
                     "a bandwidth setting exceeded the Sound Unit bit budget");

        for(const auto mode : modes)
        {
            for(std::uint32_t seed = 1; seed <= 12; ++seed)
            {
                auto damaged = clean;
                auto randomState = seed * 7919u;
                atracglitch::AtracGlitchEngine::mutate(damaged, mode, 1.0f, 1.0f, randomState);
                std::array<float, atracglitch::samplesPerFrame> decoded {};
                codec.decode(damaged, decoded);
                ok &= expect(std::all_of(decoded.begin(), decoded.end(),
                                         [](const float value) { return std::isfinite(value) && std::abs(value) <= 8.0f; }),
                             "damaged frame escaped decode safety bounds");
            }
        }
    }
    return ok;
}

bool testDeterministicMutation()
{
    atracglitch::AtracCodec codec;
    atracglitch::EncodedFrame original;
    codec.encode(makeSignal(), 1.0f, original);

    for(const auto mode : { atracglitch::GlitchMode::scaleFactor,
                            atracglitch::GlitchMode::spectrum,
                            atracglitch::GlitchMode::wordLength })
    {
        auto first = original;
        auto second = original;
        std::uint32_t firstState = 12345;
        std::uint32_t secondState = 12345;
        atracglitch::AtracGlitchEngine::mutate(first, mode, 1.0f, 0.8f, firstState);
        atracglitch::AtracGlitchEngine::mutate(second, mode, 1.0f, 0.8f, secondState);
        if(! expect(first.bytes == second.bytes, "same seed produced different mutations"))
            return false;
        if(! expect(first.bytes != original.bytes, "maximum-density mutation left the frame unchanged"))
            return false;
    }
    return true;
}

bool testEngineLatencyAndSafety()
{
    atracglitch::AtracGlitchEngine engine;
    engine.prepare(48000.0, 127, 2);

    constexpr int totalSamples = 2048;
    std::array<std::vector<float>, 2> input {
        std::vector<float>(totalSamples), std::vector<float>(totalSamples)
    };
    std::array<std::vector<float>, 2> output {
        std::vector<float>(totalSamples), std::vector<float>(totalSamples)
    };
    input[0][0] = 0.5f;
    input[1][0] = -0.25f;

    atracglitch::Parameters parameters;
    parameters.mode = atracglitch::GlitchMode::clean;
    parameters.mix = 0.0f;
    parameters.outputDb = 0.0f;

    int offset = 0;
    const std::array<int, 7> blockSizes { 17, 127, 64, 251, 31, 509, 537 };
    for(const auto requested : blockSizes)
    {
        if(offset >= totalSamples)
            break;
        const auto count = std::min(requested, totalSamples - offset);
        float* outputs[] { output[0].data() + offset, output[1].data() + offset };
        const float* inputs[] { input[0].data() + offset, input[1].data() + offset };
        engine.process(outputs, inputs, 2, count, parameters);
        offset += count;
    }

    bool ok = true;
    ok &= expect(std::all_of(output[0].begin(), output[0].begin() + atracglitch::AtracGlitchEngine::latencySamples,
                             [](const float value) { return value == 0.0f; }),
                 "engine emitted dry audio before its declared latency");
    ok &= expect(std::abs(output[0][atracglitch::AtracGlitchEngine::latencySamples] - 0.5f) < 1.0e-6f,
                 "delayed dry path did not preserve the left impulse");
    ok &= expect(std::abs(output[1][atracglitch::AtracGlitchEngine::latencySamples] + 0.25f) < 1.0e-6f,
                 "delayed dry path did not preserve the right impulse");
    ok &= expect(std::all_of(output[0].begin(), output[0].end(), [](const float value) { return std::isfinite(value); }),
                 "engine output contains a non-finite sample");
    return ok;
}
} // namespace

int main()
{
    bool ok = true;
    ok &= testBitPacking();
    ok &= testCodecRoundTrip();
    ok &= testAllBandwidthsAndDamagedDecode();
    ok &= testDeterministicMutation();
    ok &= testEngineLatencyAndSafety();

    if(ok)
        std::cout << "ATRAC Glitch core tests passed\n";
    return ok ? 0 : 1;
}
