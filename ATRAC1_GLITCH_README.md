# ATRAC1 Glitch Experiment

`atrac1_glitch.py` mutates ATRAC1 / MiniDisc SP compressed data at codec-aware
locations rather than blindly corrupting the whole file.

## Recommended first experiment

Start with scale-factor jitter:

```bash
python atrac1_glitch.py clean.aea sf.aea \
  --mode sf-jitter --prob 0.05 --amount 4 --seed 1

ffmpeg -i sf.aea -c:a pcm_s24le sf.wav
```

Then compare:

```bash
python atrac1_glitch.py clean.aea spec.aea \
  --mode spectrum --prob 0.0005 --amount 1 --seed 2

python atrac1_glitch.py clean.aea wl.aea \
  --mode wordlen --prob 0.005 --amount 1 --seed 3

python atrac1_glitch.py clean.aea bsm.aea \
  --mode bsm --prob 0.02 --amount 1 --seed 4
```

## Expected sonic behavior

- `sf-jitter`: local spectral-band gain jumps; tonal bursts, whistles, metallic
  emphasis. Structural boundaries remain intact, so this is the best baseline.
- `sf-xor`: more discontinuous scale changes than `sf-jitter`.
- `spectrum`: coefficient-level corruption; granular/metallic noise and short
  chirps, generally contained to each 512-sample Sound Unit.
- `wordlen`: changes the number of bits consumed by BFUs. The decoder then
  interprets following coefficient boundaries differently. Expect much harsher
  bursts and occasional invalid frames.
- `bsm`: changes short/long transform interpretation. Expect transient smearing,
  ringing, or parser rejection depending on the unit.

## Practical sweep

For reproducible comparisons, keep the input and seed fixed and sweep one
parameter:

```bash
for p in 0.01 0.03 0.05 0.10; do
  python atrac1_glitch.py clean.aea "sf_${p}.aea" \
    --mode sf-jitter --prob "$p" --amount 4 --seed 123
done
```

Use headphones cautiously: malformed scale factors/coefficients can generate
unexpectedly high sample peaks after decoding.
