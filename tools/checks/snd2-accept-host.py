"""Check overlapping pc98emu windows against an independently synthesized tone."""
import base64
import json
import math
import struct
from io import BytesIO
from unittest.mock import patch
import sys
import tempfile
from pathlib import Path

CHECK = dict(name='snd2-accept-host', tier=1, needs=[], budget=1,
             covers=['tools/accept/snd2/*.py'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §13-3'])


def latency_capture(script, directory, mutate):
    def reply(start, right):
        raw = b''.join(struct.pack('<hh', 0, r) for r in right)
        return BytesIO(json.dumps(dict(ok=True, rate=44100, frames=len(right),
                        frames_total=start+len(right), pcm=base64.b64encode(raw).decode())).encode())

    source = script.read_text()
    for mutant in range(1 + int(mutate)):
        code = source
        if mutant:
            anchor = 'prior = (prior + fresh)[-256:]'
            assert code.count(anchor) == 1
            code = code.replace(anchor, 'prior = fresh[-256:]')
        output = directory / f'latency-{mutant}'
        # The reference is exactly one period back. An overlapping window
        # without a hit intervenes, then the hit arrives in an adjacent window.
        replies = [reply(990, [10007] + [0]*127), BytesIO(b'{"ok":true}'),
                   reply(1100, [0]*146), reply(1246, [-23456])]
        with patch('urllib.request.urlopen', side_effect=replies), \
             patch('time.monotonic_ns', side_effect=[0, 1, 2, 3, 4, 5]), \
             patch.object(sys, 'argv', [str(script), str(output)]):
            try:
                exec(compile(code, str(script), 'exec'), {'__name__': '__main__'})
            except ValueError:
                assert mutant
                print('RED runtime lost previous window')
            except SystemExit as done:
                assert not mutant and done.code == 0
                result = json.loads((output / 'timing.json').read_text())
                assert result['capture_frame'] == 1246 and result['frame'] == 8*256
                print('GREEN latency across windows')


def run(ctx):
    scripts = ctx.root / 'tools/accept/snd2'
    for path in scripts.glob('*.py'):
        compile(path.read_text(), str(path), 'exec')
    verifier = scripts / 'verify_wave.py'
    source = verifier.read_text()
    changes = [('end-start', '0'), ('(left,right)==expected', 'right == expected[1]')]
    with tempfile.TemporaryDirectory() as td:
        directory = Path(td)
        latency_capture(scripts / 'latency.py', directory, ctx.mutate)
        def window(name, rate, start, samples):
            pcm = b''.join(struct.pack('<hh', *sample) for sample in samples)
            (directory / name).write_text(json.dumps(dict(
                ok=True, rate=rate, frames=len(samples), frames_total=start+len(samples),
                pcm=base64.b64encode(pcm).decode())))
        samples = [(round(12000 * math.sin(math.tau * ((i*1000 % 44100)*32//44100)/32)),
                    0 if i % 256 else 10000+i//256) for i in range(513)]
        for change in [None] + (changes if ctx.mutate else []):
            code = source
            if change:
                assert code.count(change[0]) == 1
                code = code.replace(*change)
            ns = {'__name__': 'wave_under_test'}
            exec(compile(code, str(verifier), 'exec'), ns)
            def matches():
                try:
                    ns['verify'](ns['capture'](directory), len(samples))
                except (AssertionError, StopIteration, ValueError):
                    return False
                return True
            window('0.json', 8000, 0, [(0, 0)])
            window('1.json', 44100, 10, samples[:300])
            window('2.json', 44100, 266, samples[256:])
            good = matches()
            damaged = samples[256:]
            damaged[-1] = (1, damaged[-1][1])
            window('2.json', 44100, 266, damaged)
            rejects_damage = not matches()
            window('2.json', 44100, 311, samples[301:])
            passed = good and rejects_damage and not matches()
            assert passed == (change is None), change
            print('RED runtime ' + change[0] if change else 'GREEN synthetic capture')
    return 0
