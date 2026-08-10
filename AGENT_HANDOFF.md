# Agent Handoff: ATRAC1 / MiniDisc SP Glitch Research

## Goal
Build a reproducible codec-aware ATRAC1 glitch system. Mutate meaningful
compressed-domain fields rather than performing undirected file corruption,
then characterize the audible artifacts.

## Current prototype
`atrac1_glitch.py` supports:
- `sf-jitter`: bounded scale-factor-index perturbation
- `sf-xor`: scale-factor XOR
- `spectrum`: bit flips restricted to the parsed coefficient region
- `wordlen`: word-length corruption, intentionally disturbing downstream parsing
- `bsm`: transform block-size-mode mutation

It supports FFmpeg-compatible `.aea` and headerless ATRAC1 payloads.

## Structural assumptions to verify
Per channel:
- Sound Unit = 212 bytes
- 512 decoded samples
- block-size-mode information
- BFU/allocation information
- `num_bfus * 4-bit` word-length indices
- `num_bfus * 6-bit` scale-factor indices
- variable-width quantized spectral coefficients
- maximum 52 BFUs

These assumptions are based on FFmpeg's ATRAC1 implementation. Verify against
current upstream source/test vectors before extending the parser.

## Next work
1. Verify bit offsets and malformed-frame behavior.
2. Add BFU selection and frequency-band targeting.
3. Add structured mutations: previous-frame SF copy, BFU swap/freeze/repeat,
   deterministic masks, and cross-channel substitution.
4. Emit a JSON/CSV mutation manifest with exact frame/channel/BFU/bit changes.
5. Add parameter-sweep runner.
6. Decode outputs automatically with FFmpeg and collect decoder errors,
   peak/RMS/clipping and useful spectral statistics.
7. Add tests and a small reproducible listening-test corpus.
8. Investigate ATRAC3/ATRAC3plus separately; do not assume ATRAC1 layout applies.

## Suggested starting sweep
- sf-jitter: p=0.01..0.10, amount=1..8
- spectrum: p=0.0001..0.005
- wordlen: p=0.001..0.02, amount=1
- bsm: p=0.005..0.05

Keep source, seed, decoder version and playback gain fixed.

## Safety
Malformed frames may decode to unexpectedly high PCM peaks. Analyze/attenuate
outputs before headphone monitoring.
