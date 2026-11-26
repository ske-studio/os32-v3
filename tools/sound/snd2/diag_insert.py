"""Locate changes in the spacing or sequence of application period markers."""
import sys

from verify_wave import capture


def diagnose(data):
    previous = None
    for index, (_, mark) in enumerate(data):
        if not 10000 <= mark < 26000:
            continue
        if previous is not None:
            last, prior_mark = previous
            extra = index - last - 256
            if extra or mark != 10000 + (prior_mark - 9999) % 16000:
                print(f'capture_frame={index} markers={prior_mark},{mark} extra_frames={extra}')
        previous = index, mark


if __name__ == '__main__':
    diagnose(capture(sys.argv[1]))
