#include "DSP/AtracCodec.h"
#include "DSP/AtracGlitchEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#ifndef FFMPEG_EXECUTABLE
#error FFMPEG_EXECUTABLE must name the FFmpeg executable
#endif

namespace
{
constexpr int sampleRate = 44100;
constexpr float pi = 3.14159265358979323846f;

bool expect(const bool condition, const char* message)
{
    if(! condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

std::vector<float> makeSignal(const std::size_t samples)
{
    std::vector<float> result(samples);
    for(std::size_t i = 0; i < samples; ++i)
    {
        const auto t = static_cast<float>(i) / static_cast<float>(sampleRate);
        result[i] = 0.55f * std::sin(2.0f * pi * 440.0f * t)
                  + 0.20f * std::sin(2.0f * pi * 3100.0f * t);
    }
    return result;
}

void writeAea(const std::filesystem::path& path,
              const std::vector<float>& input,
              const bool glitch)
{
    const auto frames = (input.size() + atracglitch::samplesPerFrame - 1) / atracglitch::samplesPerFrame;
    std::vector<std::uint8_t> file(2048 + frames * atracglitch::soundUnitBytes, 0);
    file[0] = 0x00;
    file[1] = 0x08;
    constexpr auto title = "AtracGlitch FFmpeg interop test";
    std::memcpy(file.data() + 4, title, sizeof(title) - 1);
    const auto frameCount = static_cast<std::uint32_t>(frames);
    file[260] = static_cast<std::uint8_t>(frameCount);
    file[261] = static_cast<std::uint8_t>(frameCount >> 8);
    file[262] = static_cast<std::uint8_t>(frameCount >> 16);
    file[263] = static_cast<std::uint8_t>(frameCount >> 24);
    file[264] = 1;

    atracglitch::AtracCodec codec;
    std::uint32_t randomState = 0x5f3759dfu;
    for(std::size_t frameIndex = 0; frameIndex < frames; ++frameIndex)
    {
        std::array<float, atracglitch::samplesPerFrame> block {};
        const auto sourceOffset = frameIndex * block.size();
        const auto available = std::min(block.size(), input.size() - sourceOffset);
        std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(sourceOffset), available, block.begin());

        atracglitch::EncodedFrame encoded;
        codec.encode(block, 1.0f, encoded);
        if(glitch)
            atracglitch::AtracGlitchEngine::mutate(encoded,
                                                   atracglitch::GlitchMode::spectrum,
                                                   0.7f,
                                                   0.7f,
                                                   randomState);
        std::copy(encoded.bytes.begin(), encoded.bytes.end(),
                  file.begin() + static_cast<std::ptrdiff_t>(2048 + frameIndex * atracglitch::soundUnitBytes));
    }

    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
}

std::vector<float> readRawFloat(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    const auto bytes = stream.tellg();
    if(bytes <= 0 || bytes % static_cast<std::streamoff>(sizeof(float)) != 0)
        return {};
    std::vector<float> result(static_cast<std::size_t>(bytes) / sizeof(float));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(result.data()), bytes);
    return result;
}

bool decodeWithFFmpeg(const std::filesystem::path& input, const std::filesystem::path& output)
{
    const std::string command = std::string("\"") + FFMPEG_EXECUTABLE
                              + "\" -y -loglevel error -i \"" + input.string()
                              + "\" -map 0:a:0 -ac 1 -ar 44100 -f f32le -c:a pcm_f32le \""
                              + output.string() + "\"";
    return std::system(command.c_str()) == 0;
}
} // namespace

int main()
{
    constexpr std::size_t frames = 32;
    const auto input = makeSignal(frames * atracglitch::samplesPerFrame);
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
                         / ("atracglitch-ffmpeg-" + std::to_string(stamp));
    std::filesystem::create_directories(directory);

    const auto cleanAea = directory / "clean.aea";
    const auto glitchAea = directory / "glitch.aea";
    const auto cleanRaw = directory / "clean.f32";
    const auto glitchRaw = directory / "glitch.f32";
    writeAea(cleanAea, input, false);
    writeAea(glitchAea, input, true);

    bool ok = true;
    ok &= expect(decodeWithFFmpeg(cleanAea, cleanRaw), "FFmpeg rejected the clean ATRAC1 stream");
    ok &= expect(decodeWithFFmpeg(glitchAea, glitchRaw), "FFmpeg rejected the spectrum-glitched ATRAC1 stream");

    const auto clean = readRawFloat(cleanRaw);
    const auto glitch = readRawFloat(glitchRaw);
    constexpr auto delay = static_cast<std::size_t>(atracglitch::AtracGlitchEngine::codecDelaySamples);
    const auto count = clean.size() > delay ? std::min(input.size(), clean.size() - delay) : 0;
    ok &= expect(count > input.size() / 2, "FFmpeg produced too few decoded samples");
    ok &= expect(glitch.size() >= clean.size(), "glitched decode was unexpectedly truncated");

    double inputEnergy = 0.0;
    double cleanEnergy = 0.0;
    double errorEnergy = 0.0;
    double glitchDifferenceEnergy = 0.0;
    double dot = 0.0;
    bool finite = true;
    for(std::size_t i = 0; i < count; ++i)
    {
        const auto expected = static_cast<double>(input[i]);
        const auto decoded = static_cast<double>(clean[i + delay]);
        const auto damaged = static_cast<double>(glitch[i + delay]);
        finite &= std::isfinite(decoded) && std::isfinite(damaged);
        inputEnergy += expected * expected;
        cleanEnergy += decoded * decoded;
        errorEnergy += (expected - decoded) * (expected - decoded);
        glitchDifferenceEnergy += (decoded - damaged) * (decoded - damaged);
        dot += expected * decoded;
    }

    const auto divisor = static_cast<double>(std::max<std::size_t>(count, 1));
    const auto rms = std::sqrt(cleanEnergy / divisor);
    const auto glitchDifferenceRms = std::sqrt(glitchDifferenceEnergy / divisor);
    const auto correlation = dot / std::sqrt(std::max(1.0e-30, inputEnergy * cleanEnergy));
    const auto snrDb = 10.0 * std::log10(inputEnergy / std::max(1.0e-30, errorEnergy));
    std::cout << "ffmpeg_clean_rms=" << rms
              << " correlation=" << correlation
              << " snr_db=" << snrDb
              << " glitch_difference_rms=" << glitchDifferenceRms
              << " samples=" << count << '\n';

    ok &= expect(finite, "FFmpeg emitted non-finite samples");
    ok &= expect(rms > 0.05, "FFmpeg clean decode is silent or nearly silent");
    ok &= expect(correlation > 0.98, "FFmpeg clean decode lost waveform correlation");
    ok &= expect(snrDb > 20.0, "FFmpeg clean decode quality is below the regression floor");
    ok &= expect(glitchDifferenceRms > 0.01, "FFmpeg glitch decode did not audibly differ from clean decode");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    return ok ? 0 : 1;
}
