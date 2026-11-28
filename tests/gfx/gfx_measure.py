"""Exercise display measurement arithmetic with clipped, repeated writes."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tests.support.gfx2_host import run_host
from tests.support.media import Failure, main
from tools.gfx_measure import s1, g5m, retry, serial_trace


def verify(config):
    for flags in (['-DEXPECT_FRAMES=0'], ['-DGFX_MEASURE_DIAG', '-DEXPECT_FRAMES=2']):
        run_host('gfx_packed_retry', ['sys/hal/gfx/gfx_present.c'],
                 {1: 'failed packed present lost dirty', 2: 'incorrect transfer observation mode'},
                 gc_sections=True, cflags=['-D__cdecl=', *flags])
    run_host('gfx_snapshot', [], {1: 'scan bank, scroll or reference pixels'},
             gc_sections=True, cflags=['-D__cdecl='])
    frame = dict(draw=[[-2, 0, 6, 2], [2, 1, 8, 2]], raw=[], snap=[], merge=[], present=[],
                 writes=[dict(bytes=2, plane=p, page=1) for p in range(4)])
    sample, = s1(dict(size=[6, 2], format='PLANAR4', samples=[dict(frames=[frame, frame])]))
    if (sample['P'], sample['T'], sample['bytes']) != (20, 32, 16):
        raise Failure('clipped union or plane transfer')
    lines = ['GXT1 begin 0', 'GXT1 0 1 0 0 0 0 0 0',
             'GXT1 1 1 0 0 16 1 0 0', 'GXT1 3 1 0 0 16 1 2 1',
             'GXT1 7 1 0 0 32 2 0 1', 'GXT1 pixels 4001 4001',
             'GXT1 end 0', 'GOP1 move 0 37 8 1 0']
    decoded = serial_trace(lines, [32, 2], 'PLANAR4')
    row, = s1(decoded)
    if (row['P'] != 16 or row['bytes'] != 8 or
            decoded['samples'][0]['frames'][0]['scan'] != '01' * 64):
        raise Failure('kernel present trace')
    retry_log = ['GXT1 begin 0', 'GXT1 0 0 0 0 0 0 0 0', 'GXT1 8 0 0 0 2 1 0 0',
                 'GXT1 0 0 0 0 0 0 0 0', 'GXT1 1 0 0 0 2 1 0 0',
                 'GXT1 2 1 0 0 2 1 2 0', 'GXT1 7 1 0 0 2 1 0 0',
                 'GXT1 pixels 0203 0203', 'GXT1 end 0', 'GOP1 move 0 10 2 1 0']
    retried = serial_trace(retry_log, [2, 1], 'INDEX8')
    if not retry(retried)['passed']:
        raise Failure('retry pixels')
    retried['samples'][0]['frames'][1]['scan'] = '0300'
    if retry(retried)['passed']:
        raise Failure('retry pixels')
    shape = ['put', 8, 20, 15, 1]
    groups = [dict(direction='put', w=8, h=20, pitch=15, offset=1, route=route, group=i,
                   us=us, count=100, rc=0, diff=0, guard_diff=0)
              for route, us in [('cpu', 2), ('m2mf', 1)] for i in range(51)]
    run = dict(observed=[shape], groups=groups, completed_groups=len(groups))
    if g5m(run)['put']['shapes'] != [tuple(shape[1:])]:
        raise Failure('shape selection')
    for previous, expected in ((32, 8), (8, 4), (0, 0)):
        mixed = [*lines[:3], f'GXT1 4 1 {previous} 0 16 1 0 0',
                 'GXT1 3 1 0 0 48 1 6 1', 'GXT1 end 0', 'GOP1 move 0 37 24 1 0']
        sample, = serial_trace(mixed, [48, 1], 'PLANAR4')['samples']
        if sample['restore_bytes'] != expected:
            raise Failure('mixed page restoration')
    groups[-1]['guard_diff'] = 1
    if g5m(run)['put']['shapes']:
        raise Failure('guard failure accepted')
    groups[-1]['guard_diff'] = 0
    rows = [f"G5M1 put 8 20 15 1 {r['route']} {r['group']} 100 {r['us']} 0 0 0 100"
            for r in groups]
    for tail in ([], ['G5M1 done 103'],
                 ['G5M1 get 8 20 15 1 m2mf 0 0 0 -1 0 0 0', 'G5M1 done 103']):
        log = ['G5M1 map WC 640 307200 10000', *rows, *tail]
        decoded = serial_trace(log, [640, 480], 'INDEX8')
        decoded['observed'] = [shape]
        if g5m(decoded)['put']['threshold'] is not None:
            raise Failure('incomplete or failed sweep accepted')


if __name__ == '__main__':
    main(lambda config: None, verify)
