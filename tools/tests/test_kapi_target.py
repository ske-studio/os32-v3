#!/usr/bin/env python3
"""KAPI target selection and isolated output roots."""
CHECK = dict(name='kapi-target-host', tier=1, needs=[], budget=5,
             covers=['sdk/gen_kapi.py', 'sdk/target_profile.py', 'sdk/kapi.json',
                     'kapi/kapi_generated.c', 'sdk/link/generations.ld',
                     'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs',
                     'tools/tests/test_kapi_target.py'])
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = ('include/kapi_call_generated.h', 'sdk/include/os32/os32_kapi_generated.h',
           'sdk/include/os32/os32_kapi_slots.h', 'kapi/kapi_generated.c',
           'exec/exec_kapi_init.inc', 'sdk/include/os32/os32_generations.h',
           'sdk/os32_generations.py', 'sdk/rust/os32api/src/generations.rs',
           'sdk/include/os32/os32_unit_stamp.h', 'sdk/crt/generations.inc',
           'sdk/link/generations.ld')


def generate(script, target, output):
    return subprocess.run([sys.executable, '-B', str(script), '--target', target,
                           '--out-dir', str(output), str(ROOT / 'sdk/kapi.json')],
                          cwd=ROOT, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        script = ROOT / 'sdk/gen_kapi.py'
        x86, ppc = root / 'x86', root / 'ppc'
        for target, output in [('x86-pc98', x86), ('ppc-powermac', ppc)]:
            result = generate(script, target, output)
            assert result.returncode == 0, result.stderr
            assert all((output / name).is_file() for name in OUTPUTS)
        for name in OUTPUTS:
            assert (x86 / name).read_bytes() == (ROOT / name).read_bytes(), name
        denied = subprocess.run([sys.executable, '-B', str(script), '--target',
                                 'ppc-powermac', '--check'], cwd=ROOT,
                                capture_output=True, text=True)
        assert denied.returncode != 0 and 'requires --out-dir' in denied.stderr
        if args.mutate:
            source = script.read_text()
            anchor = "require(args.target, 'sdk_user')"
            assert source.count(anchor) == 1
            mutant = root / 'gen_kapi.py'
            mutant.write_text(source.replace(anchor, "require(args.target, 'layout')"))
            # The copied generator imports its real helper modules from sdk/.
            import os
            env = dict(os.environ, PYTHONPATH=str(ROOT / 'sdk'))
            result = subprocess.run([sys.executable, '-B', str(mutant), '--target',
                                     'ppc-powermac', '--out-dir', str(root / 'mutant'),
                                     str(ROOT / 'sdk/kapi.json')], cwd=ROOT, env=env,
                                    capture_output=True, text=True)
            assert result.returncode != 0 and 'unsupported kernel layout target' in result.stderr
            assert not (root / 'mutant').exists()
            print('RED PPC acceptance: layout capability rejects target')
    print('PASS target output isolation and x86 byte equality')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
