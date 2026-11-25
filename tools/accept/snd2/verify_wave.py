import base64, json, struct, sys
from pathlib import Path
sine = (0,2341,4592,6667,8485,9978,11087,11769,12000,11769,11087,9978,
        8485,6667,4592,2341,0,-2341,-4592,-6667,-8485,-9978,-11087,
        -11769,-12000,-11769,-11087,-9978,-8485,-6667,-4592,-2341)


def capture(directory):
    frames, end = bytearray(), None
    for path in sorted(Path(directory).glob('*.json')):
        r = json.loads(path.read_text())
        if end is None and r['ok'] and r['rate'] == 8000:
            continue
        assert r['ok'] and r['rate'] == 44100, path
        raw = base64.b64decode(r['pcm'], validate=True)
        assert len(raw) == 4*r['frames'], path
        stop = r['frames_total']
        start = stop - r['frames']
        if end is None:
            end = start
        assert start <= end <= stop, f'capture gap or reset: {path}'
        frames.extend(raw[4*(end-start):])
        end = stop
    return struct.iter_unpack('<hh', frames)


def verify(data, total=44100*30):
    for frame in data:
        if frame == (0, 10000):
            break
    else:
        raise ValueError('missing first marker')
    for i in range(1, total):
        left, right = next(data)
        expected = (sine[(i*1000 % 44100)*32//44100],
                    0 if i%256 else 10000+(i//256)%16000)
        assert (left,right)==expected, (i,(left,right),expected)
    print(f'PASS {total} consecutive stereo frames')


if __name__ == '__main__':
    verify(capture(sys.argv[1]))
