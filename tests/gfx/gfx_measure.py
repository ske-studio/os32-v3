"""Exercise display measurement arithmetic with clipped, repeated writes."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.media import Failure, main
from tools.gfx_measure import s1, g5m


def verify(config):
    frame = dict(draw=[[-2, 0, 6, 2], [2, 1, 8, 2]], raw=[], snap=[], merge=[], present=[],
                 writes=[dict(bytes=2, plane=p, page=1) for p in range(4)])
    sample, = s1(dict(size=[6, 2], format='PLANAR4', samples=[dict(frames=[frame, frame])]))
    if (sample['P'], sample['T'], sample['bytes']) != (20, 32, 16):
        raise Failure('clipped union or plane transfer')
    shape = ['get', 8, 20, 15, 1]
    groups = [dict(direction='get', w=8, h=20, pitch=15, offset=1, route=route, group=i,
                   us=us, count=100, rc=0, diff=0, guard_diff=0)
              for route, us in [('cpu', 2), ('m2mf', 1)] for i in range(51)]
    run = dict(observed=[shape], groups=groups)
    if g5m(run)['get']['shapes'] != [tuple(shape[1:])]:
        raise Failure('shape selection')
    groups[-1]['guard_diff'] = 1
    if g5m(run)['get']['shapes']:
        raise Failure('guard failure accepted')


if __name__ == '__main__':
    main(lambda config: None, verify)
