#!/usr/bin/env python3
"""Run the PowerPC kboot regression tests sequentially."""
import argparse
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/out/ppc/kboot-all'
EXCLUDE = {'kboot_run', 'kboot_mutations', 'kboot_all', 'kboot_audit'}  # tools, not tests


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', nargs='+')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--timeout-each', type=float, default=900)
    parser.add_argument('--json', type=Path, default=OUT / 'result.json')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--keep-going', dest='fail_fast', action='store_false')
    group.add_argument('--fail-fast', dest='fail_fast', action='store_true')
    parser.set_defaults(fail_fast=False)
    args = parser.parse_args()
    if args.timeout_each <= 0:
        parser.error('--timeout-each must be positive')
    tests = {p.stem: p for p in sorted((ROOT / 'tools/ppc').glob('kboot_*.py'))
             if p.stem not in EXCLUDE}
    if args.only:
        names = [name if name.startswith('kboot_') else 'kboot_' + name
                 for name in args.only]
        unknown = [name for name in names if name not in tests]
        if unknown:
            parser.error('unknown tests: ' + ', '.join(unknown))
        tests = {name: tests[name] for name in names}
    OUT.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.setdefault('CROSS_DIR', '/home/hight/opt/cross')
    env['HOST32_RUNNERS'] = 'qemu'
    env.setdefault('TMPDIR', str(OUT / 'tmp'))
    Path(env['TMPDIR']).mkdir(parents=True, exist_ok=True)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    results = []
    for name, path in tests.items():
        command = [sys.executable, '-B', str(path.relative_to(ROOT))]
        if args.mutate and '--mutate' in path.read_text(encoding='utf-8'):
            command.append('--mutate')
        print(f'START {name}', flush=True)
        start = time.monotonic()
        with (OUT / (name + '.log')).open('w', encoding='utf-8') as log:
            try:
                proc = subprocess.run(command, cwd=ROOT, env=env, stdout=log,
                                      stderr=subprocess.STDOUT, timeout=args.timeout_each)
                rc = proc.returncode
                status = 'PASS' if rc == 0 else 'FAIL'
            except subprocess.TimeoutExpired:
                rc, status = None, 'TIMEOUT'
        elapsed = time.monotonic() - start
        results.append({'name': name, 'result': status, 'rc': rc, 'seconds': elapsed})
        print(f'{status} {name} rc={rc} {elapsed:.2f}s', flush=True)
        if status != 'PASS':
            print('\n'.join((OUT / (name + '.log')).read_text(
                encoding='utf-8', errors='replace').splitlines()[-30:]), flush=True)
            if args.fail_fast:
                break
    print('\nNAME RESULT RC SECONDS')
    for result in results:
        print(f"{result['name']} {result['result']} {result['rc']} {result['seconds']:.2f}")
    print(f"TOTAL {len(results)} PASS {sum(r['result'] == 'PASS' for r in results)} "
          f"FAIL {sum(r['result'] == 'FAIL' for r in results)} "
          f"TIMEOUT {sum(r['result'] == 'TIMEOUT' for r in results)} "
          f"{sum(r['seconds'] for r in results):.2f}s")
    output = {'head': head, 'datetime': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'mode': 'mutate' if args.mutate else 'normal', 'tests': results}
    json_path = args.json if args.json.is_absolute() else ROOT / args.json
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(output, indent=2) + '\n', encoding='utf-8')
    return int(any(r['result'] != 'PASS' for r in results))


if __name__ == '__main__':
    sys.exit(main())
