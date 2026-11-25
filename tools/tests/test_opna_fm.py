"""Compare each integer sample to the original host C++ channel."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'lib/os32_opna'
TEST = ROOT / 'tools/tests/opna_fm'


def run(work, mutate):
    source = CORE / 'upstream'
    (work / 'compiler.h').touch()
    objects = []
    for unit in [TEST / 'oracle.cpp', *(source / f'fmgen_{part}.cpp' for part in
                 ('fmgen', 'opna', 'psg', 'fmtimer'))]:
        obj = work / (unit.stem + '.o')
        subprocess.run(['g++', '-std=c++17', '-O2', '-fwrapv', '-ffunction-sections',
                        '-fdata-sections', '-DSUPPORT_FMGEN', '-DMAX_PATH=260',
                        '-I', work, '-I', source, '-c', unit, '-o', obj], check=True)
        objects.append(obj)
    subprocess.run(['ar', 'rcs', work / 'liboracle.a', *objects], check=True)
    for module in ('state', 'tables', 'operator', 'channel'):
        shutil.copy(CORE / 'src' / f'{module}.rs', work)
    shutil.copy(TEST / 'compare.rs', work)
    def compare():
        subprocess.run(['rustc', '--edition=2021', '-O', '-Adead_code', work / 'compare.rs',
                        '-L', work, '-lstatic=oracle', '-lstdc++', '-lm',
                        '-o', work / 'compare'], check=True)
        return subprocess.run([work / 'compare'], capture_output=True, text=True)
    baseline = compare()
    if baseline.returncode:
        raise RuntimeError(baseline.stderr)
    print('FM samples match the C++ oracle')
    if mutate:
        for module, old, new in [
            ('operator', 'self.tone(input >> 1, lfo)', 'self.tone(input >> 2, lfo)'),
            ('channel', '0 | 2 => b.out', '0 | 2 => a.out'),
            ('channel', 'bus[2] + bus[3]', 'bus[1] + bus[3]'),
        ]:
            path = work / f'{module}.rs'
            original = path.read_text()
            assert original.count(old) == 1
            path.write_text(original.replace(old, new))
            result = compare()
            if result.returncode == 0 or 'frame=' not in result.stderr:
                raise RuntimeError('expected sample mismatch: ' + result.stderr)
            print('RED', module, next(s for s in result.stderr.splitlines() if 'frame=' in s))
            path.write_text(original)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as work:
        run(Path(work), args.mutate)
