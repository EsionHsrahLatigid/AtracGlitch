#!/usr/bin/env python3
"""
atrac1_glitch.py — targeted ATRAC1 / MiniDisc SP bitstream glitcher.

Supports FFmpeg-compatible .aea files (2048-byte AEA header) and headerless
ATRAC1 payloads. ATRAC1 Sound Unit size is 212 bytes per channel.

The parser follows FFmpeg's ATRAC1 decoder layout:
  bits 0..7      block-size mode
  bits 8..15     BFU count / allocation info
  then           num_bfus * 4-bit word-length indices
  then           num_bfus * 6-bit scale-factor indices
  then           variable-width signed spectral coefficients

No audio is decoded by this program. It mutates the compressed representation.

Examples:
  python atrac1_glitch.py in.aea out.aea --mode sf-jitter --prob 0.08 --amount 5 --seed 1
  python atrac1_glitch.py in.aea out.aea --mode sf-xor    --prob 0.03 --seed 2
  python atrac1_glitch.py in.aea out.aea --mode spectrum  --prob 0.0008 --seed 3
  python atrac1_glitch.py in.aea out.aea --mode wordlen   --prob 0.01 --seed 4
  ffmpeg -i out.aea -c:a pcm_s24le out.wav

Notes:
- "sf-jitter" is the most controlled mode: it changes scale-factor indices
  while preserving all field boundaries.
- "spectrum" flips bits only inside coefficient data derived from the ORIGINAL
  word-length map, so structural damage is relatively limited.
- "wordlen" is deliberately destructive: changing a word length changes how
  the decoder partitions all following coefficient bits within that Sound Unit.
- "bsm" changes transform block-size mode and can easily create invalid units.
"""

import argparse
import random
import struct
import sys
from pathlib import Path

SU_SIZE = 212
AEA_HEADER = 2048
BFU_AMOUNT_TAB1 = [20, 28, 32, 36, 40, 44, 48, 52]
SPECS_PER_BFU = [
     8,  8,  8,  8,  4,  4,  4,  4,  8,  8,  8,  8,  6,  6,  6,  6,  6,  6,  6,  6,
     6,  6,  6,  6,  7,  7,  7,  7,  9,  9,  9,  9, 10, 10, 10, 10,
    12, 12, 12, 12, 12, 12, 12, 12, 20, 20, 20, 20, 20, 20, 20, 20,
]

def get_bits(buf: bytearray, bitpos: int, n: int) -> int:
    """MSB-first, matching FFmpeg GetBitContext semantics used by atrac1.c."""
    v = 0
    for i in range(n):
        p = bitpos + i
        byte = buf[p >> 3]
        bit = (byte >> (7 - (p & 7))) & 1
        v = (v << 1) | bit
    return v

def set_bits(buf: bytearray, bitpos: int, n: int, value: int) -> None:
    value &= (1 << n) - 1
    for i in range(n):
        p = bitpos + i
        shift = 7 - (p & 7)
        mask = 1 << shift
        bit = (value >> (n - 1 - i)) & 1
        if bit:
            buf[p >> 3] |= mask
        else:
            buf[p >> 3] &= ~mask

def flip_bit(buf: bytearray, bitpos: int) -> None:
    buf[bitpos >> 3] ^= 1 << (7 - (bitpos & 7))

def parse_su(su: bytearray):
    if len(su) != SU_SIZE:
        raise ValueError("bad Sound Unit size")
    info_idx = get_bits(su, 8, 3)
    num_bfus = BFU_AMOUNT_TAB1[info_idx]
    wl_start = 16
    sf_start = wl_start + num_bfus * 4
    spec_start = sf_start + num_bfus * 6

    idwls = [get_bits(su, wl_start + i * 4, 4) for i in range(num_bfus)]
    # FFmpeg: word_len = !!idwl + idwl
    spec_bits = 0
    for i, idwl in enumerate(idwls):
        word_len = idwl + (1 if idwl else 0)
        spec_bits += word_len * SPECS_PER_BFU[i]

    spec_end = min(SU_SIZE * 8, spec_start + spec_bits)
    return {
        "num_bfus": num_bfus,
        "wl_start": wl_start,
        "sf_start": sf_start,
        "spec_start": spec_start,
        "spec_end": spec_end,
        "idwls": idwls,
    }

def mutate_sf_jitter(su, meta, rng, prob, amount):
    n = 0
    for i in range(meta["num_bfus"]):
        if rng.random() < prob:
            p = meta["sf_start"] + 6 * i
            old = get_bits(su, p, 6)
            delta = rng.randint(-amount, amount)
            if delta == 0:
                delta = 1 if old < 63 else -1
            new = max(0, min(63, old + delta))
            set_bits(su, p, 6, new)
            n += 1
    return n

def mutate_sf_xor(su, meta, rng, prob, amount):
    n = 0
    max_bits = max(1, min(6, amount))
    for i in range(meta["num_bfus"]):
        if rng.random() < prob:
            p = meta["sf_start"] + 6 * i
            old = get_bits(su, p, 6)
            mask = 0
            for _ in range(rng.randint(1, max_bits)):
                mask |= 1 << rng.randrange(6)
            set_bits(su, p, 6, old ^ mask)
            n += 1
    return n

def mutate_wordlen(su, meta, rng, prob, amount):
    n = 0
    max_bits = max(1, min(4, amount))
    for i in range(meta["num_bfus"]):
        if rng.random() < prob:
            p = meta["wl_start"] + 4 * i
            old = get_bits(su, p, 4)
            mask = 0
            for _ in range(rng.randint(1, max_bits)):
                mask |= 1 << rng.randrange(4)
            set_bits(su, p, 4, old ^ mask)
            n += 1
    return n

def mutate_spectrum(su, meta, rng, prob, amount):
    # Flip each coefficient-region bit independently with probability prob.
    # amount is an integer multiplier, useful for coarse CLI control.
    n = 0
    effective = min(1.0, prob * max(1, amount))
    for p in range(meta["spec_start"], meta["spec_end"]):
        if rng.random() < effective:
            flip_bit(su, p)
            n += 1
    return n

def mutate_bsm(su, meta, rng, prob, amount):
    # Valid code values according to FFmpeg:
    # low/mid 2-bit values: 0 or 2; high: 0 or 3.
    # We intentionally choose valid alternatives to get audible transform
    # mismatches without guaranteeing a parser rejection.
    if rng.random() >= prob:
        return 0
    low = get_bits(su, 0, 2)
    mid = get_bits(su, 2, 2)
    high = get_bits(su, 4, 2)
    choices = []
    choices.append((0, 2 if low == 0 else 0))
    choices.append((2, 2 if mid == 0 else 0))
    choices.append((4, 3 if high == 0 else 0))
    rng.shuffle(choices)
    count = min(max(1, amount), 3)
    for p, v in choices[:count]:
        set_bits(su, p, 2, v)
    return count

def detect_layout(data: bytes, force_raw=False, channels_override=None):
    if not force_raw and len(data) >= AEA_HEADER + SU_SIZE:
        # FFmpeg's AEA demuxer reads channels at byte 264 and payload at 2048.
        ch = data[264]
        if ch in range(1, 9) and (len(data) - AEA_HEADER) >= SU_SIZE * ch:
            return AEA_HEADER, ch, "aea"
    ch = channels_override or 2
    if ch < 1 or ch > 8:
        raise ValueError("channels must be 1..8")
    return 0, ch, "raw"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--mode", choices=["sf-jitter", "sf-xor", "spectrum", "wordlen", "bsm"],
                    default="sf-jitter")
    ap.add_argument("--prob", type=float, default=0.05,
                    help="mutation probability (field-wise; bit-wise for spectrum)")
    ap.add_argument("--amount", type=int, default=3,
                    help="mode-dependent strength: SF index delta, max XOR bits, etc.")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--every", type=int, default=1,
                    help="only mutate every Nth audio frame")
    ap.add_argument("--start-frame", type=int, default=0)
    ap.add_argument("--end-frame", type=int, default=None)
    ap.add_argument("--channel", type=int, default=None,
                    help="0-based channel to mutate; default: all channels")
    ap.add_argument("--raw", action="store_true",
                    help="force headerless ATRAC1 payload")
    ap.add_argument("--channels", type=int, default=None,
                    help="channel count for --raw (default 2)")
    args = ap.parse_args()

    if not (0.0 <= args.prob <= 1.0):
        ap.error("--prob must be in [0,1]")
    if args.amount < 1:
        ap.error("--amount must be >= 1")
    if args.every < 1:
        ap.error("--every must be >= 1")

    src = Path(args.input).read_bytes()
    data = bytearray(src)
    offset, channels, kind = detect_layout(src, args.raw, args.channels)
    frame_size = SU_SIZE * channels
    payload_len = len(data) - offset
    frames = payload_len // frame_size

    if frames <= 0:
        raise SystemExit("No complete ATRAC1 frame found.")
    if args.channel is not None and not (0 <= args.channel < channels):
        raise SystemExit(f"--channel must be 0..{channels-1}")

    rng = random.Random(args.seed)
    units_touched = 0
    mutations = 0
    invalid_before = 0

    end = frames if args.end_frame is None else min(frames, args.end_frame)
    for f in range(max(0, args.start_frame), end):
        if (f - args.start_frame) % args.every:
            continue
        chs = [args.channel] if args.channel is not None else range(channels)
        for ch in chs:
            base = offset + f * frame_size + ch * SU_SIZE
            su = data[base:base + SU_SIZE]
            try:
                meta = parse_su(su)
            except Exception:
                invalid_before += 1
                continue

            if args.mode == "sf-jitter":
                n = mutate_sf_jitter(su, meta, rng, args.prob, args.amount)
            elif args.mode == "sf-xor":
                n = mutate_sf_xor(su, meta, rng, args.prob, args.amount)
            elif args.mode == "spectrum":
                n = mutate_spectrum(su, meta, rng, args.prob, args.amount)
            elif args.mode == "wordlen":
                n = mutate_wordlen(su, meta, rng, args.prob, args.amount)
            else:
                n = mutate_bsm(su, meta, rng, args.prob, args.amount)

            if n:
                data[base:base + SU_SIZE] = su
                units_touched += 1
                mutations += n

    Path(args.output).write_bytes(data)

    print(f"format={kind} channels={channels} frames={frames}")
    print(f"mode={args.mode} seed={args.seed} prob={args.prob} amount={args.amount}")
    print(f"sound_units_touched={units_touched} mutations={mutations}")
    if invalid_before:
        print(f"warning: {invalid_before} input Sound Units could not be parsed")
    print(f"wrote: {args.output}")

if __name__ == "__main__":
    main()
