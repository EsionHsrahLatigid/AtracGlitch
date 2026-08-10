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

float energy(const std::array<float, atracglitch::samplesPerFrame>& signal)
{
    return std::inner_product(signal.begin(), signal.end(), signal.begin(), 0.0f);
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
    atracglitch::EncodedFrame frame;
    const auto source = makeSignal();
    codec.encode(source, 1.0f, frame);

    const auto layout = atracglitch::AtracCodec::inspect(frame);
    bool ok = true;
    ok &= expect(frame.bytes[0] == 0xac, "encoder did not emit the valid long-block header");
    ok &= expect(layout.numBfus == 52, "full bandwidth did not select all 52 BFUs");
    ok &= expect(layout.spectrumStart == 536, "52-BFU spectrum offset is incorrect");
    ok &= expect(layout.spectrumEnd <= static_cast<int>(atracglitch::soundUnitBytes * 8),
                 "encoded spectrum exceeded a 212-byte Sound Unit");

    std::array<float, atracglitch::samplesPerFrame> decoded {};
    codec.decode(frame, decoded);
    ok &= expect(std::all_of(decoded.begin(), decoded.end(), [](const float value) { return std::isfinite(value); }),
                 "decoded frame contains a non-finite sample");
    ok &= expect(energy(decoded) > 0.01f, "decoded frame unexpectedly contains silence");

    const auto dot = std::inner_product(source.begin(), source.end(), decoded.begin(), 0.0f);
    const auto correlation = dot / std::sqrt(std::max(1.0e-20f, energy(source) * energy(decoded)));
    ok &= expect(correlation > 0.75f, "clean codec round-trip lost signal correlation");
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

    constexpr int totalSamples = 1536;
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
    ok &= expect(std::all_of(output[0].begin(), output[0].begin() + 512,
                             [](const float value) { return value == 0.0f; }),
                 "engine emitted audio before its declared 512-sample latency");
    ok &= expect(std::abs(output[0][512] - 0.5f) < 1.0e-6f,
                 "delayed dry path did not preserve the left impulse");
    ok &= expect(std::abs(output[1][512] + 0.25f) < 1.0e-6f,
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
