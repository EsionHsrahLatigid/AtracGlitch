#!/bin/sh
set -eu
python3 atrac1_glitch.py clean.aea glitch_sf.aea --mode sf-jitter --prob 0.05 --amount 4 --seed 1
ffmpeg -y -i glitch_sf.aea -c:a pcm_s24le glitch_sf.wav
