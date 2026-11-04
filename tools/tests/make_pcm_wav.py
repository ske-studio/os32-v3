#!/usr/bin/env python3
"""Create a two-second 44.1 kHz, 16-bit stereo sine WAV for pcm_play.

Usage: python3 tools/tests/make_pcm_wav.py /path/to/test.wav
Copy the resulting file to HostDrv for guest playback.
"""
import argparse
import math
import struct
import wave


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output')
    args = parser.parse_args()
    with wave.open(args.output, 'wb') as wav:
        wav.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
        for frame in range(2 * 44100):
            sample = round(12000 * math.sin(2 * math.pi * 440 * frame / 44100))
            wav.writeframesraw(struct.pack('<hh', sample, sample))


if __name__ == '__main__':
    main()
