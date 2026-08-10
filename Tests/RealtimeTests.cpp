#include "DSP/AtracGlitchEngine.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

namespace
{
std::atomic<bool> trackAllocations { false };
std::atomic<std::size_t> allocationCount { 0 };
}

void* operator new(const std::size_t size)
{
    if(trackAllocations.load(std::memory_order_relaxed))
        allocationCount.fetch_add(1, std::memory_order_relaxed);
    if(auto* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](const std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

int main()
{
    constexpr int sampleRate = 48000;
    constexpr int blockSize = 128;
    constexpr int durationSeconds = 3;
    constexpr int totalSamples = sampleRate * durationSeconds;

    atracglitch::AtracGlitchEngine engine;
    engine.prepare(sampleRate, blockSize, 2);

    std::array<std::vector<float>, 2> input {
        std::vector<float>(blockSize), std::vector<float>(blockSize)
    };
    std::array<std::vector<float>, 2> output {
        std::vector<float>(blockSize), std::vector<float>(blockSize)
    };

    atracglitch::Parameters parameters;
    parameters.mode = atracglitch::GlitchMode::wordLength;
    parameters.density = 0.2f;
    parameters.amount = 0.6f;
    parameters.mix = 1.0f;
    parameters.seed = 987654u;

    allocationCount.store(0, std::memory_order_relaxed);
    trackAllocations.store(true, std::memory_order_relaxed);
    const auto start = std::chrono::steady_clock::now();

    for(int offset = 0; offset < totalSamples; offset += blockSize)
    {
        for(int i = 0; i < blockSize; ++i)
        {
            const auto phase = static_cast<float>(offset + i) / static_cast<float>(sampleRate);
            input[0][static_cast<std::size_t>(i)] = 0.4f * std::sin(phase * 2764.6015f);
            input[1][static_cast<std::size_t>(i)] = 0.4f * std::sin(phase * 4146.9023f);
        }
        float* outputs[] { output[0].data(), output[1].data() };
        const float* inputs[] { input[0].data(), input[1].data() };
        engine.process(outputs, inputs, 2, blockSize, parameters);
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    trackAllocations.store(false, std::memory_order_relaxed);

    bool finite = true;
    for(const auto& channel : output)
        for(const auto sample : channel)
            finite &= std::isfinite(sample);

    const auto allocations = allocationCount.load(std::memory_order_relaxed);
    const auto realtimeRatio = elapsed / static_cast<double>(durationSeconds);
    std::cout << "processed_seconds=" << durationSeconds
              << " elapsed_seconds=" << elapsed
              << " realtime_ratio=" << realtimeRatio
              << " audio_thread_allocations=" << allocations << '\n';

    if(allocations != 0)
    {
        std::cerr << "FAIL: process path allocated memory\n";
        return 1;
    }
    if(! finite)
    {
        std::cerr << "FAIL: process path emitted non-finite audio\n";
        return 1;
    }
#if defined(NDEBUG)
    if(realtimeRatio >= 0.75)
    {
        std::cerr << "FAIL: DSP did not retain a minimum realtime margin\n";
        return 1;
    }
#endif
    return 0;
}
