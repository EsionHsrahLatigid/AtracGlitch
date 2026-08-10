# ATRAC Glitch

ATRAC Glitch is a JUCE audio effect that converts incoming PCM into a fixed-size,
ATRAC1-structured transform frame, mutates meaningful compressed-domain fields,
then decodes the result back to audio.

The current plug-in builds as VST3, Audio Unit (macOS), and Standalone. It has a
dependency-free DSP core and does not launch FFmpeg from the audio thread.

## What is implemented

- 512 samples per frame and 212 bytes per channel Sound Unit
- Stateful 48-tap cascaded QMF analysis/synthesis for the three ATRAC1 bands
- ATRAC1 long-block MDCT/IMDCT, 32-sample overlap windows, and band reversal
- The ATRAC1 20..52 BFU layouts used by FFmpeg
- 4-bit word-length indices and 6-bit scale-factor indices
- MSB-first variable-width signed coefficient packing
- Scale-factor jitter, spectral bit flips, word-length corruption, and frame freeze
- Deterministic mutation from a user-visible seed
- Stereo/mono support, latency reporting, delayed dry/wet mix, and bounded output

The encoder now emits ATRAC1 Sound Units that FFmpeg's standard ATRAC1 decoder
accepts and decodes as correlated, audible PCM. It implements the interoperable
long-block signal path rather than the earlier whole-frame DCT approximation.
It is still not a bit-exact Sony encoder: bit allocation is deliberately simple,
short-block transient switching is not implemented, and MiniDisc hardware
interchange has not been tested.

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

The plug-in reports 778 samples of latency: one 512-sample frame plus the measured
266-sample ATRAC1 analysis/synthesis delay. The dry path is delayed by the same
amount. Malformed scale factors and word lengths can create loud peaks, so wet
output is bounded; still begin monitoring
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
- `Tests/FFmpegInteropTests.cpp` — clean/glitched AEA decode checks against FFmpeg
- `atrac1_glitch.py` — original offline `.aea`/raw ATRAC1 mutation prototype
- `AGENT_HANDOFF.md` — initial research handoff and follow-up questions

## References

- [FFmpeg ATRAC1 decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac1.c)
- [FFmpeg ATRAC1 data tables](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac1data.h)
- [FFmpeg ATRAC common tables/QMF](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac.c)
- [FFmpeg AEA demuxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/aeadec.c)
- [AtracDEnc ATRAC1 encoder/decoder](https://github.com/dcherednik/atracdenc)
- [Sony ATRAC technical overview](https://www.minidisc.org/aes_atrac.html)

## Verified interoperability

With FFmpeg available, CMake enables `AtracGlitch.Interop.FFmpeg`. The test emits
clean and spectrum-glitched AEA files, decodes them with FFmpeg, and rejects
silent, uncorrelated, non-finite, low-SNR, or unchanged output. The 2026-08-11
reference run measured clean RMS `0.414476`, input correlation `0.999784`, SNR
`33.6247 dB`, and clean-vs-glitched difference RMS `0.128086`.
