"""Check the first second of sndl_test soft against the original fmgen PCM."""
from pathlib import Path
import subprocess
import tempfile
import itertools
import struct
import sys
from verify_wave import capture

def reference():
    here = Path(__file__).resolve().parent
    upstream = here.parents[2] / 'lib/os32_opna/upstream'
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / 'compiler.h').touch()
        exe = work / 'tone'
        subprocess.run(['g++', '-std=c++17', '-O2', '-fwrapv', '-ffunction-sections',
                        '-fdata-sections', '-DSUPPORT_FMGEN', '-DMAX_PATH=260',
                        '-I', work, '-I', upstream, '-Wl,--gc-sections', here / 'soft_tone.cpp',
                        *(upstream / f'fmgen_{part}.cpp' for part in
                          ('fmgen', 'opna', 'fmtimer', 'psg')), '-o', exe], check=True)
        return subprocess.check_output([exe])


def verify(frames):
    aligned = itertools.dropwhile(lambda frame: frame[1] != 12345, frames)
    pcm = reference()
    if len(pcm) != 44100 * 4:
        raise ValueError("incomplete fmgen reference")
    for i, expected in enumerate(struct.iter_unpack('<hh', pcm)):
        actual = next(aligned, None)
        if actual != expected:
            raise ValueError(f"frame {i}: expected {expected}, got {actual}")
    print("PASS 44100 consecutive software OPNA stereo frames")


if __name__ == "__main__":
    verify(capture(sys.argv[1]))
