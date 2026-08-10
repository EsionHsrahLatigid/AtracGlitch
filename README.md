# ATRAC Glitch

ATRAC Glitch is a JUCE audio effect that converts incoming PCM into a fixed-size,
ATRAC1-structured transform frame, mutates meaningful compressed-domain fields,
then decodes the result back to audio.

The current plug-in builds as VST3, Audio Unit (macOS), and Standalone. It has a
dependency-free DSP core and does not launch FFmpeg from the audio thread.

## What is implemented

- 512 samples per frame and 212 bytes per channel Sound Unit
- The ATRAC1 20..52 BFU layouts used by FFmpeg
- 4-bit word-length indices and 6-bit scale-factor indices
- MSB-first variable-width signed coefficient packing
- Scale-factor jitter, spectral bit flips, word-length corruption, and frame freeze
- Deterministic mutation from a user-visible seed
- Stereo/mono support, latency reporting, delayed dry/wet mix, and bounded output

The encoder is an **ATRAC1-structured real-time model**, not a bit-exact Sony
ATRAC1 encoder. It uses an orthonormal DCT-IV analysis/synthesis transform while
preserving the compressed fields targeted by the glitch modes. FFmpeg currently
ships an ATRAC1 decoder but no ATRAC1 encoder, so a fully interoperable encoder is
outside this first version. The distinction matters if you intend to exchange
generated frames with MiniDisc hardware or `.aea` files.

## Parameters

| Parameter | Meaning |
| --- | --- |
| Mode | Clean Codec, SF Jitter, Spectrum, Word Length, or Freeze |
| Density | Probability or frequency of mutations |
| Amount | Scale jump, bit-flip intensity, or word-length damage |
| Bandwidth | Selects 20, 28, 32, 36, 40, 44, 48, or 52 active BFUs |
| Mix | Latency-aligned dry/wet mix |
| Output | Output gain in dB before safety limiting |
| Seed | Reproducible random sequence |

The plug-in reports 512 samples of latency. Malformed scale factors and word
lengths can create loud peaks, so wet output is bounded; still begin monitoring
at a conservative level.

## Build

JUCE 8 is recommended. Point CMake at a local JUCE checkout:

```bash
cmake -S . -B build -G Ninja \
  -DATRAC_GLITCH_JUCE_PATH=/path/to/JUCE \
  -DATRAC_GLITCH_BUILD_PLUGIN=ON \
  -DATRAC_GLITCH_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

For a core-only build, JUCE is not required:

```bash
cmake -S . -B build-core -G Ninja \
  -DATRAC_GLITCH_BUILD_PLUGIN=OFF \
  -DATRAC_GLITCH_BUILD_TESTS=ON
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Repository map

- `Source/DSP/AtracCodec.*` — transform, BFU allocation, frame packing and decoding
- `Source/DSP/AtracGlitchEngine.*` — mutation, buffering, latency-aligned mix and safety
- `Source/Plugin/PluginProcessor.*` — JUCE processor, parameters and state
- `Tests/CoreTests.cpp` — field, codec, mutation, latency and finite-output checks
- `atrac1_glitch.py` — original offline `.aea`/raw ATRAC1 mutation prototype
- `AGENT_HANDOFF.md` — initial research handoff and follow-up questions

## References

- [FFmpeg ATRAC1 decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac1.c)
- [FFmpeg ATRAC1 data tables](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac1data.h)
- [FFmpeg ATRAC common tables/QMF](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac.c)
- [FFmpeg AEA demuxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/aeadec.c)

