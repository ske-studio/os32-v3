"""Check the first second of sndl_test soft against the original fmgen PCM."""
import hashlib
import itertools
import struct
import sys
from verify_wave import capture

# FM::OPNA reset at 7987200 Hz / 44100 Hz, channel 0, algorithm 7,
# carrier 1, MUL=1, TL=12, AR=31, left pan, block=4, F-number=0x269.
EXPECTED = "a20a9e0f2ec0a61068a2b6e867c54c7162aac76664bfabe21e295689789be1fc"


def verify(frames):
    aligned = itertools.dropwhile(lambda frame: frame[1] != 12345, frames)
    digest = hashlib.sha256()
    for i in range(44100):
        frame = next(aligned, None)
        if frame is None or frame[1] != (12345 if i % 256 == 0 else 0):
            raise ValueError(f"missing or displaced PCM at frame {i}")
        digest.update(struct.pack("<hh", *frame))
    if digest.hexdigest() != EXPECTED:
        raise ValueError(f"software OPNA waveform differs: {digest.hexdigest()}")
    print("PASS 44100 consecutive software OPNA stereo frames")


if __name__ == "__main__":
    verify(capture(sys.argv[1]))
