# References

Validate implementation details against current upstream sources.

- FFmpeg ATRAC1 decoder:
  https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac1.c
- FFmpeg ATRAC1 data tables:
  https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/atrac1data.h
- FFmpeg AEA demuxer:
  https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/aeadec.c
- Historical ATRAC technical overview:
  https://www.minidisc.org/aes_atrac.html
- AtracDEnc ATRAC1 encoder/decoder reference (LGPL-2.1-or-later):
  https://github.com/dcherednik/atracdenc
- IETF CELLAR codec mapping draft, which identifies AtracDEnc as an ATRAC1
  encoder/decoder implementation:
  https://datatracker.ietf.org/doc/draft-ietf-cellar-codec/12/

AtracDEnc was used as an external behavioral reference for QMF, MDCT windowing,
normalization, and scale-factor interpretation. It is not linked, vendored, or
copied as a dependency by this repository. The implementation remains a small,
independent DSP core and is verified against FFmpeg's decoder output.

The handoff notes are not a normative ATRAC specification.
